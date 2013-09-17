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

#include "connection.h"


pa_connection_new_data *pa_connection_new_data_init(pa_connection_new_data *data) {
    pa_assert(data);

    pa_zero(*data);

    return data;
}


static bool get_connection_features(pa_domain_routing_plan *plan, pa_node *input, pa_node *output, pa_node_features *features) {
    pa_node_features *feat1, *feat2;
    pa_node_features buf1, buf2;

    pa_assert(input);
    pa_assert(output);
    pa_assert(plan);
    pa_assert(plan->domain);
    pa_assert(features);


    feat1 = pa_node_get_features(input, plan->domain, &buf1);
    feat2 = pa_node_get_features(output, plan->domain, &buf2);

    return pa_node_common_features(feat1, feat2, features);
}

static pa_connection *setup_new_connection(pa_node *input, pa_node *output, pa_connection_type_t type, uint32_t routing_plan_id, uint64_t key) {
    pa_core *core;
    pa_router *router;
    pa_domain *domain;
    pa_domain_routing_plan *plan;
    pa_node_features features;
    pa_connection *conn;
    bool nodes_available;

    pa_assert(output);
    pa_assert(input);
    pa_assert(input->core == output->core);

    pa_assert_se((core = input->core));
    router = &core->router;

    if (!(domain = pa_domain_list_common(core, &input->domains, &output->domains))) {
        pa_log_debug("     can't connect '%s' => '%s'. No common domain", input->name, output->name);
        return NULL;
    }

    pa_assert_se((plan = pa_domain_get_routing_plan(domain, routing_plan_id)));

    pa_assert(routing_plan_id == plan->id);
    pa_assert(domain == plan->domain);

    if (!(nodes_available = pa_node_available(input, domain) && pa_node_available(output, domain))) {
        if (type != PA_CONN_TYPE_EXPLICIT) {
            pa_log_debug("     can't connect '%s' (%d) => '%s' (%d). Node unavailable", input->name, input->index, output->name, output->index);
            return NULL;
        }
    }

    if (!get_connection_features(plan, input, output, &features)) {
        pa_log_debug("     can't connect '%s' (%u) => '%s' (%u). Feauture mismatch", input->name, input->index, output->name, output->index);
        return NULL;
    }

    conn = pa_xnew0(pa_connection, 1);
    conn->core = core;
    conn->type = type;
    conn->input_index = input->index;
    conn->output_index = output->index;
    conn->key = key;
    conn->domain_index = domain->index;
    conn->routing_plan_id = routing_plan_id;

    pa_assert(!pa_hashmap_put(router->connections, &conn->key, conn));

    if (!nodes_available) {
        pa_log_debug("     created new dormant connection '%s' (%u) => '%s' (%u)", input->name, input->index, output->name, output->index);
        conn->userdata = NULL;
    }
    else {
        if (!pa_node_reserve_path_to_node(input, plan, &features)) {
            pa_log_debug("     can't connect '%s' => '%s'. Failed to set input features", input->name, output->name);
            goto failed_to_set_features;
        }

        if (!pa_node_reserve_path_to_node(output, plan, &features)) {
            pa_log_debug("     can't connect '%s' => '%s'. Failed to set output features", input->name, output->name);
            goto failed_to_set_features;
        }

        pa_log_debug("     setup new connection '%s' (%u) => '%s' (%u)", input->name, input->index, output->name, output->index);
        conn->userdata = pa_domain_create_new_connection(plan, input, output);
    }

    return conn;

 failed_to_set_features:
    if (conn->type != PA_CONN_TYPE_EXPLICIT)
        pa_connection_free(conn);

    return NULL;
}

static pa_connection *reallocate_connection(pa_connection *conn, pa_node *input, pa_node *output, pa_connection_type_t type, uint32_t routing_plan_id) {
    pa_core *core;
    pa_router *router;
    pa_domain *domain;
    pa_domain_routing_plan *plan;
    pa_node_features features;

    pa_assert_se((core = conn->core));
    pa_assert(conn->input_index == input->index);
    pa_assert(conn->output_index == output->index);

    router = &core->router;

    pa_assert_se((domain = pa_idxset_get_by_index(router->domains, conn->domain_index)));
    pa_assert_se((plan = pa_domain_get_routing_plan(domain, routing_plan_id)));

    pa_assert(routing_plan_id == plan->id);
    pa_assert(domain == plan->domain);

    /* convert an implicit route to explicit, if needed */
    if (conn->type != PA_CONN_TYPE_EXPLICIT && type == PA_CONN_TYPE_EXPLICIT) {
        conn->type = PA_CONN_TYPE_EXPLICIT;
        /* TODO: check the possible disasterous consequences of the following lines
           if we happend to iterate the same hashmap */
        pa_hashmap_remove(router->connections, &conn->key);
        pa_assert(!pa_hashmap_put(router->connections, &conn->key, conn));
        pa_log_debug("     converted connection '%s' => '%s' to explicit", input->name, output->name);
    }

    /* if the connection is not part of the plan reserve the pathes to nodes */
    if (conn->routing_plan_id == routing_plan_id)
        pa_log_debug("     nothing to do: connection '%s' (%u) => '%s' (%u) is already part of the plan",
                     input->name, input->index, output->name, output->index);
    else {
        if (!get_connection_features(plan, input, output, &features)) {
            pa_log_debug("     can't connect '%s' => '%s'. Feauture mismatch", input->name, output->name);
            return NULL;
        }

        if (!pa_node_available(input, domain) || !pa_node_available(output, domain)) {
            if (conn->type != PA_CONN_TYPE_EXPLICIT)
                return NULL;
        } else {
            if (!pa_node_reserve_path_to_node(input, plan, &features)) {
                pa_log_debug("     can't connect '%s' => '%s'. Failed to set input features", input->name, output->name);
                return NULL;
            }

            if (!pa_node_reserve_path_to_node(output, plan, &features)) {
                pa_log_debug("     can't connect '%s' => '%s'. Failed to set output features", input->name, output->name);
                return NULL;
            }
        }

        conn->routing_plan_id = routing_plan_id;

        pa_domain_update_existing_connection(plan, conn->userdata);

        pa_log_debug("     reallocated connection '%s' (%u) => '%s' (%u)", input->name, input->index, output->name, output->index);
    }

    return conn;
}


pa_connection *pa_connection_new(pa_core *core, pa_connection_new_data *data) {
    pa_router *router;
    pa_node *node1, *node2, *input, *output;
    uint64_t key;
    pa_connection *conn;

    pa_assert(core);
    pa_assert(data);
    pa_assert(data->type == PA_CONN_TYPE_IMPLICIT || data->type == PA_CONN_TYPE_EXPLICIT);

    if (data->node1_index == data->node2_index)
        return NULL;

    if (!(node1 = pa_idxset_get_by_index(core->nodes, data->node1_index)) ||
        !(node2 = pa_idxset_get_by_index(core->nodes, data->node2_index))) {
        pa_log_debug("     can't connect '%s'(%d) and '%s' (%d). Nonexisting node",
                     node1 ? node1->name : "<nonexistent>", data->node1_index,
                     node2 ? node2->name : "<nonexistent>", data->node2_index);
        return NULL;
    }

    router = &core->router;
    input = NULL;
    output = NULL;

    if (node1->direction == PA_DIRECTION_INPUT)
        input = node1;
    else if (node1->direction == PA_DIRECTION_OUTPUT)
        output = node1;

    if (node2->direction == PA_DIRECTION_INPUT)
        input = node2;
    else if (node2->direction == PA_DIRECTION_OUTPUT)
        output = node2;

    if (!input || !output)
        return NULL;

    key = ((uint64_t)input->index << 32) | (uint64_t)output->index;

    if ((conn = pa_hashmap_get(router->connections, &key)))
        /* existing connection */
        return reallocate_connection(conn, input, output, data->type, data->routing_plan_id);

    /* new connection */
    return setup_new_connection(input, output, data->type, data->routing_plan_id, key);
}

void pa_connection_free(pa_connection *conn) {
    pa_core *core;
    pa_router *router;
    pa_domain *domain;
    pa_domain_routing_plan *plan;

    pa_assert(conn);
    pa_assert_se((core = conn->core));

    router = &core->router;

    if ((domain = pa_idxset_get_by_index(router->domains, conn->domain_index)) &&
        (plan = pa_domain_get_routing_plan(domain, conn->routing_plan_id)))
        pa_domain_delete_connection(plan, conn->userdata);

    pa_assert_se(conn == pa_hashmap_remove(router->connections, &conn->key));

    pa_xfree(conn);
}

pa_connection *pa_connection_update(pa_connection *conn, uint32_t routing_plan_id) {
    pa_core *core;
    pa_node *input, *output;

    pa_assert(conn);
    pa_assert_se((core = conn->core));

    if (!(input = pa_idxset_get_by_index(core->nodes, conn->input_index)) ||
        !(output = pa_idxset_get_by_index(core->nodes, conn->output_index))) {
        if (conn->type == PA_CONN_TYPE_IMPLICIT) {
            pa_log_debug("     delete connection '%s'(%u) => '%s' (%u). Nonexisting node",
                         input ? input->name : "<nonexistent>", conn->input_index,
                         output ? output->name : "<nonexistent>", conn->output_index);
            pa_connection_free(conn);
        }
        return NULL;
    }

    return reallocate_connection(conn, input, output, conn->type, routing_plan_id);
}

bool pa_connection_isvalid(pa_connection *conn) {
    pa_core *core;
    pa_router *router;
    pa_domain *domain;
    pa_node *input, *output;

    pa_assert(conn);
    pa_assert_se((core = conn->core));

    router = &core->router;

    pa_assert_se((domain = pa_idxset_get_by_index(router->domains, conn->domain_index)));

    if (!(input = pa_idxset_get_by_index(core->nodes, conn->input_index)) ||
        !(output = pa_idxset_get_by_index(core->nodes, conn->output_index)))
        return false;

    return true;
}

pa_domain_routing_plan *pa_connection_get_routing_plan(pa_connection *conn) {
    pa_core *core;
    pa_router *router;
    pa_domain *domain;

    pa_assert(conn);

    pa_assert_se((core = conn->core));
    router = &core->router;

    if ((domain = router->pulse_domain)->index != conn->domain_index)
        pa_assert_se((domain = pa_idxset_get_by_index(router->domains, conn->domain_index)));

    return pa_domain_get_routing_plan(domain, conn->routing_plan_id);
}
