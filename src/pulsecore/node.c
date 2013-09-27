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

#include <pulsecore/core-util.h>
#include <pulsecore/namereg.h>
#include <pulsecore/strbuf.h>

#include "node.h"

pa_node_new_data *pa_node_new_data_init(pa_node_new_data *data) {
    pa_assert(data);

    pa_zero(*data);
    pa_domain_list_init(&data->domains);
    data->direction = PA_DIRECTION_OUTPUT;
    data->device_class = PA_DEVICE_CLASS_UNKNOWN;
    data->implicit_routing_enabled = true;

    return data;
}

void pa_node_new_data_set_fallback_name_prefix(pa_node_new_data *data, const char* prefix) {
    pa_assert(data);

    pa_xfree(data->fallback_name_prefix);
    data->fallback_name_prefix = pa_xstrdup(prefix);
}

void pa_node_new_data_set_description(pa_node_new_data *data, const char *description) {
    pa_assert(data);

    pa_xfree(data->description);
    data->description = pa_xstrdup(description);
}

void pa_node_new_data_set_type(pa_node_new_data *data, pa_node_type_t type) {
    pa_assert(data);

    data->type = type;
}

void pa_node_new_data_set_direction(pa_node_new_data *data, pa_direction_t direction) {
    pa_assert(data);

    data->direction = direction;
}

void pa_node_new_data_add_domain(pa_node_new_data *data, pa_domain *domain) {
    pa_assert(data);
    pa_assert(domain);

    pa_assert_se(pa_domain_list_add(&data->domains, domain) == 0);
}

void pa_node_new_data_set_device_class(pa_node_new_data *data, pa_device_class_t class) {
    pa_assert(data);

    data->device_class = class;
}

void pa_node_new_data_set_explicit_connections(pa_node_new_data *data, pa_node * const *nodes, unsigned n_nodes) {
    pa_assert(data);
    pa_assert(nodes || n_nodes == 0);

    pa_xfree(data->explicit_connections);
    data->explicit_connections = pa_xmemdup(nodes, n_nodes * sizeof(*nodes));
    data->n_explicit_connections = n_nodes;
}

void pa_node_new_data_set_implicit_routing_enabled(pa_node_new_data *data, bool enabled) {
    pa_assert(data);

    data->implicit_routing_enabled = enabled;
}

void pa_node_new_data_set_shared_routing_node(pa_node_new_data *data, pa_node *node) {
    pa_assert(data);
    pa_assert(node);

    pa_log("Unimplemented: pa_node_new_data_set_shared_routing_node()");
}

void pa_node_new_data_done(pa_node_new_data *data) {
    pa_assert(data);

    pa_xfree(data->explicit_connections);
    pa_domain_list_free(&data->domains);
    pa_xfree(data->description);
    pa_xfree(data->fallback_name_prefix);
}

pa_node *pa_node_new(pa_core *core, pa_node_new_data *data) {
    bool use_fallback_name_prefix;
    pa_strbuf *name_buf;
    char *name = NULL;
    pa_node *n = NULL;
    const char *registered_name = NULL;

    pa_assert(core);
    pa_assert(data);
    pa_assert(data->description);
    pa_assert(data->direction == PA_DIRECTION_INPUT || data->direction == PA_DIRECTION_OUTPUT);
    pa_assert(!pa_domain_list_is_empty(&data->domains));
    pa_assert(pa_domain_list_is_valid(core, &data->domains));

    use_fallback_name_prefix = !!data->fallback_name_prefix;

    name_buf = pa_strbuf_new();

    if (data->device_class != PA_DEVICE_CLASS_UNKNOWN) {
        pa_strbuf_printf(name_buf, "%s-", pa_device_class_to_string(data->device_class));
        use_fallback_name_prefix = false;
    }

    if (use_fallback_name_prefix)
        pa_strbuf_printf(name_buf, "%s-", data->fallback_name_prefix);

    pa_strbuf_puts(name_buf, data->direction == PA_DIRECTION_OUTPUT ? "output" : "input");

    name = pa_strbuf_tostring_free(name_buf);

    n = pa_xnew0(pa_node, 1);
    n->core = core;
    pa_assert_se(pa_idxset_put(core->nodes, n, &n->index) >= 0);
    n->state = PA_NODE_STATE_INIT;

    if (!(registered_name = pa_namereg_register(core, name, PA_NAMEREG_NODE, n, false))) {
        pa_log("Failed to register name %s.", name);
        goto fail;
    }

    pa_xfree(name);

    n->name = pa_xstrdup(registered_name);
    n->description = pa_xstrdup(data->description);
    n->type = data->type;
    n->direction = data->direction;
    pa_domain_list_copy(&n->domains, &data->domains);
    n->connections = pa_dynarray_new(NULL);
    n->connected_nodes = pa_dynarray_new(NULL);
    n->requested_explicit_connections = pa_xmemdup(data->explicit_connections,
                                                   data->n_explicit_connections * sizeof(pa_node *));
    n->n_requested_explicit_connections = data->n_explicit_connections;
    n->explicit_connection_requests = pa_dynarray_new(NULL);

    PA_SEQUENCE_LIST_INIT(n->implicit_route.list);
    PA_SEQUENCE_HEAD_INIT(n->implicit_route.member_of, NULL);
    n->implicit_route.group = NULL;

    return n;

fail:
    pa_xfree(name);
    pa_node_free(n);

    return NULL;
}

void pa_node_free(pa_node *node) {
    pa_router_group_entry *entry, *next;
    pa_assert(node);

    if (node->state == PA_NODE_STATE_LINKED)
        pa_node_unlink(node);

    PA_SEQUENCE_REMOVE(node->implicit_route.list);

    PA_SEQUENCE_FOREACH_SAFE(entry, next, node->implicit_route.member_of, pa_router_group_entry, node_list)
        pa_router_group_entry_free(entry);

    if (node->explicit_connection_requests)
        pa_dynarray_free(node->explicit_connection_requests);

    pa_xfree(node->requested_explicit_connections);

    if (node->connected_nodes)
        pa_dynarray_free(node->connected_nodes);

    if (node->connections)
        pa_dynarray_free(node->connections);

    pa_xfree(node->description);

    if (node->name) {
        pa_namereg_unregister(node->core, node->name);
        pa_xfree(node->name);
    }

    pa_assert_se(pa_idxset_remove_by_index(node->core->nodes, node->index));
    pa_xfree(node);
}

int pa_node_put(pa_node *node) {
    pa_assert(node);
    pa_assert(node->state == PA_NODE_STATE_INIT);
    pa_assert(node->owner);

    node->state = PA_NODE_STATE_LINKED;

    pa_router_register_node(&node->core->router, node);
    pa_router_make_routing(&node->core->router);

    if (node->state == PA_NODE_STATE_UNLINKED) {
        pa_log("Failed to route node %s.", node->name);
        return -1;
    }

    pa_log_debug("Created node %s.", node->name);

    return 0;
}

void pa_node_unlink(pa_node *node) {
    pa_core *core;

    pa_assert(node);
    pa_assert_se((core = node->core));
    pa_assert(node->state != PA_NODE_STATE_INIT);

    if (node->state == PA_NODE_STATE_UNLINKED)
        return;

    pa_log_debug("Unlinking node %s.", node->name);

    pa_router_unregister_node(&core->router, node);

    node->state = PA_NODE_STATE_UNLINKED;

    pa_router_make_routing(&core->router);
}

const char *pa_node_get_name(pa_node *node) {
    pa_assert(node);

    return node->name;
}

void *pa_node_get_owner(pa_node *node, pa_domain *domain) {
    pa_domain *pulse_domain;

    pa_assert(node);
    pa_assert(node->core);

    pa_assert_se((pulse_domain = node->core->router.pulse_domain));

    if (!domain || domain == pulse_domain) {
        pa_assert(node->type == PA_NODE_TYPE_PORT || node->type == PA_NODE_TYPE_SINK || node->type == PA_NODE_TYPE_SOURCE ||
                  node->type == PA_NODE_TYPE_SINK_INPUT || node->type == PA_NODE_TYPE_SOURCE_OUTPUT);
        return node->owner;
    }

    pa_assert(node->type == PA_NODE_TYPE_NONPULSE);
    pa_assert(node->get_owner);

    return node->get_owner(node, domain);
}

pa_node * const *pa_node_get_connected_nodes(pa_node *node, unsigned *n) {
    pa_assert(node);
    pa_assert(n);

    *n = pa_dynarray_size(node->connected_nodes);

    return (pa_node * const *) pa_dynarray_get_array(node->connected_nodes);
}

bool pa_node_available(pa_node *node, pa_domain *domain) {
    pa_assert(node);
    pa_assert(domain);

    return node->available ? node->available(node, domain) : true;
}

pa_node_features *pa_node_get_features(pa_node *node, pa_domain *domain, pa_node_features *buf) {
    static pa_node_features default_features = {
        2, 2,                                            /* channels min & max */
        PA_NODE_LATENCY_MEDIUM, PA_NODE_LATENCY_MEDIUM,  /* latency min & max */
        16000, 48000                                     /* sample rate min & max */
    };

    pa_node_features *features;

    pa_assert(node);
    pa_assert(domain);

    if (node->get_features)
        features = node->get_features(node, domain, buf);
    else
        features = &default_features;

    pa_assert(features);

    return features;
}

bool pa_node_reserve_path_to_node(pa_node *node, pa_domain_routing_plan *plan, pa_node_features *features) {
    pa_assert(node);
    pa_assert(plan);
    pa_assert(features);

    return node->reserve_path_to_node ? node->reserve_path_to_node(node, plan, features) : true;
}

bool pa_node_activate_path_to_node(pa_node *node, pa_domain_routing_plan *plan) {
    pa_assert(node);
    pa_assert(plan);

    return node->activate_path_to_node ? node->activate_path_to_node(node, plan) : true;
}

bool pa_node_common_features(pa_node_features *f1, pa_node_features *f2, pa_node_features *common) {
    uint8_t channels_min, channels_max;
    pa_node_latency_t latency_min, latency_max;
    uint32_t rate_min, rate_max;

    pa_assert(f1);
    pa_assert(f2);

    channels_min = PA_MAX(f1->channels_min, f2->channels_min);
    channels_max = PA_MIN(f1->channels_max, f2->channels_max);

    latency_min = PA_MAX(f1->latency_min, f2->latency_min);
    latency_max = PA_MIN(f1->latency_max, f2->latency_max);

    rate_min = PA_MAX(f1->rate_min, f2->rate_min);
    rate_max = PA_MIN(f1->rate_max, f2->rate_max);

    if (channels_min > channels_max || latency_min > latency_max || rate_min > rate_max) {
        if (common)
            pa_zero(*common);
        return false;
    }

    if (common) {
        common->channels_min = channels_min;
        common->channels_max = channels_max;

        common->latency_min = latency_min;
        common->latency_max = latency_max;

        common->rate_min = rate_min;
        common->rate_max = rate_max;
    }

    return true;
}

void pa_node_add_explicit_connection_request(pa_node *node, pa_explicit_connection_request *request) {
    pa_assert(node);
    pa_assert(request);

    pa_dynarray_append(node->explicit_connection_requests, request);
}

int pa_node_remove_explicit_connection_request(pa_node *node, pa_explicit_connection_request *request) {
    pa_assert(node);
    pa_assert(request);

    return pa_dynarray_remove_by_data_fast(node->explicit_connection_requests, request);
}
