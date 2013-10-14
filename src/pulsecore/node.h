#ifndef foonodehfoo
#define foonodehfoo

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

#include <pulsecore/device-class.h>
#include <pulsecore/domain.h>

typedef struct pa_node_features pa_node_features;
typedef struct pa_node_new_data pa_node_new_data;
typedef struct pa_node pa_node;
typedef struct pa_node_implicit_route_data pa_node_implicit_route_data;

/* Forward declarations for external structs. */
typedef struct pa_dynarray pa_dynarray;
typedef struct pa_explicit_connection_request pa_explicit_connection_request;
typedef struct pa_routing_group pa_routing_group;
typedef struct pa_routing_plan_node_data pa_routing_plan_node_data;

typedef enum {
    PA_NODE_LATENCY_INVALID = 0,
    PA_NODE_LATENCY_LOW,
    PA_NODE_LATENCY_MEDIUM,
    PA_NODE_LATENCY_HIGH
} pa_node_latency_t;

struct pa_node_features {
    uint8_t channels_min, channels_max;
    pa_node_latency_t latency_min, latency_max;
    uint32_t rate_min, rate_max; /* sample rate */
};

typedef enum {
    PA_NODE_STATE_INIT,
    PA_NODE_STATE_LINKED,
    PA_NODE_STATE_UNLINKED
} pa_node_state_t;

struct pa_node_new_data {
    /* Node names are generated automatically as much as possible, but
     * sometimes the available information for automatic generation isn't
     * sufficient, in which case the generated node names would be just "input"
     * or "output". In such cases the fallback name prefix, if set, is used to
     * generate slightly more informative names, such as "jack-output" for JACK
     * output nodes (in this example the fallback prefix would be "jack"). */
    char *fallback_name_prefix;

    char *description;

    pa_direction_t direction;
    pa_domain_list domains;
    pa_device_class_t device_class; /* For nodes representing physical devices. */
    pa_node **explicit_connections;
    unsigned n_explicit_connections;
    bool implicit_routing_enabled;
    pa_hashmap *domain_data;
};

struct pa_node_implicit_route_data {
    pa_sequence_list list;
    pa_sequence_head member_of;
    pa_routing_group *group;
};

struct pa_node {
    pa_core *core;

    uint32_t index;
    char *name;
    char *description;

    pa_direction_t direction;
    pa_domain_list domains;

    pa_node_state_t state;
    pa_dynarray *connections; /* Values are pa_connection objects. */

    /* Values are pa_node objects. This duplicates the information in
     * connections, because having the list of connected nodes directly is
     * convenient. */
    pa_dynarray *connected_nodes;

    /* requested_explicit_connections is the list of nodes that someone would
     * like this node to connect to. That list is converted by pa_router into
     * an explicit connection request object, and that object, plus any other
     * explicit connection requests that concern this node, are stored in
     * explicit_connection_requests.
     *
     * explicit_connection_requests is private data of pa_router, and others
     * shouldn't care about it. Even though it's private to pa_router, it's
     * stored in pa_node for convenience. */
    pa_node **requested_explicit_connections;
    unsigned n_requested_explicit_connections;
    pa_dynarray *explicit_connection_requests;

    void *(*get_owner)(pa_node *, pa_domain *domain);
    bool (*available)(pa_node *node, pa_domain *domain);
    pa_node_features *(*get_features)(pa_node *node, pa_domain *domain, pa_node_features *buf);

    pa_node_implicit_route_data implicit_route;
    pa_hashmap *domain_data;
    pa_routing_plan_node_data *routing_plan_data;
};

pa_node_new_data *pa_node_new_data_init(pa_node_new_data *data);
void pa_node_new_data_set_fallback_name_prefix(pa_node_new_data *data, const char *prefix);
void pa_node_new_data_set_description(pa_node_new_data *data, const char *description);
void pa_node_new_data_set_direction(pa_node_new_data *data, pa_direction_t direction);
void pa_node_new_data_add_domain(pa_node_new_data *data, pa_domain *domain, void *domain_data,
                                 pa_free_cb_t domain_data_free_cb);
void pa_node_new_data_set_device_class(pa_node_new_data *data, pa_device_class_t decice_class);
void pa_node_new_data_set_explicit_connections(pa_node_new_data *data, pa_node * const *nodes, unsigned n_nodes);
void pa_node_new_data_set_implicit_routing_enabled(pa_node_new_data *data, bool enabled);
void pa_node_new_data_set_shared_routing_node(pa_node_new_data *data, pa_node *node);
void pa_node_new_data_done(pa_node_new_data *data);

pa_node *pa_node_new(pa_core *core, pa_node_new_data *data);
void pa_node_free(pa_node *node);

/* Unlike most _put() functions, this one can fail. Routing the new node is
 * done here, and if the node requests a routing that can't be fulfilled, then
 * the node will be unlinked. */
int pa_node_put(pa_node *node);

void pa_node_unlink(pa_node *node);

const char *pa_node_get_name(pa_node *node);

/* Returns NULL if there are no common domains. */
pa_domain *pa_node_get_common_domain(pa_node *node1, pa_node *node2);

void pa_node_set_routing_group(pa_node *node, pa_routing_group *group);

/* Returns an array of nodes. The returned array is internal memory of
 * node->connected_nodes, so the caller must not free the returned array. Also,
 * the returned array will likely become invalid at any time the node
 * connections change, so be careful and don't save the array anywhere (or if
 * you need to save it, copy it before saving). */
pa_node * const *pa_node_get_connected_nodes(pa_node *node, unsigned *n);

bool pa_node_available(pa_node *node, pa_domain *domain);
pa_node_features *pa_node_get_features(pa_node *node, pa_domain *domain, pa_node_features *buf);
bool pa_node_common_features(pa_node_features *f1, pa_node_features *f2, pa_node_features *common);

void pa_node_set_domain_data(pa_node *node, pa_domain *domain, void *data);
void *pa_node_get_domain_data(pa_node *node, pa_domain *domain);

/* To be called only from router.c. */
void pa_node_add_explicit_connection_request(pa_node *node, pa_explicit_connection_request *request);
int pa_node_remove_explicit_connection_request(pa_node *node, pa_explicit_connection_request *request);

#endif
