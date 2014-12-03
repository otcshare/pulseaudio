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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "tunnel-manager.h"

#include <modules/tunnel-manager/remote-server.h>
#include <modules/tunnel-manager/tunnel-manager-config.h>

#include <pulsecore/core-util.h>
#include <pulsecore/shared.h>

const char *pa_tunnel_manager_remote_device_tunnel_enabled_condition_to_string(
        pa_tunnel_manager_remote_device_tunnel_enabled_condition_t condition) {
    switch (condition) {
        case PA_TUNNEL_MANAGER_REMOTE_DEVICE_TUNNEL_ENABLED_CONDITION_NOT_MONITOR:
            return "!device.is_monitor";

        case PA_TUNNEL_MANAGER_REMOTE_DEVICE_TUNNEL_ENABLED_CONDITION_NOT_MONITOR_AND_SEAT_IS_OK:
            return "!device.is_monitor && (!device.seat || seats.contains(device.seat))";
    }

    pa_assert_not_reached();
}

int pa_tunnel_manager_remote_device_tunnel_enabled_condition_from_string(
        const char *str, pa_tunnel_manager_remote_device_tunnel_enabled_condition_t *_r) {
    pa_tunnel_manager_remote_device_tunnel_enabled_condition_t condition;

    pa_assert(str);
    pa_assert(_r);

    if (pa_streq(str, "!device.is_monitor"))
        condition = PA_TUNNEL_MANAGER_REMOTE_DEVICE_TUNNEL_ENABLED_CONDITION_NOT_MONITOR;
    else if (pa_streq(str, "!device.is_monitor && (!device.seat || seats.contains(device.seat))"))
        condition = PA_TUNNEL_MANAGER_REMOTE_DEVICE_TUNNEL_ENABLED_CONDITION_NOT_MONITOR_AND_SEAT_IS_OK;
    else
        return -PA_ERR_INVALID;

    *_r = condition;
    return 0;
}

static pa_tunnel_manager *tunnel_manager_new(pa_core *core) {
    pa_tunnel_manager *manager;
    pa_tunnel_manager_config *manager_config;
    pa_tunnel_manager_config_value *config_value;
    pa_tunnel_manager_remote_server_config *server_config;
    void *state;

    pa_assert(core);

    manager = pa_xnew0(pa_tunnel_manager, 1);
    manager->core = core;
    manager->remote_device_tunnel_enabled_condition = PA_TUNNEL_MANAGER_REMOTE_DEVICE_TUNNEL_ENABLED_CONDITION_NOT_MONITOR;
    manager->remote_servers = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    manager->refcnt = 1;
#ifdef HAVE_SYSTEMD_LOGIN
    manager->logind = pa_logind_get(core);
#endif

    manager_config = pa_tunnel_manager_config_new();

    config_value = manager_config->remote_device_tunnel_enabled_condition;
    if (config_value) {
        int r;
        pa_tunnel_manager_remote_device_tunnel_enabled_condition_t condition;

        r = pa_tunnel_manager_remote_device_tunnel_enabled_condition_from_string(config_value->value, &condition);
        if (r >= 0)
            manager->remote_device_tunnel_enabled_condition = condition;
        else
            pa_log("[%s:%u] Invalid condition: \"%s\"", config_value->filename, config_value->lineno, config_value->value);
    }

    pa_log_debug("Created the tunnel manager.");
    pa_log_debug("    Remote device tunnel enabled condition: %s",
                 pa_tunnel_manager_remote_device_tunnel_enabled_condition_to_string(
                         manager->remote_device_tunnel_enabled_condition));

    PA_HASHMAP_FOREACH(server_config, manager_config->remote_servers, state)
        pa_tunnel_manager_remote_server_new(manager, server_config);

    pa_tunnel_manager_config_free(manager_config);

    pa_shared_set(core, "tunnel_manager", manager);

    return manager;
}

static void tunnel_manager_free(pa_tunnel_manager *manager) {
    pa_assert(manager);
    pa_assert(manager->refcnt == 0);

    pa_log_debug("Freeing the tunnel manager.");

    pa_shared_remove(manager->core, "tunnel_manager");

    if (manager->remote_servers) {
        pa_tunnel_manager_remote_server *server;

        while ((server = pa_hashmap_first(manager->remote_servers)))
            pa_tunnel_manager_remote_server_free(server);
    }

#ifdef HAVE_SYSTEMD_LOGIN
    if (manager->logind)
        pa_logind_unref(manager->logind);
#endif

    if (manager->remote_servers) {
        pa_assert(pa_hashmap_isempty(manager->remote_servers));
        pa_hashmap_free(manager->remote_servers);
    }

    pa_xfree(manager);
}

pa_tunnel_manager *pa_tunnel_manager_get(pa_core *core, bool ref) {
    pa_tunnel_manager *manager;

    pa_assert(core);

    manager = pa_shared_get(core, "tunnel_manager");
    if (manager) {
        if (ref)
            manager->refcnt++;

        return manager;
    }

    if (ref)
        return tunnel_manager_new(core);

    return NULL;
}

void pa_tunnel_manager_unref(pa_tunnel_manager *manager) {
    pa_assert(manager);
    pa_assert(manager->refcnt > 0);

    manager->refcnt--;

    if (manager->refcnt == 0)
        tunnel_manager_free(manager);
}
