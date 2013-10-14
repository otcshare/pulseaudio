#ifndef foorouterhfoo
#define foorouterhfoo

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

#include <pulsecore/sequence.h>

typedef struct pa_router_policy_implementation_data pa_router_policy_implementation_data;
typedef struct pa_router pa_router;
typedef struct pa_routing_group_new_data pa_routing_group_new_data;
typedef struct pa_routing_group pa_routing_group;
typedef struct pa_routing_group_entry pa_routing_group_entry;
typedef struct pa_explicit_connection_request pa_explicit_connection_request;

/* Forward declarations for external structs. */
typedef struct pa_core pa_core;
typedef struct pa_dynarray pa_dynarray;
typedef struct pa_fallback_routing_policy pa_fallback_routing_policy;
typedef struct pa_hashmap pa_hashmap;
typedef struct pa_idxset pa_idxset;
typedef struct pa_module pa_module;
typedef struct pa_node pa_node;
typedef struct pa_pulse_domain pa_pulse_domain;
typedef struct pa_routing_plan pa_routing_plan;
typedef struct pa_sequence_head pa_sequence_head;

typedef pa_routing_group *(*pa_router_implicit_accept_t)(pa_router *router, pa_node *node);
typedef bool (*pa_routing_group_accept_t)(pa_routing_group *routing_group, pa_node *node);
typedef int (*pa_router_compare_t)(pa_node *node1, pa_node *node2);

typedef enum {
    PA_ROUTER_STATE_READY,
    PA_ROUTER_STATE_BUSY
} pa_router_state_t;

struct pa_router_policy_implementation_data {
    pa_module *module;
    struct {
        pa_router_compare_t compare;
        pa_router_implicit_accept_t accept;
    } implicit_route;
    void *userdata;
};

struct pa_router {
    pa_core *core;
    pa_router_state_t state;
    pa_module *module;
    pa_idxset *domains;
    pa_pulse_domain *pulse_domain;
    struct {
        pa_sequence_head node_list;
        pa_router_compare_t compare;
        pa_router_implicit_accept_t accept;
        pa_idxset *groups;
    } implicit_route;
    pa_routing_plan *routing_plan; /* Valid only during pa_router_make_routing(). */
    pa_hashmap *connections;
    unsigned next_explicit_connection_request_serial;
    pa_sequence_head explicit_connection_requests;
    pa_dynarray *nodes_waiting_for_unlinking;
    pa_fallback_routing_policy *fallback_policy;
    void *userdata;
};

struct pa_routing_group_new_data {
    char *name;
    pa_direction_t direction;
    pa_routing_group_accept_t accept;
    pa_router_compare_t compare;
};

struct pa_routing_group {
    pa_core *core;

    char *name;
    uint32_t index;
    pa_direction_t direction;

    pa_routing_group_accept_t accept;
    pa_router_compare_t compare;

    void *userdata;             /* we need this for scripting */

    pa_sequence_head entries;
};

struct pa_routing_group_entry {
    pa_sequence_list group_list;
    pa_sequence_list node_list;
    pa_routing_group *group;
    pa_node *node;
    bool blocked;
    uint32_t routing_plan_id;
};


void pa_router_init(pa_router *router, pa_core *core);
void pa_router_done(pa_router *router);

void pa_router_policy_implementation_data_init(pa_router_policy_implementation_data *data);
void pa_router_policy_implementation_data_done(pa_router_policy_implementation_data *data);
int pa_router_register_policy_implementation(pa_router *router, pa_router_policy_implementation_data *data);
void pa_router_unregister_policy_implementation(pa_router *router);

pa_routing_group_new_data *pa_routing_group_new_data_init(pa_routing_group_new_data *data);
void pa_routing_group_new_data_set_name(pa_routing_group_new_data *data, const char *name);
void pa_routing_group_new_data_done(pa_routing_group_new_data *data);

pa_routing_group *pa_routing_group_new(pa_core *core, pa_routing_group_new_data *data);
void pa_routing_group_free(pa_routing_group *node);

void pa_routing_group_update_target_ordering(pa_routing_group *group);

void pa_routing_group_entry_free(pa_routing_group_entry *entry);

void pa_router_register_node(pa_router *router, pa_node *node);
void pa_router_unregister_node(pa_router *router, pa_node *node);

void pa_router_make_routing(pa_router *router);

#endif
