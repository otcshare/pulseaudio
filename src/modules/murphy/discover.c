/*
 * module-murphy-ivi -- PulseAudio module for providing audio routing support
 * Copyright (c) 2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St - Fifth Floor, Boston,
 * MA 02110-1301 USA.
 *
 */
#include <stdio.h>
#include <assert.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/hashmap.h>
#include <pulsecore/idxset.h>
#include <pulsecore/client.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <pulsecore/card.h>
#include <pulsecore/device-port.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>
#include <pulsecore/strbuf.h>


#include "discover.h"
#include "node.h"
#include "audiomgr.h"
#include "router.h"
#include "constrain.h"
#include "multiplex.h"
#include "loopback.h"
#include "fader.h"
#include "classify.h"
#include "utils.h"
#include "extapi.h"
#include "stream-state.h"
#include "murphyif.h"

#define MAX_CARD_TARGET   4
#define MAX_NAME_LENGTH   256

#define ACTIVE_PORT       NULL

/* Bluetooth service class */
#define BIT(x)    (1U << (x))

#define BT_SERVICE_MASK          0xffe
#define BT_SERVICE_INFORMATION   BIT(23) /**< WEB-server, WAP-server, etc */
#define BT_SERVICE_TELEPHONY     BIT(22) /**< Modem, Headset, etc*/
#define BT_SERVICE_AUDIO         BIT(21) /**< Speaker, Microphone, Headset */
#define BT_SERVICE_OBJECT_XFER   BIT(20) /**< v-Inbox, v-Folder, etc */
#define BT_SERVICE_CAPTURING     BIT(19) /**< Scanner, Microphone, etc */
#define BT_SERVICE_RENDERING     BIT(18) /**< Printing, Speaker, etc */
#define BT_SERVICE_NETWORKING    BIT(17) /**< LAN, Ad hoc, etc */
#define BT_SERVICE_POSITIONING   BIT(16) /**< Location identification */


typedef struct {
    struct userdata *u;
    uint32_t index;
} card_check_t;

typedef struct {
    struct userdata *u;
    pa_muxnode *mux;
    pa_loopnode *loop;
} source_cleanup_t;

typedef struct {
    struct userdata *u;
    uint32_t index;
} stream_uncork_t;

static const char combine_pattern[]   = "Simultaneous output on ";
static const char loopback_outpatrn[] = "Loopback from ";
static const char loopback_inpatrn[]  = "Loopback to ";

static void handle_alsa_card(struct userdata *, pa_card *);
static void handle_bluetooth_card(struct userdata *, pa_card *);
static bool get_bluetooth_port_availability(mir_node *, pa_device_port *);

static void handle_udev_loaded_card(struct userdata *, pa_card *,
                                    mir_node *, const char *);
static void handle_card_ports(struct userdata *, mir_node *,
                              pa_card *, pa_card_profile *);

static mir_node *create_node(struct userdata *, mir_node *, bool *);
static void destroy_node(struct userdata *, mir_node *);
static bool update_node_availability(struct userdata *, mir_node *,
                                          bool);
static bool update_node_availability_by_device(struct userdata *,
                                                    mir_direction,
                                                    void *, pa_device_port *,
                                                    bool);

static void parse_profile_name(pa_card_profile *,
                               char **, char **, char *, int);

static const char *node_key(struct userdata *, mir_direction,
                      void *, pa_device_port *, char *, size_t);

static pa_sink *make_output_prerouting(struct userdata *, mir_node *,
                                       pa_channel_map *, const char *,
                                       mir_node **);
static pa_source *make_input_prerouting(struct userdata *, mir_node *,
                                        const char *, mir_node **);

static mir_node_type get_stream_routing_class(pa_proplist *);
static const char *get_stream_amname(mir_node_type, const char *, pa_proplist *);

static void set_bluetooth_profile(struct userdata *, pa_card *, pa_direction_t);


static void schedule_deferred_routing(struct userdata *);
static void schedule_card_check(struct userdata *, pa_card *);
static void schedule_source_cleanup(struct userdata *, mir_node *);
#if 0
static void schedule_stream_uncorking(struct userdata *, pa_sink *);
#endif

struct pa_discover *pa_discover_init(struct userdata *u)
{
    pa_discover *discover = pa_xnew0(pa_discover, 1);

    discover->chmin = 1;
    discover->chmax = 2;
    discover->selected = true;

    discover->nodes.byname = pa_hashmap_new(pa_idxset_string_hash_func,
                                            pa_idxset_string_compare_func);
    discover->nodes.byptr  = pa_hashmap_new(pa_idxset_trivial_hash_func,
                                            pa_idxset_trivial_compare_func);
    return discover;
}

void pa_discover_done(struct userdata *u)
{
    pa_discover *discover;
    void *state;
    mir_node *node;

    if (u && (discover = u->discover)) {
        PA_HASHMAP_FOREACH(node, discover->nodes.byname, state) {
            mir_node_destroy(u, node);
        }
        pa_hashmap_free(discover->nodes.byname);
        pa_hashmap_free(discover->nodes.byptr);
        pa_xfree(discover);
        u->discover = NULL;
    }
}

void pa_discover_domain_up(struct userdata *u)
{
    pa_discover *discover;
    mir_node    *node;
    void        *state;

    pa_assert(u);
    pa_assert_se((discover = u->discover));

    PA_HASHMAP_FOREACH(node, discover->nodes.byname, state) {
        node->amid = AM_ID_INVALID;

        if ((node->visible && node->available) ||
            (node->type == mir_gateway_sink ||
             node->type == mir_gateway_source)) {
            pa_audiomgr_register_node(u, node);
            extapi_signal_node_change(u);
        }
    }
}

void pa_discover_domain_down(struct userdata *u)
{
}

void pa_discover_add_card(struct userdata *u, pa_card *card)
{
    const char *bus;

    pa_assert(u);
    pa_assert(card);

    if (!(bus = pa_utils_get_card_bus(card))) {
        pa_log_debug("ignoring card '%s' due to lack of '%s' property",
                     pa_utils_get_card_name(card), PA_PROP_DEVICE_BUS);
        return;
    }

    if (pa_streq(bus, "pci") || pa_streq(bus, "usb") || pa_streq(bus, "platform")) {
        handle_alsa_card(u, card);
        return;
    }
    else if (pa_streq(bus, "bluetooth")) {
        handle_bluetooth_card(u, card);
        return;
    }

    pa_log_debug("ignoring card '%s' due to unsupported bus type '%s'",
                 pa_utils_get_card_name(card), bus);
}

void pa_discover_remove_card(struct userdata *u, pa_card *card)
{
    const char  *bus;
    pa_discover *discover;
    mir_node    *node;
    void        *state;

    pa_assert(u);
    pa_assert(card);
    pa_assert_se((discover = u->discover));

    if (!(bus = pa_utils_get_card_bus(card)))
        bus = "<unknown>";

    PA_HASHMAP_FOREACH(node, discover->nodes.byname, state) {
        if (node->implement == mir_device &&
            node->pacard.index == card->index)
        {
            if (pa_streq(bus, "pci") || pa_streq(bus, "usb") || pa_streq(bus, "platform"))
                mir_constrain_destroy(u, node->paname);

            destroy_node(u, node);
        }
    }

    if (pa_streq(bus, "bluetooth"))
        mir_constrain_destroy(u, card->name);
}

void pa_discover_profile_changed(struct userdata *u, pa_card *card)
{
    pa_core         *core;
    pa_card_profile *prof;
    pa_sink         *sink;
    pa_source       *source;
    pa_discover     *discover;
    const char      *bus;
    bool        pci;
    bool        usb;
    bool        bluetooth;
    bool	platform;
    uint32_t         stamp;
    mir_node        *node;
    void            *state;
    uint32_t         index;
    bool        need_routing;

    pa_assert(u);
    pa_assert(card);
    pa_assert_se((core = u->core));
    pa_assert_se((discover = u->discover));

    if ((bus = pa_utils_get_card_bus(card)) == NULL) {
        pa_log_debug("ignoring profile change on card '%s' due to lack of '%s'"
                     "property", pa_utils_get_card_name(card),
                     PA_PROP_DEVICE_BUS);
        return;
    }

    pci = pa_streq(bus, "pci");
    usb = pa_streq(bus, "usb");
    bluetooth = pa_streq(bus, "bluetooth");
    platform = pa_streq(bus, "platform");

    if (!pci && !usb && !bluetooth && !platform) {
        pa_log_debug("ignoring profile change on card '%s' due to unsupported "
                     "bus type '%s'", pa_utils_get_card_name(card), bus);
        u->state.sink = u->state.source = PA_IDXSET_INVALID;
        return;
    }

    if ((index = u->state.sink) != PA_IDXSET_INVALID) {
        if ((sink = pa_idxset_get_by_index(core->sinks, index)))
            pa_discover_add_sink(u, sink, true);
        else
            pa_log_debug("sink.%u is gone", index);
        u->state.sink = PA_IDXSET_INVALID;
    }

    if ((index = u->state.source) != PA_IDXSET_INVALID) {
        if ((source = pa_idxset_get_by_index(core->sources, index)))
            pa_discover_add_source(u, source);
        else
            pa_log_debug("source.%u is gone", index);
        u->state.source = PA_IDXSET_INVALID;
    }

    if (bluetooth) {
        pa_assert_se((prof = card->active_profile));

        pa_log_debug("bluetooth profile changed to '%s' on card '%s'",
                     prof->name, card->name);

        if (!prof->n_sinks && !prof->n_sources) {
            /* switched off but not unloaded yet */
            need_routing = false;

            PA_HASHMAP_FOREACH(node, discover->nodes.byname, state) {
                if (node->implement == mir_device &&
                    node->pacard.index == card->index)
                {
                    if (node->type != mir_bluetooth_a2dp &&
                        node->type != mir_bluetooth_sco)
                    {
                        if (node->available) {
                            node->available = false;
                            need_routing = true;
                        }
                    }
                }
            }

            if (need_routing)
                schedule_deferred_routing(u);
        }
    }
    else {
        pa_log_debug("alsa profile changed to '%s' on card '%s'",
                     card->active_profile->name, card->name);

        stamp = pa_utils_get_stamp();

        handle_alsa_card(u, card);

        PA_HASHMAP_FOREACH(node, discover->nodes.byname, state) {
            if (node->implement == mir_device &&
                node->pacard.index == card->index &&
                node->stamp < stamp)
            {
                destroy_node(u, node);
            }
        }
    }

}

void pa_discover_port_available_changed(struct userdata *u,
                                        pa_device_port  *port)
{
    pa_core       *core;
    pa_sink       *sink;
    pa_source     *source;
    mir_node      *node;
    uint32_t       idx;
    bool      available;
    const char    *state;
    bool      btport;
    bool      route;
    pa_direction_t direction;
    void          *iter;

    pa_assert(u);
    pa_assert(port);
    pa_assert_se((core = u->core));

    switch (port->available) {
    case PA_AVAILABLE_NO:    state = "not available";  break;
    case PA_AVAILABLE_YES:   state = "available";      break;
    default:                 state = "unknown";        break;
    }

    pa_log_debug("port '%s' availabilty changed to %s. Updating",
                 port->name, state);

    btport = false;
    route = false;
    direction = 0;
    iter = NULL;

    while ((node = pa_utils_get_node_from_port(u, port, &iter))) {
        btport = true;
        available = get_bluetooth_port_availability(node, port);
        route |= update_node_availability(u, node, available);
        direction |= (node->direction == mir_input) ? PA_DIRECTION_INPUT : PA_DIRECTION_OUTPUT;
    }

    if (btport)
        set_bluetooth_profile(u, port->card, direction);
    else {
        switch (port->available) {
        case PA_AVAILABLE_NO:    available = false;    break;
        case PA_AVAILABLE_YES:   available = true;     break;
        default:                 /* do nothing */      return;
        }

        if (port->direction == PA_DIRECTION_OUTPUT) {
            PA_IDXSET_FOREACH(sink, core->sinks, idx) {
                if (sink->ports) {
                    if (port == pa_hashmap_get(sink->ports, port->name)) {
                        pa_log_debug("   sink '%s'", sink->name);
                        route |= update_node_availability_by_device(
                                                     u, mir_output,
                                                     sink, port,
                                                     available);
                    }
                }
            }
        }

        if (port->direction == PA_DIRECTION_INPUT) {
            PA_IDXSET_FOREACH(source, core->sources, idx) {
                if (source->ports) {
                    if (port == pa_hashmap_get(source->ports, port->name)) {
                        pa_log_debug("   source '%s'", source->name);
                        route |= update_node_availability_by_device(
                                                      u, mir_input,
                                                      source, port,
                                                      available);
                    }
                }
            }
        }
    }

    if (route)
        mir_router_make_routing(u);
}

void pa_discover_add_sink(struct userdata *u, pa_sink *sink, bool route)
{
    static pa_nodeset_resdef def_resdef = {0, {0, 0}};

    pa_core           *core;
    pa_discover       *discover;
    pa_module         *module;
    mir_node          *node;
    pa_card           *card;
    const char        *key;
    char               kbf[256];
    char               nbf[2048];
    const char        *loopback_role;
    pa_nodeset_map    *map;
    pa_nodeset_resdef *resdef;
    bool               make_rset;
    pa_source         *ns;
    mir_node           data;
    mir_node_type      type;
    bool               add_to_hash;

    pa_assert(u);
    pa_assert(sink);
    pa_assert_se((core = u->core));
    pa_assert_se((discover = u->discover));

    module = sink->module;

    if ((card = sink->card)) {
        if (!(key = node_key(u, mir_output,sink,ACTIVE_PORT, kbf,sizeof(kbf))))
            return;
        if (!(node = pa_discover_find_node_by_key(u, key))) {
            if (u->state.profile)
                pa_log_debug("can't find node for sink (key '%s')", key);
            else
                u->state.sink = sink->index;
            return;
        }
        pa_log_debug("node for '%s' found (key %s). Updating with sink data",
                     node->paname, node->key);
        node->paidx = sink->index;
        node->available = true;
        pa_discover_add_node_to_ptr_hash(u, sink, node);

        if ((loopback_role = pa_classify_loopback_stream(node))) {
            if (!(ns = pa_utils_get_null_source(u))) {
                pa_log("Can't load loopback module: no initial null source");
                return;
            }

            map = pa_nodeset_get_map_by_role(u, loopback_role);
            make_rset = (map && map->resdef);
            resdef = make_rset ? map->resdef : &def_resdef;

            node->loop = pa_loopback_create(u->loopback, core,
                                            PA_LOOPBACK_SINK, node->index,
                                            ns->index, sink->index,
                                            loopback_role,
                                            resdef->priority,
                                            resdef->flags.rset,
                                            resdef->flags.audio);

            mir_node_print(node, nbf, sizeof(nbf));
            pa_log_debug("updated node:\n%s", nbf);

            if (make_rset)
                pa_murphyif_create_resource_set(u, node, resdef);
        }

        if (route) {
            type = node->type;

            if (type != mir_bluetooth_a2dp && type != mir_bluetooth_sco)
                mir_router_make_routing(u);
            else {
                if (!u->state.profile)
                    schedule_deferred_routing(u);
            }
        }
    }
    else if (!module || !pa_streq(module->name, "module-combine-sink-new")) {
        add_to_hash = false;

        memset(&data, 0, sizeof(data));
        data.key = pa_xstrdup(sink->name);
        data.direction = mir_output;
        data.implement = mir_device;
        data.channels  = sink->channel_map.channels;
        data.available = true;
        data.paidx     = sink->index;

        if (sink == pa_utils_get_null_sink(u)) {
            data.visible = false;
            data.type = mir_null;
            data.amname = pa_xstrdup("Silent");
            data.amid = AM_ID_INVALID;
            data.paname = pa_xstrdup(sink->name);
        }
        else if (pa_classify_node_by_property(&data, sink->proplist)) {
            if (data.type == mir_gateway_sink) {
                data.privacy = mir_private;
                data.visible = false;
                data.amname = pa_xstrdup(sink->name);
                data.amid = AM_ID_INVALID;
                data.paname = pa_xstrdup(sink->name);
            }
            else {
                data.privacy = mir_public;
                data.visible = true;
                data.amname = pa_xstrdup(mir_node_type_str(data.type));
                data.amid = AM_ID_INVALID;
                data.paname = pa_xstrdup(sink->name);
            }

            add_to_hash = true;
        }
        else {
            pa_xfree(data.key); /* for now */
            pa_log_info("currently we do not support statically loaded "
                        "sinks without " PA_PROP_NODE_TYPE " property");
            return;
        }

        node = create_node(u, &data, NULL);

        if (add_to_hash)
            pa_discover_add_node_to_ptr_hash(u, sink, node);
    }
}


void pa_discover_remove_sink(struct userdata *u, pa_sink *sink)
{
    pa_discover    *discover;
    mir_node       *node;
    const char     *name;
    mir_node_type   type;

    pa_assert(u);
    pa_assert(sink);
    pa_assert_se((discover = u->discover));

    name = pa_utils_get_sink_name(sink);

    if (!(node = pa_hashmap_get(discover->nodes.byptr, sink)))
        pa_log_debug("can't find node for sink (name '%s')", name);
    else {
        pa_log_debug("node found for '%s'. Reseting sink data", name);
        pa_murphyif_destroy_resource_set(u, node);
        schedule_source_cleanup(u, node);
        node->paidx = PA_IDXSET_INVALID;
        pa_hashmap_remove(discover->nodes.byptr, sink);

        type = node->type;

        if (sink->card) {
            if (type != mir_bluetooth_a2dp && type != mir_bluetooth_sco)
                node->available = false;
            else {
                if (!u->state.profile)
                    schedule_deferred_routing(u);
            }
        }
        else {
            pa_log_info("currently we do not support statically loaded sinks");
        }
    }
}


void pa_discover_add_source(struct userdata *u, pa_source *source)
{
    static pa_nodeset_resdef def_resdef = {0, {0, 0}};

    pa_core           *core;
    pa_discover       *discover;
    mir_node          *node;
    pa_card           *card;
    const char        *key;
    char               kbf[256];
    char               nbf[2048];
    const char        *loopback_role;
    pa_nodeset_map    *map;
    pa_nodeset_resdef *resdef;
    bool          make_rset;
    uint32_t           sink_index;
    pa_sink           *ns;
    mir_node           data;

    pa_assert(u);
    pa_assert(source);
    pa_assert_se((core = u->core));
    pa_assert_se((discover = u->discover));

    if ((card = source->card)) {
        if (!(key = node_key(u,mir_input,source,ACTIVE_PORT,kbf,sizeof(kbf))))
            return;
        if (!(node = pa_discover_find_node_by_key(u, key))) {
            if (u->state.profile)
                pa_log_debug("can't find node for source (key '%s')", key);
            else
                u->state.source = source->index;
            return;
        }
        pa_log_debug("node for '%s' found. Updating with source data",
                     node->amname);
        node->paidx = source->index;
        node->available = true;
        pa_discover_add_node_to_ptr_hash(u, source, node);
        if ((loopback_role = pa_classify_loopback_stream(node))) {
            if (!(ns = pa_utils_get_null_sink(u))) {
                pa_log("Can't load loopback module: no initial null sink");
                return;
            }

            map = pa_nodeset_get_map_by_role(u, loopback_role);
            make_rset = (map && map->resdef);
            resdef = make_rset ? map->resdef : &def_resdef;

            node->loop = pa_loopback_create(u->loopback, core,
                                            PA_LOOPBACK_SOURCE, node->index,
                                            source->index, ns->index,
                                            loopback_role,
                                            resdef->priority,
                                            resdef->flags.rset,
                                            resdef->flags.audio);
            if (node->loop) {
                sink_index = pa_loopback_get_sink_index(core, node->loop);
                node->mux = pa_multiplex_find_by_sink(u->multiplex,sink_index);
            }

            mir_node_print(node, nbf, sizeof(nbf));
            pa_log_debug("updated node:\n%s", nbf);

            if (make_rset)
                pa_murphyif_create_resource_set(u, node, resdef);

            pa_fader_apply_volume_limits(u, node->stamp);
        }
    }
    else {
        memset(&data, 0, sizeof(data));
        data.key = pa_xstrdup(source->name);
        data.direction = mir_input;
        data.implement = mir_device;
        data.channels  = source->channel_map.channels;
        data.available = true;

        if (source == pa_utils_get_null_source(u)) {
            data.visible = false;
            data.type = mir_null;
            data.amname = pa_xstrdup("Silent");
            data.amid = AM_ID_INVALID;
            data.paname = pa_xstrdup(source->name);
            data.paidx = source->index;
        }
        else if (pa_classify_node_by_property(&data, source->proplist)) {
            if (data.type == mir_gateway_source) {
                data.privacy = mir_private;
                data.visible = false;
                data.amname = pa_xstrdup(source->name);
                data.amid = AM_ID_INVALID;
                data.paname = pa_xstrdup(source->name);
            }
            else {
                data.privacy = mir_public;
                data.visible = true;
                data.amname = pa_xstrdup(mir_node_type_str(data.type));
                data.amid   = AM_ID_INVALID;
                data.paname = pa_xstrdup(source->name);
            }
        }
        else {
            pa_xfree(data.key); /* for now */
            pa_log_info("currently we do not support statically loaded "
                        "sources without " PA_PROP_NODE_TYPE " property");
            return;
        }

        create_node(u, &data, NULL);
    }
}

void pa_discover_remove_source(struct userdata *u, pa_source *source)
{
    pa_discover    *discover;
    mir_node       *node;
    const char     *name;
    mir_node_type   type;

    pa_assert(u);
    pa_assert(source);
    pa_assert_se((discover = u->discover));

    name = pa_utils_get_source_name(source);

    if (!(node = pa_hashmap_get(discover->nodes.byptr, source)))
        pa_log_debug("can't find node for source (name '%s')", name);
    else {
        pa_log_debug("node found. Reseting source data");
        pa_murphyif_destroy_resource_set(u, node);
        schedule_source_cleanup(u, node);
        node->paidx = PA_IDXSET_INVALID;
        pa_hashmap_remove(discover->nodes.byptr, source);

        type = node->type;

        if (source->card) {
            if (type != mir_bluetooth_sco)
                node->available = false;
            else {
                if (!u->state.profile)
                    schedule_deferred_routing(u);
            }
        }
        else {
            pa_log_info("currently we do not support statically "
                        "loaded sources");
        }
    }
}


void pa_discover_register_sink_input(struct userdata *u, pa_sink_input *sinp)
{
    pa_core           *core;
    pa_discover       *discover;
    pa_proplist       *pl;
    const char        *name;
    const char        *media;
    mir_node_type      type;
    mir_node           data;
    mir_node          *node;
    mir_node          *target;
    char               key[256];
    pa_sink           *sink;
    const char        *role;
    pa_nodeset_resdef *resdef;

    pa_assert(u);
    pa_assert(sinp);
    pa_assert_se((core = u->core));
    pa_assert_se((discover = u->discover));
    pa_assert_se((pl = sinp->proplist));

    if ((media = pa_proplist_gets(sinp->proplist, PA_PROP_MEDIA_NAME))) {
        if (!strncmp(media, combine_pattern, sizeof(combine_pattern)-1)) {
            pa_log_debug("Seems to be a combine stream. Nothing to do ...");
            return;
        }
        if (!strncmp(media, loopback_outpatrn, sizeof(loopback_outpatrn)-1)) {
            pa_log_debug("Seems to be a loopback stream. Nothing to do ...");
            return;
        }
    }

    name = pa_utils_get_sink_input_name(sinp);

    pa_log_debug("registering input stream '%s'", name);

    if (!(type = pa_classify_guess_stream_node_type(u, pl, &resdef))) {
        pa_log_debug("cant find stream class for '%s'. "
                     "Leaving it alone", name);
        return;
    }

    pa_utils_set_stream_routing_properties(pl, type, NULL);

    snprintf(key, sizeof(key), "stream_input.%d", sinp->index);

    memset(&data, 0, sizeof(data));
    data.key       = key;
    data.direction = mir_input;
    data.implement = mir_stream;
    data.channels  = sinp->channel_map.channels;
    data.type      = type;
    data.zone      = pa_utils_get_zone(sinp->proplist);
    data.visible   = true;
    data.available = true;
    data.amname    = (char *)get_stream_amname(type, name, pl);
    data.amdescr   = (char *)pa_proplist_gets(pl, PA_PROP_MEDIA_NAME);
    data.amid      = AM_ID_INVALID;
    data.paname    = (char *)name;
    data.paidx     = sinp->index;
    data.rsetid    = (char *)pa_proplist_gets(pl, PA_PROP_RESOURCE_SET_ID);

    /*
     * here we can't guess whether the application requested an explicit
     * route by sepcifying the target sink @ stream creation time.
     *
     * the brute force solution: we make a default route for this stream
     * possibly overwiriting the orginal app request :(
     */
    /* this will set data.mux */
    role = pa_proplist_gets(sinp->proplist, PA_PROP_MEDIA_ROLE);
    sink = make_output_prerouting(u, &data, &sinp->channel_map, role, &target);

    node = create_node(u, &data, NULL);
    pa_assert(node);
    pa_discover_add_node_to_ptr_hash(u, sinp, node);

    if (sink && target) {
        pa_log_debug("move stream to sink %u (%s)", sink->index, sink->name);

        if (pa_sink_input_move_to(sinp, sink, false) < 0)
            pa_log("failed to route '%s' => '%s'",node->amname,target->amname);
        else
            pa_audiomgr_add_default_route(u, node, target);
    }
}

bool pa_discover_preroute_sink_input(struct userdata *u,
                                     pa_sink_input_new_data *data)
{
    pa_core           *core;
    pa_module         *m;
    pa_proplist       *pl;
    pa_discover       *discover;
    pa_multiplex      *multiplex;
    mir_node           fake;
    pa_sink           *sink;
    pa_sink_input     *sinp;
    const char        *mnam;
    const char        *role;
    mir_node_type      type;
    mir_node          *node;
    pa_muxnode        *mux;
    pa_nodeset_resdef *resdef;
    bool               loopback;
    bool               remap = false;

    pa_assert(u);
    pa_assert(data);
    pa_assert_se((core = u->core));
    pa_assert_se((discover = u->discover));
    pa_assert_se((multiplex = u->multiplex));
    pa_assert_se((pl = data->proplist));

    mnam = (m = data->module) ? m->name : "";

    if (pa_streq(mnam, "module-combine-sink-new")) {
        loopback = false;
        type = mir_node_type_unknown;

        if (!(mux  = pa_multiplex_find_by_module(multiplex, m)) ||
            !(sink = pa_idxset_get_by_index(core->sinks, mux->sink_index)) ||
            !(sinp = pa_idxset_first(sink->inputs, NULL)) ||
            !(type = pa_utils_get_stream_class(sinp->proplist)))
        {
            pa_log_debug("can't figure out the type of multiplex stream");
        }
        else {
            pa_utils_set_stream_routing_properties(data->proplist, type, NULL);
        }
    }
    else {
        loopback = pa_streq(mnam, "module-loopback");
        remap = false;

        if (loopback) {
            if (!(node = pa_utils_get_node_from_data(u, mir_input, data))) {
                pa_log_debug("can't find loopback node for sink-input");
                return true;
            }

            if (node->direction == mir_output) {
                pa_log_debug("refuse to preroute loopback sink-input "
                             "(current route: sink %u @ %p)", data->sink ?
                             data->sink->index : PA_IDXSET_INVALID,data->sink);
                return true;
            }

            data->sink = NULL;

            type = pa_classify_guess_stream_node_type(u, pl, NULL);
        }
        else {
            remap = pa_streq(mnam, "module-remap-sink");
            type = pa_classify_guess_stream_node_type(u, pl, &resdef);

            pa_utils_set_resource_properties(pl, resdef);

            if (pa_stream_state_start_corked(u, data, resdef)) {
                pa_log_debug("start corked");
            }
        }

        pa_utils_set_stream_routing_properties(pl, type, data->sink);
    }

    memset(&fake, 0, sizeof(fake));
    fake.direction = mir_input;
    fake.implement = mir_stream;
    fake.type      = type;

    if (!data->sink) {
        fake.channels  = data->channel_map.channels;
        fake.zone      = pa_utils_get_zone(data->proplist);
        fake.visible   = true;
        fake.available = true;
        fake.amname    = pa_xstrdup("<preroute sink-input>");
        fake.amid      = AM_ID_INVALID;
        fake.paidx     = PA_IDXSET_INVALID;

        role = pa_proplist_gets(data->proplist, PA_PROP_MEDIA_ROLE);
        sink = make_output_prerouting(u, &fake, &data->channel_map, role,NULL);

        if (sink) {
#if 0
            if (fake.mux && !(data->flags & PA_SINK_INPUT_START_CORKED)) {
                data->flags |= PA_SINK_INPUT_START_CORKED;
                schedule_stream_uncorking(u, sink);
            }
#endif

            if (pa_sink_input_new_data_set_sink(data, sink, false))
                pa_log_debug("set sink %u for new sink-input", sink->index);
            else {
                pa_log("can't set sink %u for new sink-input", sink->index);
                                                      /* copes wit NULL mux */
                pa_multiplex_destroy(u->multiplex, core, fake.mux);
                return false;
            }
        }
    }

    if (remap) {
        /* no ramp needed */
        return true;
    }
    if (loopback && data->sink && data->sink->module) {
        /* no ramp needed */
        if (pa_streq(data->sink->module->name, "module-combine-sink-new"))
            return true;
    }

    if (pa_classify_ramping_stream(&fake)) {
        pa_log_debug("set sink-input ramp-muted");
        data->flags |= PA_SINK_INPUT_START_RAMP_MUTED;
    }

    return true;
}


void pa_discover_add_sink_input(struct userdata *u, pa_sink_input *sinp)
{
    pa_core           *core;
    pa_sink           *s;
    pa_sink_input     *csinp;
    pa_proplist       *pl;
    pa_discover       *discover;
    pa_multiplex      *multiplex;
    mir_node           data;
    mir_node          *node;
    mir_node          *snod;
    const char        *name;
    const char        *media;
    mir_node_type      type;
    char               key[256];
    bool               created;
    pa_muxnode        *mux;
    pa_nodeset_resdef *resdef;
    pa_nodeset_resdef  rdbuf;

    pa_assert(u);
    pa_assert(sinp);
    pa_assert_se((core = u->core));
    pa_assert_se((discover = u->discover));
    pa_assert_se((multiplex = u->multiplex));
    pa_assert_se((pl = sinp->proplist));

    resdef = NULL;

    if (!(media = pa_proplist_gets(sinp->proplist, PA_PROP_MEDIA_NAME)))
        media = "<unknown>";

    if (!strncmp(media, combine_pattern, sizeof(combine_pattern)-1)) {
        if (!pa_utils_stream_has_default_route(sinp->proplist) ||
            !(mux = pa_multiplex_find_by_module(multiplex, sinp->module)) ||
            mux->defstream_index != PA_IDXSET_INVALID)
        {
            pa_log_debug("New stream is a combine stream. Nothing to do ...");
        }
        else {
            pa_log_debug("New stream is a combine stream. Setting as default");
            mux->defstream_index = sinp->index;
            mir_router_make_routing(u);
        }
        return;
    } else if (!strncmp(media, loopback_outpatrn,sizeof(loopback_outpatrn)-1)){
        pa_log_debug("New stream is a loopback output stream");

        if ((node = pa_utils_get_node_from_stream(u, mir_input, sinp))) {
            if (node->direction == mir_input)
                pa_log_debug("loopback stream node '%s' found", node->amname);
            else {
                pa_log_debug("ignoring it");
                return;
            }
        }
        else {
            pa_log_debug("can't find node for the loopback stream");
            return;
        }

        s = sinp->sink;
    }
    else {
        name = pa_utils_get_sink_input_name(sinp);

        pa_log_debug("dealing with new input stream '%s'", name);

        if ((type = get_stream_routing_class(pl)))
            resdef = pa_utils_get_resource_properties(pl, &rdbuf);
        else {
            if (!(type = pa_classify_guess_stream_node_type(u, pl, &resdef))) {
                pa_log_debug("cant find stream class for '%s'. "
                             "Leaving it alone", name);
                return;
            }

            pa_utils_set_stream_routing_properties(pl, type, NULL);

            /* if needed, make some post-routing here */
        }

        /* we need to add this to main hashmap as that is used for loop
           through on all nodes. */
        snprintf(key, sizeof(key), "stream_input.%d", sinp->index);

        memset(&data, 0, sizeof(data));
        data.key       = key;
        data.direction = mir_input;
        data.implement = mir_stream;
        data.channels  = sinp->channel_map.channels;
        data.type      = type;
        data.zone      = pa_utils_get_zone(pl);
        data.visible   = true;
        data.available = true;
        data.amname    = (char *)get_stream_amname(type, name, pl);
        data.amdescr   = (char *)pa_proplist_gets(pl, PA_PROP_MEDIA_NAME);
        data.amid      = AM_ID_INVALID;
        data.paname    = (char *)name;
        data.paidx     = sinp->index;
        data.mux       = pa_multiplex_find_by_sink(u->multiplex,
                                                   sinp->sink->index);
        data.rsetid    = (char *)pa_proplist_gets(pl, PA_PROP_RESOURCE_SET_ID);
        node = create_node(u, &data, &created);

        pa_assert(node);

        if (!created) {
            pa_log("%s: confused with stream. '%s' did exists",
                   __FILE__, node->amname);
            return;
        }

        if (node->rsetid)
            pa_murphyif_add_node(u, node);
        else if (resdef)
            pa_murphyif_create_resource_set(u, node, resdef);

        pa_discover_add_node_to_ptr_hash(u, sinp, node);

        if (!data.mux)
            s = sinp->sink;
        else {
            csinp = pa_idxset_get_by_index(core->sink_inputs,
                                           data.mux->defstream_index);
            s = csinp ? csinp->sink : NULL;

            if ((sinp->flags & PA_SINK_INPUT_START_RAMP_MUTED)) {
                pa_log_debug("ramp '%s' to 100%%", media);
                pa_fader_ramp_volume(u, sinp, PA_VOLUME_NORM);
            }
        }
    }

    if (s)
        pa_log_debug("routing target candidate is %u (%s)", s->index, s->name);

    if (!s || !(snod = pa_hashmap_get(discover->nodes.byptr, s)))
        pa_log_debug("can't figure out where this stream is routed");
    else {
        pa_log_debug("register route '%s' => '%s'",
                     node->amname, snod->amname);

        if (pa_utils_stream_has_default_route(sinp->proplist))
            pa_audiomgr_add_default_route(u, node, snod);

        /* FIXME: register explicit routes */
        /* else pa_audiomgr_add/register_explicit_route() */


        pa_fader_apply_volume_limits(u, pa_utils_get_stamp());
    }
}


void pa_discover_remove_sink_input(struct userdata *u, pa_sink_input *sinp)
{
    pa_discover    *discover;
    mir_node       *node;
    mir_node       *sinknod;
    const char     *name;
    bool       had_properties = false;

    pa_assert(u);
    pa_assert(sinp);
    pa_assert_se((discover = u->discover));

    name = pa_utils_get_sink_input_name(sinp);

    pa_log_debug("sink-input '%s' going to be destroyed", name);

    if (sinp->proplist)
        had_properties = pa_utils_unset_stream_routing_properties(sinp->proplist);

    if (!(node = pa_discover_remove_node_from_ptr_hash(u, sinp))) {
        if (!pa_multiplex_sink_input_remove(u->multiplex, sinp))
            pa_log_debug("nothing to do for sink-input (name '%s')", name);
    }
    else {
        pa_log_debug("node found for '%s'. After clearing routes "
                     "it will be destroyed", name);

        if (!(sinknod = pa_hashmap_get(discover->nodes.byptr, sinp->sink)))
            pa_log_debug("can't figure out where this stream is routed");
        else {
            pa_log_debug("clear route '%s' => '%s'",
                         node->amname, sinknod->amname);

            /* FIXME: and actually do it ... */

        }

        destroy_node(u, node);
    }

    if (node || had_properties)
        mir_router_make_routing(u);
}


void pa_discover_register_source_output(struct userdata  *u,
                                        pa_source_output *sout)
{
    pa_core           *core;
    pa_discover       *discover;
    pa_proplist       *pl;
    const char        *name;
    const char        *media;
    mir_node_type      type;
    mir_node           data;
    mir_node          *node;
    mir_node          *target;
    char               key[256];
    pa_source         *source;
    const char        *role;
    pa_nodeset_resdef *resdef;

    pa_assert(u);
    pa_assert(sout);
    pa_assert_se((core = u->core));
    pa_assert_se((discover = u->discover));
    pa_assert_se((pl = sout->proplist));

    if ((media = pa_proplist_gets(sout->proplist, PA_PROP_MEDIA_NAME))) {
        if (!strncmp(media, loopback_inpatrn, sizeof(loopback_inpatrn)-1)) {
            pa_log_debug("Seems to be a loopback stream. Nothing to do ...");
            return;
        }
    }

    name = pa_utils_get_source_output_name(sout);

    pa_log_debug("registering output stream '%s'", name);

    if (!(type = pa_classify_guess_stream_node_type(u, pl, &resdef))) {
        pa_log_debug("cant find stream class for '%s'. "
                     "Leaving it alone", name);
        return;
    }

    pa_utils_set_stream_routing_properties(pl, type, NULL);

    snprintf(key, sizeof(key), "stream_output.%d", sout->index);

    memset(&data, 0, sizeof(data));
    data.key       = key;
    data.direction = mir_output;
    data.implement = mir_stream;
    data.channels  = sout->channel_map.channels;
    data.type      = type;
    data.zone      = pa_utils_get_zone(sout->proplist);
    data.visible   = true;
    data.available = true;
    data.amname    = (char *)name;
    data.amdescr   = (char *)pa_proplist_gets(pl, PA_PROP_MEDIA_NAME);
    data.amid      = AM_ID_INVALID;
    data.paname    = (char *)name;
    data.paidx     = sout->index;
    data.rsetid    = (char *)pa_proplist_gets(pl, PA_PROP_RESOURCE_SET_ID);

    /*
     * here we can't guess whether the application requested an explicit
     * route by sepcifying the target source @ stream creation time.
     *
     * the brute force solution: we make a default route for this stream
     * possibly overwiriting the orginal app request :(
     */
    role   = pa_proplist_gets(sout->proplist, PA_PROP_MEDIA_ROLE);
    source = make_input_prerouting(u, &data, role, &target);

    node = create_node(u, &data, NULL);
    pa_assert(node);
    pa_discover_add_node_to_ptr_hash(u, sout, node);

    if (source && target) {
        pa_log_debug("move stream to source %u (%s)",
                     source->index, source->name);

        if (pa_source_output_move_to(sout, source, false) < 0)
            pa_log("failed to route '%s' => '%s'",node->amname,target->amname);
        else {
            pa_log_debug("register route '%s' => '%s'",
                         node->amname, target->amname);
            /* FIXME: and actually do it ... */
        }
    }
}

bool pa_discover_preroute_source_output(struct userdata *u,
                                        pa_source_output_new_data *data)
{
    pa_core           *core;
    pa_module         *m;
    pa_proplist       *pl;
    pa_discover       *discover;
    mir_node           fake;
    pa_source         *source;
    const char        *mnam;
    const char        *role;
    mir_node_type      type;
    mir_node          *node;
    pa_nodeset_resdef *resdef;

    pa_assert(u);
    pa_assert(data);
    pa_assert_se((core = u->core));
    pa_assert_se((discover = u->discover));
    pa_assert_se((pl = data->proplist));

    mnam = (m = data->module) ? m->name : "";

    if (pa_streq(mnam, "module-loopback")) {
        if (!(node = pa_utils_get_node_from_data(u, mir_output, data))) {
            pa_log_debug("can't find loopback node for source-output");
            return true;
        }

        if (node->direction == mir_input) {
            pa_log_debug("refuse to preroute loopback source-output "
                         "(current route: source %u @ %p)", data->source ?
                         data->source->index : PA_IDXSET_INVALID,data->source);
            return true;
        }

        data->source = NULL;

        type = pa_classify_guess_stream_node_type(u, pl, NULL);
    }
    else {
        type = pa_classify_guess_stream_node_type(u, pl, &resdef);

        pa_utils_set_resource_properties(pl, resdef);
    }

    pa_utils_set_stream_routing_properties(pl, type, data->source);

    if (!data->source) {
        memset(&fake, 0, sizeof(fake));
        fake.direction = mir_output;
        fake.implement = mir_stream;
        fake.channels  = data->channel_map.channels;
        fake.type      = type;
        fake.zone      = pa_utils_get_zone(data->proplist);
        fake.visible   = true;
        fake.available = true;
        fake.amname    = pa_xstrdup("<preroute source-output>");

        role   = pa_proplist_gets(data->proplist, PA_PROP_MEDIA_ROLE);
        source = make_input_prerouting(u, &fake, role, NULL);

        if (source) {
            if (pa_source_output_new_data_set_source(data, source, false)) {
                pa_log_debug("set source %u for new source-output",
                             source->index);
            }
            else {
                pa_log("can't set source %u for new source-output",
                       source->index);
            }
        }
    }

    return true;
}


void pa_discover_add_source_output(struct userdata *u, pa_source_output *sout)
{
    pa_core           *core;
    pa_source         *s;
    pa_proplist       *pl;
    pa_discover       *discover;
    mir_node           data;
    mir_node          *node;
    mir_node          *snod;
    const char        *name;
    const char        *media;
    mir_node_type      type;
    char               key[256];
    bool          created;
    pa_nodeset_resdef *resdef;
    pa_nodeset_resdef  rdbuf;

    pa_assert(u);
    pa_assert(sout);
    pa_assert_se((core = u->core));
    pa_assert_se((discover = u->discover));
    pa_assert_se((pl = sout->proplist));

    resdef = NULL;

    if (!(media = pa_proplist_gets(sout->proplist, PA_PROP_MEDIA_NAME)))
        media = "<unknown>";

    if (!strncmp(media, loopback_inpatrn, sizeof(loopback_inpatrn)-1)) {
        pa_log_debug("New stream is a loopback input stream");

        if ((node = pa_utils_get_node_from_stream(u, mir_output, sout))) {
            if (node->direction == mir_output)
                pa_log_debug("loopback stream node '%s' found", node->amname);
            else {
                pa_log_debug("ignoring it");
                return;
            }
        }
        else {
            pa_log_debug("can't find node for the loopback stream");
            return;
        }
    }
    else {
        name = pa_utils_get_source_output_name(sout);

        pa_log_debug("dealing with new output stream '%s'", name);

        if ((type = get_stream_routing_class(pl)))
            resdef = pa_utils_get_resource_properties(pl, &rdbuf);
        else {
            if (!(type = pa_classify_guess_stream_node_type(u, pl, &resdef))) {
                pa_log_debug("cant find stream class for '%s'. "
                             "Leaving it alone", name);
                return;
            }

            pa_utils_set_stream_routing_properties(pl, type, NULL);

            /* if needed, make some post-routing here */
        }

        /* we need to add this to main hashmap as that is used for loop
           through on all nodes. */
        snprintf(key, sizeof(key), "stream_output.%d", sout->index);

        memset(&data, 0, sizeof(data));
        data.key       = key;
        data.direction = mir_output;
        data.implement = mir_stream;
        data.channels  = sout->channel_map.channels;
        data.type      = type;
        data.zone      = pa_utils_get_zone(pl);
        data.visible   = true;
        data.available = true;
        data.amname    = (char *)name;
        data.amdescr   = (char *)pa_proplist_gets(pl, PA_PROP_MEDIA_NAME);
        data.amid      = AM_ID_INVALID;
        data.paname    = (char *)name;
        data.paidx     = sout->index;
        data.rsetid    = (char *)pa_proplist_gets(pl, PA_PROP_RESOURCE_SET_ID);

        node = create_node(u, &data, &created);

        pa_assert(node);

        if (!created) {
            pa_log("%s: confused with stream. '%s' did exists",
                   __FILE__, node->amname);
            return;
        }

        if (node->rsetid)
            pa_murphyif_add_node(u, node);
        else if (resdef)
            pa_murphyif_create_resource_set(u, node, resdef);

        pa_discover_add_node_to_ptr_hash(u, sout, node);
    }

    if ((s = sout->source))
        pa_log_debug("routing target candidate is %u (%s)", s->index, s->name);

    if (!s || !(snod = pa_hashmap_get(discover->nodes.byptr, s)))
        pa_log_debug("can't figure out where this stream is routed");
    else {
        pa_log_debug("register route '%s' => '%s'",
                     snod->amname, node->amname);
        pa_audiomgr_add_default_route(u, node, snod);
    }
}


void pa_discover_remove_source_output(struct userdata  *u,
                                      pa_source_output *sout)
{
    pa_discover    *discover;
    mir_node       *node;
    mir_node       *srcnod;
    const char     *name;

    pa_assert(u);
    pa_assert(sout);
    pa_assert_se((discover = u->discover));

    name = pa_utils_get_source_output_name(sout);

    pa_log_debug("source-output '%s' going to be destroyed", name);

    if (!(node = pa_discover_remove_node_from_ptr_hash(u, sout)))
        pa_log_debug("can't find node for source-output (name '%s')", name);
    else {
        pa_log_debug("node found for '%s'. After clearing routes "
                     "it will be destroyed", name);

        if (!(srcnod = pa_hashmap_get(discover->nodes.byptr, sout->source)))
            pa_log_debug("can't figure out where this stream is routed");
        else {
            pa_log_debug("clear route '%s' => '%s'",
                         node->amname, srcnod->amname);

            /* FIXME: and actually do it ... */

        }

        destroy_node(u, node);

        mir_router_make_routing(u);
    }
}


mir_node *pa_discover_find_node_by_key(struct userdata *u, const char *key)
{
    pa_discover *discover;
    mir_node    *node;

    pa_assert(u);
    pa_assert_se((discover = u->discover));

    if (key)
        node = pa_hashmap_get(discover->nodes.byname, key);
    else
        node = NULL;

    return node;
}

mir_node *pa_discover_find_node_by_ptr(struct userdata *u, void *ptr)
{
    pa_discover *discover;
    mir_node    *node;

    pa_assert(u);
    pa_assert_se((discover = u->discover));

    if (ptr)
        node = pa_hashmap_get(discover->nodes.byptr, ptr);
    else
        node = NULL;

    return node;
}

void pa_discover_add_node_to_ptr_hash(struct userdata *u,
                                      void *ptr,
                                      mir_node *node)
{
    pa_discover *discover;

    pa_assert(u);
    pa_assert(ptr);
    pa_assert(node);
    pa_assert_se((discover = u->discover));

    pa_hashmap_put(discover->nodes.byptr, ptr, node);
}

mir_node *pa_discover_remove_node_from_ptr_hash(struct userdata *u, void *ptr)
{
    pa_discover *discover;

    pa_assert(u);
    pa_assert(ptr);
    pa_assert_se((discover = u->discover));

    return pa_hashmap_remove(discover->nodes.byptr, ptr);
}


static void handle_alsa_card(struct userdata *u, pa_card *card)
{
    mir_node    data;
    const char *udd;
    const char *cnam;
    const char *cid;

    memset(&data, 0, sizeof(data));
    data.zone = pa_utils_get_zone(card->proplist);
    data.visible = true;
    data.amid = AM_ID_INVALID;
    data.implement = mir_device;
    data.paidx = PA_IDXSET_INVALID;
    data.stamp = pa_utils_get_stamp();

    cnam = pa_utils_get_card_name(card);
    udd  = pa_proplist_gets(card->proplist, "module-udev-detect.discovered");

    if (udd && pa_streq(udd, "1")) {
        /* udev loaded alsa card */
        if (!strncmp(cnam, "alsa_card.", 10)) {
            cid = cnam + 10;
            handle_udev_loaded_card(u, card, &data, cid);
            return;
        }
    }
    else {
        /* statically loaded pci or usb card */
    }

    pa_log_debug("ignoring unrecognized pci card '%s'", cnam);
}


#if 0
static void handle_bluetooth_card(struct userdata *u, pa_card *card)
{
    pa_discover     *discover;
    pa_card_profile *prof;
    mir_node         data;
    mir_node        *node;
    mir_constr_def  *cd;
    char            *cnam;
    char            *cid;
    const char      *cdescr;
    void            *state;
    char             paname[MAX_NAME_LENGTH+1];
    char             amname[MAX_NAME_LENGTH+1];
    char             key[MAX_NAME_LENGTH+1];

    pa_assert_se((discover = u->discover));

    cdescr = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_DESCRIPTION);


    memset(paname, 0, sizeof(paname));
    memset(amname, 0, sizeof(amname));
    memset(key   , 0, sizeof(key)   );

    memset(&data, 0, sizeof(data));
    data.key = key;
    data.visible = true;
    data.amid = AM_ID_INVALID;
    data.implement = mir_device;
    data.paidx = PA_IDXSET_INVALID;
    data.paname = paname;
    data.amname = amname;
    data.amdescr = (char *)cdescr;
    data.pacard.index = card->index;
    data.stamp = pa_utils_get_stamp();

    cnam = pa_utils_get_card_name(card);

    if (!strncmp(cnam, "bluez_card.", 11)) {
        cid = cnam + 11;

        pa_assert(card->ports);

        cd = mir_constrain_create(u, "profile", mir_constrain_profile, cnam);

        PA_HASHMAP_FOREACH(prof, card->profiles, cstate) {
            data.available = false;
            data.pacard.profile = prof->name;

            if (prof->n_sinks > 0) {
                data.direction = mir_output;
                data.channels = prof->max_sink_channels;
                data.amname = amname;
                amname[0] = '\0';
                snprintf(paname, sizeof(paname), "bluez_sink.%s", cid);
                snprintf(key, sizeof(key), "%s@%s", paname, prof->name);
                pa_classify_node_by_card(&data, card, prof, NULL);
                node = create_node(u, &data, NULL);
                mir_constrain_add_node(u, cd, node);
            }

            if (prof->n_sources > 0) {
                data.direction = mir_input;
                data.channels = prof->max_source_channels;
                data.amname = amname;
                amname[0] = '\0';
                snprintf(paname, sizeof(paname), "bluez_source.%s", cid);
                snprintf(key, sizeof(key), "%s@%s", paname, prof->name);
                pa_classify_node_by_card(&data, card, prof, NULL);
                node = create_node(u, &data, NULL);
                mir_constrain_add_node(u, cd, node);
            }
        }

        if (!(prof = card->active_profile))
            pa_log("card '%s' has no active profile", card->name);
        else {
            pa_log_debug("card '%s' default profile '%s'",
                         card->name, prof->name);
        }

        schedule_card_check(u, card);
    }
}
#endif


static void handle_bluetooth_card(struct userdata *u, pa_card *card)
{
    pa_discover     *discover;
    pa_card_profile *prof;
    pa_device_port  *port;
    mir_node         data;
    mir_node        *node;
    mir_constr_def  *cd;
    const char      *cnam;
    const char      *cid;
    const char      *cdescr;
    void            *state0, *state1;
    char             paname[MAX_NAME_LENGTH+1];
    char             amname[MAX_NAME_LENGTH+1];
    char             key[MAX_NAME_LENGTH+1];
    int              len;
    bool        input;
    bool        output;

    pa_assert_se((discover = u->discover));

    cdescr = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_DESCRIPTION);


    memset(paname, 0, sizeof(paname));
    memset(amname, 0, sizeof(amname));
    memset(key   , 0, sizeof(key)   );

    memset(&data, 0, sizeof(data));
    data.key = key;
    data.zone = pa_utils_get_zone(card->proplist);
    data.visible = true;
    data.amid = AM_ID_INVALID;
    data.implement = mir_device;
    data.paidx = PA_IDXSET_INVALID;
    data.paname = paname;
    data.amname = amname;
    data.amdescr = (char *)cdescr;
    data.pacard.index = card->index;
    data.stamp = pa_utils_get_stamp();

    cnam = pa_utils_get_card_name(card);

    if (!strncmp(cnam, "bluez_card.", 11)) {
        cid = cnam + 11;

        pa_assert(card->ports);

        cd = mir_constrain_create(u, "profile", mir_constrain_profile, cnam);

        PA_HASHMAP_FOREACH(port, card->ports, state0) {
            pa_assert(port->profiles);


            input = output = true;
            len = strlen(port->name);
            if (len >= 6 && !strcmp("-input", port->name + (len-6)))
                output = false;
            else if (len >= 7 && !strcmp("-output", port->name + (len-7)))
                input  = false;


            PA_HASHMAP_FOREACH(prof, port->profiles, state1) {
                data.pacard.profile = prof->name;
                data.available = get_bluetooth_port_availability(&data, port);

                if (output && prof->n_sinks > 0) {
                    data.direction = mir_output;
                    data.channels = prof->max_sink_channels;
                    data.amname = amname;
                    amname[0] = '\0';
                    snprintf(paname, sizeof(paname), "bluez_sink.%s", cid);
                    snprintf(key, sizeof(key), "%s@%s.%s", paname, port->name, prof->name);
                    pa_classify_node_by_card(&data, card, prof, NULL);
                    node = create_node(u, &data, NULL);
                    mir_constrain_add_node(u, cd, node);
                    pa_utils_set_port_properties(port, node);
                }

                if (input && prof->n_sources > 0) {
                    data.direction = mir_input;
                    data.channels = prof->max_source_channels;
                    data.amname = amname;
                    amname[0] = '\0';
                    snprintf(paname, sizeof(paname), "bluez_source.%s", cid);
                    snprintf(key, sizeof(key), "%s@%s.%s", paname, port->name, prof->name);
                    pa_classify_node_by_card(&data, card, prof, NULL);
                    node = create_node(u, &data, NULL);
                    mir_constrain_add_node(u, cd, node);
                    pa_utils_set_port_properties(port, node);
                }
            }
        }

        if (!(prof = card->active_profile))
            pa_log("card '%s' has no active profile", card->name);
        else {
            pa_log_debug("card '%s' default profile '%s'",
                         card->name, prof->name);
        }

        schedule_card_check(u, card);
    }
}

static bool get_bluetooth_port_availability(mir_node *node,
                                                 pa_device_port *port)
{
    bool available = false;
    const char *prof;

    pa_assert(node);
    pa_assert(port);

    if ((prof = node->pacard.profile)) {
        if (!strcmp(prof, "hfgw")        ||
            !strcmp(prof, "a2dp_source") ||
            !strcmp(prof, "a2dp_sink"))
            available = (port->available != PA_AVAILABLE_NO);
        else
            available = true;
    }

    return available;
}

static void handle_udev_loaded_card(struct userdata *u, pa_card *card,
                                    mir_node *data, const char *cardid)
{
    pa_discover      *discover;
    pa_card_profile  *prof;
    pa_card_profile  *active;
    void             *state;
    const char       *alsanam;
    char             *sid;
    char             *sinks[MAX_CARD_TARGET+1];
    char             *sources[MAX_CARD_TARGET+1];
    char              buf[MAX_NAME_LENGTH+1];
    char              paname[MAX_NAME_LENGTH+1];
    char              amname[MAX_NAME_LENGTH+1];
    int               i;

    pa_assert(card);
    pa_assert(card->profiles);
    pa_assert_se((discover = u->discover));

    alsanam = pa_proplist_gets(card->proplist, "alsa.card_name");

    memset(amname, 0, sizeof(amname));

    data->paname  = paname;
    data->amname  = amname;
    data->amdescr = (char *)alsanam;

    data->pacard.index = card->index;

    active = card->active_profile;

    PA_HASHMAP_FOREACH(prof, card->profiles, state) {
        /* filtering: deal with selected profiles if requested so */
        if (discover->selected && (!active || (active && prof != active)))
            continue;

        /* filtering: skip the 'off' profiles */
        if (!prof->n_sinks && !prof->n_sources)
            continue;

        /* filtering: consider sinks with suitable amount channels */
        if (prof->n_sinks &&
            (prof->max_sink_channels < discover->chmin ||
             prof->max_sink_channels  > discover->chmax  ))
            continue;

        /* filtering: consider sources with suitable amount channels */
        if (prof->n_sources &&
            (prof->max_source_channels <  discover->chmin ||
             prof->max_source_channels >  discover->chmax   ))
            continue;

        data->pacard.profile = prof->name;

        parse_profile_name(prof, sinks,sources, buf,sizeof(buf));

        data->direction = mir_output;
        data->channels = prof->max_sink_channels;
        for (i = 0;  (sid = sinks[i]);  i++) {
            snprintf(paname, sizeof(paname), "alsa_output.%s.%s", cardid, sid);
            handle_card_ports(u, data, card, prof);
        }

        data->direction = mir_input;
        data->channels = prof->max_source_channels;
        for (i = 0;  (sid = sources[i]);  i++) {
            snprintf(paname, sizeof(paname), "alsa_input.%s.%s", cardid, sid);
            handle_card_ports(u, data, card, prof);
        }
    }
}


static void handle_card_ports(struct userdata *u, mir_node *data,
                              pa_card *card, pa_card_profile *prof)
{
    mir_node       *node = NULL;
    bool       have_ports = false;
    mir_constr_def *cd = NULL;
    char           *amname = data->amname;
    pa_device_port *port;
    void           *state;
    bool       created;
    char            key[MAX_NAME_LENGTH+1];

    pa_assert(u);
    pa_assert(data);
    pa_assert(card);
    pa_assert(prof);

    if (card->ports) {
        PA_HASHMAP_FOREACH(port, card->ports, state) {
            /*
             * if this port did not belong to any profile
             * (ie. prof->profiles == NULL) we assume that this port
             * does works with all the profiles
             */
            if (port->profiles && pa_hashmap_get(port->profiles, prof->name) &&
                ((port->direction == PA_DIRECTION_INPUT && data->direction == mir_input)||
                 (port->direction == PA_DIRECTION_OUTPUT && data->direction == mir_output)))
            {
                have_ports = true;

                amname[0] = '\0';
                snprintf(key, sizeof(key), "%s@%s", data->paname, port->name);

                data->key       = key;
                data->available = (port->available != PA_AVAILABLE_NO);
                data->type      = 0;
                data->amname    = amname;
                data->paport    = port->name;

                pa_classify_node_by_card(data, card, prof, port);

                node = create_node(u, data, &created);

                if (!created)
                    node->stamp = data->stamp;
                else {
                    cd = mir_constrain_create(u, "port", mir_constrain_port,
                                              data->paname);
                    mir_constrain_add_node(u, cd, node);
                }
            }
        }
    }

    if (!have_ports) {
        data->key = data->paname;
        data->available = true;

        pa_classify_node_by_card(data, card, prof, NULL);

        node = create_node(u, data, &created);

        if (!created)
            node->stamp = data->stamp;
    }

    data->amname = amname;
    *amname = '\0';
}


static mir_node *create_node(struct userdata *u, mir_node *data,
                             bool *created_ret)
{
    pa_discover *discover;
    mir_node    *node;
    bool    created;
    char         buf[2048];

    pa_assert(u);
    pa_assert(data);
    pa_assert(data->key);
    pa_assert(data->paname);
    pa_assert_se((discover = u->discover));

    if ((node = pa_hashmap_get(discover->nodes.byname, data->key)))
        created = false;
    else {
        created = true;

        node = mir_node_create(u, data);
        pa_hashmap_put(discover->nodes.byname, node->key, node);

        mir_node_print(node, buf, sizeof(buf));
        pa_log_debug("new node:\n%s", buf);

        if (node->available)
            pa_audiomgr_register_node(u, node);
    }

    if (created_ret)
        *created_ret = created;

    return node;
}

static void destroy_node(struct userdata *u, mir_node *node)
{
    pa_discover *discover;
    mir_node    *removed;

    pa_assert(u);
    pa_assert_se((discover = u->discover));

    if (node) {
        removed = pa_hashmap_remove(discover->nodes.byname, node->key);

        if (node != removed) {
            if (removed)
                pa_log("%s: confused with data structures: key mismatch. "
                       " attempted to destroy '%s'; actually destroyed '%s'",
                       __FILE__, node->key, removed->key);
            else
                pa_log("%s: confused with data structures: node '%s' "
                       "is not in the hash table", __FILE__, node->key);
            return;
        }

        pa_log_debug("destroying node: %s / '%s'", node->key, node->amname);

        if (node->implement == mir_stream) {
            if (node->direction == mir_input) {
                if (node->mux) {
                    pa_log_debug("removing multiplexer");
                }
            }
        }

        pa_audiomgr_unregister_node(u, node);

        extapi_signal_node_change(u);

        mir_constrain_remove_node(u, node);

        pa_loopback_destroy(u->loopback, u->core, node->loop);
        pa_multiplex_destroy(u->multiplex, u->core, node->mux);

        mir_node_destroy(u, node);
    }
}

static bool update_node_availability(struct userdata *u,
                                          mir_node *node,
                                          bool available)
{
    pa_assert(u);
    pa_assert(node);

    if ((!available &&  node->available) ||
        ( available && !node->available)  )
    {
        node->available = available;

        if (available)
            pa_audiomgr_register_node(u, node);
        else
            pa_audiomgr_unregister_node(u, node);

        extapi_signal_node_change(u);

        return true; /* routing needed */
    }

    return false;
}

static bool update_node_availability_by_device(struct userdata *u,
                                                    mir_direction direction,
                                                    void *data,
                                                    pa_device_port *port,
                                                    bool available)
{
    mir_node   *node;
    const char *key;
    char        buf[256];

    pa_assert(u);
    pa_assert(data);
    pa_assert(port);
    pa_assert(direction == mir_input || direction == mir_output);

    if ((key = node_key(u, direction, data, port, buf, sizeof(buf)))) {
        if (!(node = pa_discover_find_node_by_key(u, key)))
            pa_log_debug("      can't find node (key '%s')", key);
        else {
            pa_log_debug("      node for '%s' found (key %s)",
                         node->paname, node->key);

            return update_node_availability(u, node, available);
        }
    }

    return false; /* no routing needed */
}

static char *get_name(char **string_ptr, int offs)
{
    char c, *name, *end;

    name = *string_ptr + offs;

    for (end = name;  (c = *end);   end++) {
        if (c == '+') {
            *end++ = '\0';
            break;
        }
    }

    *string_ptr = end;

    return name;
}

static void parse_profile_name(pa_card_profile *prof,
                               char           **sinks,
                               char           **sources,
                               char            *buf,
                               int              buflen)
{
    char *p = buf;
    int   i = 0;
    int   j = 0;

    pa_assert(prof->name);

    strncpy(buf, prof->name, buflen);
    buf[buflen-1] = '\0';

    memset(sinks, 0, sizeof(char *) * (MAX_CARD_TARGET+1));
    memset(sources, 0, sizeof(char *) * (MAX_CARD_TARGET+1));

    do {
        if (!strncmp(p, "output:", 7)) {
            if (i >= MAX_CARD_TARGET) {
                pa_log_debug("number of outputs exeeds the maximum %d in "
                             "profile name '%s'", MAX_CARD_TARGET, prof->name);
                return;
            }
            sinks[i++] = get_name(&p, 7);
        }
        else if (!strncmp(p, "input:", 6)) {
            if (j >= MAX_CARD_TARGET) {
                pa_log_debug("number of inputs exeeds the maximum %d in "
                             "profile name '%s'", MAX_CARD_TARGET, prof->name);
                return;
            }
            sources[j++] = get_name(&p, 6);
        }
        else {
            pa_log("%s: failed to parse profile name '%s'",
                   __FILE__, prof->name);
            return;
        }
    } while (*p);
}


static const char *node_key(struct userdata *u, mir_direction direction,
                      void *data, pa_device_port *port, char *buf, size_t len)
{
    pa_card         *card;
    pa_card_profile *profile;
    const char      *bus;
    bool             pci;
    bool             usb;
    bool             bluetooth;
    bool             platform;
    char            *type;
    const char      *name;
    const char      *profile_name;
    const char      *key;

    pa_assert(u);
    pa_assert(data);
    pa_assert(buf);
    pa_assert(direction == mir_input || direction == mir_output);

    if (direction == mir_output) {
        pa_sink *sink = data;
        type = pa_xstrdup("sink");
        name = pa_utils_get_sink_name(sink);
        card = sink->card;
        if (!port)
            port = sink->active_port;
    }
    else {
        pa_source *source = data;
        type = pa_xstrdup("source");
        name = pa_utils_get_source_name(source);
        card = source->card;
        if (!port)
            port = source->active_port;
    }

    if (!card)
        return NULL;

    pa_assert_se((profile = card->active_profile));

    if (!u->state.profile)
        profile_name = profile->name;
    else {
        pa_log_debug("state.profile is not null. '%s' supresses '%s'",
                     u->state.profile, profile->name);
        profile_name = u->state.profile;
    }


    if (!(bus = pa_utils_get_card_bus(card))) {
        pa_log_debug("ignoring %s '%s' due to lack of '%s' property "
                     "on its card", type, name, PA_PROP_DEVICE_BUS);
        return NULL;
    }

    pci = pa_streq(bus, "pci");
    usb = pa_streq(bus, "usb");
    platform = pa_streq(bus, "platform");
    bluetooth = pa_streq(bus, "bluetooth");

    if (!pci && !usb && !bluetooth && !platform) {
        pa_log_debug("ignoring %s '%s' due to unsupported bus type '%s' "
                     "of its card", type, name, bus);
        return NULL;
    }

    if (bluetooth) {
        if (!port)
            key = NULL;
        else {
            key = buf;
            snprintf(buf, len, "%s@%s.%s", name, port->name, profile_name);
        }
    }
    else {
        if (!port)
            key = name;
        else {
            key = buf;
            snprintf(buf, len, "%s@%s", name, port->name);
        }
    }

    return key;
}

static pa_sink *make_output_prerouting(struct userdata *u,
                                       mir_node        *data,
                                       pa_channel_map  *chmap,
                                       const char      *media_role,
                                       mir_node       **target_ret)
{
    pa_core    *core;
    mir_node   *target;
    pa_sink    *sink = NULL;

    pa_assert(u);
    pa_assert(data);
    pa_assert(chmap);
    pa_assert_se((core = u->core));


    target = mir_router_make_prerouting(u, data);

    if (!target)
        pa_log("there is no default route for the stream '%s'", data->amname);
    else if (target->paidx == PA_IDXSET_INVALID)
        pa_log("can't route to default '%s': no sink", target->amname);
    else {
        if (!(sink = pa_idxset_get_by_index(core->sinks, target->paidx)))
            pa_log("no route to default '%s': sink is gone", target->amname);
        else {
            if (u->enable_multiplex == true) {
                if (pa_classify_multiplex_stream(data)) {
                    data->mux = pa_multiplex_create(u->multiplex, core,
                                                    sink->index, chmap, NULL,
                                                    media_role, data->type);
                    if (data->mux) {
                        sink = pa_idxset_get_by_index(core->sinks,
                                                      data->mux->sink_index);
                        pa_assert(sink);
                    }
                }
            }
        }
    }

    if (target_ret)
        *target_ret = target;

    return sink;
}


static pa_source *make_input_prerouting(struct userdata *u,
                                        mir_node        *data,
                                        const char      *media_role,
                                        mir_node       **target_ret)
{
    pa_core    *core;
    mir_node   *target;
    pa_source  *source = NULL;

    pa_assert(u);
    pa_assert(data);
    pa_assert_se((core = u->core));

    target = mir_router_make_prerouting(u, data);

    if (!target)
        pa_log("there is no default route for the stream '%s'", data->amname);
    else if (target->paidx == PA_IDXSET_INVALID)
        pa_log("can't route to default '%s': no source", target->amname);
    else {
        if (!(source = pa_idxset_get_by_index(core->sources, target->paidx)))
            pa_log("no route to default '%s': source is gone",target->amname);
    }

    if (target_ret)
        *target_ret = target;

    return source;
}

static mir_node_type get_stream_routing_class(pa_proplist *pl)
{
    mir_node_type t;

    pa_assert(pl);

    t = pa_utils_get_stream_class(pl);

    if (t >= mir_application_class_begin && t <  mir_application_class_end)
        return t;

    return mir_node_type_unknown;
}

static const char *get_stream_amname(mir_node_type type, const char *name, pa_proplist *pl)
{
    const char *appid;

    switch (type) {

    case mir_radio:
        return pa_xstrdup("radio");

    case mir_player:
    case mir_game:
    case mir_browser:
    case mir_camera:
        appid = pa_utils_get_appid(pl);

        if (!strcmp(appid, "threaded-ml")         ||
            !strcmp(appid, "WebProcess")          ||
            !strcmp(appid,"wrt_launchpad_daemon")  )
        {
            return "wrtApplication";
        }
        return "icoApplication";

    case mir_navigator:
        return "navigator";

    case mir_phone:
        return "phone";

    default:
        return name;
    }
}


static void set_bluetooth_profile(struct userdata *u,
                                  pa_card *card,
                                  pa_direction_t direction)
{
    pa_core *core;
    pa_device_port *port;
    pa_card_profile *prof, *make_active;
    void *state0, *state1;
    bool port_available;
    bool switch_off;
    int nport;

    pa_assert(u);
    pa_assert(card);
    pa_assert_se((core = u->core));

    make_active = NULL;
    switch_off = false;
    nport = 0;

    pa_log_debug("which profile to make active:");

    PA_HASHMAP_FOREACH(prof, card->profiles, state0) {
        if (!prof->n_sinks && !prof->n_sources) {
            if (!make_active) {
                pa_log_debug("   considering %s", prof->name);
                make_active = prof;
                switch_off = true;
            }
        }
        else {
            port_available = false;

            PA_HASHMAP_FOREACH(port, card->ports, state1) {
                if ((direction & port->direction) &&
                    pa_hashmap_get(port->profiles, prof->name))
                {
                    port_available = (port->available != PA_AVAILABLE_NO);
                    break;
                }
            }

            if (!port_available)
                pa_log_debug("   ruling out %s (port not available)", prof->name);
            else if (prof->available != PA_AVAILABLE_YES)
                pa_log_debug("   ruling out %s (profile not available)", prof->name);
            else {
                nport++;

                if (((direction & PA_DIRECTION_INPUT)  && prof->n_sources > 0) ||
                    ((direction & PA_DIRECTION_OUTPUT) && prof->n_sinks   > 0)   ) {
                    if (make_active && prof->priority < make_active->priority)
                        pa_log_debug("   ruling out %s (low priority)", prof->name);
                    else {
                        pa_log_debug("   considering %s", prof->name);
                        make_active = prof;
                    }
                }
                else {
                    pa_log_debug("   ruling out %s (direction)", prof->name);
                }
            }
        }
    }

    if (!make_active)
        pa_log_debug("No suitable profile found. Frustrated and do nothing");
    else {
        if (make_active == card->active_profile)
            pa_log_debug("Profile %s already set. Do nothing", make_active->name);
        else {
            if (switch_off && nport) {
                pa_log_debug("Do not switch to %s as active ports are existing "
                             "to the other direction", make_active->name);
            }
            else {
                pa_log_debug("Set profile %s", make_active->name);

                if ((prof = pa_hashmap_get(card->profiles, make_active->name)) != NULL &&
                    pa_card_set_profile(card, prof, false) < 0) {
                    pa_log_debug("Failed to change profile to %s",
                                 make_active->name);
                }
            }
        }
    }
}

static void deferred_routing_cb(pa_mainloop_api *m, void *d)
{
    struct userdata *u = d;

    (void)m;

    pa_assert(u);

    pa_log_debug("deferred routing starts");

    mir_router_make_routing(u);
}


static void schedule_deferred_routing(struct userdata *u)
{
    pa_core *core;

    pa_assert(u);
    pa_assert_se((core = u->core));

    pa_log_debug("scheduling deferred routing");

    pa_mainloop_api_once(core->mainloop, deferred_routing_cb, u);
}


static void card_check_cb(pa_mainloop_api *m, void *d)
{
    card_check_t *cc = d;
    struct userdata *u;
    pa_core *core;
    pa_card *card;
    pa_sink *sink;
    pa_source *source;
    int n_sink, n_source;
    uint32_t idx;

    (void)m;

    pa_assert(cc);
    pa_assert((u = cc->u));
    pa_assert((core = u->core));

    pa_log_debug("card check starts");

    if (!(card = pa_idxset_get_by_index(core->cards, cc->index)))
        pa_log_debug("card %u is gone", cc->index);
    else {
        n_sink = n_source = 0;

        PA_IDXSET_FOREACH(sink, core->sinks, idx) {
            if ((sink->card) && sink->card->index == card->index)
                n_sink++;
        }

        PA_IDXSET_FOREACH(source, core->sources, idx) {
            if ((source->card) && source->card->index == card->index)
                n_sink++;
        }

        if (n_sink || n_source) {
            pa_log_debug("found %u sinks and %u sources belonging to "
                         "'%s' card", n_sink, n_source, card->name);
            pa_log_debug("nothing to do");
        }
        else {
            pa_log_debug("card '%s' has no sinks/sources. Do routing ...",
                         card->name);
            mir_router_make_routing(u);
        }
    }

    pa_xfree(cc);
}


static void schedule_card_check(struct userdata *u, pa_card *card)
{
    pa_core *core;
    card_check_t *cc;

    pa_assert(u);
    pa_assert(card);
    pa_assert_se((core = u->core));

    pa_log_debug("scheduling card check");

    cc = pa_xnew0(card_check_t, 1);
    cc->u = u;
    cc->index = card->index;

    pa_mainloop_api_once(core->mainloop, card_check_cb, cc);
}


static void source_cleanup_cb(pa_mainloop_api *m, void *d)
{
    source_cleanup_t *sc = d;
    struct userdata *u;
    pa_core *core;

    (void)m;

    pa_assert(sc);
    pa_assert((u = sc->u));
    pa_assert((core = u->core));

    pa_log_debug("source cleanup starts");

    pa_loopback_destroy(u->loopback, u->core, sc->loop);
    pa_multiplex_destroy(u->multiplex, u->core, sc->mux);

    pa_log_debug("source cleanup ends");

    pa_xfree(sc);
}


static void schedule_source_cleanup(struct userdata *u, mir_node *node)
{
    pa_core *core;
    source_cleanup_t *sc;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((core = u->core));

    pa_log_debug("scheduling source cleanup");

    sc = pa_xnew0(source_cleanup_t, 1);
    sc->u = u;
    sc->mux = node->mux;
    sc->loop = node->loop;

    node->mux = NULL;
    node->loop = NULL;

    pa_mainloop_api_once(core->mainloop, source_cleanup_cb, sc);
}


#if 0
static void stream_uncork_cb(pa_mainloop_api *m, void *d)
{
    stream_uncork_t *suc = d;
    struct userdata *u;
    pa_core *core;
    pa_sink *sink;
    pa_sink_input *sinp;
    uint32_t index;

    (void)m;

    pa_assert(suc);
    pa_assert((u = suc->u));
    pa_assert((core = u->core));

    pa_log_debug("start uncorking stream");

    if (!(sink = pa_idxset_get_by_index(core->sinks, suc->index))) {
        pa_log_debug("sink.%d gone", suc->index);
        goto out;
    }

    if (!(sinp = pa_idxset_first(core->sink_inputs, &index))) {
        pa_log_debug("sink_input is gone");
        goto out;
    }

    pa_sink_input_cork(sinp, false);

    pa_log_debug("stream.%u uncorked", sinp->index);

 out:

    pa_xfree(suc);
}


static void schedule_stream_uncorking(struct userdata *u, pa_sink *sink)
{
    pa_core *core;
    stream_uncork_t *suc;

    pa_assert(u);
    pa_assert(sink);
    pa_assert_se((core = u->core));

    pa_log_debug("scheduling stream uncorking");

    suc = pa_xnew0(stream_uncork_t, 1);
    suc->u = u;
    suc->index = sink->index;

    pa_mainloop_api_once(core->mainloop, stream_uncork_cb, suc);
}
#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
