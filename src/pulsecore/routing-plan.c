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
#include <pulsecore/domain.h>
#include <pulsecore/dynarray.h>
#include <pulsecore/hashmap.h>
#include <pulsecore/macro.h>
#include <pulsecore/node.h>

#include "routing-plan.h"

struct pa_routing_plan {
    pa_core *core;
    pa_hashmap *connections; /* Connection key -> planned connection */
};

struct pa_routing_plan_node_data {
    pa_dynarray *planned_connections;
};

typedef enum {
    PLANNED_CONNECTION_STATE_INIT,
    PLANNED_CONNECTION_STATE_LINKED,
    PLANNED_CONNECTION_STATE_UNLINKED
} planned_connection_state_t;

struct planned_connection {
    pa_routing_plan *plan;
    planned_connection_state_t state;
    uint64_t key;
    pa_node *input_node;
    pa_node *output_node;
    pa_domain *domain;
    pa_dynarray *explicit_connection_requests;
    bool implicit;
};

static void planned_connection_free(struct planned_connection *connection);

static struct planned_connection *planned_connection_new(pa_routing_plan *plan, pa_node *input, pa_node *output) {
    struct planned_connection *connection;

    pa_assert(input);
    pa_assert(input->direction == PA_DIRECTION_INPUT);
    pa_assert(output);
    pa_assert(output->direction == PA_DIRECTION_OUTPUT);

    connection = pa_xnew0(struct planned_connection, 1);
    connection->plan = plan;
    connection->state = PLANNED_CONNECTION_STATE_INIT;
    connection->key = pa_connection_key(input->index, output->index);
    connection->input_node = input;
    connection->output_node = output;

    /* XXX: There may be multiple common domains, and it would be good to avoid
     * choosing one too early, because it's good to keep all options open as
     * long as possible. I'm not aware of any real-world problems with choosing
     * the domain early, however, so changing this might just add unnecessary
     * complexity. */
    connection->domain = pa_node_get_common_domain(input, output);

    if (!connection->domain) {
        pa_log("Failed to allocate connection from %s to %s: no common domains.", input->name, output->name);
        goto fail;
    }

    connection->explicit_connection_requests = pa_dynarray_new(NULL);

    return connection;

fail:
    planned_connection_free(connection);

    return NULL;
}

static int planned_connection_put(struct planned_connection *connection) {
    pa_assert(connection);
    pa_assert(connection->state == PLANNED_CONNECTION_STATE_INIT);

    pa_assert_se(pa_hashmap_put(connection->plan->connections, &connection->key, connection) >= 0);

    if (pa_domain_allocate_connection(connection->domain, connection->input_node, connection->output_node) < 0)
        return -1;

    pa_dynarray_append(connection->input_node->routing_plan_data->planned_connections, connection);
    pa_dynarray_append(connection->output_node->routing_plan_data->planned_connections, connection);

    connection->state = PLANNED_CONNECTION_STATE_LINKED;

    return 0;
}

static void planned_connection_unlink(struct planned_connection *connection) {
    pa_assert(connection);

    if (connection->state != PLANNED_CONNECTION_STATE_LINKED)
        return;

    connection->state = PLANNED_CONNECTION_STATE_UNLINKED;

    pa_assert_se(pa_dynarray_remove_by_data_fast(connection->output_node->routing_plan_data->planned_connections,
                                                 connection) >= 0);
    pa_assert_se(pa_dynarray_remove_by_data_fast(connection->input_node->routing_plan_data->planned_connections,
                                                 connection) >= 0);
    pa_domain_deallocate_connection(connection->domain, connection->input_node, connection->output_node);
    pa_assert_se(pa_hashmap_remove(connection->plan->connections, &connection->key));
}

static void planned_connection_free(struct planned_connection *connection) {
    pa_assert(connection);

    if (connection->state == PLANNED_CONNECTION_STATE_LINKED)
        planned_connection_unlink(connection);

    if (connection->explicit_connection_requests)
        pa_dynarray_free(connection->explicit_connection_requests);

    pa_xfree(connection);
}

static bool planned_connection_is_valid(struct planned_connection *connection) {
    pa_assert(connection);

    return connection->implicit || pa_dynarray_size(connection->explicit_connection_requests) > 0;
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

static void planned_connection_set_implicit(struct planned_connection *connection, bool implicit) {
    pa_assert(connection);

    connection->implicit = implicit;
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

void pa_routing_plan_clear(pa_routing_plan *plan, bool clear_temporary_constraints) {
    pa_domain *domain;
    uint32_t idx;

    pa_assert(plan);

    pa_hashmap_remove_all(plan->connections, (pa_free_cb_t) planned_connection_free);

    if (clear_temporary_constraints) {
        PA_IDXSET_FOREACH(domain, plan->core->router.domains, idx)
            pa_domain_clear_temporary_constraints(domain);
    }
}

static struct planned_connection *allocate_connection(pa_routing_plan *plan, pa_node *input, pa_node *output) {
    uint64_t key;
    struct planned_connection *connection;

    pa_assert(plan);
    pa_assert(input);
    pa_assert(output);

    key = pa_connection_key(input->index, output->index);
    connection = pa_hashmap_get(plan->connections, &key);

    if (!connection) {
        connection = planned_connection_new(plan, input, output);

        if (!connection)
            return NULL;

        if (planned_connection_put(connection) < 0) {
            planned_connection_free(connection);
            return NULL;
        }
    }

    return connection;
}

int pa_routing_plan_allocate_explicit_connection(pa_routing_plan *plan, pa_node *input, pa_node *output,
                                                 pa_explicit_connection_request *request) {
    struct planned_connection *connection;

    pa_assert(plan);
    pa_assert(input);
    pa_assert(output);
    pa_assert(request);

    connection = allocate_connection(plan, input, output);

    if (!connection)
        return -1;

    planned_connection_add_explicit_connection_request(connection, request);

    return 0;
}

int pa_routing_plan_allocate_implicit_connection(pa_routing_plan *plan, pa_node *input, pa_node *output) {
    struct planned_connection *connection;

    pa_assert(plan);
    pa_assert(input);
    pa_assert(output);

    connection = allocate_connection(plan, input, output);

    if (!connection)
        return -1;

    planned_connection_set_implicit(connection, true);

    return 0;
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
        planned_connection_free(connection);
}

void pa_routing_plan_deallocate_connections_of_node(pa_routing_plan *plan, pa_node *node) {
    struct planned_connection *connection;

    pa_assert(plan);
    pa_assert(node);

    while ((connection = pa_dynarray_get_last(node->routing_plan_data->planned_connections)))
        planned_connection_free(connection);
}

int pa_routing_plan_execute(pa_routing_plan *plan) {
    pa_connection *real_connection;
    uint32_t idx;
    struct planned_connection *planned_connection;
    void *state;

    pa_assert(plan);

    PA_IDXSET_FOREACH(real_connection, plan->core->connections, idx) {
        planned_connection = pa_hashmap_get(plan->connections, &real_connection->key);

        if (!planned_connection || real_connection->domain != planned_connection->domain)
            pa_domain_delete_connection(real_connection->domain, real_connection);
    }

    PA_HASHMAP_FOREACH(planned_connection, plan->connections, state) {
        if (pa_domain_implement_connection(planned_connection->domain, planned_connection->input_node,
                                           planned_connection->output_node) < 0)
            return -1;
    }

    return 0;
}

pa_routing_plan_node_data *pa_routing_plan_node_data_new(void) {
    pa_routing_plan_node_data *data;

    data = pa_xnew0(pa_routing_plan_node_data, 1);
    data->planned_connections = pa_dynarray_new(NULL);

    return data;
}

void pa_routing_plan_node_data_free(pa_routing_plan_node_data *data) {
    pa_assert(data);

    if (data->planned_connections)
        pa_dynarray_free(data->planned_connections);
}
