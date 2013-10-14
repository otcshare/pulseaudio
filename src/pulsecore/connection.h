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

#include <inttypes.h>

typedef struct pa_connection pa_connection;
typedef struct pa_connection_new_data pa_connection_new_data;

/* Forward declarations for external structs. */
typedef struct pa_core pa_core;
typedef struct pa_domain pa_domain;
typedef struct pa_node pa_node;

struct pa_connection_new_data {
};

struct pa_connection {
    pa_core *core;
    uint32_t index;
    pa_node *input_node;
    pa_node *output_node;
    uint64_t key;
    pa_domain *domain;
    void *userdata;  /* domain specific implementation of the connection */
};

unsigned pa_connection_key_hash_func(const void *p);
int pa_connection_key_compare_func(const void *a, const void *b);
uint64_t pa_connection_key(uint32_t input_index, uint32_t output_index);

pa_connection_new_data *pa_connection_new_data_init(pa_connection_new_data *data);

pa_connection *pa_connection_new(pa_core *core, pa_connection_new_data *data);
void pa_connection_free(pa_connection *connection);

#define PA_CONNECTION_FOREACH(conn, core, state) \
    for ((state) = NULL, (conn) = pa_hashmap_iterate_backwards(core->router.connections, &(state), NULL); (conn); (conn) = pa_hashmap_iterate_backwards(core->router.connections, &(state), NULL))

#endif
