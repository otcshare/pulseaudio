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

#include <pulse/xmalloc.h>

#include <pulsecore/connection.h>
#include <pulsecore/core.h>
#include <pulsecore/macro.h>

#include "routing-plan.h"

struct pa_routing_plan {
    pa_core *core;
    pa_hashmap *connections; /* Connection key -> planned connection */
};

struct planned_connection {
    uint64_t key;
    pa_node *input_node;
    pa_node *output_node;
    pa_dynarray *explicit_connection_requests;
};

static struct planned_connection *planned_connection_new(pa_node *input, pa_node *output) {
    pa_domain *domain;
    struct planned_connection *connection;

    pa_assert(input);
    pa_assert(input->direction == PA_DIRECTION_INPUT);
    pa_assert(output);
    pa_assert(output->direction == PA_DIRECTION_OUTPUT);

    /* XXX: There may be multiple common domains, and it would be good to avoid
     * choosing one too early, because it's good to keep all options open as
     * long as possible. I'm not aware of any real-world problems with choosing
     * the domain early, however, so changing this might just add unnecessary
     * complexity. */
    domain = pa_node_get_common_domain(input, output);

    if (!domain) {
        pa_log("Failed to allocate connection from %s to %s: no common domains.", input->name, output->name);
        return NULL;
    }

    if (pa_domain_routing_plan_allocate_connection(pa_domain_get_routing_plan(domain), input, output) < 0)
        return NULL;

    connection = pa_xnew0(struct planned_connection, 1);
    connection->key = pa_connection_key(input->index, output->index);
    connection->input_node = input;
    connection->output_node = output;
    connection->domain = domain;
    connection->explicit_connection_requests = pa_dynarray_new(NULL);

    return connection;
}

static void planned_connection_free(struct planned_connection *connection) {
    pa_assert(connection);

    pa_domain_routing_plan_deallocate_connection(pa_domain_get_routing_plan(connection->domain), connection->input_node,
                                                 connection->output_node);

    if (connection->explicit_connection_requests)
        pa_dynarray_free(connection->explicit_connection_requests);

    pa_xfree(connection);
}

static bool planned_connection_is_valid(struct planned_connection *connection) {
    pa_assert(connection);

    return pa_dynarray_size(connection->explicit_connection_requests) > 0;
}

static void planned_connection_add_explicit_connection_request(struct planned_connection *connection,
                                                               pa_explicit_connection_request *request) {
    pa_assert(connection);
    pa_assert(request);

    pa_dynarray_append(connection->explicit_connection_requests, request);
}

static void planned_connection_remove_explicit_connection_request(struct planned_connection *connection,
                                                                  pa_explicit_connection_request *request) {
    pa_assert(connection);
    pa_assert(request);

    pa_assert_se(pa_dynarray_remove_by_data_fast(connection->explicit_connection_requests, request) >= 0);
}

pa_routing_plan *pa_routing_plan_new(pa_core *core) {
    pa_routing_plan *plan;

    pa_assert(core);

    plan = pa_xnew0(pa_routing_plan, 1);
    plan->core = core;
    plan->connections = pa_hashmap_new(pa_connection_key_hash_func, pa_connection_key_compare_func);

    return plan;
}

void pa_routing_plan_free(pa_routing_plan *plan) {
    pa_assert(plan);

    pa_hashmap_free(plan->connections, (pa_free_cb_t) planned_connection_free);
    pa_xfree(plan);
}

int pa_routing_plan_allocate_explicit_connection(pa_routing_plan *plan, pa_node *input, pa_node *output,
                                                 pa_explicit_connection_request *request) {
    uint64_t key;
    struct planned_connection *connection;

    pa_assert(plan);
    pa_assert(input);
    pa_assert(output);
    pa_assert(request);

    key = pa_connection_key(input->index, output->index);
    connection = pa_hashmap_get(plan->connections, &key);

    if (!connection) {
        connection = planned_connection_new(input, output);

        if (connection)
            pa_hashmap_put(plan->connections, &connection->key, connection);
        else
            return -1;
    }

    planned_connection_add_explicit_connection_request(connection, request);

    return 0;
}

static void remove_planned_connection(pa_routing_plan *plan, struct planned_connection *connection) {
    pa_assert(plan);
    pa_assert(connection);

    pa_assert_se(pa_hashmap_remove(plan->connections, &connection->key));
    planned_connection_free(connection);
}

void pa_routing_plan_deallocate_explicit_connection(pa_routing_plan *plan, pa_node *input_node, pa_node *output_node,
                                                    pa_explicit_connection_request *request) {
    uint64_t key;
    struct planned_connection *connection;

    pa_assert(plan);
    pa_assert(input_node);
    pa_assert(output_node);
    pa_assert(request);

    key = pa_connection_key(input_node->index, output_node->index);
    connection = pa_hashmap_get(plan->connections, &key);
    pa_assert(connection);
    planned_connection_remove_explicit_connection_request(connection, request);

    if (!planned_connection_is_valid(connection))
        remove_planned_connection(plan, connection);
}
