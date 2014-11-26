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

#include "userdata.h"

#include "loopback.h"
#include "utils.h"

typedef struct {
    const char *media_role;
    int         time;
} latency_def;


static int get_latency(const char *);


pa_loopback *pa_loopback_init(void)
{
    pa_loopback *loopback = pa_xnew0(pa_loopback, 1);

    return loopback;
}


void pa_loopback_done(pa_loopback *loopback, pa_core *core)
{
    pa_loopnode *loop, *n;

    PA_LLIST_FOREACH_SAFE(loop,n, loopback->loopnodes) {
        pa_module_unload_by_index(core, loop->module_index, false);
    }
}



pa_loopnode *pa_loopback_create(pa_loopback      *loopback,
                                pa_core          *core,
                                pa_loopback_type  type,
                                uint32_t          node_index,
                                uint32_t          source_index,
                                uint32_t          sink_index,
                                const char       *media_role,
                                uint32_t          resource_priority,
                                uint32_t          resource_set_flags,
                                uint32_t          resource_audio_flags)
{
    static const char *modnam = "module-loopback";

    pa_loopnode       *loop;
    pa_source         *source;
    pa_sink           *sink;
    pa_module         *module;
    pa_sink_input     *sink_input;
    pa_source_output  *source_output;
    char               args[512];
    uint32_t           idx;

    pa_assert(core);
    pa_assert(media_role);
    pa_assert(type == PA_LOOPBACK_SOURCE || type == PA_LOOPBACK_SINK);

    if (!(source = pa_idxset_get_by_index(core->sources, source_index))) {
        pa_log_debug("can't find source (index %u) for loopback",source_index);
        return NULL;
    }

    if (!(sink = pa_idxset_get_by_index(core->sinks, sink_index))) {
        pa_log_debug("can't find the primary sink (index %u) for loopback",
                     sink_index);
        return NULL;
    }


    if (type == PA_LOOPBACK_SOURCE) {
        snprintf(args, sizeof(args), "source=\"%s\" sink=\"%s\" "
                 "latency_msec=%d "
                 "sink_input_properties=\"%s=%s %s=%u %s=%u %s=%u %s=%u\" "
                 "source_output_properties=\"%s=%s %s=%u\"",
                 source->name, sink->name,
                 get_latency(media_role),
                 PA_PROP_MEDIA_ROLE, media_role,
                 PA_PROP_NODE_INDEX, node_index,
                 PA_PROP_RESOURCE_PRIORITY, resource_priority,
                 PA_PROP_RESOURCE_SET_FLAGS, resource_set_flags,
                 PA_PROP_RESOURCE_AUDIO_FLAGS, resource_audio_flags,
                 PA_PROP_MEDIA_ROLE, media_role,
                 PA_PROP_NODE_INDEX, node_index);
    }
    else {
        snprintf(args, sizeof(args), "source=\"%s\" sink=\"%s\" "
                 "latency_msec=%d "
                 "sink_input_properties=\"%s=%s %s=%u\" "
                 "source_output_properties=\"%s=%s %s=%u %s=%u %s=%u %s=%u\"",
                 source->name, sink->name,
                 get_latency(media_role),
                 PA_PROP_MEDIA_ROLE, media_role,
                 PA_PROP_NODE_INDEX, node_index,
                 PA_PROP_MEDIA_ROLE, media_role,
                 PA_PROP_NODE_INDEX, node_index,
                 PA_PROP_RESOURCE_PRIORITY, resource_priority,
                 PA_PROP_RESOURCE_SET_FLAGS, resource_set_flags,
                 PA_PROP_RESOURCE_AUDIO_FLAGS, resource_audio_flags);
    }

    pa_log_debug("loading %s %s", modnam, args);

    if (!(module = pa_module_load(core, modnam, args))) {
        pa_log("failed to load module '%s %s'. can't loopback", modnam, args);
        return NULL;
    }

    PA_IDXSET_FOREACH(sink_input, core->sink_inputs, idx) {
        if (sink_input->module == module)
            break;
    }

    PA_IDXSET_FOREACH(source_output, core->source_outputs, idx) {
        if (source_output->module == module)
            break;
    }

    if (!sink_input || !source_output) {
        if (!sink_input) {
            pa_log("can't find output stream of loopback module (index %u)",
                   module->index);
        }
        if (!source_output) {
            pa_log("can't find input stream of loopback module (index %u)",
                   module->index);
        }
        pa_module_unload(core, module, false);
        return NULL;
    }

    pa_assert(sink_input->index != PA_IDXSET_INVALID);
    pa_assert(source_output->index != PA_IDXSET_INVALID);

    loop = pa_xnew0(pa_loopnode, 1);
    loop->module_index = module->index;
    loop->node_index = node_index;
    loop->sink_input_index = sink_input->index;
    loop->source_output_index = source_output->index;

    PA_LLIST_PREPEND(pa_loopnode, loopback->loopnodes, loop);

    pa_log_debug("loopback succesfully loaded. Module index %u",module->index);

    return loop;
}


void pa_loopback_destroy(pa_loopback *loopback,
                         pa_core     *core,
                         pa_loopnode *loop)
{
    pa_assert(loopback);
    pa_assert(core);

    if (loop) {
        PA_LLIST_REMOVE(pa_loopnode, loopback->loopnodes, loop);
        pa_module_unload_by_index(core, loop->module_index, false);
        pa_xfree(loop);
    }
}

uint32_t pa_loopback_get_sink_index(pa_core *core, pa_loopnode *loop)
{
    pa_sink_input *sink_input;
    pa_sink *sink;

    pa_assert(core);
    pa_assert(loop);

    sink_input = pa_idxset_get_by_index(core->sink_inputs,
                                        loop->sink_input_index);

    if (sink_input && (sink = sink_input->sink))
        return sink->index;

    return PA_IDXSET_INVALID;
}

int pa_loopback_print(pa_loopnode *loop, char *buf, int len)
{
    char *p, *e;

    pa_assert(buf);
    pa_assert(len > 0);

    e = (p = buf) + len;

    if (!loop)
        p += snprintf(p, e-p, "<not set>");
    else {
        p += snprintf(p, e-p, "module %u, sink_input %u",
                      loop->module_index, loop->sink_input_index);
    }

    return p - buf;
}

static int get_latency(const char *media_role)
{
    static latency_def  latencies[] = {
        { "phone"   , 50 },
        { "ringtone", 50 },
        {    NULL   , 0  }
    };

    latency_def *l;

    pa_assert(media_role);

    for (l = latencies;  l->media_role;  l++) {
        if (pa_streq(media_role, l->media_role))
            return l->time;
    }

    return 200;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
