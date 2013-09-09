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

#include <pulsecore/namereg.h>

#include "domain.h"


pa_domain_new_data *pa_domain_new_data_init(pa_domain_new_data *data) {
    pa_assert(data);

    pa_zero(*data);

    return data;
}

static unsigned routing_plan_hash_func(const void *p) {
    pa_assert(p);
    return (unsigned)(*(uint32_t *)p);
}

static int routing_plan_compare_func(const void *a, const void *b) {
    uint32_t aid, bid;
    pa_assert(a);
    pa_assert(b);

    aid = *(uint32_t *)a;
    bid = *(uint32_t *)b;

    if (aid > bid)
        return 1;

    if (aid < bid)
        return -1;

    return 0;
}

pa_domain *pa_domain_new(pa_core *core, pa_domain_new_data *data) {
    pa_domain *dom;
    pa_router *router;

    pa_assert(core);
    pa_assert(data);
    pa_assert(data->name);

    router = &core->router;

    dom = pa_xnew0(pa_domain, 1);
    dom->core = core;
    dom->name = pa_xstrdup(data->name);

    if (!pa_namereg_register(core, dom->name, PA_NAMEREG_DOMAIN, dom, true)) {
        pa_log("failed to register domain name '%s'", dom->name);
        pa_xfree(dom->name);
        pa_xfree(dom);
        return NULL;
    }

    dom->routing_plans = pa_hashmap_new(routing_plan_hash_func, routing_plan_compare_func);

    pa_assert_se(pa_idxset_put(router->domains, dom, &dom->index) == 0);

    pa_log_debug("registered '%s' router domain", dom->name);

    return dom;
}

void pa_domain_free(pa_domain *dom) {
    pa_core *core;
    pa_router *router;

    pa_assert(dom);

    pa_assert_se((core = dom->core));
    router = &core->router;

    pa_assert_se((void *)dom == pa_idxset_remove_by_index(router->domains, dom->index));

    pa_assert(dom->name);
    pa_namereg_unregister(core, dom->name);

    pa_assert(dom->routing_plans);
    pa_assert(pa_hashmap_isempty(dom->routing_plans));
    pa_hashmap_free(dom->routing_plans, NULL);

    pa_xfree(dom->name);

    pa_xfree(dom);
}


void pa_domain_list_init(pa_domain_list *list) {
    pa_assert(list);

    list = 0;
};

void pa_domain_list_free(pa_domain_list *list) {
    pa_domain_list_init(list);
};

int pa_domain_list_add(pa_domain_list *list, pa_domain *dom) {
    pa_assert(list);
    pa_assert(dom);

    if (dom->index > sizeof(pa_domain_list) * 8) {
        pa_log("can't add domain '%s' to list: domain index too big", dom->name);
        return -1;
    }

    *list |= ((pa_domain_list)1 << dom->index);

    return 0;
}

void pa_domain_list_copy(pa_domain_list *to, pa_domain_list *from) {
    pa_assert(to);
    pa_assert(from);

    *to = *from;
}

bool pa_domain_list_is_empty(pa_domain_list *list) {
    pa_assert(list);

    return *list ? false : true;
}

bool pa_domain_list_includes(pa_domain_list *list, pa_domain *dom) {
    pa_assert(list);
    pa_assert(dom);

    return (*list & ((pa_domain_list)1 << dom->index)) ? true : false;
}

bool pa_domain_list_is_valid(pa_core *core, pa_domain_list *list) {
    pa_router *router;
    pa_domain_list bits;
    int domidx;

    pa_assert(core);
    pa_assert(list);

    router = &core->router;

    for (domidx = 0, bits = *list;   bits;   domidx++, bits >>= 1) {
        if ((bits & ((pa_domain_list)1 << domidx)) && !pa_idxset_get_by_index(router->domains, domidx))
            return false;
    }

    return true;
}

pa_domain *pa_domain_list_common(pa_core *core, pa_domain_list *list1, pa_domain_list *list2) {
    pa_router *router;
    pa_domain_list common;
    uint32_t index;

    pa_assert(core);
    pa_assert(list1);
    pa_assert(list2);

    router = &core->router;
    common = (*list1) & (*list2);

    /* this result that the eralier registered domain has higher priority,
       which makes pulse_domain the highest priority of all */
    for (index = 0;   common;   index++, common >>= 1) {
        if ((common & 1))
            return pa_idxset_get_by_index(router->domains, index);
    }

    return NULL;
}

pa_domain_routing_plan *pa_domain_create_routing_plan(pa_domain *dom, uint32_t routing_plan_id) {
    pa_domain_routing_plan *plan;
    pa_assert(dom);

    if (dom->create_new_routing_plan)
        plan = dom->create_new_routing_plan(dom, routing_plan_id);
    else
        plan = pa_domain_routing_plan_new(dom, routing_plan_id, 0);

    return plan;
}

void pa_domain_delete_routing_plan(pa_domain *dom, uint32_t routing_plan_id) {
    pa_domain_routing_plan *plan;

    pa_assert(dom);
    pa_assert(dom->routing_plans);
    pa_assert_se((plan = pa_hashmap_get(dom->routing_plans, &routing_plan_id)));

    if (dom->delete_routing_plan) {
        pa_assert(dom->create_new_routing_plan);
        dom->delete_routing_plan(plan);
    }
    else {
        pa_assert(!dom->create_new_routing_plan);
        pa_domain_routing_plan_done(plan);
    }
}


pa_domain_routing_plan *pa_domain_routing_plan_new(pa_domain *dom, uint32_t routing_plan_id, size_t extra) {
    pa_domain_routing_plan *plan;

    pa_assert(dom);
    pa_assert(dom->routing_plans);

    plan = pa_xmalloc0(PA_ALIGN(sizeof(pa_domain_routing_plan)) + extra);
    plan->domain = dom;
    plan->id = routing_plan_id;

    if (pa_hashmap_put(dom->routing_plans, &plan->id, plan)) {
        pa_log("attempt for multiple creation of routing plan %u in domain '%s'", routing_plan_id, dom->name);
        pa_xfree(plan);
        return NULL;
    }

    return plan;
}

void pa_domain_routing_plan_done(pa_domain_routing_plan *plan) {
    pa_domain *dom;

    pa_assert(plan);
    pa_assert_se((dom = plan->domain));

    pa_assert_se(plan == pa_hashmap_remove(dom->routing_plans, &plan->id));

    pa_xfree(plan);
}

pa_domain_routing_plan *pa_domain_get_routing_plan(pa_domain *dom, uint32_t id) {
    pa_domain_routing_plan *plan;

    pa_assert(dom);
    pa_assert(dom->routing_plans);

    if ((plan = pa_hashmap_last(dom->routing_plans))) {
        if (plan->id == id)
            return plan;

        if ((plan = pa_hashmap_get(dom->routing_plans, &id)))
            return plan;
    }

    return NULL;
}

void *pa_domain_create_new_connection(pa_domain_routing_plan *plan, pa_node *input, pa_node *output) {
    pa_domain *domain;

    pa_assert(plan);
    pa_assert(input);
    pa_assert(output);
    pa_assert_se((domain = plan->domain));

    if (domain->create_new_connection)
        return domain->create_new_connection(plan, input, output);

    return NULL;
}

void pa_domain_update_existing_connection(pa_domain_routing_plan *plan, void *connection) {
    pa_domain *domain;

    pa_assert(plan);
    pa_assert_se((domain = plan->domain));

    if (connection && domain->update_existing_connection)
        domain->update_existing_connection(plan, connection);
}

void pa_domain_implement_connection(pa_domain_routing_plan *plan, void *connection) {
    pa_domain *domain;

    pa_assert(plan);
    pa_assert_se((domain = plan->domain));

    if (connection && domain->implement_connection)
        domain->implement_connection(plan, connection);
}

void pa_domain_delete_connection(pa_domain_routing_plan *plan, void *connection) {
    pa_domain *domain;

    pa_assert(plan);
    pa_assert_se((domain = plan->domain));

    if (connection && domain->delete_connection)
        domain->delete_connection(plan, connection);
}
