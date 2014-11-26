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
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/def.h>

#include <pulsecore/core-util.h>

#include "userdata.h"
#include "audiomgr.h"
#include "node.h"
#include "discover.h"
#include "router.h"
#include "routerif.h"

#define AUDIOMGR_DOMAIN   "PULSE"
#define AUDIOMGR_NODE     "pulsePlugin"

/*
 * these must match their counterpart
 * in audiomanagertypes.h
 */
/* domain status */
#define DS_UNKNOWN        0
#define DS_CONTROLLED     1
#define DS_RUNDOWN        2
#define DS_DOWN           255

/* interrupt state */
#define IS_OFF            1
#define IS_INTERRUPTED    2

/* availability status */
#define AS_AVAILABLE      1
#define AS_UNAVAILABLE    2

/* availability reason */
#define AR_NEWMEDIA       1
#define AR_SAMEMEDIA      2
#define AR_NOMEDIA        3
#define AR_TEMPERATURE    4
#define AR_VOLTAGE        5
#define AR_ERRORMEDIA     6

/* mute state */
#define MS_MUTED          1
#define MS_UNMUTED        2

/* connection format */
#define CF_MONO           1
#define CF_STEREO         2
#define CF_AUTO           4

typedef struct {
    const char *name;
    uint16_t    id;
    uint16_t    state;
} domain_t;


typedef struct {
    uint16_t    fromidx;
    uint16_t    toidx;
    uint32_t    channels;
} link_t;

typedef struct {
    int          maxlink;
    int          nlink;
    link_t      *links;
} routes_t;

struct pa_audiomgr {
    domain_t      domain;
    pa_hashmap   *nodes;        /**< nodes ie. sinks and sources */
    pa_hashmap   *conns;        /**< connections */
    routes_t      defrts;       /**< default routes */
};


/*
static bool find_default_route(struct userdata *, mir_node *,
                               am_connect_data *);
*/

static void *node_hash(mir_direction, uint16_t);
static void *conn_hash(uint16_t);
static void fill_am_data_and_register(struct userdata *, mir_node *, pa_audiomgr *);

struct pa_audiomgr *pa_audiomgr_init(struct userdata *u)
{
    /* pa_module *m = u->module; */
    pa_audiomgr *am;

    pa_assert(u);

    am = pa_xnew0(pa_audiomgr, 1);

    am->domain.id = AM_ID_INVALID;
    am->domain.state = DS_DOWN;
    am->nodes = pa_hashmap_new(pa_idxset_trivial_hash_func,
                                     pa_idxset_trivial_compare_func);
    am->conns = pa_hashmap_new(pa_idxset_trivial_hash_func,
                               pa_idxset_trivial_compare_func);
    return am;
}

void pa_audiomgr_done(struct userdata *u)
{
    pa_audiomgr *am;

    if (u && (am = u->audiomgr)) {
        if (u->routerif && am->domain.id != AM_ID_INVALID)
            pa_routerif_unregister_domain(u, am->domain.id);

        pa_hashmap_free(am->nodes);
        pa_hashmap_free(am->conns);
        pa_xfree((void *)am->domain.name);
        pa_xfree(am);
        u->audiomgr = NULL;
    }
}


void pa_audiomgr_register_domain(struct userdata *u)
{
    pa_audiomgr        *am;
    am_domainreg_data  *dr;

    pa_assert(u);
    pa_assert_se((am = u->audiomgr));

    dr = pa_xnew0(am_domainreg_data, 1);

    dr->domain_id = 0;
    dr->name      = AUDIOMGR_DOMAIN;  /* AM domain name */
    dr->bus_name  = AUDIOMGR_NODE;    /* AM internal bus name. */
    dr->node_name = AUDIOMGR_NODE;    /* node name on AM's internal bus */
    dr->early     = false;
    dr->complete  = false;
    dr->state     = 1;

    pa_routerif_register_domain(u, dr);
}

void pa_audiomgr_domain_registered(struct userdata   *u,
                                   uint16_t           id,
                                   uint16_t           state,
                                   am_domainreg_data *dr)
{
    pa_audiomgr *am;

    pa_assert(u);
    pa_assert(dr);
    pa_assert_se((am = u->audiomgr));


    am->domain.name  = pa_xstrdup(dr->name);
    am->domain.id    = id;
    am->domain.state = state;

    pa_log_debug("start domain registration for '%s' domain", dr->name);

    pa_discover_domain_up(u);

    pa_log_debug("domain registration for '%s' domain is complete", dr->name);

    pa_routerif_domain_complete(u, id);

    pa_xfree(dr);
}


void pa_audiomgr_unregister_domain(struct userdata *u, bool send_state)
{
    pa_audiomgr *am;
    mir_node    *node;
    const void  *key;
    void        *state = NULL;

    pa_assert(u);
    pa_assert_se((am = u->audiomgr));

    pa_log_debug("unregistering domain '%s'", am->domain.name);

    while ((node  = pa_hashmap_iterate(am->nodes, &state, &key))) {
        pa_log_debug("   unregistering '%s' (%p/%p)", node->amname, key,node);
        node->amid = AM_ID_INVALID;
        pa_hashmap_remove(am->nodes, key);
    }

    am->domain.id = AM_ID_INVALID;
    am->domain.state = DS_DOWN;
}

void fill_am_data_and_register(struct userdata *u, mir_node *node, pa_audiomgr *am)
{
    am_nodereg_data  *rd;
    am_method         method;
    bool              success;

    rd = pa_xnew0(am_nodereg_data, 1);
    rd->key     = pa_xstrdup(node->key);
    rd->name    = pa_xstrdup(node->amname);
    rd->domain  = am->domain.id;
    rd->class   = 0x43;
    rd->state   = 1;
    rd->volume  = 32767;
    rd->visible = node->visible;
    rd->avail.status = AS_AVAILABLE;
    rd->avail.reason = 0;
    rd->mainvol = 32767;

    if (node->direction == mir_input) {
        rd->interrupt = IS_OFF;
        method = audiomgr_register_source;
    }
    else {
        rd->mute = MS_UNMUTED;
        method = audiomgr_register_sink;
    }

    success = pa_routerif_register_node(u, method, rd);

    if (success) {
        pa_log_debug("initiate registration node '%s' (%p)"
                     "to audio manager", rd->name, node);
    }
    else {
        pa_log("%s: failed to register node '%s' (%p)"
               "to audio manager", __FILE__, rd->name, node);
    }

    return;
}

void pa_audiomgr_register_node(struct userdata *u, mir_node *node)
{
    static const char *classes_to_register[] = {
        "wrtApplication",
        "icoApplication",
        "navigator",
        "phone",
        "radio",
        NULL
    };

    pa_audiomgr      *am;
    const char       *class_to_register;
    int               i;

    pa_assert(u);
    pa_assert_se((am = u->audiomgr));

    if (am->domain.state == DS_DOWN || am->domain.state == DS_RUNDOWN) {
        pa_log_debug("skip registering nodes while the domain is down");
        return;
    }

    for (i = 0;   (class_to_register = classes_to_register[i]);   i++) {
        if (!strcmp(node->amname, class_to_register)) {
            if (node->direction == mir_input || node->direction == mir_output){
                fill_am_data_and_register(u, node, am);
            }

            return;
        }
    } /* for classes_to_register */

    /* ok, register also the gateways */
    if (strncmp(node->amname, "gw", 2) == 0) {
        if (node->direction == mir_input || node->direction == mir_output){
            fill_am_data_and_register(u, node, am);
        }
        return;
    }

    pa_log_debug("skip registration of node '%s' (%p): "
                 "not known by audio manager", node->amname, node);
}

void pa_audiomgr_node_registered(struct userdata *u,
                                 uint16_t         id,
                                 uint16_t         state,
                                 am_nodereg_data *rd)
{
    pa_audiomgr     *am;
    mir_node        *node;
    void            *key;

    pa_assert(u);
    pa_assert(rd);
    pa_assert(rd->key);
    pa_assert_se((am = u->audiomgr));

    if (!(node = pa_discover_find_node_by_key(u, rd->key)))
        pa_log("%s: can't find node with key '%s'", __FILE__, rd->key);
    else {
        node->amid = id;

        key = node_hash(node->direction, id);

        pa_log_debug("registering node '%s' (%p/%p)",
                     node->amname, key, node);

        pa_hashmap_put(am->nodes, key, node);

        /* we don't want implicit connections register and confuse */
        /* audio manager. Implicit connections are handled by      */
        /* creating a resource through murphy */
        /*
        if (find_default_route(u, node, &cd))
            pa_routerif_register_implicit_connection(u, &cd);
        */
    }

    pa_xfree((void *)rd->key);
    pa_xfree((void *)rd->name);
    pa_xfree((void *)rd);
}

void pa_audiomgr_unregister_node(struct userdata *u, mir_node *node)
{
    pa_audiomgr       *am;
    am_nodeunreg_data *ud;
    am_method          method;
    mir_node          *removed;
    bool          success;
    void              *key;

    pa_assert(u);
    pa_assert_se((am = u->audiomgr));

    if (am->domain.state == DS_DOWN || am->domain.state == DS_RUNDOWN)
        pa_log_debug("skip unregistering nodes while the domain is down");
    else if (node->amid == AM_ID_INVALID)
        pa_log_debug("node '%s' was not registered", node->amname);
    else if (node->direction == mir_input || node->direction == mir_output) {
        ud = pa_xnew0(am_nodeunreg_data, 1);
        ud->id   = node->amid;
        ud->name = pa_xstrdup(node->amname);

        key = node_hash(node->direction, node->amid);
        removed = pa_hashmap_remove(am->nodes, key);

        if (node != removed) {
            if (removed)
                pa_log("%s: confused with data structures: key mismatch. "
                       "attempted to remove '%s' (%p/%p); "
                       "actually removed '%s' (%p/%p)", __FILE__,
                       node->amname, key, node, removed->amname,
                       node_hash(removed->direction, removed->amid), removed);
            else
                pa_log("%s: confused with data structures: node %u (%p)"
                       "is not in the hash table", __FILE__, node->amid, node);
        }


        if (node->direction == mir_input)
            method = audiomgr_deregister_source;
        else
            method = audiomgr_deregister_sink;


        success = pa_routerif_unregister_node(u, method, ud);

        if (success) {
            pa_log_debug("sucessfully unregistered node '%s' (%p/%p)"
                         "from audio manager", node->amname, key, node);
        }
        else {
            pa_log("%s: failed to unregister node '%s' (%p)"
                   "from audio manager", __FILE__, node->amname, node);
        }
    }
}

void pa_audiomgr_node_unregistered(struct userdata   *u,
                                   am_nodeunreg_data *ud)
{
    (void)u;

    /* can't do too much here anyways,
       since the node is gone already */

    pa_xfree((void *)ud->name);
    pa_xfree((void *)ud);
}


void pa_audiomgr_delete_default_routes(struct userdata *u)
{
    pa_audiomgr *am;
    routes_t    *defrts;

    pa_assert(u);
    pa_assert_se((am = u->audiomgr));

    defrts = &am->defrts;

    defrts->nlink = 0;
}

void pa_audiomgr_add_default_route(struct userdata *u,
                                   mir_node        *from,
                                   mir_node        *to)
{
    pa_audiomgr *am;
    routes_t    *defrts;
    link_t      *link;
    size_t       size;

    pa_assert(u);
    pa_assert(from);
    pa_assert(to);
    pa_assert_se((am = u->audiomgr));

    defrts = &am->defrts;

    if (from->paidx == PA_IDXSET_INVALID || to->paidx == PA_IDXSET_INVALID) {
        pa_log_debug("ignoring default route %s => %s: incomplete "
                     "input or output", from->amname, to->amname);
    }
    else {
        pa_log_debug("adding default route %s => %s", from->amname,to->amname);

        if (defrts->nlink >= defrts->maxlink) {
            defrts->maxlink += 16;

            size = sizeof(link_t) * defrts->maxlink;
            defrts->links = realloc(defrts->links, size);
            pa_assert(defrts->links);
        }

        link = defrts->links + defrts->nlink++;

        link->fromidx  = from->index;
        link->toidx    = to->index;
        link->channels = from->channels < to->channels ?
                         from->channels : to->channels;
    }
}

void pa_audiomgr_send_default_routes(struct userdata *u)
{
#define MAX_DEFAULT_ROUTES 128

    pa_audiomgr     *am;
    routes_t        *defrts;
    link_t          *link;
    mir_node        *from;
    mir_node        *to;
    am_connect_data  cds[MAX_DEFAULT_ROUTES];
    am_connect_data *cd;
    int              ncd;

    pa_assert(u);
    pa_assert_se((am = u->audiomgr));

    defrts = &am->defrts;

    pa_assert(defrts->nlink < MAX_DEFAULT_ROUTES);

    for (ncd = 0;   ncd < defrts->nlink;   ncd++) {
        link = defrts->links + ncd;
        cd = cds + ncd;

        if (!(from = mir_node_find_by_index(u, link->fromidx)) ||
            !(to   = mir_node_find_by_index(u, link->toidx))     )
        {
            pa_log_debug("will not send default route: node not found");
            continue;
        }

        if (from->amid == AM_ID_INVALID || to->amid == AM_ID_INVALID) {
            pa_log_debug("wil not send default route: invalid audiomgr ID");
            continue;
        }

        cd->handle = 0;
        cd->connection = 0;
        cd->source = from->amid;
        cd->sink = to->amid;
        cd->format = link->channels >= 2 ? CF_STEREO : CF_MONO;
    }

    /* we don't want implicit connections register and confuse */
    /* audio manager. Implicit connections are handled by      */
    /* creating a resource through murphy */
    /*
    if (ncd > 0)
        pa_routerif_register_implicit_connections(u, ncd, cds);
    */

#undef MAX_DEFAULT_ROUTES
}

void pa_audiomgr_connect(struct userdata *u, am_connect_data *cd)
{
    pa_audiomgr    *am;
    am_ack_data     ad;
    mir_connection *conn;
    uint16_t        cid;
    mir_node       *from = NULL;
    mir_node       *to   = NULL;
    int             err  = E_OK;
    bool            autoconn = false;

    pa_assert(u);
    pa_assert(cd);
    pa_assert_se((am = u->audiomgr));

    if (cd->format == CF_AUTO) {
        autoconn = true;
        pa_log_debug("automatic connection request received");
    }

    if (autoconn == false) {
        if ((from = pa_hashmap_get(am->nodes, node_hash(mir_input, cd->source))) &&
            (to   = pa_hashmap_get(am->nodes, node_hash(mir_output, cd->sink))))
            {
                cid = cd->connection;

                pa_log_debug("routing '%s' => '%s'", from->amname, to->amname);

                if (!(conn = mir_router_add_explicit_route(u, cid, from, to)))
                    err = E_NOT_POSSIBLE;
                else {
                    pa_log_debug("registering connection (%u/%p)",
                                 cd->connection, conn);
                    pa_hashmap_put(am->conns, conn_hash(cid), conn);
                }
            }
        else {
            pa_log_debug("failed to connect: can't find node for %s %u",
                         from ? "sink" : "source", from ? cd->sink : cd->source);
            err = E_NON_EXISTENT;
        }
    }

    memset(&ad, 0, sizeof(ad));
    ad.handle = cd->handle;
    ad.param1 = cd->connection;
    ad.error  = err;

    pa_routerif_acknowledge(u, audiomgr_connect_ack, &ad);
}

void pa_audiomgr_disconnect(struct userdata *u, am_connect_data *cd)
{
    pa_audiomgr    *am;
    mir_connection *conn;
    uint16_t        cid;
    am_ack_data     ad;
    int             err = E_OK;

    pa_assert(u);
    pa_assert(cd);
    pa_assert_se((am = u->audiomgr));

    cid = cd->connection;

    if ((conn = pa_hashmap_remove(am->conns, conn_hash(cid))))
        mir_router_remove_explicit_route(u, conn);
    else {
        pa_log_debug("failed to disconnect: can't find connection %u", cid);
        err = E_NON_EXISTENT;
    }

    memset(&ad, 0, sizeof(ad));
    ad.handle = cd->handle;
    ad.param1 = cd->connection;
    ad.error  = err;

    pa_routerif_acknowledge(u, audiomgr_disconnect_ack, &ad);
}

#if 0
static bool find_default_route(struct userdata *u,
                               mir_node        *node,
                               am_connect_data *cd)
{
    pa_audiomgr *am;
    routes_t    *defrts;
    link_t      *link;
    mir_node    *pair;
    int          i;

    pa_assert(u);
    pa_assert(node);
    pa_assert(cd);
    pa_assert_se((am = u->audiomgr));

    defrts = &am->defrts;

    memset(cd, 0, sizeof(am_connect_data));

    for (i = 0;  i < defrts->nlink;  i++) {
        link = defrts->links + i;

        cd->format = link->channels >= 2 ? CF_STEREO : CF_MONO;

        if (node->direction == mir_input && link->fromidx == node->index) {
            return (pair = mir_node_find_by_index(u, link->toidx)) &&
                   (cd->source = node->amid) != AM_ID_INVALID &&
                   (cd->sink   = pair->amid) != AM_ID_INVALID;
        }

        if (node->direction == mir_output && link->toidx == node->index) {
            return (pair = mir_node_find_by_index(u, link->fromidx)) &&
                   (cd->source = pair->amid) != AM_ID_INVALID &&
                   (cd->sink = node->amid) != AM_ID_INVALID;
        }
    }

    return false;
}
#endif

static void *node_hash(mir_direction direction, uint16_t amid)
{
    return (char *)NULL + ((uint32_t)direction << 16 | (uint32_t)amid);
}

static void *conn_hash(uint16_t connid)
{
    return (char *)NULL + (uint32_t)connid;
}



/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
