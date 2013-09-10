/***
  This file is part of PulseAudio.

  Copyright 2004-2008 Lennart Poettering
  Copyright 2013 Intel Corporation

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/rtclock.h>
#include <pulse/timeval.h>

#include <pulsecore/i18n.h>
#include <pulsecore/thread.h>

#include "null-sink.h"

#define DEFAULT_SINK_NAME "null"
#define BLOCK_USEC (PA_USEC_PER_SEC * 2)

struct pa_null_sink {
    pa_rtpoll *rtpoll;
    pa_thread_mq thread_mq;
    pa_sink *sink;
    pa_thread *thread;
    pa_usec_t block_usec;
    pa_usec_t timestamp;
};

void pa_null_sink_new_data_init(pa_null_sink_new_data *data) {
    pa_assert(data);

    pa_zero(*data);

    data->proplist = pa_proplist_new();
}

void pa_null_sink_new_data_set_name(pa_null_sink_new_data *data, const char *name) {
    pa_assert(data);

    pa_xfree(data->name);
    data->name = pa_xstrdup(name);
}

void pa_null_sink_new_data_done(pa_null_sink_new_data *data) {
    pa_assert(data);

    pa_proplist_free(data->proplist);
    pa_xfree(data->name);
}

static int sink_process_msg_cb(pa_msgobject *o, int code, void *data, int64_t offset, pa_memchunk *chunk) {
    pa_null_sink *null_sink;

    pa_assert(o);

    null_sink = PA_SINK(o)->userdata;

    switch (code) {
        case PA_SINK_MESSAGE_SET_STATE:

            if (PA_PTR_TO_UINT(data) == PA_SINK_RUNNING)
                null_sink->timestamp = pa_rtclock_now();

            break;

        case PA_SINK_MESSAGE_GET_LATENCY: {
            pa_usec_t now;

            now = pa_rtclock_now();
            *((pa_usec_t*) data) = null_sink->timestamp > now ? null_sink->timestamp - now : 0ULL;

            return 0;
        }
    }

    return pa_sink_process_msg(o, code, data, offset, chunk);
}

static void sink_update_requested_latency_cb(pa_sink *s) {
    pa_null_sink *null_sink;
    size_t nbytes;

    pa_assert(s);
    pa_assert_se(null_sink = s->userdata);

    null_sink->block_usec = pa_sink_get_requested_latency_within_thread(s);

    if (null_sink->block_usec == (pa_usec_t) -1)
        null_sink->block_usec = s->thread_info.max_latency;

    nbytes = pa_usec_to_bytes(null_sink->block_usec, &s->sample_spec);
    pa_sink_set_max_rewind_within_thread(s, nbytes);
    pa_sink_set_max_request_within_thread(s, nbytes);
}

static void process_rewind(pa_null_sink *null_sink, pa_usec_t now) {
    size_t rewind_nbytes, in_buffer;
    pa_usec_t delay;

    pa_assert(null_sink);

    rewind_nbytes = null_sink->sink->thread_info.rewind_nbytes;

    if (!PA_SINK_IS_OPENED(null_sink->sink->thread_info.state) || rewind_nbytes <= 0)
        goto do_nothing;

    pa_log_debug("Requested to rewind %lu bytes.", (unsigned long) rewind_nbytes);

    if (null_sink->timestamp <= now)
        goto do_nothing;

    delay = null_sink->timestamp - now;
    in_buffer = pa_usec_to_bytes(delay, &null_sink->sink->sample_spec);

    if (in_buffer <= 0)
        goto do_nothing;

    if (rewind_nbytes > in_buffer)
        rewind_nbytes = in_buffer;

    pa_sink_process_rewind(null_sink->sink, rewind_nbytes);
    null_sink->timestamp -= pa_bytes_to_usec(rewind_nbytes, &null_sink->sink->sample_spec);

    pa_log_debug("Rewound %lu bytes.", (unsigned long) rewind_nbytes);
    return;

do_nothing:
    pa_sink_process_rewind(null_sink->sink, 0);
}

static void process_render(pa_null_sink *null_sink, pa_usec_t now) {
    size_t ate = 0;

    pa_assert(null_sink);

    /* This is the configured latency. Sink inputs connected to us
    might not have a single frame more than the maxrequest value
    queued. Hence: at maximum read this many bytes from the sink
    inputs. */

    /* Fill the buffer up the latency size */
    while (null_sink->timestamp < now + null_sink->block_usec) {
        pa_memchunk chunk;

        pa_sink_render(null_sink->sink, null_sink->sink->thread_info.max_request, &chunk);
        pa_memblock_unref(chunk.memblock);

        null_sink->timestamp += pa_bytes_to_usec(chunk.length, &null_sink->sink->sample_spec);
        ate += chunk.length;

        if (ate >= null_sink->sink->thread_info.max_request)
            break;
    }
}

static void thread_func(void *userdata) {
    pa_null_sink *null_sink = userdata;

    pa_assert(null_sink);

    pa_log_debug("Thread starting up");
    pa_thread_mq_install(&null_sink->thread_mq);
    null_sink->timestamp = pa_rtclock_now();

    for (;;) {
        pa_usec_t now = 0;
        int ret;

        if (PA_SINK_IS_OPENED(null_sink->sink->thread_info.state))
            now = pa_rtclock_now();

        if (PA_UNLIKELY(null_sink->sink->thread_info.rewind_requested))
            process_rewind(null_sink, now);

        /* Render some data and drop it immediately */
        if (PA_SINK_IS_OPENED(null_sink->sink->thread_info.state)) {
            if (null_sink->timestamp <= now)
                process_render(null_sink, now);

            pa_rtpoll_set_timer_absolute(null_sink->rtpoll, null_sink->timestamp);
        } else
            pa_rtpoll_set_timer_disabled(null_sink->rtpoll);

        /* Hmm, nothing to do. Let's sleep */
        if ((ret = pa_rtpoll_run(null_sink->rtpoll, true)) < 0)
            goto fail;

        if (ret == 0)
            goto finish;
    }

fail:
    /* If this was no regular exit from the loop we have to continue
     * processing messages until we received PA_MESSAGE_SHUTDOWN */
    pa_asyncmsgq_wait_for(null_sink->thread_mq.inq, PA_MESSAGE_SHUTDOWN);

finish:
    pa_log_debug("Thread shutting down");
}

pa_null_sink *pa_null_sink_new(pa_core *core, pa_null_sink_new_data *null_sink_data) {
    pa_null_sink *null_sink;
    pa_sink_new_data sink_data;
    size_t nbytes;

    pa_assert(core);
    pa_assert(null_sink_data);

    null_sink = pa_xnew0(pa_null_sink, 1);
    null_sink->rtpoll = pa_rtpoll_new();
    pa_thread_mq_init(&null_sink->thread_mq, core->mainloop, null_sink->rtpoll);

    pa_sink_new_data_init(&sink_data);
    sink_data.driver = __FILE__;
    sink_data.module = null_sink_data->module;
    pa_sink_new_data_set_name(&sink_data, null_sink_data->name ? null_sink_data->name : DEFAULT_SINK_NAME);
    pa_sink_new_data_set_sample_spec(&sink_data, null_sink_data->sample_spec_is_set ?
                                                 &null_sink_data->sample_spec :
                                                 &core->default_sample_spec);
    pa_sink_new_data_set_channel_map(&sink_data, null_sink_data->channel_map_is_set ?
                                                 &null_sink_data->channel_map :
                                                 &core->default_channel_map);
    pa_proplist_sets(sink_data.proplist, PA_PROP_DEVICE_DESCRIPTION, _("Null Output"));
    pa_proplist_update(sink_data.proplist, PA_UPDATE_REPLACE, null_sink_data->proplist);

    null_sink->sink = pa_sink_new(core, &sink_data, PA_SINK_LATENCY | PA_SINK_DYNAMIC_LATENCY);
    pa_sink_new_data_done(&sink_data);

    if (!null_sink->sink) {
        pa_log("Failed to create sink %s.", sink_data.name);
        goto fail;
    }

    null_sink->sink->parent.process_msg = sink_process_msg_cb;
    null_sink->sink->update_requested_latency = sink_update_requested_latency_cb;
    null_sink->sink->userdata = null_sink;

    pa_sink_set_asyncmsgq(null_sink->sink, null_sink->thread_mq.inq);
    pa_sink_set_rtpoll(null_sink->sink, null_sink->rtpoll);

    null_sink->block_usec = BLOCK_USEC;
    nbytes = pa_usec_to_bytes(null_sink->block_usec, &null_sink->sink->sample_spec);
    pa_sink_set_max_rewind(null_sink->sink, nbytes);
    pa_sink_set_max_request(null_sink->sink, nbytes);

    if (!(null_sink->thread = pa_thread_new("null-sink", thread_func, null_sink))) {
        pa_log("Failed to create thread.");
        goto fail;
    }

    pa_sink_set_latency_range(null_sink->sink, 0, BLOCK_USEC);

    pa_sink_put(null_sink->sink);

    return null_sink;

fail:
    pa_null_sink_free(null_sink);

    return NULL;
}

void pa_null_sink_free(pa_null_sink *null_sink) {
    pa_assert(null_sink);

    if (null_sink->sink)
        pa_sink_unlink(null_sink->sink);

    if (null_sink->thread) {
        pa_asyncmsgq_send(null_sink->thread_mq.inq, NULL, PA_MESSAGE_SHUTDOWN, NULL, 0, NULL);
        pa_thread_free(null_sink->thread);
    }

    pa_thread_mq_done(&null_sink->thread_mq);

    if (null_sink->sink)
        pa_sink_unref(null_sink->sink);

    if (null_sink->rtpoll)
        pa_rtpoll_free(null_sink->rtpoll);

    pa_xfree(null_sink);
}

pa_sink *pa_null_sink_get_sink(pa_null_sink *null_sink) {
    pa_assert(null_sink);

    return null_sink->sink;
}
