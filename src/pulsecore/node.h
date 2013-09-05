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

#include <pulsecore/core.h>
#include <pulsecore/device-class.h>
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
    PA_NODE_TYPE_SOURCE_OUTPUT  /* owner: pa_source_output or pa_source_output_new_data depending on state */
} pa_node_type_t;

typedef enum {
    PA_NODE_STATE_UNDER_CONSTRUCTION,
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

    bool (*available)(pa_node *node, pa_domain *domain);
    pa_node_features *(*get_features)(pa_node *node, pa_domain *domain, pa_node_features *buf);
    bool (*set_features)(pa_node *node, pa_domain *domain, pa_node_features *features);
    bool (*activate_features)(pa_node *node, pa_domain *domain);

    pa_node_implicit_route_data implicit_route;
};


pa_node_new_data *pa_node_new_data_init(pa_node_new_data *data);
void pa_node_new_data_set_fallback_name_prefix(pa_node_new_data *data, const char *prefix);
void pa_node_new_data_set_description(pa_node_new_data *data, const char *description);
void pa_node_new_data_set_type(pa_node_new_data *data, pa_node_type_t type);
void pa_node_new_data_set_direction(pa_node_new_data *data, pa_direction_t direction);
void pa_node_new_data_add_domain(pa_node_new_data *data, pa_domain *domain);
void pa_node_new_data_set_device_class(pa_node_new_data *data, pa_device_class_t decice_class);
void pa_node_new_data_done(pa_node_new_data *data);

pa_node *pa_node_new(pa_core *core, pa_node_new_data *data);
void pa_node_free(pa_node *node);

void pa_node_put(pa_node *node);
void pa_node_unlink(pa_node *node);

bool pa_node_available(pa_node *node, pa_domain *domain);
pa_node_features *pa_node_get_features(pa_node *node, pa_domain *domain, pa_node_features *buf);
bool pa_node_set_features(pa_node *node, pa_domain *domain, pa_node_features *features);
bool pa_node_activate_features(pa_node *node, pa_domain *domain);
bool pa_node_common_features(pa_node_features *f1, pa_node_features *f2, pa_node_features *common);

#endif
