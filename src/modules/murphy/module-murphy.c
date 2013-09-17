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

#include "module-murphy-symdef.h"

struct userdata {
    bool policy_registered;
    pa_router_group **groups;
};

static bool implicit_route_accept(pa_router *router, pa_node *node) {
    pa_module *m;
    struct userdata *u;
    pa_router_group *g;
    size_t i;

    pa_assert(router);
    pa_assert(node);
    pa_assert_se((m = router->module));
    pa_assert_se((u = m->userdata));
    pa_assert(u->groups);

    if (node->type == PA_NODE_TYPE_SINK_INPUT || node->type == PA_NODE_TYPE_SOURCE_OUTPUT) {
        for (i = 0;  (g = u->groups[i]);  i++) {
            if (g->direction == node->direction) {
                node->implicit_route.group = g;
                return true;
            }
        }
    }

    return false;
}

static int implicit_route_compare(pa_node *n1, pa_node *n2) {
    pa_assert(n1);
    pa_assert(n2);
    return 1;
}


static unsigned get_node_priority(pa_node *node) {
    void *owner;
    int pri;

    pa_assert(node);
    pa_assert_se((owner = node->owner));

    switch (node->type) {
    case PA_NODE_TYPE_SINK:    pri = ((pa_sink *)owner)->priority;         break;
    case PA_NODE_TYPE_SOURCE:  pri = ((pa_source *)owner)->priority;       break;
    case PA_NODE_TYPE_PORT:    pri = ((pa_device_port *)owner)->priority;  break;
    default:                   pri = 0;                                    break;
    }

    return pri;
}

static bool default_output_accept(pa_router_group *group, pa_node *node) {
    pa_assert(group);
    pa_assert(node);

    return (node->type == PA_NODE_TYPE_PORT || node->type == PA_NODE_TYPE_SINK);
}

static bool default_input_accept(pa_router_group *group, pa_node *node) {
    pa_assert(group);
    pa_assert(node);

    return (node->type == PA_NODE_TYPE_PORT || node->type == PA_NODE_TYPE_SOURCE);
}

static int routing_group_compare(pa_node *n1, pa_node *n2) {
    unsigned p1, p2;

    pa_assert(n1);
    pa_assert(n2);

    if (n1->state != PA_NODE_STATE_LINKED || n2->state != PA_NODE_STATE_LINKED)
        return -1;

    p1 = get_node_priority(n1);
    p2 = get_node_priority(n2);

    return (p1 == p2) ? 0 : (p1 > p2) ? 1 : -1;
}

int pa__init(pa_module *m) {
    static pa_router_group_new_data groups[] = {
        { (char *)"default_output", PA_DIRECTION_OUTPUT, default_output_accept, routing_group_compare },
        { (char *)"default_input", PA_DIRECTION_INPUT , default_input_accept, routing_group_compare },
    };

    struct userdata *u;
    pa_router_policy_implementation_data data;
    int r;
    pa_router_group **g;
    size_t ngroup, i;

    pa_assert(m);

    m->userdata = u = pa_xnew0(struct userdata, 1);

    pa_router_policy_implementation_data_init(&data);
    data.module = m;
    data.implicit_route.compare = implicit_route_compare;
    data.implicit_route.accept = implicit_route_accept;

    r = pa_router_register_policy_implementation(&m->core->router, &data);
    pa_router_policy_implementation_data_done(&data);

    if (r < 0) {
        pa_log("Failed to register the policy implementation.");
        goto fail;
    }

    u->policy_registered = true;

    ngroup = PA_ELEMENTSOF(groups);
    u->groups = g = pa_xnew0(pa_router_group *, ngroup + 1);

    for (i = 0;  i < ngroup;  i++)
        g[i] = pa_router_group_new(m->core, groups + i);

    return 0;

fail:
    pa__done(m);

    return -1;
}

void pa__done(pa_module *m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->policy_registered)
        pa_router_unregister_policy_implementation(&m->core->router);

    pa_xfree(u->groups);
    pa_xfree(u);
}
