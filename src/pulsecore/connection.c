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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/core.h>
#include <pulsecore/domain.h>
#include <pulsecore/idxset.h>

#include "connection.h"

unsigned pa_connection_key_hash_func(const void *p) {
    static uint32_t mask1[16] = {
        0x00, 0x02, 0x08, 0x0A, 0x20, 0x22, 0x28, 0x2A, 0x80, 0x82, 0x88, 0x8A, 0xA0, 0xA2, 0xA8, 0xAA
    };
    static uint32_t mask2[16] = {
        0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15, 0x40, 0x41, 0x44, 0x45, 0x50, 0x51, 0x54, 0x55
    };

    uint64_t key = *(uint64_t *) p;
    uint32_t n1 = (uint32_t) ((key >> 32) & 0xffff);
    uint32_t n2 = (uint32_t) (key & 0xffff);
    uint32_t hash;
    int i;

    for (i = 0, hash = 0; i < 4; i++, n1 >>= 4, n2 >>= 4)
        hash = (hash << 8) | (mask1[n1 & 15] | mask2[n2 & 15]);

    return (unsigned) hash;
}

int pa_connection_key_compare_func(const void *a, const void *b) {
    uint64_t key1 = *(uint64_t *)a;
    uint64_t key2 = *(uint64_t *)b;

    if (key1 > key2)
        return 1;

    if (key1 < key2)
        return -1;

    return 0;
}

uint64_t pa_connection_key(uint32_t input_index, uint32_t output_index) {
    return ((uint64_t) input_index << 32) | (uint64_t) output_index;
}

pa_connection_new_data *pa_connection_new_data_init(pa_connection_new_data *data) {
    pa_assert(data);

    pa_zero(*data);

    return data;
}

pa_connection *pa_connection_new(pa_core *core, pa_connection_new_data *data) {
    pa_connection *connection;

    pa_assert(core);
    pa_assert(data);

    connection = pa_xnew0(pa_connection, 1);
    connection->core = core;
    pa_assert_se(pa_idxset_put(core->connections, connection, &connection->index) >= 0);

    return connection;
}

void pa_connection_free(pa_connection *conn) {
    pa_assert(conn);

    pa_assert_se(pa_idxset_remove_by_index(conn->core->nodes, conn->index));
    pa_xfree(conn);
}
