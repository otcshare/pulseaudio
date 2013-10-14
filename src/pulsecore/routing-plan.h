#ifndef fooroutingplanhfoo
#define fooroutingplanhfoo

/***
  This file is part of PulseAudio.

  Copyright (c) 2013 Intel Corporation

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

typedef struct pa_routing_plan pa_routing_plan;
typedef struct pa_routing_plan_node_data pa_routing_plan_node_data;

/* Forward declarations for external structs. */
typedef struct pa_explicit_connection_request pa_explicit_connection_request;
typedef struct pa_node pa_node;

pa_routing_plan *pa_routing_plan_new(pa_core *core);
void pa_routing_plan_free(pa_routing_plan *plan);

void pa_routing_plan_clear(pa_routing_plan *plan, bool clear_temporary_constraints);
int pa_routing_plan_allocate_explicit_connection(pa_routing_plan *plan, pa_node *input, pa_node *output,
                                                 pa_explicit_connection_request *request);
int pa_routing_plan_allocate_implicit_connection(pa_routing_plan *plan, pa_node *input, pa_node *output);
void pa_routing_plan_deallocate_explicit_connection(pa_routing_plan *plan, pa_node *input, pa_node *output,
                                                    pa_explicit_connection_request *request);
void pa_routing_plan_deallocate_connections_of_node(pa_routing_plan *plan, pa_node *node);
int pa_routing_plan_execute(pa_routing_plan *plan);

pa_routing_plan_node_data *pa_routing_plan_node_data_new(void);
void pa_routing_plan_node_data_free(pa_routing_plan_node_data *data);

#endif
