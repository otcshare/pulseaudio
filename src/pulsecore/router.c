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
#include <pulsecore/strbuf.h>
#include <pulsecore/namereg.h>

#include "pulse-domain.h"
#include "connection.h"

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

static unsigned connection_hash(const void *p) {
    static uint32_t mask1[16] = {
        0x00, 0x02, 0x08, 0x0A, 0x20, 0x22, 0x28, 0x2A, 0x80, 0x82, 0x88, 0x8A, 0xA0, 0xA2, 0xA8, 0xAA
    };
    static uint32_t mask2[16] = {
        0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15, 0x40, 0x41, 0x44, 0x45, 0x50, 0x51, 0x54, 0x55
    };

    uint64_t conn = *(uint64_t *)p;
    uint32_t n1 = (uint32_t)((conn >> 32) & 0xffff);
    uint32_t n2 = (uint32_t)(conn & 0xffff);
    uint32_t hash;
    int i;

    for (i = 0, hash = 0;   i < 4;  i++, n1 >>= 4, n2 >>= 4)
        hash = (hash << 8) | (mask1[n1&15] | mask2[n2&15]);

    return (unsigned)hash;
}

static int connection_compare(const void *a, const void *b) {
    uint64_t conn1 = *(uint64_t *)a;
    uint64_t conn2 = *(uint64_t *)b;

    if (conn1 > conn2)
        return 1;

    if (conn1 < conn2)
        return -1;

    return 0;
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

    router->connections = pa_hashmap_new(connection_hash, connection_compare);

    router->fallback_policy = pa_fallback_routing_policy_new(core);
    pa_assert(router->fallback_policy);
}

void pa_router_done(pa_router *router) {
    pa_assert(router);
    pa_assert(!router->module);

    if (router->fallback_policy)
        pa_fallback_routing_policy_free(router->fallback_policy);

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
    pa_sequence_list *l, *n;
    pa_router_group_entry *entry;

    pa_assert(router);

    if (router->module)
        pa_log_info("Unregistering the routing policy implementation of %s.", router->module->name);
    else
        pa_log_info("Unregistering the fallback routing policy implementation.");

    PA_SEQUENCE_FOREACH_SAFE(l, n, router->implicit_route.node_list) {
        entry = PA_SEQUENCE_LIST_ENTRY(l, pa_router_group_entry, node_list);
        pa_router_group_entry_free(entry);
    }

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

static int routing_group_compare(pa_sequence_list *l1, pa_sequence_list *l2)
{
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
    pa_sequence_list *l, *n;
    pa_router_group_entry *entry;

    pa_assert(rtg);

    PA_SEQUENCE_FOREACH_SAFE(l, n, rtg->entries) {
        entry = PA_SEQUENCE_LIST_ENTRY(l, pa_router_group_entry, group_list);
        pa_router_group_entry_free(entry);
    }

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
        pa_router_make_routing(group->core);
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


void pa_router_register_node(pa_node *node) {
    pa_core *core;
    pa_router *router;
    pa_router_group *rtg;
    uint32_t idx;

    pa_assert(node);
    pa_assert_se((core = node->core));
    pa_assert(node->direction == PA_DIRECTION_INPUT || node->direction == PA_DIRECTION_OUTPUT);

    router = &core->router;

    if (router->implicit_route.accept && router->implicit_route.accept(router, node) && node->implicit_route.group) {
        PA_SEQUENCE_INSERT(router->implicit_route.node_list, node->implicit_route.list);
    }

    PA_IDXSET_FOREACH(rtg, router->implicit_route.groups, idx) {
        if ((rtg->direction ^ node->direction) && rtg->accept(rtg, node))
            router_group_add_node(rtg, node);
    }
}

void pa_router_unregister_node(pa_node *node) {
    pa_core *core;
    pa_sequence_list *l, *n;
    pa_router_group_entry *entry;

    pa_assert(node);
    pa_assert_se((core = node->core));
    pa_assert(node->direction == PA_DIRECTION_INPUT || node->direction == PA_DIRECTION_OUTPUT);

    PA_SEQUENCE_REMOVE(node->implicit_route.list);

    PA_SEQUENCE_FOREACH_SAFE(l, n, node->implicit_route.member_of) {
        entry = PA_SEQUENCE_LIST_ENTRY(l, pa_router_group_entry, node_list);
        pa_router_group_entry_free(entry);
    }
}

static void make_explicit_routing(pa_core *core, uint32_t routing_plan_id) {
    pa_connection *conn;
    void *state;

    pa_assert(core);

    pa_log_debug("start making explicit routes");

    PA_CONNECTION_FOREACH(conn, core, state) {
        if (conn->type != PA_CONN_TYPE_EXPLICIT)
            continue;

        pa_connection_update(conn, routing_plan_id);
    }

    pa_log_debug("explicit routing is done");
}

static void make_implicit_routing(pa_core *core, uint32_t routing_plan_id) {
    pa_router *router;
    pa_sequence_list *e1, *e2, *n1, *n2;
    pa_node *node1, *node2;
    pa_router_group *group;
    pa_router_group_entry *rte;
    pa_connection_new_data data;

    pa_assert(core);

    router = &core->router;

    pa_log_debug("start making implicit routes");

    PA_SEQUENCE_FOREACH_SAFE(e1, n1, router->implicit_route.node_list) {
        node1 = PA_SEQUENCE_LIST_ENTRY(e1, pa_node, implicit_route.list);

        pa_assert_se((group = node1->implicit_route.group));

        pa_log_debug("  route '%s' using routing group '%s'", node1->name, group->name);

        PA_SEQUENCE_FOREACH_SAFE(e2, n2, group->entries) {
            rte = PA_SEQUENCE_LIST_ENTRY(e2, pa_router_group_entry, group_list);

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

void pa_router_make_routing(pa_core *core) {
    static uint32_t plan_id = 0;

    pa_router *router;
    pa_domain *domain;
    uint32_t domidx;

    pa_assert(core);
    router = &core->router;

    plan_id++;

    PA_IDXSET_FOREACH(domain, router->domains, domidx)
        pa_domain_create_routing_plan(domain, plan_id);

    make_explicit_routing(core, plan_id);
    make_implicit_routing(core, plan_id);

    implement_routes(core, plan_id);

    PA_IDXSET_FOREACH(domain, router->domains, domidx)
        pa_domain_delete_routing_plan(domain, plan_id);
}
