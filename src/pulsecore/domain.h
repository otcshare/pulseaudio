#ifndef foododomainhfoo
#define foododomainhfoo

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

#include <pulsecore/core.h>

typedef struct pa_domain pa_domain;
typedef struct pa_domain_new_data pa_domain_new_data;
typedef uint32_t pa_domain_list;
typedef struct pa_domain_routing_plan pa_domain_routing_plan;

struct pa_domain_new_data {
    char *name;
};

struct pa_domain {
    pa_core *core;

    uint32_t index;
    char *name;

    pa_hashmap *routing_plans;
    uint32_t routing_plan_id;

    pa_domain_routing_plan *(*create_new_routing_plan)(pa_domain *domain, uint32_t routing_plan_id);
    void (*delete_routing_plan)(pa_domain_routing_plan *routing_plan);

    void *(*create_new_connection)(pa_domain_routing_plan *routing_plan, pa_node *input, pa_node *output);
    void (*update_existing_connection)(pa_domain_routing_plan *routing_plan, void *connection);
    void (*implement_connection)(pa_domain_routing_plan *routing_plan, void *connection);
    void (*delete_connection)(pa_domain_routing_plan *routing_plan, void *connection);

    void *userdata; /* domain implementation specific data */
};

#define PA_DOMAIN_ROUTING_PLAN_DATA(d) ((void*) ((uint8_t*)d + PA_ALIGN(sizeof(pa_domain_routing_plan))))

struct pa_domain_routing_plan {
    pa_domain *domain;
    uint32_t id;  /* routing plan id */

    /* followed by domain specific data */
};

pa_domain_new_data *pa_domain_new_data_init(pa_domain_new_data *data);
pa_domain *pa_domain_new(pa_core *core, pa_domain_new_data *data);
void pa_domain_free(pa_domain *domain);

void pa_domain_list_init(pa_domain_list *list);
void pa_domain_list_free(pa_domain_list *list);
int pa_domain_list_add(pa_domain_list *list, pa_domain *domain);
void pa_domain_list_copy(pa_domain_list *to, pa_domain_list *from);
bool pa_domain_list_is_empty(pa_domain_list *list);
bool pa_domain_list_includes(pa_domain_list *list, pa_domain *domain);
bool pa_domain_list_is_valid(pa_core *core, pa_domain_list *list);
pa_domain *pa_domain_list_common(pa_core *core, pa_domain_list *list1, pa_domain_list *list2);

pa_domain_routing_plan *pa_domain_create_routing_plan(pa_domain *domain, uint32_t routing_plan_id);
void pa_domain_delete_routing_plan(pa_domain *domain, uint32_t routing_plan_id);

pa_domain_routing_plan *pa_domain_routing_plan_new(pa_domain *domain, uint32_t routing_plan_id, size_t extra);
void pa_domain_routing_plan_done(pa_domain_routing_plan *routing_plan);
pa_domain_routing_plan *pa_domain_get_routing_plan(pa_domain *domain, uint32_t routing_plan_id);

void *pa_domain_create_new_connection(pa_domain_routing_plan *plan, pa_node *input, pa_node *output);
void pa_domain_update_existing_connection(pa_domain_routing_plan *plan, void *connection);
void pa_domain_implement_connection(pa_domain_routing_plan *plan, void *connection);
void pa_domain_delete_connection(pa_domain_routing_plan *plan, void *connection);

#endif
