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

#include <pulse/proplist.h>
#include <pulsecore/core-util.h>
#include <pulsecore/sink.h>
#include <pulsecore/sink-input.h>

#include "fader.h"
#include "node.h"
#include "discover.h"
#include "volume.h"
#include "utils.h"

typedef struct {
    uint32_t fade_out;
    uint32_t fade_in;
} transition_time;


struct pa_fader {
    transition_time transit;
};

static void set_stream_volume_limit(struct userdata *, pa_sink_input *,
                                    pa_volume_t, uint32_t);

pa_fader *pa_fader_init(const char *fade_out_str, const char *fade_in_str)
{
    pa_fader *fader = pa_xnew0(pa_fader, 1);

    if (!fade_out_str || pa_atou(fade_out_str, &fader->transit.fade_out) < 0)
        fader->transit.fade_out = 100;

    if (!fade_in_str || pa_atou(fade_in_str, &fader->transit.fade_in) < 0)
        fader->transit.fade_in = 1000;

    if (fader->transit.fade_out > 10000)
        fader->transit.fade_out = 10000;

    if (fader->transit.fade_in > 10000)
        fader->transit.fade_in = 10000;

    pa_log_info("fader transition times: out %u ms, in %u ms",
                fader->transit.fade_out, fader->transit.fade_in);

    return fader;
}

void pa_fader_done(struct userdata *u)
{
    if (u) {
        pa_xfree(u->fader);
    }
}



void pa_fader_apply_volume_limits(struct userdata *u, uint32_t stamp)
{
    pa_core         *core;
    transition_time *transit;
    pa_sink         *sink;
    pa_sink_input   *sinp;
    pa_cvolume_ramp_int  *ramp;
    mir_node        *node;
    double           dB;
    pa_volume_t      newvol;
    pa_volume_t      oldvol;
    uint32_t         time;
    uint32_t         i,j;
    int              class;
    bool        rampit;

    pa_assert(u);
    pa_assert_se(u->fader);
    pa_assert_se((core = u->core));

    transit = &u->fader->transit;
    rampit  = transit->fade_in > 0 &&  transit->fade_out > 0;

    pa_log_debug("applying volume limits ...");

    PA_IDXSET_FOREACH(sink, core->sinks, i) {
        if ((node = pa_discover_find_node_by_ptr(u, sink))) {
            pa_log_debug("   node '%s'", node->amname);

            PA_IDXSET_FOREACH(sinp, sink->inputs, j) {
                class = pa_utils_get_stream_class(sinp->proplist);

                pa_log_debug("     stream %u (class %u)", sinp->index, class);

                if (!class) {
                    if (!(sinp->flags & PA_SINK_INPUT_START_RAMP_MUTED))
                        pa_log_debug("        skipping");
                    else {
                        sinp->flags &= ~PA_SINK_INPUT_START_RAMP_MUTED;
                        time = transit->fade_in;

                        pa_log_debug("        attenuation 0 dB "
                                     "transition time %u ms", time);
                        set_stream_volume_limit(u, sinp, PA_VOLUME_NORM, time);
                    }
                }
                else {
                    dB = mir_volume_apply_limits(u, node, class, stamp);
                    newvol = pa_sw_volume_from_dB(dB);

                    if (rampit) {
                        ramp   = &sinp->ramp;
                        oldvol = ramp->ramps[0].target;

                        if (oldvol > newvol)
                            time = transit->fade_out;
                        else if (oldvol < newvol)
                            time = transit->fade_in;
                        else
                            time = 0;
                    }
                    else {
                        oldvol = sinp->volume_factor.values[0];
                        time = 0;
                    }

                    if (oldvol == newvol)
                        pa_log_debug("         attenuation %.2lf dB",dB);
                    else {
                        pa_log_debug("         attenuation %.2lf dB "
                                     "transition time %u ms", dB, time);
                        set_stream_volume_limit(u, sinp, newvol, time);
                    }
                }
            } /* PA_IDXSET_FOREACH sinp */
        }
    } /* PA_IDXSET_FOREACH sink */
}

void pa_fader_ramp_volume(struct userdata *u,
                          pa_sink_input *sinp,
                          pa_volume_t newvol)
{
    transition_time *transit;
    bool             rampit;
    pa_volume_t      oldvol;
    pa_cvolume_ramp_int  *ramp;
    uint32_t         time;
    pa_cvolume_ramp  rampvol;

    pa_assert(u);
    pa_assert(u->fader);
    pa_assert(sinp);

    transit = &u->fader->transit;
    rampit  = transit->fade_in > 0 &&  transit->fade_out > 0;
    ramp    = &sinp->ramp;
    oldvol  = ramp->ramps[0].target;

    if (rampit && oldvol != newvol) {
        time = (oldvol > newvol) ? transit->fade_out : transit->fade_in;

        pa_cvolume_ramp_set(&rampvol,
                            sinp->volume.channels,
                            PA_VOLUME_RAMP_TYPE_LINEAR,
                            time, newvol);

        pa_sink_input_set_volume_ramp(sinp, &rampvol, true, false);
    }
}


void pa_fader_set_volume(struct userdata *u,
                          pa_sink_input *sinp,
                          pa_volume_t newvol)
{
    pa_volume_t oldvol;
    pa_cvolume_ramp_int *ramp;
    pa_cvolume_ramp  rampvol;

    pa_assert(u);
    pa_assert(sinp);

    ramp   = &sinp->ramp;
    oldvol = ramp->ramps[0].target;

    if (oldvol != newvol) {
        pa_cvolume_ramp_set(&rampvol,
                            sinp->volume.channels,
                            PA_VOLUME_RAMP_TYPE_LINEAR,
                            0, newvol);

        pa_sink_input_set_volume_ramp(sinp, &rampvol, true, false);
    }
}

pa_volume_t pa_fader_get_volume(struct userdata *u, pa_sink_input *sinp)
{
    pa_cvolume_ramp_int *ramp;
    pa_volume_t vol;

    pa_assert(u);
    pa_assert(sinp);

    ramp = &sinp->ramp;
    vol  = ramp->ramps[0].target;

    return vol;
}

static void set_stream_volume_limit(struct userdata *u,
                                    pa_sink_input   *sinp,
                                    pa_volume_t      vol,
                                    uint32_t         ramp_time)
{
    pa_sink *sink;
    pa_cvolume_ramp rampvol;

    pa_assert(u);
    pa_assert(sinp);
    pa_assert_se((sink = sinp->sink));

    if (!ramp_time) {
        pa_cvolume_set(&sinp->volume_factor, sinp->volume.channels, vol);

        if (pa_sink_flat_volume_enabled(sink)) {
            pa_sink_set_volume(sink, NULL, true, false);
        }
        else {
            pa_sw_cvolume_multiply(&sinp->soft_volume, &sinp->real_ratio,
                                   &sinp->volume_factor);

            pa_asyncmsgq_send(sink->asyncmsgq, PA_MSGOBJECT(sinp),
                              PA_SINK_INPUT_MESSAGE_SET_SOFT_VOLUME,
                              NULL, 0, NULL);
        }
    }
    else {
        pa_cvolume_ramp_set(&rampvol,
                            sinp->volume.channels,
                            PA_VOLUME_RAMP_TYPE_LINEAR,
                            ramp_time,
                            vol);

        pa_sink_input_set_volume_ramp(sinp, &rampvol, true, false);
    }
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
