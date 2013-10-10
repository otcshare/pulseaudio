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

typedef struct pa_node_features pa_node_features;
typedef struct pa_node_new_data pa_node_new_data;
typedef struct pa_node pa_node;
typedef struct pa_node_implicit_route_data pa_node_implicit_route_data;

#include <pulsecore/device-class.h>
#include <pulsecore/dynarray.h>
#include <pulsecore/sequence.h>

#include "router.h"

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

/* The node type determines what the owner pointer of pa_node points to. */
typedef enum {
    PA_NODE_TYPE_PORT,          /* owner: pa_port */
    PA_NODE_TYPE_SINK,          /* owner: pa_sink */
    PA_NODE_TYPE_SOURCE,        /* owner: pa_source */
    PA_NODE_TYPE_SINK_INPUT,    /* owner: pa_sink_input or pa_sink_input_new_data depending on state*/
    PA_NODE_TYPE_SOURCE_OUTPUT, /* owner: pa_source_output or pa_source_output_new_data depending on state */
    PA_NODE_TYPE_NONPULSE       /* owner: unknown */
} pa_node_type_t;

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

    pa_node_type_t type;
    pa_direction_t direction;
    pa_domain_list domains;
    pa_device_class_t device_class; /* For nodes representing physical devices. */
    pa_node **explicit_connections;
    unsigned n_explicit_connections;
    bool implicit_routing_enabled;
};


struct pa_node_implicit_route_data {
    pa_sequence_list list;
    pa_sequence_head member_of;
    pa_router_group *group;
};


struct pa_node {
    pa_core *core;

    uint32_t index;
    char *name;
    char *description;

    pa_node_type_t type;
    pa_direction_t direction;
    pa_domain_list domains;

    pa_node_state_t state;

    void *owner;

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
    bool (*reserve_path_to_node)(pa_node *node, pa_domain_routing_plan *routing_plan, pa_node_features *features);
    bool (*activate_path_to_node)(pa_node *node, pa_domain_routing_plan *routing_plan);

    pa_node_implicit_route_data implicit_route;
};


pa_node_new_data *pa_node_new_data_init(pa_node_new_data *data);
void pa_node_new_data_set_fallback_name_prefix(pa_node_new_data *data, const char *prefix);
void pa_node_new_data_set_description(pa_node_new_data *data, const char *description);
void pa_node_new_data_set_type(pa_node_new_data *data, pa_node_type_t type);
void pa_node_new_data_set_direction(pa_node_new_data *data, pa_direction_t direction);
void pa_node_new_data_add_domain(pa_node_new_data *data, pa_domain *domain);
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
void *pa_node_get_owner(pa_node *node, pa_domain *domain);

/* Returns NULL if there are no common domains. */
pa_domain *pa_node_get_common_domain(pa_node *node1, pa_node *node2);

/* Returns an array of nodes. The returned array is internal memory of
 * node->connected_nodes, so the caller must not free the returned array. Also,
 * the returned array will likely become invalid at any time the node
 * connections change, so be careful and don't save the array anywhere (or if
 * you need to save it, copy it before saving). */
pa_node * const *pa_node_get_connected_nodes(pa_node *node, unsigned *n);

bool pa_node_available(pa_node *node, pa_domain *domain);
pa_node_features *pa_node_get_features(pa_node *node, pa_domain *domain, pa_node_features *buf);
bool pa_node_reserve_path_to_node(pa_node *node, pa_domain_routing_plan *routing_plan, pa_node_features *features);
bool pa_node_activate_path_to_node(pa_node *node, pa_domain_routing_plan *routing_plan);
bool pa_node_common_features(pa_node_features *f1, pa_node_features *f2, pa_node_features *common);

/* To be called only from router.c. */
void pa_node_add_explicit_connection_request(pa_node *node, pa_explicit_connection_request *request);
int pa_node_remove_explicit_connection_request(pa_node *node, pa_explicit_connection_request *request);

#endif
