#ifndef fooconnectionhfoo
#define fooconnectionhfoo

/***
  This file is part of PulseAudio.

  Copyright (c) 2012 Intel Corporation
  Janos Kovacs <jankovac503@gmail.com>

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

typedef struct pa_connection pa_connection;
typedef struct pa_connection_new_data pa_connection_new_data;

#include <pulsecore/core.h>
#include <pulsecore/device-class.h>

typedef enum {
    PA_CONN_TYPE_IMPLICIT,
    PA_CONN_TYPE_EXPLICIT
} pa_connection_type_t;

struct pa_connection_new_data {
    pa_connection_type_t type;
    uint32_t node1_index, node2_index;
    uint32_t routing_plan_id;
};

struct pa_connection {
    pa_core *core;
    pa_connection_type_t type;
    uint32_t input_index, output_index;
    uint64_t key;
    uint32_t domain_index;
    uint32_t routing_plan_id;
    void *userdata;  /* domian specific implementation of the connection */
};

pa_connection_new_data *pa_connection_new_data_init(pa_connection_new_data *data);
pa_connection *pa_connection_new(pa_core *core, pa_connection_new_data *data);
void pa_connection_free(pa_connection *connection);

pa_connection *pa_connection_update(pa_connection *connection, uint32_t routing_plan_id);

bool pa_connection_isvalid(pa_connection *connection);
pa_domain_routing_plan *pa_connection_get_routing_plan(pa_connection *connection);

#define PA_CONNECTION_FOREACH(conn, core, state) \
    for ((state) = NULL, (conn) = pa_hashmap_iterate_backwards(core->router.connections, &(state), NULL); (conn); (conn) = pa_hashmap_iterate_backwards(core->router.connections, &(state), NULL))

#endif
