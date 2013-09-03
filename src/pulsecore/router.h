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

typedef struct pa_router_module_registration_data pa_router_module_registration_data;
typedef struct pa_router pa_router;
typedef struct pa_router_group_new_data pa_router_group_new_data;
typedef struct pa_router_group pa_router_group;
typedef struct pa_router_group_entry pa_router_group_entry;


#include <pulsecore/core.h>
#include <pulsecore/sequence.h>
#include <pulsecore/module.h>

#include "domain.h"

typedef bool (*pa_router_implicit_accept_t)(pa_router *router, pa_node *node);
typedef bool (*pa_router_group_accept_t)(pa_router_group *routing_group, pa_node *node);
typedef int (*pa_router_compare_t)(pa_node *node1, pa_node *node2);


struct pa_router_module_registration_data {
    struct {
        pa_router_compare_t compare;
        pa_router_implicit_accept_t accept;
    } implicit_route;
};


struct pa_router {
    pa_module *module;
    pa_idxset *domains;
    pa_domain *pulse_domain;
    struct {
        pa_sequence_head node_list;
        pa_router_compare_t compare;
        pa_router_implicit_accept_t accept;
        pa_idxset *groups;
    } implicit_route;
    pa_hashmap *connections;
    uint32_t stamp;
};


struct pa_router_group_new_data {
    char *name;
    pa_direction_t direction;
    pa_router_group_accept_t accept;
    pa_router_compare_t compare;
};

struct pa_router_group {
    pa_core *core;

    char *name;
    uint32_t index;
    pa_direction_t direction;

    pa_router_group_accept_t accept;
    pa_router_compare_t compare;

    void *userdata;             /* we need this for scripting */

    pa_sequence_head entries;
};

struct pa_router_group_entry {
    pa_sequence_list group_list;
    pa_sequence_list node_list;
    pa_router_group *group;
    pa_node *node;
    bool blocked;
    uint32_t stamp;
};


void pa_router_init(pa_core *core);
void pa_router_free(pa_router *router);

pa_router_module_registration_data *pa_router_module_registration_data_init(pa_router_module_registration_data *data);
int pa_router_module_register(pa_module *module, pa_router_module_registration_data *data);
void pa_router_module_unregister(pa_module *module);

pa_router_group_new_data *pa_router_group_new_data_init(pa_router_group_new_data *data);
void pa_router_group_new_data_set_name(pa_router_group_new_data *data, const char *name);
void pa_router_group_new_data_done(pa_router_group_new_data *data);

pa_router_group *pa_router_group_new(pa_core *core, pa_router_group_new_data *data);
void pa_router_group_free(pa_router_group *node);

void pa_router_group_entry_free(pa_router_group_entry *entry);

void pa_router_register_node(pa_node *node);
void pa_router_unregister_node(pa_node *node);

void pa_router_make_routing(pa_core *core);

#endif
