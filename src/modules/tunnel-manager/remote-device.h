#ifndef fooremotedevicehfoo
#define fooremotedevicehfoo

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

#include <modules/tunnel-manager/remote-server.h>

typedef struct pa_tunnel_manager_remote_device pa_tunnel_manager_remote_device;

enum {
    PA_TUNNEL_MANAGER_REMOTE_DEVICE_HOOK_UNLINKED,
    PA_TUNNEL_MANAGER_REMOTE_DEVICE_HOOK_PROPLIST_CHANGED,
    PA_TUNNEL_MANAGER_REMOTE_DEVICE_HOOK_MAX,
};

struct pa_tunnel_manager_remote_device {
    pa_tunnel_manager_remote_server *server;
    char *name;
    pa_device_type_t type;
    uint32_t index;
    pa_proplist *proplist;
    pa_sample_spec sample_spec;
    pa_channel_map channel_map;
    bool is_monitor;
    bool tunnel_enabled;
    pa_hook hooks[PA_TUNNEL_MANAGER_REMOTE_DEVICE_HOOK_MAX];

    pa_operation *get_info_operation;
#ifdef HAVE_SYSTEMD_LOGIN
    pa_hook_slot *seat_added_slot;
    pa_hook_slot *seat_removed_slot;
#endif
    pa_module *tunnel_module;
    pa_hook_slot *module_unload_slot;

    /* These are a workaround for the problem that the introspection API's info
     * callbacks are called multiple times, which means that if the userdata
     * needs to be freed during the callbacks, the freeing needs to be
     * postponed until the last call. */
    bool can_free;
    bool dead;
};

void pa_tunnel_manager_remote_device_new(pa_tunnel_manager_remote_server *server, pa_device_type_t type, const void *info);
void pa_tunnel_manager_remote_device_free(pa_tunnel_manager_remote_device *device);
void pa_tunnel_manager_remote_device_update(pa_tunnel_manager_remote_device *device);

#endif
