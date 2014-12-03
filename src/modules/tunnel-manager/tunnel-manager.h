#ifndef footunnelmanagerhfoo
#define footunnelmanagerhfoo

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

#ifdef HAVE_SYSTEMD_LOGIN
#include <modules/logind/logind.h>
#endif

#include <pulsecore/core.h>

#define PA_TUNNEL_MANAGER_MAX_DEVICES_PER_SERVER 50

typedef struct pa_tunnel_manager pa_tunnel_manager;

typedef enum {
    PA_TUNNEL_MANAGER_REMOTE_DEVICE_TUNNEL_ENABLED_CONDITION_NOT_MONITOR,
    PA_TUNNEL_MANAGER_REMOTE_DEVICE_TUNNEL_ENABLED_CONDITION_NOT_MONITOR_AND_SEAT_IS_OK,
} pa_tunnel_manager_remote_device_tunnel_enabled_condition_t;

const char *pa_tunnel_manager_remote_device_tunnel_enabled_condition_to_string(
        pa_tunnel_manager_remote_device_tunnel_enabled_condition_t condition);
int pa_tunnel_manager_remote_device_tunnel_enabled_condition_from_string(
        const char *str, pa_tunnel_manager_remote_device_tunnel_enabled_condition_t *_r);

struct pa_tunnel_manager {
    pa_core *core;
    pa_tunnel_manager_remote_device_tunnel_enabled_condition_t remote_device_tunnel_enabled_condition;
    pa_hashmap *remote_servers; /* name -> pa_tunnel_manager_remote_server */

    unsigned refcnt;
#ifdef HAVE_SYSTEMD_LOGIN
    pa_logind *logind;
#endif
};

/* If ref is true, the reference count of the manager is incremented, and also
 * the manager is created if it doesn't exist yet. If ref is false, the
 * reference count is not incremented, and if the manager doesn't exist, the
 * function returns NULL. */
pa_tunnel_manager *pa_tunnel_manager_get(pa_core *core, bool ref);

void pa_tunnel_manager_unref(pa_tunnel_manager *manager);

#endif
