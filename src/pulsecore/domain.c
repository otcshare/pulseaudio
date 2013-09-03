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

    pa_assert_se(pa_idxset_put(router->domains, dom, &dom->index) == 0);

    pa_log_debug("registered '%s' router domain", dom->name);

    return dom;
}

void pa_domain_free(pa_domain *dom) {
    pa_assert(dom);
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

void pa_domain_routing_start(pa_domain *dom) {
    pa_assert(dom);
}

void pa_domain_routing_end(pa_domain *dom) {
    pa_assert(dom);
}
