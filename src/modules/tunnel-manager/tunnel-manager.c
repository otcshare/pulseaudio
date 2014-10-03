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

static pa_tunnel_manager *tunnel_manager_new(pa_core *core) {
    pa_tunnel_manager *manager;
    pa_tunnel_manager_config *manager_config;
    pa_tunnel_manager_remote_server_config *server_config;
    void *state;

    pa_assert(core);

    manager = pa_xnew0(pa_tunnel_manager, 1);
    manager->core = core;
    manager->remote_servers = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    manager->refcnt = 1;

    manager_config = pa_tunnel_manager_config_new();

    pa_log_debug("Created the tunnel manager.");

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
