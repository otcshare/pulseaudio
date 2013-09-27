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

#include <pulse/xmalloc.h>

#include <pulsecore/core.h>
#include <pulsecore/macro.h>

#include "routing-plan.h"

struct pa_routing_plan {
    pa_core *core;
};

pa_routing_plan *pa_routing_plan_new(pa_core *core) {
    pa_routing_plan *plan;

    pa_assert(core);

    plan = pa_xnew0(pa_routing_plan, 1);
    plan->core = core;

    return plan;
}

void pa_routing_plan_free(pa_routing_plan *plan) {
    pa_assert(plan);

    pa_xfree(plan);
}

int pa_routing_plan_allocate_explicit_connection(pa_routing_plan *plan, pa_node *input_node, pa_node *output_node,
                                                 pa_explicit_connection_request *request) {
    pa_assert(plan);
    pa_assert(input_node);
    pa_assert(output_node);
    pa_assert(request);

    /* FIXME: Not implemented. */
    pa_assert_not_reached();
}

void pa_routing_plan_deallocate_explicit_connection(pa_routing_plan *plan, pa_node *input_node, pa_node *output_node,
                                                    pa_explicit_connection_request *request) {
    pa_assert(plan);
    pa_assert(input_node);
    pa_assert(output_node);
    pa_assert(request);

    /* FIXME: Not implemented. */
    pa_assert_not_reached();
}
