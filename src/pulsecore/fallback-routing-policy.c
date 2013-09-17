/***
  This file is part of PulseAudio.

  Copyright 2013 Intel Corporation

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

#include <pulsecore/namereg.h>

#include "fallback-routing-policy.h"

struct pa_fallback_routing_policy {
    pa_router *router;
    bool registered;
    pa_router_group *input_routing_group;
    pa_router_group *output_routing_group;
    pa_hook_slot *default_sink_changed_slot;
    pa_hook_slot *default_source_changed_slot;
};

static bool routee_accept_func(pa_router *router, pa_node *node) {
    pa_assert(node);

    if (node->type == PA_NODE_TYPE_SINK_INPUT) {
        node->implicit_route.group = ((pa_fallback_routing_policy *) router->userdata)->input_routing_group;
        return true;
    }

    if (node->type == PA_NODE_TYPE_SOURCE_OUTPUT) {
        node->implicit_route.group = ((pa_fallback_routing_policy *) router->userdata)->output_routing_group;
        return true;
    }

    return false;
}

static int routee_compare_func(pa_node *node1, pa_node *node2) {
    pa_assert(node1);
    pa_assert(node2);

    if (node1->index < node2->index)
        return 1;
    else if (node1->index > node2->index)
        return -1;

    pa_assert(node1 == node2);

    return 0;
}

static bool routing_target_accept_func(pa_router_group *group, pa_node *node) {
    pa_assert(group);
    pa_assert(node);

    if (node->type == PA_NODE_TYPE_PORT || node->type == PA_NODE_TYPE_SINK || node->type == PA_NODE_TYPE_SOURCE)
        return true;

    return false;
}

static pa_node *get_node_for_default_sink(pa_core *core) {
    pa_sink *default_sink;

    pa_assert(core);

    default_sink = pa_namereg_get_default_sink(core);

    if (!default_sink)
        return NULL;

    if (default_sink->node)
        return default_sink->node;

    if (default_sink->active_port)
        return default_sink->active_port->node;

    return NULL;
}

static pa_node *get_node_for_default_source(pa_core *core) {
    pa_source *default_source;

    pa_assert(core);

    default_source = pa_namereg_get_default_source(core);

    if (!default_source)
        return NULL;

    if (default_source->node)
        return default_source->node;

    if (default_source->active_port)
        return default_source->active_port->node;

    return NULL;
}

static bool is_monitor_source(pa_node *node) {
    pa_assert(node);

    return node->type == PA_NODE_TYPE_SOURCE && ((pa_source *) node->owner)->monitor_of;
}

static int routing_target_compare_func(pa_node *node1, pa_node *node2) {
    pa_node *default_node;

    pa_assert(node1);
    pa_assert(node2);
    pa_assert(node1->type == PA_NODE_TYPE_SINK || node1->type == PA_NODE_TYPE_SOURCE || node1->type == PA_NODE_TYPE_PORT);
    pa_assert(node2->type == PA_NODE_TYPE_SINK || node2->type == PA_NODE_TYPE_SOURCE || node2->type == PA_NODE_TYPE_PORT);

    if (node1 == node2)
        return 0;

    if (node1->direction == PA_DIRECTION_OUTPUT)
        default_node = get_node_for_default_sink(node1->core);
    else
        default_node = get_node_for_default_source(node1->core);

    /* First check if either of the nodes is the default sink or source (or the
     * active port of the default sink or source). If a node is the default
     * sink or source, it wins always. */

    if (node1 == default_node)
        return -1;

    if (node2 == default_node)
        return 1;

    /* Monitor sources always lose to non-monitor source nodes. */

    if (is_monitor_source(node1) && !is_monitor_source(node2))
        return 1;

    if (is_monitor_source(node2) && !is_monitor_source(node1))
        return -1;

    /* Finally, let's prefer the node that is newer (bigger index). We could
     * also compare the sink/source/port priorities, but if one of the nodes is
     * a sink or source and the other is a port, the priorities won't really be
     * comparable. Comparing just the node indexes is a simple and good enough
     * for the fallback policy. */

    if (node1->index > node2->index)
        return -1;

    if (node2->index > node1->index)
        return 1;

    pa_assert_not_reached();
}

static pa_hook_result_t default_sink_changed_cb(void *hook_data, void *call_data, void *slot_data) {
    pa_fallback_routing_policy *policy = slot_data;

    pa_assert(policy);

    pa_router_group_update_target_ordering(policy->input_routing_group);

    return PA_HOOK_OK;
}

static pa_hook_result_t default_source_changed_cb(void *hook_data, void *call_data, void *slot_data) {
    pa_fallback_routing_policy *policy = slot_data;

    pa_assert(policy);

    pa_router_group_update_target_ordering(policy->output_routing_group);

    return PA_HOOK_OK;
}

pa_fallback_routing_policy *pa_fallback_routing_policy_new(pa_core *core) {
    pa_fallback_routing_policy *policy;
    pa_router_policy_implementation_data policy_data;
    int r;
    pa_router_group_new_data group_data;

    pa_assert(core);

    policy = pa_xnew0(pa_fallback_routing_policy, 1);
    policy->router = &core->router;

    pa_router_policy_implementation_data_init(&policy_data);
    policy_data.implicit_route.accept = routee_accept_func;
    policy_data.implicit_route.compare = routee_compare_func;
    policy_data.userdata = policy;

    r = pa_router_register_policy_implementation(policy->router, &policy_data);
    pa_router_policy_implementation_data_done(&policy_data);

    if (r < 0) {
        pa_log("Failed to register the fallback routing policy implementation.");
        goto fail;
    }

    policy->registered = true;

    pa_router_group_new_data_init(&group_data);
    pa_router_group_new_data_set_name(&group_data, "input");
    group_data.direction = PA_DIRECTION_INPUT;
    group_data.accept = routing_target_accept_func;
    group_data.compare = routing_target_compare_func;
    policy->input_routing_group = pa_router_group_new(core, &group_data);
    pa_router_group_new_data_done(&group_data);

    if (!policy->input_routing_group) {
        pa_log("Failed to create the input routing group.");
        goto fail;
    }

    pa_router_group_new_data_init(&group_data);
    pa_router_group_new_data_set_name(&group_data, "output");
    group_data.direction = PA_DIRECTION_OUTPUT;
    group_data.accept = routing_target_accept_func;
    group_data.compare = routing_target_compare_func;
    policy->output_routing_group = pa_router_group_new(core, &group_data);
    pa_router_group_new_data_done(&group_data);

    if (!policy->output_routing_group) {
        pa_log("Failed to create the output routing group.");
        goto fail;
    }

    policy->default_sink_changed_slot = pa_hook_connect(&core->hooks[PA_CORE_HOOK_DEFAULT_SINK_CHANGED], PA_HOOK_NORMAL,
                                                        default_sink_changed_cb, policy);
    policy->default_source_changed_slot = pa_hook_connect(&core->hooks[PA_CORE_HOOK_DEFAULT_SOURCE_CHANGED], PA_HOOK_NORMAL,
                                                          default_source_changed_cb, policy);

    pa_log_debug("Created a fallback routing policy.");

    return policy;

fail:
    pa_fallback_routing_policy_free(policy);

    return NULL;
}

void pa_fallback_routing_policy_free(pa_fallback_routing_policy *policy) {
    pa_assert(policy);

    pa_log_debug("Freeing a fallback routing policy.");

    if (policy->default_source_changed_slot)
        pa_hook_slot_free(policy->default_source_changed_slot);

    if (policy->default_sink_changed_slot)
        pa_hook_slot_free(policy->default_sink_changed_slot);

    if (policy->output_routing_group)
        pa_router_group_free(policy->output_routing_group);

    if (policy->input_routing_group)
        pa_router_group_free(policy->input_routing_group);

    if (policy->registered)
        pa_router_unregister_policy_implementation(policy->router);

    pa_xfree(policy);
}
