/***
  This file is part of PulseAudio.

  Copyright 2012 Lennart Poettering
  Copyright 2014 Intel Corporation

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

#include "logind.h"

#include <pulsecore/core-error.h>
#include <pulsecore/dynarray.h>
#include <pulsecore/shared.h>

static void seat_new(pa_logind *logind, const char *id);
static void seat_free(pa_logind_seat *seat);

static void session_new(pa_logind *logind, const char *id);
static void session_free(pa_logind_session *session);

static void get_seats(pa_logind *logind) {
    int r;
    char **seats;
    pa_hashmap *old_seats;
    pa_logind_seat *seat;
    void *state;
    pa_dynarray *new_ids;
    char *id;

    pa_assert(logind);

    r = sd_uid_get_seats(getuid(), 0, &seats);
    if (r < 0) {
        pa_log("sd_uid_get_seats() failed: %s", pa_cstrerror(r));
        return;
    }

    /* When we iterate over the new seats, we drop the encountered seats from
     * old_seats. The seats that remain in old_seats in the end will be
     * removed. */
    old_seats = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL, (pa_free_cb_t) seat_free);
    PA_HASHMAP_FOREACH(seat, logind->seats, state)
        pa_hashmap_put(old_seats, seat->id, seat);

    new_ids = pa_dynarray_new(NULL);

    if (seats) {
        char **s;

        /* Note that the seats array is allocated with libc's malloc()/free()
         * calls, hence do not use pa_xfree() to free this here. */

        for (s = seats; *s; s++) {
            seat = pa_hashmap_remove(old_seats, *s);
            if (seat)
                pa_hashmap_put(logind->seats, seat->id, seat);
            else {
                /* We don't create the seat yet, because creating the seat
                 * fires a hook, and we want to postpone firing any hooks until
                 * the seats hashmap is fully updated. */
                pa_dynarray_append(new_ids, pa_xstrdup(*s));
            }

            free(*s);
        }

        free(seats);
    }

    pa_hashmap_free(old_seats);

    while ((id = pa_dynarray_steal_last(new_ids))) {
        seat_new(logind, id);
        pa_xfree(id);
    }

    pa_dynarray_free(new_ids);
}

static void get_sessions(pa_logind *logind) {
    int r;
    char **sessions;
    pa_hashmap *old_sessions;
    pa_logind_session *session;
    void *state;
    pa_dynarray *new_ids;
    char *id;

    pa_assert(logind);

    r = sd_uid_get_sessions(getuid(), 0, &sessions);
    if (r < 0) {
        pa_log("sd_uid_get_sessions() failed: %s", pa_cstrerror(r));
        return;
    }

    /* When we iterate over the new sessions, we drop the encountered sessions
     * from old_sessions. The sessions that remain in old_sessions in the end
     * will be removed. */
    old_sessions = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL,
                                       (pa_free_cb_t) session_free);
    PA_HASHMAP_FOREACH(session, logind->sessions, state)
        pa_hashmap_put(old_sessions, session->id, session);

    new_ids = pa_dynarray_new(NULL);

    if (sessions) {
        char **s;

        /* Note that the sessions array is allocated with libc's
         * malloc()/free() calls, hence do not use pa_xfree() to free
         * this here. */

        for (s = sessions; *s; s++) {
            session = pa_hashmap_remove(old_sessions, *s);
            if (session)
                pa_hashmap_put(logind->sessions, session->id, session);
            else {
                /* We don't create the session yet, because creating the
                 * session fires a hook, and we want to postpone firing any
                 * hooks until the sessions hashmap is fully updated. */
                pa_dynarray_append(new_ids, pa_xstrdup(*s));
            }

            free(*s);
        }

        free(sessions);
    }

    pa_hashmap_free(old_sessions);

    while ((id = pa_dynarray_steal_last(new_ids))) {
        session_new(logind, id);
        pa_xfree(id);
    }

    pa_dynarray_free(new_ids);
}

static void monitor_cb(pa_mainloop_api *api, pa_io_event* event, int fd, pa_io_event_flags_t events, void *userdata) {
    pa_logind *logind = userdata;

    pa_assert(logind);

    sd_login_monitor_flush(logind->monitor);
    get_seats(logind);
    get_sessions(logind);
}

static void set_up_monitor(pa_logind *logind) {
    int r;
    sd_login_monitor *monitor = NULL;

    pa_assert(logind);
    pa_assert(!logind->monitor);

    r = sd_login_monitor_new("session", &monitor);
    if (r < 0) {
        pa_log("sd_login_monitor_new() failed: %s", pa_cstrerror(r));
        return;
    }
    logind->monitor = monitor;

    logind->monitor_event = logind->core->mainloop->io_new(logind->core->mainloop, sd_login_monitor_get_fd(monitor),
                                                           PA_IO_EVENT_INPUT, monitor_cb, logind);
}

static void tear_down_monitor(pa_logind *logind) {
    pa_assert(logind);

    if (logind->monitor_event) {
        logind->core->mainloop->io_free(logind->monitor_event);
        logind->monitor_event = NULL;
    }

    if (logind->monitor) {
        sd_login_monitor_unref(logind->monitor);
        logind->monitor = NULL;
    }
}

static pa_logind *logind_new(pa_core *core) {
    pa_logind *logind = NULL;
    unsigned i;

    pa_assert(core);

    logind = pa_xnew0(pa_logind, 1);
    logind->core = core;
    logind->seats = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    logind->sessions = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    logind->refcnt = 1;

    for (i = 0; i < PA_LOGIND_HOOK_MAX; i++)
        pa_hook_init(&logind->hooks[i], logind);

    /* If we are not actually running logind, then let's do nothing. */
    if (access("/run/systemd/seats/", F_OK) < 0)
        goto finish;

    set_up_monitor(logind);
    get_seats(logind);
    get_sessions(logind);

finish:
    pa_shared_set(core, "logind", logind);

    return logind;
}

static void logind_free(pa_logind *logind) {
    unsigned i;

    pa_assert(logind);

    pa_shared_remove(logind->core, "logind");

    if (logind->sessions) {
        pa_logind_session *session;

        while ((session = pa_hashmap_first(logind->sessions)))
            session_free(session);
    }

    if (logind->seats) {
        pa_logind_seat *seat;

        while ((seat = pa_hashmap_first(logind->seats)))
            seat_free(seat);
    }

    tear_down_monitor(logind);

    for (i = 0; i < PA_LOGIND_HOOK_MAX; i++)
        pa_hook_done(&logind->hooks[i]);

    if (logind->sessions) {
        pa_assert(pa_hashmap_isempty(logind->sessions));
        pa_hashmap_free(logind->sessions);
    }

    if (logind->seats) {
        pa_assert(pa_hashmap_isempty(logind->seats));
        pa_hashmap_free(logind->seats);
    }

    pa_xfree(logind);
}

pa_logind *pa_logind_get(pa_core *core) {
    pa_logind *logind;

    pa_assert(core);

    logind = pa_shared_get(core, "logind");
    if (logind) {
        logind->refcnt++;
        return logind;
    }

    return logind_new(core);
}

void pa_logind_unref(pa_logind *logind) {
    pa_assert(logind);
    pa_assert(logind->refcnt > 0);

    logind->refcnt--;

    if (logind->refcnt == 0)
        logind_free(logind);
}

static void seat_new(pa_logind *logind, const char *id) {
    pa_logind_seat *seat;

    pa_assert(logind);
    pa_assert(id);

    seat = pa_xnew0(pa_logind_seat, 1);
    seat->logind = logind;
    seat->id = pa_xstrdup(id);

    pa_assert_se(pa_hashmap_put(logind->seats, seat->id, seat) >= 0);

    pa_log_debug("Created seat %s.", seat->id);

    pa_hook_fire(&logind->hooks[PA_LOGIND_HOOK_SEAT_ADDED], seat);
}

static void seat_free(pa_logind_seat *seat) {
    pa_assert(seat);

    pa_log_debug("Freeing seat %s.", seat->id);

    if (pa_hashmap_remove(seat->logind->seats, seat->id))
        pa_hook_fire(&seat->logind->hooks[PA_LOGIND_HOOK_SEAT_REMOVED], seat);

    pa_xfree(seat->id);
    pa_xfree(seat);
}

static void session_new(pa_logind *logind, const char *id) {
    pa_logind_session *session;

    pa_assert(logind);
    pa_assert(id);

    session = pa_xnew0(pa_logind_session, 1);
    session->logind = logind;
    session->id = pa_xstrdup(id);

    pa_assert_se(pa_hashmap_put(logind->sessions, session->id, session) >= 0);

    pa_log_debug("Created session %s.", session->id);

    pa_hook_fire(&logind->hooks[PA_LOGIND_HOOK_SESSION_ADDED], session);
}

static void session_free(pa_logind_session *session) {
    pa_assert(session);

    pa_log_debug("Freeing session %s.", session->id);

    if (pa_hashmap_remove(session->logind->sessions, session->id))
        pa_hook_fire(&session->logind->hooks[PA_LOGIND_HOOK_SESSION_REMOVED], session);

    pa_xfree(session->id);
    pa_xfree(session);
}
