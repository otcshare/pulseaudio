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
#include <stdlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/socket.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/core-util.h>
#include <pulsecore/sink.h>
#include <pulsecore/card.h>
#include <pulsecore/source.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>

#include "userdata.h"
#include "utils.h"
#include "node.h"


#define DEFAULT_NULL_SINK_NAME "null.mir"

struct pa_null_sink {
    char      *name;
    uint32_t   module_index;
    uint32_t   sink_index;
};


static uint32_t stamp;

static char *stream_name(pa_proplist *);
static bool get_unsigned_property(pa_proplist *, const char *,uint32_t *);


pa_null_sink *pa_utils_create_null_sink(struct userdata *u, const char *name)
{
    pa_core      *core;
    pa_module    *module;
    pa_null_sink *ns;
    pa_sink      *sink;
    pa_sink      *s;
    uint32_t      idx;
    char          args[256];

    pa_assert(u);
    pa_assert_se((core = u->core));


    if (!name)
        name = DEFAULT_NULL_SINK_NAME;


    snprintf(args, sizeof(args), "sink_name=\"%s\" channels=2", name);
    module = pa_module_load(core, "module-null-sink", args);
    sink = NULL;

    if (!module)
        pa_log("failed to load null sink '%s'", name);
    else {
        PA_IDXSET_FOREACH(s, core->sinks, idx) {
            if (s->module && s->module == module) {
                sink = s;
                pa_log_info("mir null sink is '%s'", name);
                break;
            }
        }
    }

    ns = pa_xnew0(pa_null_sink, 1);
    ns->name = pa_xstrdup(name);
    ns->module_index = module ? module->index : PA_IDXSET_INVALID;
    ns->sink_index = sink ? sink->index : PA_IDXSET_INVALID;

    return ns;
}

void pa_utils_destroy_null_sink(struct userdata *u)
{
    pa_core      *core;
    pa_module    *module;
    pa_null_sink *ns;

    if (u && (ns = u->nullsink) && (core = u->core)) {
        if ((module = pa_idxset_get_by_index(core->modules,ns->module_index))){
            pa_log_info("unloading null sink '%s'", ns->name);
            pa_module_unload(core, module, false);
        }

        pa_xfree(ns->name);
        pa_xfree(ns);
    }
}

pa_sink *pa_utils_get_null_sink(struct userdata *u)
{
    pa_core *core;
    pa_null_sink *ns;

    pa_assert(u);
    pa_assert_se((core = u->core));
    pa_assert_se((ns = u->nullsink));

    return pa_idxset_get_by_index(core->sinks, ns->sink_index);
}

pa_source *pa_utils_get_null_source(struct userdata *u)
{
    pa_sink *ns = pa_utils_get_null_sink(u);

    return ns ? ns->monitor_source : NULL;
}



const char *pa_utils_get_card_name(pa_card *card)
{
    return (card && card->name) ? card->name : "<unknown>";
}

const char *pa_utils_get_card_bus(pa_card *card)
{
    const char *bus = NULL;
    const char *name;

    if (card && !(bus = pa_proplist_gets(card->proplist,PA_PROP_DEVICE_BUS))) {
        name = pa_utils_get_card_name(card);
        if (!strncmp(name, "alsa_card.", 10)) {
            if (!strncmp(name + 10, "pci-", 4))
                bus = "pci";
	    else if (!strncmp(name + 10, "platform-", 9))
		bus = "platform";
            else if (!strncmp(name + 10, "usb-", 4))
                bus = "usb";
        }
    }

    return (char *)bus;
}

const char *pa_utils_get_sink_name(pa_sink *sink)
{
    return (sink && sink->name) ? sink->name : "<unknown>";
}

const char *pa_utils_get_source_name(pa_source *source)
{
    return (source && source->name) ? source->name : "<unknown>";
}

const char *pa_utils_get_sink_input_name(pa_sink_input *sinp)
{
    char *name;

    if (sinp && sinp->proplist && (name = stream_name(sinp->proplist)))
        return name;

    return "<unknown>";
}

const char *pa_utils_get_sink_input_name_from_data(pa_sink_input_new_data *data)
{
    char *name;

    if (data && (name = stream_name(data->proplist)))
        return name;

    return "<unknown>";
}


const char *pa_utils_get_source_output_name(pa_source_output *sout)
{
    char *name;

    if (sout && sout->proplist && (name = stream_name(sout->proplist)))
        return name;

    return "<unknown>";
}

const char *pa_utils_get_source_output_name_from_data(pa_source_output_new_data*data)
{
    char *name;

    if (data && (name = stream_name(data->proplist)))
        return name;

    return "<unknown>";
}

char *pa_utils_get_zone(pa_proplist *pl)
{
    const char *zone;

    pa_assert(pl);

    if (!(zone = pa_proplist_gets(pl, PA_PROP_ZONE_NAME)))
        zone = PA_ZONE_NAME_DEFAULT;

    return (char *)zone;
}

const char *pa_utils_get_appid(pa_proplist *pl)
{
    const char *appid;

    if (pl && (appid = pa_proplist_gets(pl, PA_PROP_RESOURCE_SET_APPID)))
        return appid;

    return "<unknown>";
}

bool pa_utils_set_stream_routing_properties(pa_proplist *pl,
                                                 int          styp,
                                                 void        *target)
{
    const char    *clnam;
    const char    *method;
    char           clid[32];

    pa_assert(pl);
    pa_assert(styp >= 0);

    snprintf(clid, sizeof(clid), "%d", styp);
    clnam  = mir_node_type_str(styp);
    method = target ? PA_ROUTING_EXPLICIT : PA_ROUTING_DEFAULT;

    if (pa_proplist_sets(pl, PA_PROP_ROUTING_CLASS_NAME, clnam ) < 0 ||
        pa_proplist_sets(pl, PA_PROP_ROUTING_CLASS_ID  , clid  ) < 0 ||
        pa_proplist_sets(pl, PA_PROP_ROUTING_METHOD    , method) < 0  )
    {
        pa_log("failed to set some stream property");
        return false;
    }

    return true;
}

bool pa_utils_unset_stream_routing_properties(pa_proplist *pl)
{
    pa_assert(pl);

    if (pa_proplist_unset(pl, PA_PROP_ROUTING_CLASS_NAME) < 0 ||
        pa_proplist_unset(pl, PA_PROP_ROUTING_CLASS_ID  ) < 0 ||
        pa_proplist_unset(pl, PA_PROP_ROUTING_METHOD    ) < 0  )
    {
        pa_log("failed to unset some stream property");
        return false;
    }

    return true;
}

void pa_utils_set_stream_routing_method_property(pa_proplist *pl,
                                                 bool explicit)
{
    const char *method = explicit ? PA_ROUTING_EXPLICIT : PA_ROUTING_DEFAULT;

    pa_assert(pl);

    if (pa_proplist_sets(pl, PA_PROP_ROUTING_METHOD, method) < 0) {
        pa_log("failed to set routing method property on sink-input");
    }
}

bool pa_utils_stream_has_default_route(pa_proplist *pl)
{
    const char *method;

    pa_assert(pl);

    method = pa_proplist_gets(pl, PA_PROP_ROUTING_METHOD);

    if (method && pa_streq(method, PA_ROUTING_DEFAULT))
        return true;

    return false;
}

int pa_utils_get_stream_class(pa_proplist *pl)
{
    const char *clid_str;
    char *e;
    unsigned long int clid = 0;

    pa_assert(pl);

    if ((clid_str = pa_proplist_gets(pl, PA_PROP_ROUTING_CLASS_ID))) {
        clid = strtoul(clid_str, &e, 10);

        if (*e)
            clid = 0;
    }

    return (int)clid;
}


bool pa_utils_set_resource_properties(pa_proplist *pl,
                                           pa_nodeset_resdef *resdef)
{
    char priority[32];
    char rsetflags[32];
    char audioflags[32];

    pa_assert(pl);

    if (!resdef)
        return false;

    snprintf(priority  , sizeof(priority)  , "%d", resdef->priority   );
    snprintf(rsetflags , sizeof(rsetflags) , "%d", resdef->flags.rset );
    snprintf(audioflags, sizeof(audioflags), "%d", resdef->flags.audio);

    if (pa_proplist_sets(pl, PA_PROP_RESOURCE_PRIORITY   , priority  ) < 0 ||
        pa_proplist_sets(pl, PA_PROP_RESOURCE_SET_FLAGS  , rsetflags ) < 0 ||
        pa_proplist_sets(pl, PA_PROP_RESOURCE_AUDIO_FLAGS, audioflags) < 0  )
    {
        pa_log("failed to set some resource property");
        return false;
    }

    return true;
}

bool pa_utils_unset_resource_properties(pa_proplist *pl)
{
    pa_assert(pl);

    if (pa_proplist_unset(pl, PA_PROP_RESOURCE_PRIORITY   ) < 0 ||
        pa_proplist_unset(pl, PA_PROP_RESOURCE_SET_FLAGS  ) < 0 ||
        pa_proplist_unset(pl, PA_PROP_RESOURCE_AUDIO_FLAGS) < 0  )
    {
        pa_log("failed to unset some resource property");
        return false;
    }

    return true;
}

pa_nodeset_resdef *pa_utils_get_resource_properties(pa_proplist *pl,
                                                    pa_nodeset_resdef *rd)
{
    int success;

    pa_assert(pl);
    pa_assert(rd);

    memset(rd, 0, sizeof(pa_nodeset_resdef));

    success  = get_unsigned_property(pl, PA_PROP_RESOURCE_PRIORITY,
                                     &rd->priority);
    success |= get_unsigned_property(pl, PA_PROP_RESOURCE_SET_FLAGS,
                                     &rd->flags.rset);
    success |= get_unsigned_property(pl, PA_PROP_RESOURCE_AUDIO_FLAGS,
                                     &rd->flags.audio);

    return success ? rd : NULL;
}


static bool get_unsigned_property(pa_proplist *pl,
                                       const char *name,
                                       uint32_t *value)
{
    const char *str;
    char *e;

    pa_assert(pl);
    pa_assert(name);
    pa_assert(value);

    if (!(str = pa_proplist_gets(pl, name))) {
        *value = 0;
        return false;
    }

    *value = strtoul(str, &e, 10);

    if (e == str || *e) {
        *value = 0;
        return false;
    }

    return true;
}


void pa_utils_set_port_properties(pa_device_port *port, mir_node *node)
{
    const char *profile;
    char propnam[512];
    char nodeidx[256];

    pa_assert(port);
    pa_assert(port->proplist);
    pa_assert(node);
    pa_assert_se((profile = node->pacard.profile));

    snprintf(propnam, sizeof(propnam), "%s.%s", PA_PROP_NODE_INDEX, profile);
    snprintf(nodeidx, sizeof(nodeidx), "%u", node->index);

    pa_proplist_sets(port->proplist, propnam, nodeidx);
}

mir_node *pa_utils_get_node_from_port(struct userdata *u,
                                      pa_device_port *port,
                                      void **state)
{
    const char *name;
    const char *value;
    char *e;
    uint32_t index = PA_IDXSET_INVALID;
    mir_node *node = NULL;

    pa_assert(u);
    pa_assert(port);
    pa_assert(port->proplist);

    while ((name = pa_proplist_iterate(port->proplist, state))) {
        if (!strncmp(name, PA_PROP_NODE_INDEX, sizeof(PA_PROP_NODE_INDEX)-1)) {
            if ((value = pa_proplist_gets(port->proplist, name))) {
                index = strtoul(value, &e, 10);
                node = NULL;

                if (value[0] && !e[0])
                    node = mir_node_find_by_index(u, index);

                if (node)
                    return node;

                pa_log("Can't find node %u for port %s", index, port->name);
            }
        }
    }

    return NULL;
}

mir_node *pa_utils_get_node_from_stream(struct userdata *u,
                                        mir_direction    type,
                                        void            *ptr)
{
    pa_sink_input    *sinp;
    pa_source_output *sout;
    pa_proplist      *pl;
    mir_node         *node;
    const char       *index_str;
    uint32_t          index = PA_IDXSET_INVALID;
    char             *e;
    char              name[256];

    pa_assert(u);
    pa_assert(ptr);
    pa_assert(type == mir_input || type == mir_output);

    if (type == mir_input) {
        sinp = (pa_sink_input *)ptr;
        pl = sinp->proplist;
        snprintf(name, sizeof(name), "sink-input.%u", sinp->index);
    }
    else {
        sout = (pa_source_output *)ptr;
        pl = sout->proplist;
        snprintf(name, sizeof(name), "source-output.%u", sout->index);
    }


    if ((index_str = pa_proplist_gets(pl, PA_PROP_NODE_INDEX))) {
        index = strtoul(index_str, &e, 10);
        if (e != index_str && *e == '\0') {
            if ((node = mir_node_find_by_index(u, index)))
                return node;

            pa_log_debug("can't find find node for %s", name);
        }
    }

    return NULL;
}

mir_node *pa_utils_get_node_from_data(struct userdata *u,
                                      mir_direction    type,
                                      void            *ptr)
{
    pa_sink_input_new_data *sinp;
    pa_source_output_new_data *sout;
    pa_proplist  *pl;
    mir_node     *node;
    const char   *index_str;
    uint32_t      index = PA_IDXSET_INVALID;
    char         *e;
    char          name[256];

    pa_assert(u);
    pa_assert(ptr);
    pa_assert(type == mir_input || type == mir_output);

    if (type == mir_input) {
        sinp = (pa_sink_input_new_data *)ptr;
        pl = sinp->proplist;
        snprintf(name, sizeof(name), "sink-input");
    }
    else {
        sout = (pa_source_output_new_data *)ptr;
        pl = sout->proplist;
        snprintf(name, sizeof(name), "source-output");
    }


    if ((index_str = pa_proplist_gets(pl, PA_PROP_NODE_INDEX))) {
        index = strtoul(index_str, &e, 10);
        if (e != index_str && *e == '\0') {
            if ((node = mir_node_find_by_index(u, index)))
                return node;

            pa_log_debug("can't find find node for %s", name);
        }
    }

    return NULL;
}

static char *stream_name(pa_proplist *pl)
{
    const char  *appnam;
    const char  *binnam;

    if ((appnam = pa_proplist_gets(pl, PA_PROP_APPLICATION_NAME)))
        return (char *)appnam;

    if ((binnam = pa_proplist_gets(pl, PA_PROP_APPLICATION_PROCESS_BINARY)))
        return (char *)binnam;

    return NULL;
}


const char *pa_utils_file_path(const char *dir, const char *file,
                               char *buf, size_t len)
{
    pa_assert(file);
    pa_assert(buf);
    pa_assert(len > 0);

    snprintf(buf, len, "%s/%s", dir, file);

    return buf;
}


uint32_t pa_utils_new_stamp(void)
{
    return ++stamp;
}

uint32_t pa_utils_get_stamp(void)
{
    return stamp;
}



/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
