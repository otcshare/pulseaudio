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
#include <string.h>
#include <errno.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/core-util.h>
#include <pulsecore/namereg.h>

#include <pulsecore/card.h>
#include <pulsecore/sink.h>
#include <pulsecore/device-port.h>
#include <pulsecore/source.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>

#include "switch.h"
#include "node.h"
#include "multiplex.h"
#include "loopback.h"
#include "discover.h"
#include "utils.h"
#include "classify.h"

static bool setup_explicit_stream2dev_link(struct userdata *,
                                                mir_node *,
                                                mir_node *);
static bool teardown_explicit_stream2dev_link(struct userdata*, mir_node*,
                                                   mir_node *);

static bool setup_default_dev2stream_link(struct userdata *, mir_node *,
                                               mir_node *);
static bool setup_default_stream2dev_link(struct userdata *, mir_node *,
                                               mir_node *);
static bool setup_default_dev2dev_link(struct userdata *,
                                            mir_node *,
                                            mir_node *);

static pa_source *setup_device_input(struct userdata *, mir_node *);
static pa_sink   *setup_device_output(struct userdata *, mir_node *);

static bool set_profile(struct userdata *, mir_node *);
static bool set_port(struct userdata *, mir_node *);


bool mir_switch_setup_link(struct userdata *u,
                                mir_node *from,
                                mir_node *to,
                                bool explicit)
{
    pa_core *core;

    pa_assert(u);
    pa_assert_se((core = u->core));
    pa_assert(!from || from->direction == mir_input);
    pa_assert(!to || to->direction == mir_output);

    if (explicit) {
        /*
         * links for explic routes
         */
        pa_assert(from);
        pa_assert(to);

        switch (from->implement) {

        case mir_stream:
            switch (to->implement) {

            case mir_stream:
                pa_log_debug("routing to streams is not implemented yet");
                break;

            case mir_device:
                if (!setup_explicit_stream2dev_link(u, from, to))
                    return false;
                break;

            default:
                pa_log("%s: can't setup link: invalid sink node "
                       "implement", __FILE__);
                return false;
            }
            break;

        case mir_device:
            pa_log_debug("input device routing is not implemented yet");
            break;

        default:
            pa_log("%s: can't setup link: invalid source node "
                   "implement", __FILE__);
            return false;
        }
    }
    else {
        /*
         * links for default routes
         */
        pa_assert(from || to);

        if (to) {
            /* default input route */
            switch (to->implement) {

            case mir_stream:
                pa_assert(from);

                switch (from->implement) {
                case mir_stream:
                    pa_log_debug("routing between streams is not implemented");
                    break;

                case mir_device:
                    if (!setup_default_dev2stream_link(u, from, to))
                        return false;
                    break;

                default:
                    pa_log("%s: can't setup link: invalid source node "
                           "implement", __FILE__);
                    return false;
                }
                break;

            case mir_device:
                if (!from)
                    /* default output preroute */
                    return setup_device_output(u, to) != NULL;
                else {
                    switch (from->implement) {

                    case mir_stream:
                        if (!setup_default_stream2dev_link(u, from, to))
                            return false;
                        break;

                    case mir_device:
                        if (!setup_default_dev2dev_link(u, from, to))
                            return false;
                        break;

                    default:
                        pa_log("%s: can't setup link: invalid source node "
                               "implement", __FILE__);
                        return false;
                    }
                }
                break;

            default:
                pa_log("%s: can't setup link: invalid sink node "
                       "implement", __FILE__);
                return false;
            }
        }
        else {
            /* default input preroute */
            pa_assert(from && from->implement == mir_device);

            return setup_device_input(u, from) != NULL;
        }
    }

    pa_log_debug("link %s => %s is established", from->amname, to->amname);

    return true;
}

bool mir_switch_teardown_link(struct userdata *u,
                                   mir_node        *from,
                                   mir_node        *to)
{
    pa_core *core;

    pa_assert(u);
    pa_assert(from);
    pa_assert(to);
    pa_assert_se((core = u->core));
    pa_assert(from->direction == mir_input);
    pa_assert(to->direction == mir_output);


    switch (from->implement) {

    case mir_stream:
        switch (to->implement) {

        case mir_stream: /* stream -> stream */
            pa_log_debug("routing to streams is not implemented yet");
            break;

        case mir_device: /* stream -> device */
            if (!teardown_explicit_stream2dev_link(u, from, to))
                return false;
            break;

        default:
            pa_log("%s: can't teardown link: invalid sink node "
                   "implement", __FILE__);
            return false;
        }
        break;

    case mir_device: /* device -> stream | device->device */
        pa_log_debug("input device routing is not implemented yet");
        break;

    default:
        pa_log("%s: can't teardown link: invalid source node "
               "implement", __FILE__);
        return false;
    }

    pa_log_debug("link %s => %s is torn down", from->amname, to->amname);

    return true;
}

static bool setup_explicit_stream2dev_link(struct userdata *u,
                                                mir_node *from,
                                                mir_node *to)
{
    pa_core       *core;
    pa_sink       *sink;
    pa_sink_input *sinp;
    pa_muxnode    *mux;

    pa_assert(u);
    pa_assert(from);
    pa_assert(to);
    pa_assert((core = u->core));

    if (!(sink = setup_device_output(u, to)))
        return false;

    if (!set_profile(u, from) || !set_port(u, from)) {
        pa_log("can't route from '%s'", from->amname);
        return false;
    }

    if ((mux = from->mux)) {
        sinp = pa_idxset_get_by_index(core->sink_inputs, mux->defstream_index);

        if (sinp && sinp->sink == sink) {
            if (!pa_multiplex_remove_default_route(core, mux, true))
                return false;
        }
        else if (pa_multiplex_duplicate_route(core, mux, NULL, sink)) {
            pa_log_debug("multiplex route %s => %s already exists",
                         from->amname, to->amname);
            return true;
        }
        else {
            if (!pa_multiplex_add_explicit_route(core, mux, sink, from->type))
                return false;
        }
    }
    else {
        if ((sinp = pa_idxset_get_by_index(core->sink_inputs, from->paidx))) {
            if (sinp->sink == sink)
                pa_log_debug("direct route already exists. nothing to do");
            else {
                pa_log_debug("direct route: sink-input.%u -> sink.%u",
                             sinp->index, sink->index);

                if (pa_sink_input_move_to(sinp, sink, false) < 0)
                    return false;
            }
        }
    }

    pa_log_debug("link %s => %s is established", from->amname, to->amname);

    return true;
}


static bool teardown_explicit_stream2dev_link(struct userdata *u,
                                                   mir_node *from,
                                                   mir_node *to)
{
    pa_core       *core;
    pa_sink       *sink;
    pa_sink_input *sinp;
    pa_muxnode    *mux;

    pa_assert(u);
    pa_assert(from);
    pa_assert(to);
    pa_assert((core = u->core));

    if ((mux = from->mux)) {
        if (!(sink = pa_idxset_get_by_index(core->sinks, to->paidx))) {
            pa_log_debug("can't find sink.%u", to->paidx);
            return false;
        }

        if (!pa_multiplex_remove_explicit_route(core, mux, sink)) {
            pa_log_debug("can't remove multiplex route on mux %u", mux->module_index);
            return false;
        }
    }
    else {
        if (!(sinp = pa_idxset_get_by_index(core->sink_inputs, from->paidx))) {
            pa_log_debug("can't find source.%u", from->paidx);
            return false;
        }

        if (!(sink = pa_utils_get_null_sink(u))) {
            pa_log_debug("can't remove direct route: no null sink");
            return false;
        }

        if (pa_sink_input_move_to(sinp, sink, false) < 0)
            return false;
    }

    pa_log_debug("link %s => %s is torn down", from->amname, to->amname);

    return true;
}


static bool setup_default_dev2stream_link(struct userdata *u,
                                               mir_node *from,
                                               mir_node *to)
{
    pa_core          *core;
    pa_source        *source;
    pa_source_output *sout;

    pa_assert(u);
    pa_assert(from);
    pa_assert(to);
    pa_assert((core = u->core));

    if (!(source = setup_device_input(u, from))) {
        pa_log_debug("can't route '%s': no source", from->amname);
        return false;
    }

    if (to->paidx == PA_IDXSET_INVALID) {
        pa_log_debug("can't route '%s': no source-output", to->amname);
        return false;
    }

    if (!(sout = pa_idxset_get_by_index(core->source_outputs, to->paidx))) {
        pa_log_debug("can't find sink input for '%s'", to->amname);
        return false;
    }

    pa_log_debug("direct route: source.%d -> source->output.%d",
                 source->index, sout->index);

    if (pa_source_output_move_to(sout, source, false) < 0)
        return false;

    return true;
}

static bool setup_default_stream2dev_link(struct userdata *u,
                                               mir_node *from,
                                               mir_node *to)
{
    pa_core       *core;
    pa_sink       *sink;
    pa_sink_input *sinp;
    pa_muxnode    *mux;
    int            n;

    pa_assert(u);
    pa_assert(from);
    pa_assert(to);
    pa_assert((core = u->core));

    if (!(sink = setup_device_output(u, to)))
        return false;

    if (!set_profile(u, from) || !set_port(u, from)) {
        pa_log("can't route from '%s'", from->amname);
        return false;
    }

    if ((mux = from->mux)) {
        if (mux->defstream_index == PA_IDXSET_INVALID) {
            if ((n = pa_multiplex_no_of_routes(core, mux)) < 0)
                return false;
            else if (n > 0) {
                pa_log_debug("currently mux %u has no default route",
                             mux->module_index);
                return true;
            }
            sinp = NULL;
        }
        else {
            sinp = pa_idxset_get_by_index(core->sink_inputs,
                                          mux->defstream_index);
        }

        if (!sinp) {
            /*
             * we supposed to have a default stream but the sink-input
             * on the combine side is not existing any more. This can
             * happen, for instance, if the sink, where it was connected,
             * died for some reason.
             */
            pa_log_debug("supposed to have a default stream on multiplex "
                         "%u but non was found. Trying to make one",
                         mux->module_index);
            if (pa_multiplex_duplicate_route(core, mux, sinp, sink)) {
                pa_log_debug("the default stream on mux %u would be a "
                             "duplicate to an explicit route. "
                             "Removing it ...", mux->module_index);
                mux->defstream_index = PA_IDXSET_INVALID;
                return true; /* the routing is a success */
            }

            if (!pa_multiplex_add_default_route(core, mux,sink, from->type)) {
                pa_log_debug("failed to add default route on mux %d",
                             mux->module_index);
                mux->defstream_index = PA_IDXSET_INVALID;
                return false;
            }
        }
        else if (pa_multiplex_duplicate_route(core, mux, sinp, sink)) {
            pa_log_debug("the default stream on mux %u would be a duplicate "
                         "to an explicit route. Removing it ...",
                         mux->module_index);
            return true;        /* the routing is a success */
        }

        if (sinp) {
            pa_log_debug("multiplex route: sink-input.%d -> (sink.%d - "
                         "sink-input.%d) -> sink.%d", from->paidx,
                         mux->sink_index, sinp->index, sink->index);
        }
        else {
            pa_log_debug("multiplex route: sink-input.%d -> (sink.%d - "
                         "sink-input) -> sink.%d", from->paidx,
                         mux->sink_index, sink->index);
        }

        if (!pa_multiplex_change_default_route(core, mux, sink))
            return false;
    }
    else {
        if (from->paidx == PA_IDXSET_INVALID) {
            pa_log_debug("can't route '%s': no sink-input", to->amname);
            return false;
        }

        if (!(sinp = pa_idxset_get_by_index(core->sink_inputs, from->paidx))) {
            pa_log_debug("can't find sink input for '%s'", from->amname);
            return false;
        }

        pa_log_debug("direct route: sink-input.%d -> sink.%d",
                     sinp->index, sink->index);

        if (pa_sink_input_move_to(sinp, sink, false) < 0)
            return false;
    }

    return true;
}

static bool setup_default_dev2dev_link(struct userdata *u,
                                            mir_node *from,
                                            mir_node *to)
{
    pa_core       *core;
    pa_sink       *sink;
    pa_sink_input *sinp;
    pa_muxnode    *mux;
    pa_loopnode   *loop;
    mir_node_type  type;
    int            n;

    pa_assert(u);
    pa_assert(from);
    pa_assert(to);
    pa_assert((core = u->core));

    if (!(loop = from->loop)) {
        pa_log_debug("source is not looped back");
        return false;
    }

    if (!(sink = setup_device_output(u, to)))
        return false;

    if ((mux = from->mux)) {
        if (mux->defstream_index == PA_IDXSET_INVALID) {
            if ((n = pa_multiplex_no_of_routes(core, mux)) < 0)
                return false;
            else if (n > 0) {
                pa_log_debug("currently mux %u has no default route",
                             mux->module_index);
                return true;
            }
            sinp = NULL;
        }
        else {
            sinp = pa_idxset_get_by_index(core->sink_inputs,
                                          mux->defstream_index);
        }

        if (!sinp) {
            /*
             * we supposed to have a default stream but the sink-input
             * on the combine side is not existing any more. This can
             * happen, for instance, if the sink, where it was connected,
             * died for some reason.
             */
            pa_log_debug("supposed to have a default stream on multiplex "
                         "%u but non was found. Trying to make one",
                         mux->module_index);
            if (pa_multiplex_duplicate_route(core, mux, sinp, sink)) {
                pa_log_debug("the default stream on mux %u would be a "
                             "duplicate to an explicit route. "
                             "Removing it ...", mux->module_index);
                mux->defstream_index = PA_IDXSET_INVALID;
                return true; /* the routing is a success */
            }

            type = pa_classify_guess_application_class(from);

            if (!pa_multiplex_add_default_route(core, mux,sink, type)) {
                pa_log_debug("failed to add default route on mux %d",
                             mux->module_index);
                mux->defstream_index = PA_IDXSET_INVALID;
                return false;
            }
        }
        else if (pa_multiplex_duplicate_route(core, mux, sinp, sink)) {
            pa_log_debug("the default stream on mux %u would be a duplicate "
                         "to an explicit route. Removing it ...",
                         mux->module_index);
            return true;        /* the routing is a success */
        }

        if (sinp) {
            pa_log_debug("multiplex route: source.%d -> "
                         "(source-output - sink-input %d) -> (sink.%d - "
                         "sink-input.%d) -> sink.%d", from->paidx,
                         loop->sink_input_index,
                         mux->sink_index, sinp->index, sink->index);
        }
        else {
            pa_log_debug("multiplex route: source.%d -> "
                         "(source-output - sink-input.%d) -> (sink.%d - "
                         "sink-input) -> sink.%d", from->paidx,
                         loop->sink_input_index,
                         mux->sink_index, sink->index);
        }

        if (!pa_multiplex_change_default_route(core, mux, sink))
            return false;
    }
    else {
        sinp = pa_idxset_get_by_index(core->sink_inputs,
                                      loop->sink_input_index);

        if (!sinp) {
            pa_log_debug("can't find looped back sink input for '%s'",
                         from->amname);
            return false;
        }

        pa_log_debug("loopback route: source.%d -> (source-output - "
                     "sink-input.%d) -> sink.%d",
                     from->paidx, sinp->index, sink->index);

        if (pa_sink_input_move_to(sinp, sink, false) < 0)
            return false;
    }

    return true;
}

static pa_source *setup_device_input(struct userdata *u, mir_node *node)
{
    pa_core *core;
    pa_source *source;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((core = u->core));

    if (!set_profile(u, node) || !set_port(u, node)) {
        pa_log("can't route to '%s'", node->amname);
        return NULL;
    }

    if (node->paidx == PA_IDXSET_INVALID) {
        pa_log_debug("can't route to '%s': no source", node->amname);
        return NULL;
    }

    if (!(source = pa_idxset_get_by_index(core->sources, node->paidx))) {
        pa_log_debug("can't route to '%s': cant find source", node->amname);
        return NULL;
    }

    return source;
}


static pa_sink *setup_device_output(struct userdata *u, mir_node *node)
{
    pa_core *core;
    pa_sink *sink;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((core = u->core));

    if (!set_profile(u, node) || !set_port(u, node)) {
        pa_log("can't route to '%s'", node->amname);
        return NULL;
    }

    if (node->paidx == PA_IDXSET_INVALID) {
        pa_log_debug("can't route to '%s': no sink", node->amname);
        return NULL;
    }

    if (!(sink = pa_idxset_get_by_index(core->sinks, node->paidx))) {
        pa_log_debug("can't route to '%s': cant find sink", node->amname);
        return NULL;
    }

    return sink;
}


static bool set_profile(struct userdata *u, mir_node *node)
{
    pa_core         *core;
    pa_card         *card;
    pa_card_profile *prof;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((core = u->core));

    if (node->implement != mir_device)
        return true;

    if (node->type == mir_bluetooth_a2dp ||
        node->type == mir_bluetooth_sco)
    {
        card = pa_idxset_get_by_index(core->cards, node->pacard.index);

        if (!card) {
            pa_log("can't find card for '%s'", node->amname);
            return false;
        }

        pa_assert_se(prof = card->active_profile);

        if (!pa_streq(node->pacard.profile, prof->name)) {
            pa_log_debug("changing profile '%s' => '%s'",
                         prof->name, node->pacard.profile);

            if (u->state.profile) {
                pa_log("nested profile setting is not allowed. won't change "
                       "'%s' => '%s'", prof->name, node->pacard.profile);
                return false;
            }

            u->state.profile = node->pacard.profile;

            if ((prof = pa_hashmap_get(card->profiles, node->pacard.profile)) != NULL)
                pa_card_set_profile(card, prof, false);

            u->state.profile = NULL;
        }
    }

    return true;
}



static bool set_port(struct userdata *u, mir_node *node)
{
    pa_core   *core;
    pa_sink   *sink;
    pa_source *source;
    pa_device_port *port;
    mir_node  *oldnode;
    void      *data  = NULL;
    uint32_t   paidx = PA_IDXSET_INVALID;

    pa_assert(u);
    pa_assert(node);
    pa_assert(node->paname);
    pa_assert_se((core = u->core));

    if (node->direction != mir_input && node->direction != mir_output)
        return false;

    if (node->implement != mir_device)
        return true;

    if (!node->paport)
        return true;

    if (node->direction == mir_input) {
        source = pa_namereg_get(core, node->paname, PA_NAMEREG_SOURCE);

        if (!(data = source)) {
            pa_log("can't set port for '%s': source not found",
                   node->paname);
            return false;
        }

        if ((port = source->active_port) && pa_streq(node->paport, port->name))
            return true;

        if (pa_source_set_port(source, node->paport, false) < 0)
            return false;

        paidx = source->index;
    }

    if (node->direction == mir_output) {
        sink = pa_namereg_get(core, node->paname, PA_NAMEREG_SINK);

        if (!(data = sink)) {
            pa_log("can't set port for '%s': sink not found",
                   node->paname);
            return false;
        }

        if ((port = sink->active_port) && pa_streq(node->paport, port->name))
            return true;

        if (pa_sink_set_port(sink, node->paport, false) < 0)
            return false;

        paidx = sink->index;
    }

    if ((oldnode = pa_discover_remove_node_from_ptr_hash(u, data)))
        oldnode->paidx = PA_IDXSET_INVALID;

    node->paidx = paidx;
    pa_discover_add_node_to_ptr_hash(u, data, node);


    return true;
}



/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
