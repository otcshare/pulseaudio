#ifndef foologindhfoo
#define foologindhfoo

/***
  This file is part of PulseAudio.

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

#include <pulsecore/core.h>

#include <systemd/sd-login.h>

typedef struct pa_logind pa_logind;
typedef struct pa_logind_seat pa_logind_seat;
typedef struct pa_logind_session pa_logind_session;

enum {
    PA_LOGIND_HOOK_SEAT_ADDED,
    PA_LOGIND_HOOK_SEAT_REMOVED,
    PA_LOGIND_HOOK_SESSION_ADDED,
    PA_LOGIND_HOOK_SESSION_REMOVED,
    PA_LOGIND_HOOK_MAX,
};

/* Currently pa_logind doesn't track all seats and sessions in the system, only
 * those that belong to the current user. */
struct pa_logind {
    pa_core *core;
    pa_hashmap *seats; /* id -> pa_logind_seat */
    pa_hashmap *sessions; /* id -> pa_logind_session */
    pa_hook hooks[PA_LOGIND_HOOK_MAX];

    unsigned refcnt;
    sd_login_monitor *monitor;
    pa_io_event *monitor_event;
};

pa_logind *pa_logind_get(pa_core *core);
void pa_logind_unref(pa_logind *logind);

struct pa_logind_seat {
    pa_logind *logind;
    char *id;
};

struct pa_logind_session {
    pa_logind *logind;
    char *id;
};

#endif
