/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <pulse/xmalloc.h>
#include <pulse/timeval.h>

#include <pulsecore/dynarray.h>
#include <pulsecore/poll.h>
#include <pulsecore/core-error.h>
#include <pulsecore/core-rtclock.h>
#include <pulsecore/macro.h>
#include <pulsecore/llist.h>
#include <pulsecore/flist.h>
#include <pulsecore/core-util.h>
#include <pulsecore/ratelimit.h>
#include <pulse/rtclock.h>

#include "rtpoll.h"

/* #define DEBUG_TIMING */

struct pa_rtpoll {
    struct pollfd *pollfd, *pollfd2;
    unsigned n_pollfd_alloc, n_pollfd_used;

    struct timeval next_elapse;
    bool timer_enabled:1;

    bool scan_for_dead:1;
    bool running:1;
    bool rebuild_needed:1;
    bool quit:1;
    bool timer_elapsed:1;

#ifdef DEBUG_TIMING
    pa_usec_t timestamp;
    pa_usec_t slept, awake;
#endif

    PA_LLIST_HEAD(pa_rtpoll_item, items);

    pa_mainloop_api mainloop_api;

    pa_dynarray *io_events;

    pa_dynarray *time_events;
    pa_dynarray *enabled_time_events;
    pa_dynarray *expired_time_events;
    pa_time_event *cached_next_time_event;

    pa_dynarray *defer_events;
    pa_dynarray *enabled_defer_events;
};

struct pa_rtpoll_item {
    pa_rtpoll *rtpoll;
    bool dead;

    pa_rtpoll_priority_t priority;

    struct pollfd *pollfd;
    unsigned n_pollfd;

    int (*work_cb)(pa_rtpoll_item *i);
    int (*before_cb)(pa_rtpoll_item *i);
    void (*after_cb)(pa_rtpoll_item *i);
    void *userdata;

    PA_LLIST_FIELDS(pa_rtpoll_item);
};

struct pa_io_event {
    pa_rtpoll *rtpoll;
    pa_rtpoll_item *rtpoll_item;
    pa_io_event_flags_t events;
    pa_io_event_cb_t callback;
    pa_io_event_destroy_cb_t destroy_callback;
    void *userdata;
};

static void io_event_enable(pa_io_event *event, pa_io_event_flags_t events);

struct pa_time_event {
    pa_rtpoll *rtpoll;
    pa_usec_t time;
    bool use_rtclock;
    bool enabled;
    pa_time_event_cb_t callback;
    pa_time_event_destroy_cb_t destroy_callback;
    void *userdata;
};

static void time_event_restart(pa_time_event *event, const struct timeval *tv);

struct pa_defer_event {
    pa_rtpoll *rtpoll;
    bool enabled;
    pa_defer_event_cb_t callback;
    pa_defer_event_destroy_cb_t destroy_callback;
    void *userdata;
};

static void defer_event_enable(pa_defer_event *event, int enable);

PA_STATIC_FLIST_DECLARE(items, 0, pa_xfree);

static short map_flags_to_libc(pa_io_event_flags_t flags) {
    return (short)
        ((flags & PA_IO_EVENT_INPUT ? POLLIN : 0) |
         (flags & PA_IO_EVENT_OUTPUT ? POLLOUT : 0) |
         (flags & PA_IO_EVENT_ERROR ? POLLERR : 0) |
         (flags & PA_IO_EVENT_HANGUP ? POLLHUP : 0));
}

static pa_io_event_flags_t map_flags_from_libc(short flags) {
    return
        (flags & POLLIN ? PA_IO_EVENT_INPUT : 0) |
        (flags & POLLOUT ? PA_IO_EVENT_OUTPUT : 0) |
        (flags & POLLERR ? PA_IO_EVENT_ERROR : 0) |
        (flags & POLLHUP ? PA_IO_EVENT_HANGUP : 0);
}

static int io_event_work_cb(pa_rtpoll_item *item) {
    pa_io_event *event;
    struct pollfd *pollfd;

    pa_assert(item);

    event = pa_rtpoll_item_get_userdata(item);
    pollfd = pa_rtpoll_item_get_pollfd(item, NULL);
    event->callback(&event->rtpoll->mainloop_api, event, pollfd->fd, map_flags_from_libc(pollfd->revents), event->userdata);

    return 0;
}

static pa_io_event* io_event_new(pa_mainloop_api *api, int fd, pa_io_event_flags_t events, pa_io_event_cb_t callback,
                                 void *userdata) {
    pa_rtpoll *rtpoll;
    pa_io_event *event;
    struct pollfd *pollfd;

    pa_assert(api);
    pa_assert(api->userdata);
    pa_assert(fd >= 0);
    pa_assert(callback);

    rtpoll = api->userdata;
    pa_assert(api == &rtpoll->mainloop_api);

    event = pa_xnew0(pa_io_event, 1);
    event->rtpoll = rtpoll;
    event->rtpoll_item = pa_rtpoll_item_new(rtpoll, PA_RTPOLL_NORMAL, 1);
    pa_rtpoll_item_set_work_callback(event->rtpoll_item, io_event_work_cb);
    pa_rtpoll_item_set_userdata(event->rtpoll_item, event);
    pollfd = pa_rtpoll_item_get_pollfd(event->rtpoll_item, NULL);
    pollfd->fd = fd;
    event->callback = callback;
    event->userdata = userdata;

    pa_dynarray_append(rtpoll->io_events, event);
    io_event_enable(event, events);

    return event;
}

static void io_event_free(pa_io_event *event) {
    pa_assert(event);

    pa_dynarray_remove_by_data(event->rtpoll->io_events, event);

    if (event->destroy_callback)
        event->destroy_callback(&event->rtpoll->mainloop_api, event, event->userdata);

    if (event->rtpoll_item)
        pa_rtpoll_item_free(event->rtpoll_item);

    pa_xfree(event);
}

static void io_event_enable(pa_io_event *event, pa_io_event_flags_t events) {
    struct pollfd *pollfd;

    pa_assert(event);

    if (events == event->events)
        return;

    event->events = events;

    pollfd = pa_rtpoll_item_get_pollfd(event->rtpoll_item, NULL);
    pollfd->events = map_flags_to_libc(events);
}

static void io_event_set_destroy(pa_io_event *event, pa_io_event_destroy_cb_t callback) {
    pa_assert(event);

    event->destroy_callback = callback;
}

static pa_usec_t make_rt(const struct timeval *tv, bool *use_rtclock) {
    struct timeval ttv;

    if (!tv) {
        *use_rtclock = false;
        return PA_USEC_INVALID;
    }

    ttv = *tv;
    *use_rtclock = !!(ttv.tv_usec & PA_TIMEVAL_RTCLOCK);

    if (*use_rtclock)
        ttv.tv_usec &= ~PA_TIMEVAL_RTCLOCK;
    else
        pa_rtclock_from_wallclock(&ttv);

    return pa_timeval_load(&ttv);
}

static pa_time_event* time_event_new(pa_mainloop_api *api, const struct timeval *tv, pa_time_event_cb_t callback,
                                     void *userdata) {
    pa_rtpoll *rtpoll;
    pa_time_event *event;

    pa_assert(api);
    pa_assert(api->userdata);
    pa_assert(callback);

    rtpoll = api->userdata;
    pa_assert(api == &rtpoll->mainloop_api);

    event = pa_xnew0(pa_time_event, 1);
    event->rtpoll = rtpoll;
    event->time = PA_USEC_INVALID;
    event->callback = callback;
    event->userdata = userdata;

    pa_dynarray_append(rtpoll->time_events, event);
    time_event_restart(event, tv);

    return event;
}

static void time_event_free(pa_time_event *event) {
    pa_assert(event);

    time_event_restart(event, NULL);
    pa_dynarray_remove_by_data(event->rtpoll->time_events, event);

    if (event->destroy_callback)
        event->destroy_callback(&event->rtpoll->mainloop_api, event, event->userdata);

    pa_xfree(event);
}

static void time_event_restart(pa_time_event *event, const struct timeval *tv) {
    pa_usec_t t;
    bool use_rtclock;
    bool enabled;
    bool old_enabled;

    pa_assert(event);

    t = make_rt(tv, &use_rtclock);
    enabled = (t != PA_USEC_INVALID);
    old_enabled = event->enabled;

    /* We return early only if the event stays disabled. If the event stays
     * enabled, we can't return early, because the event time may change. */
    if (!enabled && !old_enabled)
        return;

    event->enabled = enabled;
    event->time = t;
    event->use_rtclock = use_rtclock;

    if (enabled && !old_enabled)
        pa_dynarray_append(event->rtpoll->enabled_time_events, event);
    else if (!enabled) {
        pa_dynarray_remove_by_data(event->rtpoll->enabled_time_events, event);
        pa_dynarray_remove_by_data(event->rtpoll->expired_time_events, event);
    }

    if (event->rtpoll->cached_next_time_event == event)
        event->rtpoll->cached_next_time_event = NULL;

    if (event->rtpoll->cached_next_time_event && enabled) {
        pa_assert(event->rtpoll->cached_next_time_event->enabled);

        if (t < event->rtpoll->cached_next_time_event->time)
            event->rtpoll->cached_next_time_event = event;
    }
}

static void time_event_set_destroy(pa_time_event *event, pa_time_event_destroy_cb_t callback) {
    pa_assert(event);

    event->destroy_callback = callback;
}

static pa_defer_event* defer_event_new(pa_mainloop_api *api, pa_defer_event_cb_t callback, void *userdata) {
    pa_rtpoll *rtpoll;
    pa_defer_event *event;

    pa_assert(api);
    pa_assert(api->userdata);
    pa_assert(callback);

    rtpoll = api->userdata;
    pa_assert(api == &rtpoll->mainloop_api);

    event = pa_xnew0(pa_defer_event, 1);
    event->rtpoll = rtpoll;
    event->callback = callback;
    event->userdata = userdata;

    pa_dynarray_append(rtpoll->defer_events, event);
    defer_event_enable(event, true);

    return event;
}

static void defer_event_free(pa_defer_event *event) {
    pa_assert(event);

    defer_event_enable(event, false);
    pa_dynarray_remove_by_data(event->rtpoll->defer_events, event);

    if (event->destroy_callback)
        event->destroy_callback(&event->rtpoll->mainloop_api, event, event->userdata);

    pa_xfree(event);
}

static void defer_event_enable(pa_defer_event *event, int enable) {
    pa_assert(event);

    if (enable == event->enabled)
        return;

    event->enabled = enable;

    if (enable)
        pa_dynarray_append(event->rtpoll->enabled_defer_events, event);
    else
        pa_dynarray_remove_by_data(event->rtpoll->enabled_defer_events, event);
}

static void defer_event_set_destroy(pa_defer_event *event, pa_defer_event_destroy_cb_t callback) {
    pa_assert(event);

    event->destroy_callback = callback;
}

static void mainloop_api_quit(pa_mainloop_api *api, int retval) {
    pa_rtpoll *rtpoll;

    pa_assert(api);
    pa_assert(api->userdata);

    rtpoll = api->userdata;
    pa_assert(api == &rtpoll->mainloop_api);

    pa_rtpoll_quit(rtpoll);
}

static const pa_mainloop_api vtable = {
    .userdata = NULL,

    .io_new = io_event_new,
    .io_enable = io_event_enable,
    .io_free = io_event_free,
    .io_set_destroy = io_event_set_destroy,

    .time_new = time_event_new,
    .time_restart = time_event_restart,
    .time_free = time_event_free,
    .time_set_destroy = time_event_set_destroy,

    .defer_new = defer_event_new,
    .defer_enable = defer_event_enable,
    .defer_free = defer_event_free,
    .defer_set_destroy = defer_event_set_destroy,

    .quit = mainloop_api_quit,
};

pa_rtpoll *pa_rtpoll_new(void) {
    pa_rtpoll *p;

    p = pa_xnew0(pa_rtpoll, 1);

    p->n_pollfd_alloc = 32;
    p->pollfd = pa_xnew(struct pollfd, p->n_pollfd_alloc);
    p->pollfd2 = pa_xnew(struct pollfd, p->n_pollfd_alloc);

#ifdef DEBUG_TIMING
    p->timestamp = pa_rtclock_now();
#endif

    p->mainloop_api = vtable;
    p->mainloop_api.userdata = p;
    p->io_events = pa_dynarray_new(NULL);
    p->time_events = pa_dynarray_new(NULL);
    p->enabled_time_events = pa_dynarray_new(NULL);
    p->expired_time_events = pa_dynarray_new(NULL);
    p->defer_events = pa_dynarray_new(NULL);
    p->enabled_defer_events = pa_dynarray_new(NULL);

    return p;
}

static void rtpoll_rebuild(pa_rtpoll *p) {

    struct pollfd *e, *t;
    pa_rtpoll_item *i;
    int ra = 0;

    pa_assert(p);

    p->rebuild_needed = false;

    if (p->n_pollfd_used > p->n_pollfd_alloc) {
        /* Hmm, we have to allocate some more space */
        p->n_pollfd_alloc = p->n_pollfd_used * 2;
        p->pollfd2 = pa_xrealloc(p->pollfd2, p->n_pollfd_alloc * sizeof(struct pollfd));
        ra = 1;
    }

    e = p->pollfd2;

    for (i = p->items; i; i = i->next) {

        if (i->n_pollfd > 0) {
            size_t l = i->n_pollfd * sizeof(struct pollfd);

            if (i->pollfd)
                memcpy(e, i->pollfd, l);
            else
                memset(e, 0, l);

            i->pollfd = e;
        } else
            i->pollfd = NULL;

        e += i->n_pollfd;
    }

    pa_assert((unsigned) (e - p->pollfd2) == p->n_pollfd_used);
    t = p->pollfd;
    p->pollfd = p->pollfd2;
    p->pollfd2 = t;

    if (ra)
        p->pollfd2 = pa_xrealloc(p->pollfd2, p->n_pollfd_alloc * sizeof(struct pollfd));
}

static void rtpoll_item_destroy(pa_rtpoll_item *i) {
    pa_rtpoll *p;

    pa_assert(i);

    p = i->rtpoll;

    PA_LLIST_REMOVE(pa_rtpoll_item, p->items, i);

    p->n_pollfd_used -= i->n_pollfd;

    if (pa_flist_push(PA_STATIC_FLIST_GET(items), i) < 0)
        pa_xfree(i);

    p->rebuild_needed = true;
}

void pa_rtpoll_free(pa_rtpoll *p) {
    pa_assert(p);

    if (p->defer_events) {
        pa_defer_event *event;

        while ((event = pa_dynarray_last(p->defer_events)))
            defer_event_free(event);
    }

    if (p->time_events) {
        pa_time_event *event;

        while ((event = pa_dynarray_last(p->time_events)))
            time_event_free(event);
    }

    if (p->io_events) {
        pa_io_event *event;

        while ((event = pa_dynarray_last(p->io_events)))
            io_event_free(event);
    }

    while (p->items)
        rtpoll_item_destroy(p->items);

    if (p->enabled_defer_events) {
        pa_assert(pa_dynarray_size(p->enabled_defer_events) == 0);
        pa_dynarray_free(p->enabled_defer_events);
    }

    if (p->defer_events) {
        pa_assert(pa_dynarray_size(p->defer_events) == 0);
        pa_dynarray_free(p->defer_events);
    }

    if (p->expired_time_events) {
        pa_assert(pa_dynarray_size(p->expired_time_events) == 0);
        pa_dynarray_free(p->expired_time_events);
    }

    if (p->enabled_time_events) {
        pa_assert(pa_dynarray_size(p->enabled_time_events) == 0);
        pa_dynarray_free(p->enabled_time_events);
    }

    if (p->time_events) {
        pa_assert(pa_dynarray_size(p->time_events) == 0);
        pa_dynarray_free(p->time_events);
    }

    if (p->io_events) {
        pa_assert(pa_dynarray_size(p->io_events) == 0);
        pa_dynarray_free(p->io_events);
    }

    pa_xfree(p->pollfd);
    pa_xfree(p->pollfd2);

    pa_xfree(p);
}

pa_mainloop_api *pa_rtpoll_get_mainloop_api(pa_rtpoll *rtpoll) {
    pa_assert(rtpoll);

    return &rtpoll->mainloop_api;
}

static void find_expired_time_events(pa_rtpoll *rtpoll) {
    pa_usec_t now;
    pa_time_event *event;
    unsigned idx;

    pa_assert(rtpoll);
    pa_assert(pa_dynarray_size(rtpoll->expired_time_events) == 0);

    now = pa_rtclock_now();

    PA_DYNARRAY_FOREACH(event, rtpoll->enabled_time_events, idx) {
        if (event->time <= now)
            pa_dynarray_append(rtpoll->expired_time_events, event);
    }
}

static pa_time_event *find_next_time_event(pa_rtpoll *rtpoll) {
    pa_time_event *event;
    pa_time_event *result = NULL;
    unsigned idx;

    pa_assert(rtpoll);

    if (rtpoll->cached_next_time_event)
        return rtpoll->cached_next_time_event;

    PA_DYNARRAY_FOREACH(event, rtpoll->enabled_time_events, idx) {
        if (!result || event->time < result->time)
            result = event;
    }

    rtpoll->cached_next_time_event = result;

    return result;
}

static void reset_revents(pa_rtpoll_item *i) {
    struct pollfd *f;
    unsigned n;

    pa_assert(i);

    if (!(f = pa_rtpoll_item_get_pollfd(i, &n)))
        return;

    for (; n > 0; n--)
        f[n-1].revents = 0;
}

static void reset_all_revents(pa_rtpoll *p) {
    pa_rtpoll_item *i;

    pa_assert(p);

    for (i = p->items; i; i = i->next) {

        if (i->dead)
            continue;

        reset_revents(i);
    }
}

int pa_rtpoll_run(pa_rtpoll *p) {
    pa_defer_event *defer_event;
    pa_time_event *time_event;
    pa_rtpoll_item *i;
    int r = 0;
    struct timeval timeout;
    pa_time_event *next_time_event;
    struct timeval next_time_event_elapse;
    bool timer_enabled;

    pa_assert(p);
    pa_assert(!p->running);

#ifdef DEBUG_TIMING
    pa_log("rtpoll_run");
#endif

    p->running = true;
    p->timer_elapsed = false;

    /* Dispatch all enabled defer events. */
    while ((defer_event = pa_dynarray_last(p->enabled_defer_events))) {
        if (p->quit)
            break;

        defer_event->callback(&p->mainloop_api, defer_event, defer_event->userdata);
    }

    /* Dispatch all expired time events. */
    find_expired_time_events(p);
    while ((time_event = pa_dynarray_last(p->expired_time_events))) {
        struct timeval tv;

        if (p->quit)
            break;

        time_event_restart(time_event, NULL);
        time_event->callback(&p->mainloop_api, time_event, pa_timeval_rtstore(&tv, time_event->time, time_event->use_rtclock),
                             time_event->userdata);
    }

    /* Let's do some work */
    for (i = p->items; i && i->priority < PA_RTPOLL_NEVER; i = i->next) {
        int k;

        if (i->dead)
            continue;

        if (!i->work_cb)
            continue;

        if (p->quit) {
#ifdef DEBUG_TIMING
            pa_log("rtpoll finish");
#endif
            goto finish;
        }

        if ((k = i->work_cb(i)) != 0) {
            if (k < 0)
                r = k;
#ifdef DEBUG_TIMING
            pa_log("rtpoll finish");
#endif
            goto finish;
        }
    }

    /* Now let's prepare for entering the sleep */
    for (i = p->items; i && i->priority < PA_RTPOLL_NEVER; i = i->next) {
        int k = 0;

        if (i->dead)
            continue;

        if (!i->before_cb)
            continue;

        if (p->quit || (k = i->before_cb(i)) != 0) {

            /* Hmm, this one doesn't let us enter the poll, so rewind everything */

            for (i = i->prev; i; i = i->prev) {

                if (i->dead)
                    continue;

                if (!i->after_cb)
                    continue;

                i->after_cb(i);
            }

            if (k < 0)
                r = k;
#ifdef DEBUG_TIMING
            pa_log("rtpoll finish");
#endif
            goto finish;
        }
    }

    if (p->rebuild_needed)
        rtpoll_rebuild(p);

    /* Calculate timeout */

    pa_zero(timeout);

    next_time_event = find_next_time_event(p);
    if (next_time_event)
        pa_timeval_rtstore(&next_time_event_elapse, next_time_event->time, next_time_event->use_rtclock);

    /* p->timer_enabled and p->next_elapse are controlled by the rtpoll owner,
     * while the time events can be created by anyone through pa_mainloop_api.
     * It might be a good idea to merge p->timer_enabled and p->next_elapse
     * with the time events so that we wouldn't need to handle them separately
     * here. The reason why they are currently separate is that the
     * pa_mainloop_api interface was bolted on pa_rtpoll as an afterthought. */
    timer_enabled = p->timer_enabled || next_time_event;

    if (!p->quit && timer_enabled) {
        struct timeval *next_elapse;
        struct timeval now;

        if (p->timer_enabled && next_time_event) {
            if (pa_timeval_cmp(&p->next_elapse, &next_time_event_elapse) > 0)
                next_elapse = &next_time_event_elapse;
            else
                next_elapse = &p->next_elapse;
        } else if (p->timer_enabled)
            next_elapse = &p->next_elapse;
        else
            next_elapse = &next_time_event_elapse;

        pa_rtclock_get(&now);

        if (pa_timeval_cmp(next_elapse, &now) > 0)
            pa_timeval_add(&timeout, pa_timeval_diff(next_elapse, &now));
    }

#ifdef DEBUG_TIMING
    {
        pa_usec_t now = pa_rtclock_now();
        p->awake = now - p->timestamp;
        p->timestamp = now;
        if (!p->quit && timer_enabled)
            pa_log("poll timeout: %d ms ",(int) ((timeout.tv_sec*1000) + (timeout.tv_usec / 1000)));
        else if (q->quit)
            pa_log("poll timeout is ZERO");
        else
            pa_log("poll timeout is FOREVER");
    }
#endif

    /* OK, now let's sleep */
#ifdef HAVE_PPOLL
    {
        struct timespec ts;
        ts.tv_sec = timeout.tv_sec;
        ts.tv_nsec = timeout.tv_usec * 1000;
        r = ppoll(p->pollfd, p->n_pollfd_used, (p->quit || timer_enabled) ? &ts : NULL, NULL);
    }
#else
    r = pa_poll(p->pollfd, p->n_pollfd_used, (p->quit || timer_enabled) ? (int) ((timeout.tv_sec*1000) + (timeout.tv_usec / 1000)) : -1);
#endif

    /* FIXME: We don't know whether the pa_rtpoll owner's timer elapsed or one
     * of the time events created by others through pa_mainloop_api. The alsa
     * sink and source use pa_rtpoll_timer_elapsed() to check whether *their*
     * timer elapsed, so this ambiguity is a problem for them in theory.
     * However, currently the pa_rtpoll objects of the alsa sink and source are
     * not being used through pa_mainloop_api, so in practice there's no
     * ambiguity. We could use pa_rtclock_now() to check whether p->next_elapse
     * is in the past, but we don't do that currently, because pa_rtclock_now()
     * is somewhat expensive and this ambiguity isn't currently a big issue. */
    p->timer_elapsed = r == 0;

#ifdef DEBUG_TIMING
    {
        pa_usec_t now = pa_rtclock_now();
        p->slept = now - p->timestamp;
        p->timestamp = now;

        pa_log("Process time %llu ms; sleep time %llu ms",
               (unsigned long long) (p->awake / PA_USEC_PER_MSEC),
               (unsigned long long) (p->slept / PA_USEC_PER_MSEC));
    }
#endif

    if (r < 0) {
        if (errno == EAGAIN || errno == EINTR)
            r = 0;
        else
            pa_log_error("poll(): %s", pa_cstrerror(errno));

        reset_all_revents(p);
    }

    /* Let's tell everyone that we left the sleep */
    for (i = p->items; i && i->priority < PA_RTPOLL_NEVER; i = i->next) {

        if (i->dead)
            continue;

        if (!i->after_cb)
            continue;

        i->after_cb(i);
    }

finish:

    p->running = false;

    if (p->scan_for_dead) {
        pa_rtpoll_item *n;

        p->scan_for_dead = false;

        for (i = p->items; i; i = n) {
            n = i->next;

            if (i->dead)
                rtpoll_item_destroy(i);
        }
    }

    return r < 0 ? r : !p->quit;
}

void pa_rtpoll_set_timer_absolute(pa_rtpoll *p, pa_usec_t usec) {
    pa_assert(p);

    pa_timeval_store(&p->next_elapse, usec);
    p->timer_enabled = true;
}

void pa_rtpoll_set_timer_relative(pa_rtpoll *p, pa_usec_t usec) {
    pa_assert(p);

    /* Scheduling a timeout for more than an hour is very very suspicious */
    pa_assert(usec <= PA_USEC_PER_SEC*60ULL*60ULL);

    pa_rtclock_get(&p->next_elapse);
    pa_timeval_add(&p->next_elapse, usec);
    p->timer_enabled = true;
}

void pa_rtpoll_set_timer_disabled(pa_rtpoll *p) {
    pa_assert(p);

    memset(&p->next_elapse, 0, sizeof(p->next_elapse));
    p->timer_enabled = false;
}

pa_rtpoll_item *pa_rtpoll_item_new(pa_rtpoll *p, pa_rtpoll_priority_t prio, unsigned n_fds) {
    pa_rtpoll_item *i, *j, *l = NULL;

    pa_assert(p);

    if (!(i = pa_flist_pop(PA_STATIC_FLIST_GET(items))))
        i = pa_xnew(pa_rtpoll_item, 1);

    i->rtpoll = p;
    i->dead = false;
    i->n_pollfd = n_fds;
    i->pollfd = NULL;
    i->priority = prio;

    i->userdata = NULL;
    i->before_cb = NULL;
    i->after_cb = NULL;
    i->work_cb = NULL;

    for (j = p->items; j; j = j->next) {
        if (prio <= j->priority)
            break;

        l = j;
    }

    PA_LLIST_INSERT_AFTER(pa_rtpoll_item, p->items, j ? j->prev : l, i);

    if (n_fds > 0) {
        p->rebuild_needed = 1;
        p->n_pollfd_used += n_fds;
    }

    return i;
}

void pa_rtpoll_item_free(pa_rtpoll_item *i) {
    pa_assert(i);

    if (i->rtpoll->running) {
        i->dead = true;
        i->rtpoll->scan_for_dead = true;
        return;
    }

    rtpoll_item_destroy(i);
}

struct pollfd *pa_rtpoll_item_get_pollfd(pa_rtpoll_item *i, unsigned *n_fds) {
    pa_assert(i);

    if (i->n_pollfd > 0)
        if (i->rtpoll->rebuild_needed)
            rtpoll_rebuild(i->rtpoll);

    if (n_fds)
        *n_fds = i->n_pollfd;

    return i->pollfd;
}

void pa_rtpoll_item_set_before_callback(pa_rtpoll_item *i, int (*before_cb)(pa_rtpoll_item *i)) {
    pa_assert(i);
    pa_assert(i->priority < PA_RTPOLL_NEVER);

    i->before_cb = before_cb;
}

void pa_rtpoll_item_set_after_callback(pa_rtpoll_item *i, void (*after_cb)(pa_rtpoll_item *i)) {
    pa_assert(i);
    pa_assert(i->priority < PA_RTPOLL_NEVER);

    i->after_cb = after_cb;
}

void pa_rtpoll_item_set_work_callback(pa_rtpoll_item *i, int (*work_cb)(pa_rtpoll_item *i)) {
    pa_assert(i);
    pa_assert(i->priority < PA_RTPOLL_NEVER);

    i->work_cb = work_cb;
}

void pa_rtpoll_item_set_userdata(pa_rtpoll_item *i, void *userdata) {
    pa_assert(i);

    i->userdata = userdata;
}

void* pa_rtpoll_item_get_userdata(pa_rtpoll_item *i) {
    pa_assert(i);

    return i->userdata;
}

static int fdsem_before(pa_rtpoll_item *i) {

    if (pa_fdsem_before_poll(i->userdata) < 0)
        return 1; /* 1 means immediate restart of the loop */

    return 0;
}

static void fdsem_after(pa_rtpoll_item *i) {
    pa_assert(i);

    pa_assert((i->pollfd[0].revents & ~POLLIN) == 0);
    pa_fdsem_after_poll(i->userdata);
}

pa_rtpoll_item *pa_rtpoll_item_new_fdsem(pa_rtpoll *p, pa_rtpoll_priority_t prio, pa_fdsem *f) {
    pa_rtpoll_item *i;
    struct pollfd *pollfd;

    pa_assert(p);
    pa_assert(f);

    i = pa_rtpoll_item_new(p, prio, 1);

    pollfd = pa_rtpoll_item_get_pollfd(i, NULL);

    pollfd->fd = pa_fdsem_get(f);
    pollfd->events = POLLIN;

    i->before_cb = fdsem_before;
    i->after_cb = fdsem_after;
    i->userdata = f;

    return i;
}

static int asyncmsgq_read_before(pa_rtpoll_item *i) {
    pa_assert(i);

    if (pa_asyncmsgq_read_before_poll(i->userdata) < 0)
        return 1; /* 1 means immediate restart of the loop */

    return 0;
}

static void asyncmsgq_read_after(pa_rtpoll_item *i) {
    pa_assert(i);

    pa_assert((i->pollfd[0].revents & ~POLLIN) == 0);
    pa_asyncmsgq_read_after_poll(i->userdata);
}

static int asyncmsgq_read_work(pa_rtpoll_item *i) {
    pa_msgobject *object;
    int code;
    void *data;
    pa_memchunk chunk;
    int64_t offset;

    pa_assert(i);

    if (pa_asyncmsgq_get(i->userdata, &object, &code, &data, &offset, &chunk, 0) == 0) {
        int ret;

        if (!object && code == PA_MESSAGE_SHUTDOWN) {
            pa_asyncmsgq_done(i->userdata, 0);
            pa_rtpoll_quit(i->rtpoll);
            return 1;
        }

        ret = pa_asyncmsgq_dispatch(object, code, data, offset, &chunk);
        pa_asyncmsgq_done(i->userdata, ret);
        return 1;
    }

    return 0;
}

pa_rtpoll_item *pa_rtpoll_item_new_asyncmsgq_read(pa_rtpoll *p, pa_rtpoll_priority_t prio, pa_asyncmsgq *q) {
    pa_rtpoll_item *i;
    struct pollfd *pollfd;

    pa_assert(p);
    pa_assert(q);

    i = pa_rtpoll_item_new(p, prio, 1);

    pollfd = pa_rtpoll_item_get_pollfd(i, NULL);
    pollfd->fd = pa_asyncmsgq_read_fd(q);
    pollfd->events = POLLIN;

    i->before_cb = asyncmsgq_read_before;
    i->after_cb = asyncmsgq_read_after;
    i->work_cb = asyncmsgq_read_work;
    i->userdata = q;

    return i;
}

static int asyncmsgq_write_before(pa_rtpoll_item *i) {
    pa_assert(i);

    pa_asyncmsgq_write_before_poll(i->userdata);
    return 0;
}

static void asyncmsgq_write_after(pa_rtpoll_item *i) {
    pa_assert(i);

    pa_assert((i->pollfd[0].revents & ~POLLIN) == 0);
    pa_asyncmsgq_write_after_poll(i->userdata);
}

pa_rtpoll_item *pa_rtpoll_item_new_asyncmsgq_write(pa_rtpoll *p, pa_rtpoll_priority_t prio, pa_asyncmsgq *q) {
    pa_rtpoll_item *i;
    struct pollfd *pollfd;

    pa_assert(p);
    pa_assert(q);

    i = pa_rtpoll_item_new(p, prio, 1);

    pollfd = pa_rtpoll_item_get_pollfd(i, NULL);
    pollfd->fd = pa_asyncmsgq_write_fd(q);
    pollfd->events = POLLIN;

    i->before_cb = asyncmsgq_write_before;
    i->after_cb = asyncmsgq_write_after;
    i->work_cb = NULL;
    i->userdata = q;

    return i;
}

void pa_rtpoll_quit(pa_rtpoll *p) {
    pa_assert(p);

    p->quit = true;
}

bool pa_rtpoll_timer_elapsed(pa_rtpoll *p) {
    pa_assert(p);

    return p->timer_elapsed;
}
