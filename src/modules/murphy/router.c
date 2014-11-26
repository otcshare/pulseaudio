/*
 * module-murphy-ivi -- PulseAudio module for providing audio routing support
 * Copyright (c) 2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St - Fifth Floor, Boston,
 * MA 02110-1301 USA.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/proplist.h>
#include <pulsecore/module.h>

#include "router.h"
#include "zone.h"
#include "node.h"
#include "switch.h"
#include "constrain.h"
#include "volume.h"
#include "fader.h"
#include "utils.h"
#include "classify.h"
#include "audiomgr.h"

static void rtgroup_destroy(struct userdata *, mir_rtgroup *);
static int rtgroup_print(mir_rtgroup *, char *, int);
static void rtgroup_update_module_property(struct userdata *, mir_direction,
                                           mir_rtgroup *);

static void add_rtentry(struct userdata *, mir_direction, mir_rtgroup *,
                        mir_node *);
static void remove_rtentry(struct userdata *, mir_rtentry *);

static void make_explicit_routes(struct userdata *, uint32_t);
static mir_node *find_default_route(struct userdata *, mir_node *, uint32_t);
static void implement_preroute(struct userdata *, mir_node *, mir_node *,
                               uint32_t);
static void implement_default_route(struct userdata *, mir_node *, mir_node *,
                                    uint32_t);

static int uint32_cmp(uint32_t, uint32_t);

static int node_priority(struct userdata *, mir_node *);

static int volume_class(mir_node *);

static int print_routing_table(pa_hashmap *, const char *, char *, int);

pa_router *pa_router_init(struct userdata *u)
{
    size_t num_classes = mir_application_class_end;
    pa_router *router = pa_xnew0(pa_router, 1);

    router->rtgroups.input  = pa_hashmap_new(pa_idxset_string_hash_func,
                                            pa_idxset_string_compare_func);
    router->rtgroups.output = pa_hashmap_new(pa_idxset_string_hash_func,
                                             pa_idxset_string_compare_func);

    router->maplen = num_classes;

    router->priormap = pa_xnew0(int, num_classes);

    MIR_DLIST_INIT(router->nodlist);
    MIR_DLIST_INIT(router->connlist);

    return router;
}

void pa_router_done(struct userdata *u)
{
    pa_router      *router;
    mir_connection *conn, *c;
    mir_node       *e,*n;
    void           *state;
    mir_rtgroup    *rtg;
    mir_rtgroup   **map;
    int             i;

    if (u && (router = u->router)) {
        MIR_DLIST_FOR_EACH_SAFE(mir_node, rtprilist, e,n, &router->nodlist) {
            MIR_DLIST_UNLINK(mir_node, rtprilist, e);
        }

        MIR_DLIST_FOR_EACH_SAFE(mir_connection,link, conn,c,&router->connlist){
            MIR_DLIST_UNLINK(mir_connection, link, conn);
            pa_xfree(conn);
        }

        PA_HASHMAP_FOREACH(rtg, router->rtgroups.input, state) {
            rtgroup_destroy(u, rtg);
        }

        PA_HASHMAP_FOREACH(rtg, router->rtgroups.output, state) {
            rtgroup_destroy(u, rtg);
        }

        pa_hashmap_free(router->rtgroups.input);
        pa_hashmap_free(router->rtgroups.output);

        for (i = 0;  i < MRP_ZONE_MAX;  i++) {
            if ((map = router->classmap.input[i]))
                pa_xfree(map);

            if ((map = router->classmap.output[i]))
                pa_xfree(map);
        }

        pa_xfree(router->priormap);
        pa_xfree(router);

        u->router = NULL;
    }
}


void mir_router_assign_class_priority(struct userdata *u,
                                      mir_node_type    class,
                                      int              pri)
{
    pa_router *router;
    int *priormap;

    pa_assert(u);
    pa_assert_se((router = u->router));
    pa_assert_se((priormap = router->priormap));

    if (class >= 0 && class < router->maplen) {
        pa_log_debug("assigning priority %d to class '%s'",
                     pri, mir_node_type_str(class));
        priormap[class] = pri;
    }
}


mir_rtgroup *mir_router_create_rtgroup(struct userdata      *u,
                                       mir_direction         type,
                                       const char           *name,
                                       mir_rtgroup_accept_t  accept,
                                       mir_rtgroup_compare_t compare)
{
    pa_router   *router;
    pa_hashmap  *table;
    mir_rtgroup *rtg;

    pa_assert(u);
    pa_assert(type == mir_input || type == mir_output);
    pa_assert(name);
    pa_assert(accept);
    pa_assert(compare);
    pa_assert_se((router = u->router));

    if (type == mir_input)
        table = router->rtgroups.input;
    else
        table = router->rtgroups.output;

    pa_assert(table);

    rtg = pa_xnew0(mir_rtgroup, 1);
    rtg->name    = pa_xstrdup(name);
    rtg->accept  = accept;
    rtg->compare = compare;
    MIR_DLIST_INIT(rtg->entries);

    if (pa_hashmap_put(table, rtg->name, rtg) < 0) {
        pa_xfree(rtg->name);
        pa_xfree(rtg);
        return NULL;
    }

    pa_log_debug("%s routing group '%s' created",
                 mir_direction_str(type), name);

    return rtg;
}

void mir_router_destroy_rtgroup(struct userdata *u,
                                mir_direction    type,
                                const char      *name)
{
    pa_router   *router;
    pa_hashmap  *table;
    mir_rtgroup *rtg;

    pa_assert(u);
    pa_assert(name);
    pa_assert_se((router = u->router));

    if (type == mir_input)
        table = router->rtgroups.input;
    else
        table = router->rtgroups.output;

    pa_assert(table);

    if (!(rtg = pa_hashmap_remove(table, name))) {
        pa_log_debug("can't destroy %s routing group '%s': group not found",
                     mir_direction_str(type), name);
    }
    else {
        rtgroup_destroy(u, rtg);
        pa_log_debug("routing group '%s' destroyed", name);
    }
}


bool mir_router_assign_class_to_rtgroup(struct userdata *u,
                                             mir_node_type    class,
                                             uint32_t         zone,
                                             mir_direction    type,
                                             const char      *rtgrpnam)
{
    pa_router *router;
    pa_hashmap *rtable;
    mir_rtgroup ***classmap;
    mir_rtgroup **zonemap;
    mir_rtgroup *rtg;
    const char *clnam;
    const char *direction;
    mir_zone *z;

    pa_assert(u);
    pa_assert(zone < MRP_ZONE_MAX);
    pa_assert(type == mir_input || type == mir_output);
    pa_assert(rtgrpnam);
    pa_assert_se((router = u->router));

    if (type == mir_input) {
        rtable   = router->rtgroups.input;
        classmap = router->classmap.input;
    }
    else {
        rtable   = router->rtgroups.output;
        classmap = router->classmap.output;
    }

    if (class < 0 || class >= router->maplen) {
        pa_log_debug("can't assign class (%d) to  routing group '%s': "
                     "class id is out of range (0 - %d)",
                     class, rtgrpnam, router->maplen);
        return false;
    }

    clnam = mir_node_type_str(class);
    direction = mir_direction_str(type);

    if (!(rtg = pa_hashmap_get(rtable, rtgrpnam))) {
        pa_log_debug("can't assign class '%s' to %s routing group '%s': "
                     "router group not found", clnam, direction, rtgrpnam);
    }

    if (!(zonemap = classmap[zone])) {
        zonemap = pa_xnew0(mir_rtgroup *, router->maplen);
        classmap[zone] = zonemap;
    }

    zonemap[class] = rtg;

    if ((z = pa_zoneset_get_zone_by_index(u, zone))) {
        pa_log_debug("class '%s'@'%s' assigned to %s routing group '%s'",
                     clnam, z->name, direction, rtgrpnam);
    }
    else {
        pa_log_debug("class '%s'@zone%u assigned to %s routing group '%s'",
                     clnam, zone, direction, rtgrpnam);
    }

    return true;
}



void mir_router_register_node(struct userdata *u, mir_node *node)
{
    pa_router   *router;
    mir_rtgroup *rtg;
    void        *state;
    int          priority;
    mir_node    *before;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((router = u->router));

    if (node->direction == mir_output) {
        if (node->implement == mir_device) {
            PA_HASHMAP_FOREACH(rtg, router->rtgroups.output, state) {
                add_rtentry(u, mir_output, rtg, node);
            }
        }
        return;
    }


    if (node->direction == mir_input) {
#if 0
        if (node->implement == mir_device) &&
            !pa_classify_loopback_stream(node))
            return;
#endif

        if (node->implement == mir_device) {
            PA_HASHMAP_FOREACH(rtg, router->rtgroups.input, state) {
                add_rtentry(u, mir_input, rtg, node);
            }

            if (!pa_classify_loopback_stream(node))
                return;
        }

        priority = node_priority(u, node);

        MIR_DLIST_FOR_EACH(mir_node, rtprilist, before, &router->nodlist) {
            if (priority < node_priority(u, before)) {
                MIR_DLIST_INSERT_BEFORE(mir_node, rtprilist, node,
                                        &before->rtprilist);
                return;
            }
        }

        MIR_DLIST_APPEND(mir_node, rtprilist, node, &router->nodlist);

        return;
    }
}

void mir_router_unregister_node(struct userdata *u, mir_node *node)
{
    pa_router *router;
    mir_rtentry *rte, *n;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((router = u->router));

    MIR_DLIST_FOR_EACH_SAFE(mir_rtentry,nodchain, rte,n, &node->rtentries) {
        remove_rtentry(u, rte);
    }

    MIR_DLIST_UNLINK(mir_node, rtprilist, node);
}

mir_connection *mir_router_add_explicit_route(struct userdata *u,
                                              uint16_t   amid,
                                              mir_node  *from,
                                              mir_node  *to)
{
    pa_router *router;
    mir_connection *conn;

    pa_assert(u);
    pa_assert(from);
    pa_assert(to);
    pa_assert_se((router = u->router));

    conn = pa_xnew0(mir_connection, 1);
    MIR_DLIST_INIT(conn->link);
    conn->amid = amid;
    conn->from = from->index;
    conn->to = to->index;

    MIR_DLIST_APPEND(mir_connection, link, conn, &router->connlist);

    mir_router_make_routing(u);

    return conn;
}

void mir_router_remove_explicit_route(struct userdata *u, mir_connection *conn)
{
    pa_core   *core;
    pa_router *router;
    mir_node  *from;
    mir_node  *to;

    pa_assert(u);
    pa_assert(conn);
    pa_assert_se((core = u->core));
    pa_assert_se((router = u->router));

    MIR_DLIST_UNLINK(mir_connection, link, conn);

    if (!(from = mir_node_find_by_index(u, conn->from)) ||
        !(to   = mir_node_find_by_index(u, conn->to))     )
    {
        pa_log_debug("can't remove explicit route: some node was not found");
    }
    else {
        pa_log_debug("tear down link '%s' => '%s'", from->amname, to->amname);

        if (!mir_switch_teardown_link(u, from, to)) {
            pa_log_debug("can't remove explicit route: "
                         "failed to teardown link");
        }
        else {
            if (!conn->blocked)
                mir_router_make_routing(u);
        }
    }

    pa_xfree(conn);
}


int mir_router_print_rtgroups(struct userdata *u, char *buf, int len)
{
    pa_router *router;
    char *p, *e;

    pa_assert(u);
    pa_assert(buf);
    pa_assert(len > 0);
    pa_assert_se((router = u->router));
    pa_assert(router->rtgroups.input);
    pa_assert(router->rtgroups.output);

    e = (p = buf) + len;

    if (p < e)
        p += print_routing_table(router->rtgroups.input, "input", p, e-p);

    if (p < e)
        p += print_routing_table(router->rtgroups.output, "output", p, e-p);

    return p - buf;
}


mir_node *mir_router_make_prerouting(struct userdata *u, mir_node *data)
{
    pa_router     *router;
    mir_node      *start;
    mir_node      *end;
    int            priority;
    bool      done;
    mir_node      *target;
    uint32_t       stamp;

    pa_assert(u);
    pa_assert_se((router = u->router));
    pa_assert_se((data->implement == mir_stream));

    priority = node_priority(u, data);
    done = false;
    target = NULL;
    stamp = pa_utils_new_stamp();

    make_explicit_routes(u, stamp);

    pa_audiomgr_delete_default_routes(u);

    MIR_DLIST_FOR_EACH_BACKWARD(mir_node, rtprilist, start, &router->nodlist) {
        if (start->implement == mir_device) {
#if 0
            if (start->direction == mir_output)
                continue;       /* we should never get here */
            if (!start->mux && !start->loop)
                continue;       /* skip not looped back input nodes */
#endif
            if (!start->loop)
                continue;       /* only looped back devices routed here */
        }

        if (priority >= node_priority(u, start)) {
            if ((target = find_default_route(u, data, stamp)))
                implement_preroute(u, data, target, stamp);
            done = true;
        }

        if (start->stamp >= stamp)
            continue;

        if ((end = find_default_route(u, start, stamp)))
            implement_default_route(u, start, end, stamp);
    }

    if (!done && (target = find_default_route(u, data, stamp)))
        implement_preroute(u, data, target, stamp);

    return target;
}


void mir_router_make_routing(struct userdata *u)
{
    static bool ongoing_routing;

    pa_router  *router;
    mir_node   *start;
    mir_node   *end;
    uint32_t    stamp;

    pa_assert(u);
    pa_assert_se((router = u->router));

    if (ongoing_routing)
        return;

    ongoing_routing = true;
    stamp = pa_utils_new_stamp();

    make_explicit_routes(u, stamp);

    pa_audiomgr_delete_default_routes(u);

    MIR_DLIST_FOR_EACH_BACKWARD(mir_node,rtprilist, start, &router->nodlist) {
        if (start->implement == mir_device) {
#if 0
            if (start->direction == mir_output)
                continue;       /* we should never get here */
            if (!start->mux && !start->loop)
                continue;       /* skip not looped back input nodes */
#endif
            if (!start->loop)
                continue;       /* only looped back devices routed here */
        }

        if (start->stamp >= stamp)
            continue;

        if ((end = find_default_route(u, start, stamp)))
            implement_default_route(u, start, end, stamp);
    }

    pa_audiomgr_send_default_routes(u);

    pa_fader_apply_volume_limits(u, stamp);

    ongoing_routing = false;
}



bool mir_router_default_accept(struct userdata *u, mir_rtgroup *rtg,
                                    mir_node *node)
{
    pa_core *core;
    pa_sink *sink;
    pa_source *source;
    pa_proplist *pl;
    mir_node_type class;
    bool accept;
    const char *role, *excluded_role;

    pa_assert(u);
    pa_assert(rtg);
    pa_assert(node);

    class = node->type;

    if (class == mir_bluetooth_carkit)
        accept = false;
    else if (class == mir_jack || class == mir_hdmi) {
        pa_assert_se((core = u->core));

        if (node->direction == mir_input) {
            source = pa_idxset_get_by_index(core->sources,node->paidx);
            pl = source ? source->proplist : NULL;
            excluded_role = "hfp_uplink";
        }
        else {
            sink = pa_idxset_get_by_index(core->sinks, node->paidx);
            pl = sink ? sink->proplist : NULL;
            excluded_role = "hfp_downlink";
        }
        role = pl ? pa_proplist_gets(pl, PA_PROP_NODE_ROLE) : NULL;
        accept = role ? strcmp(role, excluded_role) : true;
    }
    else {
        accept = (class >= mir_device_class_begin &&
                  class < mir_device_class_end);
    }

    return accept;
}


bool mir_router_phone_accept(struct userdata *u, mir_rtgroup *rtg,
                                  mir_node *node)
{
    mir_node_type class;

    pa_assert(u);
    pa_assert(rtg);
    pa_assert(node);

    class = node->type;

    if (class >= mir_device_class_begin &&  class < mir_device_class_end) {
        if (class != mir_bluetooth_a2dp   &&
            class != mir_spdif            &&
            class != mir_jack             &&
            class != mir_bluetooth_source &&
            class != mir_bluetooth_sink   &&
            class != mir_bluetooth_carkit   )
        {
            return true;
        }
    }

    return false;
}


int mir_router_default_compare(struct userdata *u, mir_rtgroup *rtg,
                               mir_node *n1, mir_node *n2)
{
    uint32_t p1, p2;

    (void)u;
    (void)rtg;

    pa_assert(n1);
    pa_assert(n2);

    if (n1->type == mir_null)
        return -1;
    if (n2->type == mir_null)
        return 1;

    p1 = ((((n1->channels & 31) << 5) + n1->privacy) << 2) + n1->location;
    p2 = ((((n2->channels & 31) << 5) + n2->privacy) << 2) + n2->location;

    p1 = (p1 << 8) + ((n1->type - mir_device_class_begin) & 0xff);
    p2 = (p2 << 8) + ((n2->type - mir_device_class_begin) & 0xff);

    return uint32_cmp(p1,p2);
}


int mir_router_phone_compare(struct userdata *u, mir_rtgroup *rtg,
                             mir_node *n1, mir_node *n2)
{
    uint32_t p1, p2;

    (void)u;
    (void)rtg;

    pa_assert(n1);
    pa_assert(n2);

    if (n1->type == mir_null)
        return -1;
    if (n2->type == mir_null)
        return 1;

    p1 = (n1->privacy << 8) + ((n1->type - mir_device_class_begin) & 0xff);
    p2 = (n2->privacy << 8) + ((n2->type - mir_device_class_begin) & 0xff);

    return uint32_cmp(p1,p2);
}


static void rtgroup_destroy(struct userdata *u, mir_rtgroup *rtg)
{
    mir_rtentry *rte, *n;

    pa_assert(u);
    pa_assert(rtg);

    MIR_DLIST_FOR_EACH_SAFE(mir_rtentry, link, rte,n, &rtg->entries) {
        remove_rtentry(u, rte);
    }

    pa_xfree(rtg->name);
    pa_xfree(rtg);
}

static int rtgroup_print(mir_rtgroup *rtg, char *buf, int len)
{
    mir_rtentry *rte;
    mir_node *node;
    char *p, *e;

    e = (p = buf) + len;

    *buf = 0;

    MIR_DLIST_FOR_EACH_BACKWARD(mir_rtentry, link, rte, &rtg->entries) {
        node = rte->node;
        if (p >= e)
            break;
        p += snprintf(p, e-p, " '%s'", node->amname);
    }

    return p - buf;
}

static void rtgroup_update_module_property(struct userdata *u,
                                           mir_direction    type,
                                           mir_rtgroup     *rtg)
{
    pa_module *module;
    char       key[64];
    char       value[512];
    int        ret;

    pa_assert(u);
    pa_assert(rtg);
    pa_assert(rtg->name);
    pa_assert_se((module = u->module));

    snprintf(key, sizeof(key), PA_PROP_ROUTING_TABLE ".%s.%s",
             mir_direction_str(type), rtg->name);
    ret = rtgroup_print(rtg, value, sizeof(value));

    if (!ret)
        value[1] = 0;

    pa_proplist_sets(module->proplist, key, value+1); /* skip ' '@beginning */
}

static void add_rtentry(struct userdata *u,
                        mir_direction    type,
                        mir_rtgroup     *rtg,
                        mir_node        *node)
{
    pa_router *router;
    mir_rtentry *rte, *before;

    pa_assert(u);
    pa_assert(rtg);
    pa_assert(node);
    pa_assert_se((router = u->router));

    if (!rtg->accept(u, rtg, node)) {
        pa_log_debug("refuse node '%s' registration to routing group '%s'",
                     node->amname, rtg->name);
        return;
    }

    rte = pa_xnew0(mir_rtentry, 1);

    MIR_DLIST_APPEND(mir_rtentry, nodchain, rte, &node->rtentries);
    rte->group = rtg;
    rte->node  = node;

    MIR_DLIST_FOR_EACH(mir_rtentry, link, before, &rtg->entries) {
        if (rtg->compare(u, rtg, node, before->node) < 0) {
            MIR_DLIST_INSERT_BEFORE(mir_rtentry, link, rte, &before->link);
            goto added;
        }
    }

    MIR_DLIST_APPEND(mir_rtentry, link, rte, &rtg->entries);

 added:
    rtgroup_update_module_property(u, type, rtg);
    pa_log_debug("node '%s' added to routing group '%s'",
                 node->amname, rtg->name);
}

static void remove_rtentry(struct userdata *u, mir_rtentry *rte)
{
    mir_rtgroup *rtg;
    mir_node    *node;

    pa_assert(u);
    pa_assert(rte);
    pa_assert_se((rtg = rte->group));
    pa_assert_se((node = rte->node));

    MIR_DLIST_UNLINK(mir_rtentry, link, rte);
    MIR_DLIST_UNLINK(mir_rtentry, nodchain, rte);

    pa_xfree(rte);

    rtgroup_update_module_property(u, node->direction, rtg);
}

static void make_explicit_routes(struct userdata *u, uint32_t stamp)
{
    pa_router *router;
    mir_connection *conn;
    mir_node *from;
    mir_node *to;

    pa_assert(u);
    pa_assert_se((router = u->router));

    MIR_DLIST_FOR_EACH_BACKWARD(mir_connection,link, conn, &router->connlist) {
        if (conn->blocked)
            continue;

        if (!(from = mir_node_find_by_index(u, conn->from)) ||
            !(to   = mir_node_find_by_index(u, conn->to))     )
        {
            pa_log_debug("ignoring explicit route %u: some of the nodes "
                         "not found", conn->amid);
            continue;
        }

        if (!mir_switch_setup_link(u, from, to, true))
            continue;

        if (from->implement == mir_stream)
            from->stamp = stamp;

        if (to->implement == mir_device)
            mir_volume_add_limiting_class(u, to, volume_class(from), stamp);
    }
}


static mir_node *find_default_route(struct userdata *u,
                                    mir_node        *start,
                                    uint32_t         stamp)
{
    pa_router     *router = u->router;
    mir_node_type  class  = pa_classify_guess_application_class(start);
    mir_zone      *zone   = pa_zoneset_get_zone_by_name(u, start->zone);
    mir_rtgroup ***cmap;
    mir_rtgroup  **zmap;
    mir_node      *end;
    mir_rtgroup   *rtg;
    mir_rtentry   *rte;

    if (class < 0 || class > router->maplen) {
        pa_log_debug("can't route '%s': class %d is out of range (0 - %d)",
                     start->amname, class, router->maplen);
        return NULL;
    }

    if (!zone) {
        pa_log_debug("can't route '%s': zone '%s' is unknown",
                     start->amname, start->zone);
        return NULL;
    }

    switch (start->direction) {
    case mir_input:     cmap = router->classmap.output;     break;
    case mir_output:    cmap = router->classmap.input;      break;
    default:            cmap = NULL;                        break;
    }

    if (!cmap || !(zmap = cmap[zone->index]) || !(rtg = zmap[class])) {
        pa_log_debug("node '%s' won't be routed beacuse its class '%s' "
                     "is not assigned to any router group",
                     start->amname, mir_node_type_str(class));
        return NULL;
    }

    pa_log_debug("using '%s' router group when routing '%s'",
                 rtg->name, start->amname);


    MIR_DLIST_FOR_EACH_BACKWARD(mir_rtentry, link, rte, &rtg->entries) {
        if (!(end = rte->node)) {
            pa_log("   node was null in mir_rtentry");
            continue;
        }

        if (end->ignore) {
            pa_log_debug("   '%s' ignored. Skipping...",end->amname);
            continue;
        }

        if (!end->available) {
            pa_log_debug("   '%s' not available. Skipping...", end->amname);
            continue;
        }

        if (end->paidx == PA_IDXSET_INVALID && !end->paport) {
            /* requires profile change. We do it only for BT headsets */
            if (end->type != mir_bluetooth_a2dp &&
                end->type != mir_bluetooth_sco    )
            {
                pa_log_debug("   '%s' has no sink. Skipping...", end->amname);
                continue;
            }
        }

        if (rte->stamp < stamp)
            mir_constrain_apply(u, end, stamp);
        else {
            if (rte->blocked) {
                pa_log_debug("   '%s' is blocked by constraints. Skipping...",
                             end->amname);
                continue;
            }
        }

        pa_log_debug("routing '%s' => '%s'", start->amname, end->amname);

        pa_audiomgr_add_default_route(u, start, end);

        return end;
    }

    pa_log_debug("could not find route for '%s'", start->amname);

    return NULL;
}

static void implement_preroute(struct userdata *u,
                               mir_node        *data,
                               mir_node        *target,
                               uint32_t         stamp)
{
    if (data->direction == mir_output)
        mir_switch_setup_link(u, target, NULL, false);
    else {
        mir_switch_setup_link(u, NULL, target, false);
        mir_volume_add_limiting_class(u, target, data->type, stamp);
    }
}

static void implement_default_route(struct userdata *u,
                                    mir_node        *start,
                                    mir_node        *end,
                                    uint32_t         stamp)
{
    if (start->direction == mir_output)
        mir_switch_setup_link(u, end, start, false);
    else {
        mir_switch_setup_link(u, start, end, false);
        mir_volume_add_limiting_class(u, end, volume_class(start), stamp);
    }
}


static int uint32_cmp(uint32_t v1, uint32_t v2)
{
    if (v1 > v2)
        return 1;
    if (v1 < v2)
        return -1;
    return 0;
}

static int node_priority(struct userdata *u, mir_node *node)
{
    pa_router *router;
    int class;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((router = u->router));
    pa_assert(router->priormap);

    class = pa_classify_guess_application_class(node);

    if (class < 0 || class >= router->maplen)
        return 0;

    return router->priormap[class];
}

static int volume_class(mir_node *node)
{
    int device_class[mir_device_class_end - mir_device_class_begin] = {
        [ mir_bluetooth_carkit - mir_device_class_begin ] = mir_phone,
        [ mir_bluetooth_source - mir_device_class_begin ] = mir_player,
    };

    int t;

    pa_assert(node);

    t = node->type;

    if (t >= mir_application_class_begin && t < mir_application_class_end)
        return t;

    if (t >= mir_device_class_begin && t < mir_device_class_end)
        return device_class[t - mir_device_class_begin];

    return mir_node_type_unknown;
}


static int print_routing_table(pa_hashmap  *table,
                               const char  *type,
                               char        *buf,
                               int          len)
{
    mir_rtgroup *rtg;
    void *state;
    char *p, *e;
    int n;

    pa_assert(table);
    pa_assert(type);
    pa_assert(buf);

    e = (p = buf) + len;
    n = 0;

    if (len > 0) {
        p += snprintf(p, e-p, "%s routing table:\n", type);

        state = NULL;

        if (p < e) {
            PA_HASHMAP_FOREACH(rtg, table, state) {
                n++;

                if (p >= e) break;
                p += snprintf(p, e-p, "   %s:", rtg->name);

                if (p >= e) break;
                p += rtgroup_print(rtg, p, e-p);

                if (p >= e) break;
                p += snprintf(p, e-p, "\n");
            }

            if (!n && p < e)
                p += snprintf(p, e-p, "   <empty>\n");
        }
    }

    return p - buf;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
