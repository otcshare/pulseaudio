#ifndef fooremoteserverhfoo
#define fooremoteserverhfoo

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

#include <modules/tunnel-manager/tunnel-manager.h>
#include <modules/tunnel-manager/tunnel-manager-config.h>

#include <pulse/context.h>

typedef struct pa_tunnel_manager_remote_server pa_tunnel_manager_remote_server;

struct pa_tunnel_manager_remote_server {
    pa_tunnel_manager *manager;
    char *name;
    char *address;
    pa_hashmap *devices; /* name -> pa_tunnel_manager_remote_device */
    bool failed;

    pa_context *context;
    pa_operation *list_sinks_operation;
    pa_operation *list_sources_operation;
    pa_hashmap *device_stubs; /* struct device_stub -> struct device_stub (hashmap-as-a-set) */
};

void pa_tunnel_manager_remote_server_new(pa_tunnel_manager *manager, pa_tunnel_manager_remote_server_config *config);
void pa_tunnel_manager_remote_server_free(pa_tunnel_manager_remote_server *server);
void pa_tunnel_manager_remote_server_set_failed(pa_tunnel_manager_remote_server *server, bool failed);

#endif
