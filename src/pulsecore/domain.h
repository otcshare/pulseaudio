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

#include <pulsecore/sequence.h>

typedef struct pa_domain pa_domain;
typedef struct pa_domain_new_data pa_domain_new_data;
typedef uint32_t pa_domain_list;

/* Forward declarations for external structs. */
typedef struct pa_connection pa_connection;
typedef struct pa_core pa_core;
typedef struct pa_node pa_node;
typedef struct pa_sequence_list pa_sequence_list;

struct pa_domain_new_data {
    char *name;
};

struct pa_domain {
    pa_core *core;
    uint32_t index;
    char *name;

    void (*clear_temporary_constraints)(pa_domain *domain);
    int (*allocate_connection)(pa_domain *domain, pa_node *input, pa_node *output);
    void (*deallocate_connection)(pa_domain *domain, pa_node *input, pa_node *output);
    int (*implement_connection)(pa_domain *domain, pa_node *input, pa_node *output);
    void (*delete_connection)(pa_domain *domain, pa_connection *connection);

    void *userdata; /* domain implementation specific data */
};

#define PA_DOMAIN_ROUTING_PLAN_DATA(d) ((void*) ((uint8_t*)d + PA_ALIGN(sizeof(pa_domain_routing_plan))))

void pa_domain_new_data_init(pa_domain_new_data *data);
void pa_domain_new_data_set_name(pa_domain_new_data *data, const char *name);
void pa_domain_new_data_done(pa_domain_new_data *data);

pa_domain *pa_domain_new(pa_core *core, pa_domain_new_data *data);
void pa_domain_free(pa_domain *domain);

void pa_domain_list_init(pa_domain_list *list);
void pa_domain_list_free(pa_domain_list *list);
int pa_domain_list_add(pa_domain_list *list, pa_domain *domain);
void pa_domain_list_copy(pa_domain_list *to, pa_domain_list *from);
bool pa_domain_list_is_empty(pa_domain_list *list);
bool pa_domain_list_includes(pa_domain_list *list, pa_domain *domain);
bool pa_domain_list_is_valid(pa_core *core, pa_domain_list *list);

void pa_domain_clear_temporary_constraints(pa_domain *domain);
int pa_domain_allocate_connection(pa_domain *domain, pa_node *input, pa_node *output);
void pa_domain_deallocate_connection(pa_domain *domain, pa_node *input, pa_node *output);
int pa_domain_implement_connection(pa_domain *domain, pa_node *input, pa_node *output);
void pa_domain_delete_connection(pa_domain *domain, pa_connection *connection);

#endif
