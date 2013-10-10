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
#include <pulsecore/namereg.h>
#include <pulsecore/routing-plan.h>
#include <pulsecore/strbuf.h>

#include "pulse-domain.h"
#include "connection.h"

enum explicit_connection_request_flags {
    /* When making an explicit connection fails, this flag causes the request
     * for the explicit connection to be forgotten, so that the explicit
     * connection won't be automatically restored later, unless someone makes
     * a new request for that connection. */
    EXPLICIT_CONNECTION_REQUEST_REMOVE_IF_ROUTING_FAILS = 0x2
};

struct explicit_connection_request_connection_entry {
    pa_node *input_node;
    pa_node *output_node;
    bool allocated;
};

struct pa_explicit_connection_request {
    pa_core *core;
    unsigned serial;
    struct explicit_connection_request_connection_entry **connection_entries;
    unsigned n_connection_entries;
    pa_node *unlink_node_if_first_routing_fails;
    bool remove_if_routing_fails;
    unsigned times_routed;
    pa_sequence_list list;
};

static void remove_explicit_connection_request(pa_router *router, pa_explicit_connection_request *request);

static struct explicit_connection_request_connection_entry *explicit_connection_request_connection_entry_new(
        pa_node *input_node, pa_node *output_node) {
    struct explicit_connection_request_connection_entry *entry;

    pa_assert(input_node);
    pa_assert(input_node->direction == PA_DIRECTION_INPUT);
    pa_assert(output_node);
    pa_assert(output_node->direction == PA_DIRECTION_OUTPUT);

    entry = pa_xnew0(struct explicit_connection_request_connection_entry, 1);
    entry->input_node = input_node;
    entry->output_node = output_node;

    return entry;
}

static void explicit_connection_request_connection_entry_free(struct explicit_connection_request_connection_entry *entry) {
    pa_assert(entry);

    pa_xfree(entry);
}

static pa_explicit_connection_request *explicit_connection_request_new(pa_node * const connections[][2],
                                                                       unsigned n_connections,
                                                                       pa_node *unlink_node_if_first_routing_fails,
                                                                       enum explicit_connection_request_flags flags) {
    pa_explicit_connection_request *request;
    unsigned i;

    pa_assert(connections);
    pa_assert(n_connections > 0);

    request = pa_xnew0(pa_explicit_connection_request, 1);
    request->core = connections[0][0]->core;
    request->serial = request->core->router.next_explicit_connection_request_serial++;
    request->connection_entries = pa_xnew(struct explicit_connection_request_connection_entry *, n_connections);
    request->n_connection_entries = n_connections;

    for (i = 0; i < n_connections; i++)
        request->connection_entries[i] = explicit_connection_request_connection_entry_new(connections[i][0], connections[i][1]);

    request->unlink_node_if_first_routing_fails = unlink_node_if_first_routing_fails;
    request->remove_if_routing_fails = flags & EXPLICIT_CONNECTION_REQUEST_REMOVE_IF_ROUTING_FAILS;

    return request;
}

static void explicit_connection_request_free(pa_explicit_connection_request *request) {
    unsigned i;

    pa_assert(request);

    for (i = 0; i < request->n_connection_entries; i++)
        explicit_connection_request_connection_entry_free(request->connection_entries[i]);

    pa_xfree(request->connection_entries);
    pa_xfree(request);
}

static int explicit_connection_request_compare(pa_sequence_list *entry1, pa_sequence_list *entry2) {
    pa_explicit_connection_request *request1, *request2;

    request1 = PA_SEQUENCE_LIST_ENTRY(entry1, pa_explicit_connection_request, list);
    request2 = PA_SEQUENCE_LIST_ENTRY(entry2, pa_explicit_connection_request, list);

    if (request1->serial > request2->serial)
        return -1;

    if (request1->serial < request2->serial)
        return 1;

    return 0;
}

static void explicit_connection_request_allocation_failed(pa_explicit_connection_request *request) {
    unsigned i;

    pa_assert(request);

    /* Deallocate connections that the failed request already allocated. */
    for (i = 0; i < request->n_connection_entries; i++) {
        struct explicit_connection_request_connection_entry *entry = request->connection_entries[i];

        if (entry->allocated)
            pa_routing_plan_deallocate_explicit_connection(request->core->router.routing_plan, entry->input_node,
                                                           entry->output_node, request);
    }

    if (request->unlink_node_if_first_routing_fails && request->times_routed == 1) {
        /* We don't want to trigger rerouting here, so let's postpone the node
         * unlinking. */
        pa_dynarray_append(request->core->router.nodes_waiting_for_unlinking, request->unlink_node_if_first_routing_fails);

        /* Since we didn't unlink the node yet, it's still a part of the
         * routing system. That's not good, so let's unregister the node. This
         * will also cause the request object to be freed. */
        pa_router_unregister_node(&request->core->router, request->unlink_node_if_first_routing_fails);
    } else if (request->remove_if_routing_fails)
        remove_explicit_connection_request(&request->core->router, request);
}

static int node_list_compare(pa_sequence_list *entry1, pa_sequence_list *entry2) {
    pa_router *router;
    pa_node *node1, *node2;
    pa_router_compare_t node_compare;
    int result;

    pa_assert(entry1);
    pa_assert(entry2);

    node1 = PA_SEQUENCE_LIST_ENTRY(entry1, pa_node, implicit_route.list);
    node2 = PA_SEQUENCE_LIST_ENTRY(entry2, pa_node, implicit_route.list);

    pa_assert(node1->core == node2->core);
    pa_assert(node1->core);

    router = &node1->core->router;

    if (!(node_compare = router->implicit_route.compare))
        result = 1;
    else
        result = node_compare(node1, node2);

    return result;
}

void pa_router_init(pa_router *router, pa_core *core) {
    pa_assert(router);
    pa_assert(core);

    router->core = core;
    router->module = NULL;

    router->domains = pa_idxset_new(NULL, NULL);
    router->pulse_domain = pa_pulse_domain_new(core);

    router->implicit_route.compare = NULL;
    router->implicit_route.accept = NULL;
    router->implicit_route.groups = pa_idxset_new(NULL, NULL);
    PA_SEQUENCE_HEAD_INIT(router->implicit_route.node_list, node_list_compare);

    router->routing_plan = NULL;
    router->connections = pa_hashmap_new(pa_connection_key_hash_func, pa_connection_key_compare_func);
    PA_SEQUENCE_HEAD_INIT(router->explicit_connection_requests, explicit_connection_request_compare);
    router->nodes_waiting_for_unlinking = pa_dynarray_new(NULL);
    router->fallback_policy = pa_fallback_routing_policy_new(core);
    pa_assert(router->fallback_policy);
}

void pa_router_done(pa_router *router) {
    pa_explicit_connection_request *request, *next;

    pa_assert(router);
    pa_assert(!router->module);

    if (router->fallback_policy)
        pa_fallback_routing_policy_free(router->fallback_policy);

    if (router->nodes_waiting_for_unlinking) {
        pa_assert(pa_dynarray_size(router->nodes_waiting_for_unlinking) == 0);
        pa_dynarray_free(router->nodes_waiting_for_unlinking);
    }

    PA_SEQUENCE_FOREACH_SAFE(request, next, router->explicit_connection_requests, pa_explicit_connection_request, list)
        remove_explicit_connection_request(router, request);

    pa_assert(!router->routing_plan);
    pa_pulse_domain_free(router->pulse_domain);

    pa_assert(pa_idxset_isempty(router->domains));
    pa_idxset_free(router->domains, NULL);

    pa_assert(PA_SEQUENCE_IS_EMPTY(router->implicit_route.node_list));

    pa_assert(pa_idxset_isempty(router->implicit_route.groups));
    pa_idxset_free(router->implicit_route.groups, NULL);

    pa_assert(pa_hashmap_isempty(router->connections));
    pa_hashmap_free(router->connections, NULL);
}

void pa_router_policy_implementation_data_init(pa_router_policy_implementation_data *data) {
    pa_assert(data);

    pa_zero(*data);
}

void pa_router_policy_implementation_data_done(pa_router_policy_implementation_data *data) {
    pa_assert(data);
}

int pa_router_register_policy_implementation(pa_router *router, pa_router_policy_implementation_data *data) {
    pa_assert(router);
    pa_assert(data);
    pa_assert(data->implicit_route.accept);
    pa_assert(data->implicit_route.compare);

    if (router->module) {
        pa_log("Attempted to register multiple routing policy implementations.");
        return -1;
    }

    if (router->fallback_policy) {
        pa_fallback_routing_policy_free(router->fallback_policy);
        router->fallback_policy = NULL;
    }

    pa_assert(PA_SEQUENCE_IS_EMPTY(router->implicit_route.node_list));

    router->module = data->module;
    router->implicit_route.compare = data->implicit_route.compare;
    router->implicit_route.accept = data->implicit_route.accept;
    router->userdata = data->userdata;

    if (router->module)
        pa_log_info("router module '%s' registered", router->module->name);
    else
        pa_log_info("Registered the fallback routing policy implementation.");

    return 0;
}

void pa_router_unregister_policy_implementation(pa_router *router) {
    pa_router_group_entry *entry, *next;

    pa_assert(router);

    if (router->module)
        pa_log_info("Unregistering the routing policy implementation of %s.", router->module->name);
    else
        pa_log_info("Unregistering the fallback routing policy implementation.");

    PA_SEQUENCE_FOREACH_SAFE(entry, next, router->implicit_route.node_list, pa_router_group_entry, node_list)
        pa_router_group_entry_free(entry);

    if (router->module) {
        pa_assert(!router->fallback_policy);
        router->module = NULL;
        router->fallback_policy = pa_fallback_routing_policy_new(router->core);
        pa_assert(router->fallback_policy);
    } else {
        pa_assert(router->fallback_policy);
        router->fallback_policy = NULL;
    }
}

pa_router_group_new_data *pa_router_group_new_data_init(pa_router_group_new_data *data) {
    pa_assert(data);

    pa_zero(*data);

    return data;
}

void pa_router_group_new_data_set_name(pa_router_group_new_data *data, const char *name) {
    pa_assert(data);
    pa_assert(name);

    pa_xfree(data->name);
    data->name = pa_xstrdup(name);
}

void pa_router_group_new_data_done(pa_router_group_new_data *data) {
    pa_assert(data);

    pa_xfree(data->name);
}

static int routing_group_compare(pa_sequence_list *l1, pa_sequence_list *l2) {
    pa_router_group_entry *entry1, *entry2;
    pa_router_group *rtg;
    pa_router_compare_t node_compare;
    int result;

    pa_assert(l1);
    pa_assert(l2);

    entry1 = PA_SEQUENCE_LIST_ENTRY(l1, pa_router_group_entry, group_list);
    entry2 = PA_SEQUENCE_LIST_ENTRY(l2, pa_router_group_entry, group_list);

    pa_assert(entry1->group == entry2->group);
    pa_assert_se((rtg = entry1->group));

    if (!(node_compare = rtg->compare))
        result = 1;
    else
        result = node_compare(entry1->node, entry2->node);

    return result;
}

pa_router_group *pa_router_group_new(pa_core *core, pa_router_group_new_data *data) {
    const char *registered_name = NULL;
    pa_router_group *rtg = NULL;
    pa_router *router;

    pa_assert(core);
    pa_assert(data);
    pa_assert(data->name);
    pa_assert(data->direction == PA_DIRECTION_INPUT || data->direction == PA_DIRECTION_OUTPUT);
    pa_assert(data->accept);
    pa_assert(data->compare);

    router = &core->router;

    rtg = pa_xnew0(pa_router_group, 1);

    if (!(registered_name = pa_namereg_register(core, data->name, PA_NAMEREG_ROUTING_GROUP, rtg, false))) {
        pa_log("Failed to register name %s.", data->name);
        pa_xfree(rtg);
        return NULL;
    }

    rtg->core = core;
    rtg->name = pa_xstrdup(registered_name);
    rtg->direction = data->direction;
    rtg->accept = data->accept;
    rtg->compare = data->compare;

    PA_SEQUENCE_HEAD_INIT(rtg->entries, routing_group_compare);

    pa_assert_se(pa_idxset_put(router->implicit_route.groups, rtg, NULL) == 0);

    pa_log_info("router group '%s' added", rtg->name);

    return rtg;
}

void pa_router_group_free(pa_router_group *rtg) {
    pa_router_group_entry *entry, *next;

    pa_assert(rtg);

    PA_SEQUENCE_FOREACH_SAFE(entry, next, rtg->entries, pa_router_group_entry, group_list)
        pa_router_group_entry_free(entry);

    if (rtg->name) {
        pa_namereg_unregister(rtg->core, rtg->name);
        pa_xfree(rtg->name);
    }

    pa_xfree(rtg);
}

void pa_router_group_update_target_ordering(pa_router_group *group) {
    bool changed;

    pa_assert(group);

    changed = pa_sequence_sort(&group->entries);

    if (changed)
        pa_router_make_routing(&group->core->router);
}

static void router_group_add_node(pa_router_group *rtg, pa_node *node) {
    pa_router_group_entry *entry;

    entry = pa_xnew0(pa_router_group_entry, 1);

    PA_SEQUENCE_LIST_INIT(entry->group_list);
    PA_SEQUENCE_LIST_INIT(entry->node_list);

    entry->node = node;
    entry->group = rtg;

    PA_SEQUENCE_INSERT(rtg->entries, entry->group_list);
    PA_SEQUENCE_INSERT(node->implicit_route.member_of, entry->node_list);

    pa_log_debug("node '%s' added to routing group '%s'", node->name, rtg->name);
}

void pa_router_group_entry_free(pa_router_group_entry *entry) {
    pa_assert(entry);

    entry->node->implicit_route.group = NULL;

    PA_SEQUENCE_REMOVE(entry->group_list);
    PA_SEQUENCE_REMOVE(entry->node_list);

    pa_xfree(entry);
}

void pa_router_register_node(pa_router *router, pa_node *node) {
    pa_router_group *rtg;
    uint32_t idx;

    pa_assert(router);
    pa_assert(node);
    pa_assert(node->direction == PA_DIRECTION_INPUT || node->direction == PA_DIRECTION_OUTPUT);

    if (node->requested_explicit_connections) {
        pa_node **connections;
        pa_explicit_connection_request *request;
        unsigned i;

        /* We allocate n * 2 node pointers, because
         * explicit_connection_request_new() expects to get an array of node
         * pairs. */
        connections = pa_xnew(pa_node *, node->n_requested_explicit_connections * 2);

        for (i = 0; i < node->n_requested_explicit_connections; i++) {
            if (node->direction == PA_DIRECTION_INPUT) {
                connections[i * 2] = node;
                connections[i * 2 + 1] = node->requested_explicit_connections[i];
            } else {
                connections[i * 2] = node->requested_explicit_connections[i];
                connections[i * 2 + 1] = node;
            }
        }

        request = explicit_connection_request_new((pa_node * const (*)[2]) connections, node->n_requested_explicit_connections,
                                                  node, EXPLICIT_CONNECTION_REQUEST_REMOVE_IF_ROUTING_FAILS);
        pa_xfree(connections);
        PA_SEQUENCE_INSERT(router->explicit_connection_requests, request->list);
        pa_node_add_explicit_connection_request(node, request);

        for (i = 0; i < node->n_requested_explicit_connections; i++)
            pa_node_add_explicit_connection_request(node->requested_explicit_connections[i], request);
    }

    if (router->implicit_route.accept && router->implicit_route.accept(router, node) && node->implicit_route.group) {
        PA_SEQUENCE_INSERT(router->implicit_route.node_list, node->implicit_route.list);
    }

    PA_IDXSET_FOREACH(rtg, router->implicit_route.groups, idx) {
        if ((rtg->direction ^ node->direction) && rtg->accept(rtg, node))
            router_group_add_node(rtg, node);
    }
}

static void remove_explicit_connection_request(pa_router *router, pa_explicit_connection_request *request) {
    unsigned i;

    pa_assert(router);
    pa_assert(request);

    for (i = 0; i < request->n_connection_entries; i++) {
        pa_node_remove_explicit_connection_request(request->connection_entries[i]->input_node, request);
        pa_node_remove_explicit_connection_request(request->connection_entries[i]->output_node, request);
    }

    PA_SEQUENCE_REMOVE(request->list);
    explicit_connection_request_free(request);
}

void pa_router_unregister_node(pa_router *router, pa_node *node) {
    pa_router_group_entry *entry, *next;
    pa_explicit_connection_request *request;

    pa_assert(router);
    pa_assert(node);
    pa_assert(node->direction == PA_DIRECTION_INPUT || node->direction == PA_DIRECTION_OUTPUT);

    if (router->routing_plan)
        pa_routing_plan_deallocate_connections_of_node(router->routing_plan, node);

    PA_SEQUENCE_REMOVE(node->implicit_route.list);

    PA_SEQUENCE_FOREACH_SAFE(entry, next, node->implicit_route.member_of, pa_router_group_entry, node_list)
        pa_router_group_entry_free(entry);

    while ((request = pa_dynarray_get_last(node->explicit_connection_requests)))
        remove_explicit_connection_request(router, request);
}

static void make_explicit_routing(pa_core *core) {
    pa_explicit_connection_request *request, *next;

    pa_assert(core);

    pa_log_debug("start making explicit routes");

    PA_SEQUENCE_FOREACH_SAFE(request, next, core->router.explicit_connection_requests, pa_explicit_connection_request, list) {
        unsigned i;

        request->times_routed++;

        for (i = 0; i < request->n_connection_entries; i++)
            request->connection_entries[i]->allocated = false;

        for (i = 0; i < request->n_connection_entries; i++) {
            struct explicit_connection_request_connection_entry *entry = request->connection_entries[i];

            if (pa_routing_plan_allocate_explicit_connection(core->router.routing_plan, entry->input_node, entry->output_node,
                                                             request) < 0) {
                explicit_connection_request_allocation_failed(request);
                break;
            }

            entry->allocated = true;
        }
    }

#if 0
    PA_CONNECTION_FOREACH(conn, core, state) {
        if (conn->type != PA_CONN_TYPE_EXPLICIT)
            continue;

        pa_connection_update(conn, routing_plan_id);
    }
#endif

    pa_log_debug("explicit routing is done");
}

static void make_implicit_routing(pa_core *core, uint32_t routing_plan_id) {
    pa_router *router;
    pa_node *node1, *next, *node2;
    pa_router_group *group;
    pa_router_group_entry *rte, *next_rte;
    pa_connection_new_data data;

    pa_assert(core);

    router = &core->router;

    pa_log_debug("start making implicit routes");

    PA_SEQUENCE_FOREACH_SAFE(node1, next, router->implicit_route.node_list, pa_node, implicit_route.list) {
        pa_assert_se((group = node1->implicit_route.group));

        pa_log_debug("  route '%s' using routing group '%s'", node1->name, group->name);

        PA_SEQUENCE_FOREACH_SAFE(rte, next_rte, group->entries, pa_router_group_entry, group_list) {
            pa_assert_se((node2 = rte->node));

            pa_connection_new_data_init(&data);
            data.type = PA_CONN_TYPE_IMPLICIT;
            data.node1_index = node1->index;
            data.node2_index = node2->index;
            data.routing_plan_id = routing_plan_id;

            if (pa_connection_new(core, &data)) {
                pa_log_debug("      '%s' => '%s'", node1->name, node2->name);
                break;
            }
        }
    }

    pa_log_debug("implicit routing is done");
}

static void implement_routes(pa_core *core, uint32_t routing_plan_id) {
    pa_connection *conn;
    void *state;
    pa_node *input, *output;
    pa_domain_routing_plan *plan;

    pa_log_debug("implement routes");


    PA_CONNECTION_FOREACH(conn, core, state) {
        input = pa_idxset_get_by_index(core->nodes, conn->input_index);
        output = pa_idxset_get_by_index(core->nodes, conn->output_index);

        if (conn->routing_plan_id != routing_plan_id) {
            pa_log_debug("     removing unused connection '%s'(%d) => '%s' (%d)",
                         input ? input->name : "<nonexistent>", conn->input_index,
                         output ? output->name : "<nonexistent>", conn->output_index);

            pa_connection_free(conn);
        }
        else {
            pa_assert(input);
            pa_assert(output);
            pa_assert_se((plan = pa_connection_get_routing_plan(conn)));

            pa_log_debug("     implementing connection '%s'(%d) => '%s' (%d)", input->name, input->index, output->name, output->index);

            pa_domain_implement_connection(plan, conn->userdata);
        }
    }

    pa_log_debug("routing implementation done");
}

void pa_router_make_routing(pa_router *router) {
    static uint32_t plan_id = 0;

    pa_domain *domain;
    uint32_t domidx;

    pa_assert(router);
    pa_assert(!router->routing_plan);

    plan_id++;

    router->routing_plan = pa_routing_plan_new(router->core);

    PA_IDXSET_FOREACH(domain, router->domains, domidx)
        pa_domain_create_routing_plan(domain, plan_id);

    make_explicit_routing(router->core);
    make_implicit_routing(router->core, plan_id);

    implement_routes(router->core, plan_id);

    PA_IDXSET_FOREACH(domain, router->domains, domidx)
        pa_domain_delete_routing_plan(domain, plan_id);

    pa_routing_plan_free(router->routing_plan);
    router->routing_plan = NULL;

    if (pa_dynarray_size(router->nodes_waiting_for_unlinking) > 0) {
        pa_dynarray *copy;
        pa_node *node;
        unsigned i;

        /* Unlinking a node triggers rerouting, so this function will be called
         * recursively. The recursive calls may modify the list of nodes that
         * need to be unlinked, so we need to create a local copy and clear the
         * original list before doing the pa_node_unlink() calls. */
        copy = pa_dynarray_copy(router->nodes_waiting_for_unlinking);
        pa_dynarray_remove_all(router->nodes_waiting_for_unlinking);

        PA_DYNARRAY_FOREACH(node, copy, i)
            pa_node_unlink(node);

        pa_dynarray_free(copy);
    }
}
