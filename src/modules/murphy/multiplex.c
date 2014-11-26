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
#include <strings.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/def.h>
#include <pulsecore/thread.h>
#include <pulsecore/strlist.h>
#include <pulsecore/time-smoother.h>
#include <pulsecore/sink.h>
#include <pulsecore/sink-input.h>

#include "combine/userdata.h"

#include "multiplex.h"
#include "utils.h"

#ifndef DEFAULT_RESAMPLER
#define DEFAULT_RESAMPLER "speex-fixed-3"
#endif


static void copy_media_role_property(pa_sink *, pa_sink_input *);


pa_multiplex *pa_multiplex_init(void)
{
    pa_multiplex *multiplex = pa_xnew0(pa_multiplex, 1);

    return multiplex;
}


void pa_multiplex_done(pa_multiplex *multiplex, pa_core *core)
{
    pa_muxnode *mux, *n;

    PA_LLIST_FOREACH_SAFE(mux,n, multiplex->muxnodes) {
        pa_module_unload_by_index(core, mux->module_index, false);
    }
}



pa_muxnode *pa_multiplex_create(pa_multiplex   *multiplex,
                                pa_core        *core,
                                uint32_t        sink_index,
                                pa_channel_map *chmap,
                                const char     *resampler,
                                const char     *media_role,
                                int             type)
{
    static const char *modnam = "module-combine-sink-new";

    struct userdata *u;         /* combine's userdata! */
    struct output   *o;
    pa_muxnode      *mux;
    pa_sink         *sink;
    pa_sink_input   *sinp;
    pa_module       *module;
    char             args[512];
    uint32_t         idx;
    uint32_t         channels;

    pa_assert(core);

    if (!resampler)
        resampler = DEFAULT_RESAMPLER;

    if (!(sink = pa_idxset_get_by_index(core->sinks, sink_index))) {
        pa_log_debug("can't find the primary sink (index %u) for multiplexer",
                     sink_index);
        return NULL;
    }

    channels = chmap->channels ? chmap->channels : sink->channel_map.channels;

    snprintf(args, sizeof(args), "slaves=\"%s\" resample_method=\"%s\" "
             "channels=%u", sink->name, resampler, channels);

    if (!(module = pa_module_load(core, modnam, args))) {
        pa_log("failed to load module '%s %s'. can't multiplex", modnam, args);
        return NULL;
    }

    pa_assert_se((u = module->userdata));
    pa_assert(u->sink);

    u->no_reattach = true;

    mux = pa_xnew0(pa_muxnode, 1);
    mux->module_index = module->index;
    mux->sink_index = u->sink->index;
    mux->defstream_index = PA_IDXSET_INVALID;

    PA_LLIST_PREPEND(pa_muxnode, multiplex->muxnodes, mux);

    if (!(o = pa_idxset_first(u->outputs, &idx)))
        pa_log("can't find default multiplexer stream");
    else {
        if ((sinp = o->sink_input)) {
            if (media_role)
                pa_proplist_sets(sinp->proplist,PA_PROP_MEDIA_ROLE,media_role);
            pa_utils_set_stream_routing_properties(sinp->proplist, type, NULL);
            mux->defstream_index = sinp->index;
        }
    }

    pa_log_debug("multiplexer succesfully loaded");

    return mux;
}


void pa_multiplex_destroy(pa_multiplex *multiplex,
                          pa_core      *core,
                          pa_muxnode   *mux)
{
    pa_assert(multiplex);
    pa_assert(core);

    if (mux) {
        pa_module_unload_by_index(core, mux->module_index, false);
        PA_LLIST_REMOVE(pa_muxnode, multiplex->muxnodes, mux);
        pa_xfree(mux);
    }
}

pa_muxnode *pa_multiplex_find_by_sink(pa_multiplex *multiplex,
                                      uint32_t sink_index)
{
    pa_muxnode *mux;

    if (sink_index != PA_IDXSET_INVALID) {
        PA_LLIST_FOREACH(mux, multiplex->muxnodes) {
            if (sink_index == mux->sink_index) {
                pa_log_debug("muxnode found for sink %u", sink_index);
                return mux;
            }
        }
    }

    pa_log_debug("can't find muxnode for sink %u", sink_index);

    return NULL;
}

pa_muxnode *pa_multiplex_find_by_module(pa_multiplex *multiplex,
                                        pa_module    *module)
{
    uint32_t module_index;
    pa_muxnode *mux;

    pa_assert(multiplex);

    if (module) {
        module_index = module->index;

        PA_LLIST_FOREACH(mux, multiplex->muxnodes) {
            if (mux->module_index != PA_IDXSET_INVALID && module_index == mux->module_index)
                return mux;
        }
    }

    return NULL;
}

bool pa_multiplex_sink_input_remove(pa_multiplex  *multiplex,
                                         pa_sink_input *sinp)
{
    pa_muxnode *mux;
    const char *name;

    pa_assert(multiplex);
    pa_assert(sinp);

    if ((mux = pa_multiplex_find_by_module(multiplex, sinp->module))) {
        name = pa_utils_get_sink_input_name(sinp);

        pa_log_debug("multiplex (module %u) found for sink-input "
                     "(name %s)", mux->module_index, name);

        if (sinp->index == mux->defstream_index) {
            pa_log_debug("reseting default route on multiplex (module %u)",
                         mux->module_index);
            mux->defstream_index = PA_IDXSET_INVALID;
        }
        else {
            pa_log_debug("reseting explicit route on multiplex (module %u)",
                         mux->module_index);
        }

        return true;
    }

    return false;
}

bool pa_multiplex_add_default_route(pa_core    *core,
                                         pa_muxnode *mux,
                                         pa_sink    *sink,
                                         int         type)
{
    pa_module *module;
    pa_sink_input *sinp;
    struct userdata *u;         /* combine's userdata! */

    pa_assert(core);
    pa_assert(mux);
    pa_assert(sink);

    if (!(module = pa_idxset_get_by_index(core->modules, mux->module_index)))
        pa_log_debug("module %u is gone", mux->module_index);
    else {
        pa_assert_se((u = module->userdata));

        if (sink == u->sink) {
            pa_log("%s: mux %d refuses to make a loopback to itself",
                   __FILE__, mux->module_index);
        }
        else {
            pa_log_debug("adding default route to mux %u", mux->module_index);

            if (!(sinp = u->add_slave(u, sink))) {
                pa_log("failed to add new slave to mux %u", mux->module_index);
                return false;
            }

            copy_media_role_property(u->sink, sinp);
            pa_utils_set_stream_routing_properties(sinp->proplist, type, NULL);
            mux->defstream_index = sinp->index;

            return true;
        }
    }

    return false;
}

bool pa_multiplex_remove_default_route(pa_core *core,
                                            pa_muxnode *mux,
                                            bool transfer_to_explicit)
{
    pa_module       *module;
    pa_sink_input   *sinp;
    uint32_t         idx;
    struct userdata *u;         /* combine's userdata! */

    pa_assert(core);
    pa_assert(mux);

    if (!(module = pa_idxset_get_by_index(core->modules, mux->module_index)))
        pa_log_debug("module %u is gone", mux->module_index);
    else if ((idx = mux->defstream_index) == PA_IDXSET_INVALID)
        pa_log_debug("mux %u do not have default stream", mux->module_index);
    else if (!(sinp = pa_idxset_get_by_index(core->sink_inputs, idx)))
        pa_log("can't remove default route: sink-input %u is gone", idx);
    else {
        pa_assert_se((u = module->userdata));
        mux->defstream_index = PA_IDXSET_INVALID;

        if (transfer_to_explicit) {
            pa_log_debug("converting default route sink-input.%u -> sink.%u "
                         "to explicit", sinp->index, sinp->sink->index);
            pa_utils_set_stream_routing_method_property(sinp->proplist, true);
            return true;
        }
        else {
            u->remove_slave(u, sinp, NULL);
        }
    }

    return false;
}

bool pa_multiplex_change_default_route(pa_core    *core,
                                            pa_muxnode *mux,
                                            pa_sink    *sink)
{
    pa_module       *module;
    pa_sink_input   *sinp;
    uint32_t         idx;
    struct userdata *u;         /* combine's userdata! */

    pa_assert(core);
    pa_assert(mux);
    pa_assert(sink);

    if (!(module = pa_idxset_get_by_index(core->modules, mux->module_index)))
        pa_log_debug("module %u is gone", mux->module_index);
    else if ((idx = mux->defstream_index) == PA_IDXSET_INVALID)
        pa_log_debug("mux %u do not have default stream", mux->module_index);
    else if (!(sinp = pa_idxset_get_by_index(core->sink_inputs, idx)))
        pa_log("can't remove default route: sink-input %u is gone", idx);
    else {
        pa_assert_se((u = module->userdata));
        if (u->move_slave(u, sinp, sink) < 0)
            pa_log_debug("failed to move default stream on mux %u", mux->module_index);
        else {
            pa_log_debug("default stream was successfully moved on mux %u",
                         mux->module_index);
            return true;
        }
    }

    return false;
}


bool pa_multiplex_add_explicit_route(pa_core    *core,
                                          pa_muxnode *mux,
                                          pa_sink    *sink,
                                          int         type)
{
    pa_module *module;
    pa_sink_input *sinp;
    struct userdata *u;         /* combine's userdata! */

    pa_assert(core);
    pa_assert(mux);
    pa_assert(sink);

    if (!(module = pa_idxset_get_by_index(core->modules, mux->module_index)))
        pa_log_debug("module %u is gone", mux->module_index);
    else {
        pa_assert_se((u = module->userdata));

        if (sink == u->sink) {
            pa_log("%s: mux %d refuses to make a loopback to itself",
                   __FILE__, mux->module_index);
        }
        else {
            pa_log_debug("adding explicit route to mux %u", mux->module_index);

            if (!(sinp = u->add_slave(u, sink))) {
                pa_log("failed to add new slave to mux %u", mux->module_index);
                return false;
            }

            copy_media_role_property(u->sink, sinp);
            pa_utils_set_stream_routing_properties(sinp->proplist, type, sink);

            return true;
        }
    }

    return false;
}


bool pa_multiplex_remove_explicit_route(pa_core    *core,
                                             pa_muxnode *mux,
                                             pa_sink    *sink)
{
    pa_module *module;
    struct userdata *u;         /* combine's userdata! */

    pa_assert(core);
    pa_assert(mux);
    pa_assert(sink);

    if (!(module = pa_idxset_get_by_index(core->modules, mux->module_index)))
        pa_log_debug("module %u is gone", mux->module_index);
    else {
        pa_assert_se((u = module->userdata));

        u->remove_slave(u, NULL, sink);

        pa_log_debug("link to sink.%u removed", sink->index);

        return true;
    }

    return false;
}


bool pa_multiplex_duplicate_route(pa_core       *core,
                                       pa_muxnode    *mux,
                                       pa_sink_input *sinp,
                                       pa_sink       *sink)
{
    pa_module       *module;
    struct userdata *u;   /* combine's userdata! */
    struct output   *o;
    uint32_t         idx;
    pa_sink_input   *i;

    pa_assert(core);
    pa_assert(mux);
    pa_assert(sink);

    pa_log_debug("check for duplicate route on mux %u",
                 mux->module_index);

    if (!(module = pa_idxset_get_by_index(core->modules,mux->module_index)))
        pa_log_debug("module %u is gone", mux->module_index);
    else {
        pa_assert_se((u = module->userdata));

        PA_IDXSET_FOREACH(o, u->outputs, idx) {
            if (!(i = o->sink_input))
                continue;
            if (sinp && i == sinp)
                continue;
            if (i->sink == sink) {
                pa_log_debug("route sink-input.%u -> sink.%u is a duplicate",
                             i->index, sink->index);
                return true;
            }
        }

        if (!sinp)
            pa_log_debug("no duplicate route found to sink.%u", sink->index);
        else {
            pa_log_debug("no duplicate found for route sink-input.%u -> "
                         "sink.%u", sinp->index, sink->index);
        }
    }

    return false;
}

int pa_multiplex_no_of_routes(pa_core *core, pa_muxnode *mux)
{
    pa_module       *module;
    struct userdata *u;   /* combine's userdata! */

    pa_assert(core);
    pa_assert(mux);

    if (!(module = pa_idxset_get_by_index(core->modules,mux->module_index))) {
        pa_log_debug("module %u is gone", mux->module_index);
        return -1;
    }

    pa_assert_se((u = module->userdata));

    return (int)pa_idxset_size(u->outputs);
}


int pa_multiplex_print(pa_muxnode *mux, char *buf, int len)
{
    char *p, *e;

    pa_assert(buf);
    pa_assert(len > 0);

    e = (p = buf) + len;

    if (!mux)
        p += snprintf(p, e-p, "<not set>");
    else {
        p += snprintf(p, e-p, "module %u, sink %u, default stream %u",
                      mux->module_index, mux->sink_index,mux->defstream_index);
    }

    return p - buf;
}

static void copy_media_role_property(pa_sink *sink, pa_sink_input *to)
{
    uint32_t index;
    pa_sink_input *from;
    const char *role;

    pa_assert(to);

    if (sink && (from = pa_idxset_first(sink->inputs, &index))) {
        pa_assert(from->proplist);
        pa_assert(to->proplist);

        if ((role = pa_proplist_gets(from->proplist, PA_PROP_MEDIA_ROLE)) &&
            pa_proplist_sets(to->proplist, PA_PROP_MEDIA_ROLE, role) == 0)
        {
            pa_log_debug("set media.role=\"%s\" on sink_input.%d",
                         role, to->index);
            return;
        }
    }

    pa_log_debug("failed to set media.role on sink_input.%d", to->index);
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
