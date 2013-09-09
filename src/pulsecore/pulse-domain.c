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

#include <pulsecore/core.h>

#include "pulse-domain.h"

static pa_domain_routing_plan *create_new_routing_plan(pa_domain *domain, uint32_t routing_plan_id) {
    pa_core *core;
    pa_router *router;
    pa_domain_routing_plan *plan;

    pa_assert_se(domain);
    pa_assert_se((core = domain->core));

    router = &core->router;

    pa_assert(domain == router->pulse_domain);

    pa_log_debug("creating routing plan %u in pulse domain", routing_plan_id);

    plan = pa_domain_routing_plan_new(domain, routing_plan_id, 0);

    return plan;
}

static void delete_routing_plan(pa_domain_routing_plan *plan) {
    pa_domain *domain;
    pa_core *core;
    pa_router *router;

    pa_assert(plan);
    pa_assert_se((domain = plan->domain));
    pa_assert_se((core = domain->core));

    router = &core->router;

    pa_assert(domain == router->pulse_domain);

    pa_log_debug("deleting routing plan %u in pulse domain", plan->id);

    pa_domain_routing_plan_done(plan);
}

static void *create_new_connection(pa_domain_routing_plan *plan, pa_node *input, pa_node *output) {
    static char *foo;

    pa_domain *domain;
    pa_core *core;
    pa_router *router;

    pa_assert(plan);
    pa_assert_se((domain = plan->domain));
    pa_assert_se((core = domain->core));

    router = &core->router;

    pa_assert(domain == router->pulse_domain);

    foo++;

    pa_log_debug("create new connection %p in pulse domain for routing plan %u", foo, plan->id);

    return foo;
}

static void update_existing_connection(pa_domain_routing_plan *plan, void *pulse_conn) {
    pa_domain *domain;
    pa_core *core;
    pa_router *router;

    pa_assert(plan);
    pa_assert_se((domain = plan->domain));
    pa_assert_se((core = domain->core));

    router = &core->router;

    pa_assert(domain == router->pulse_domain);

    pa_log_debug("update existing connection %p in pulse domain for routing plan %u", pulse_conn, plan->id);
}

static void implement_connection(pa_domain_routing_plan *plan, void *pulse_conn) {
    pa_domain *domain;
    pa_core *core;
    pa_router *router;

    pa_assert(plan);
    pa_assert_se((domain = plan->domain));
    pa_assert_se((core = domain->core));

    router = &core->router;

    pa_assert(domain == router->pulse_domain);

    pa_log_debug("implement connection %p in pulse domain for routing plan %u", pulse_conn, plan->id);
}

static void delete_connection(pa_domain_routing_plan *plan, void *pulse_conn) {
    pa_domain *domain;
    pa_core *core;
    pa_router *router;

    pa_assert(plan);
    pa_assert_se((domain = plan->domain));
    pa_assert_se((core = domain->core));

    router = &core->router;

    pa_assert(domain == router->pulse_domain);

    pa_log_debug("delete connection %p in pulse domain for routing plan %u", pulse_conn, plan->id);
}

pa_domain *pa_pulse_domain_new(pa_core *core) {
    pa_domain_new_data data;
    pa_domain *dom;

    pa_assert(core);

    pa_domain_new_data_init(&data);
    data.name = PA_PULSE_DOMAIN_NAME;

    dom = pa_domain_new(core, &data);

    dom->create_new_routing_plan = create_new_routing_plan;
    dom->delete_routing_plan = delete_routing_plan;
    dom->create_new_connection = create_new_connection;
    dom->update_existing_connection = update_existing_connection;
    dom->implement_connection = implement_connection;
    dom->delete_connection = delete_connection;

    return dom;
}

void pa_pulse_domain_free(pa_domain *dom) {
    pa_assert(dom);
}
