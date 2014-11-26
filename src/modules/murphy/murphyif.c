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
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <alloca.h>
#include <errno.h>

#include <pulse/utf8.h>
#include <pulse/timeval.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/module.h>
#include <pulsecore/llist.h>
#include <pulsecore/idxset.h>
#include <pulsecore/hashmap.h>
#include <pulsecore/core-util.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>

#ifdef WITH_MURPHYIF
#define WITH_DOMCTL
#define WITH_RESOURCES
#endif

#if defined(WITH_DOMCTL) || defined(WITH_RESOURCES)
#include <murphy/common/macros.h>
#include <murphy/common/mainloop.h>
#include <murphy/pulse/pulse-glue.h>
#endif

#ifdef WITH_RESOURCES
#include <murphy/resource/protocol.h>
#include <murphy/common/transport.h>
#include <murphy/resource/protocol.h>
#include <murphy/resource/data-types.h>
#endif

#include "murphyif.h"
#include "node.h"
#include "stream-state.h"
#include "utils.h"

#ifdef WITH_RESOURCES
#define INVALID_ID      (~(uint32_t)0)
#define INVALID_INDEX   (~(uint32_t)0)
#define INVALID_SEQNO   (~(uint32_t)0)
#define INVALID_REQUEST (~(uint16_t)0)

#define DISCONNECTED    -1
#define CONNECTED        0
#define CONNECTING       1

#define RESCOL_NAMES    "rsetid,autorel,state,grant,pid,policy"
#define RESCOL_RSETID   0
#define RESCOL_AUTOREL  1
#define RESCOL_STATE    2
#define RESCOL_GRANT    3
#define RESCOL_PID      4
#define RESCOL_POLICY   5

#define RSET_RELEASE    1
#define RSET_ACQUIRE    2

#define PUSH_VALUE(msg, tag, typ, val) \
    mrp_msg_append(msg, MRP_MSG_TAG_##typ(RESPROTO_##tag, val))

#define PUSH_ATTRS(msg, rif, proplist)                  \
    resource_push_attributes(msg, rif, proplist)

typedef struct resource_attribute  resource_attribute;
typedef struct resource_request    resource_request;

struct resource_attribute {
    PA_LLIST_FIELDS(resource_attribute);
    const char *prop;
    mrp_attr_t  def;
};

struct resource_request {
    PA_LLIST_FIELDS(resource_request);
    uint32_t nodidx;
    uint16_t reqid;
    uint32_t seqno;
};

#endif

typedef struct {
    const char           *addr;
#ifdef WITH_DOMCTL
    mrp_domctl_t         *ctl;
    int                   ntable;
    mrp_domctl_table_t   *tables;
    int                   nwatch;
    mrp_domctl_watch_t   *watches;
    pa_murphyif_watch_cb  watchcb;
#endif
} domctl_interface;

typedef struct {
    const char *name;
    int         tblidx;
} audio_resource_t;

typedef struct {
    const char       *addr;
    audio_resource_t  inpres;
    audio_resource_t  outres;
#ifdef WITH_RESOURCES
    mrp_transport_t *transp;
    mrp_sockaddr_t   saddr;
    socklen_t        alen;
    const char      *atype;
    bool        connected;
    struct {
        pa_time_event *evt;
        pa_usec_t      period;
    }                connect;
    struct {
        uint32_t request;
        uint32_t reply;
    }                seqno;
    struct {
        pa_hashmap *rsetid;
        pa_hashmap *pid;
    }                nodes;
    PA_LLIST_HEAD(resource_attribute, attrs);
    PA_LLIST_HEAD(resource_request, reqs);
#endif
} resource_interface;


struct pa_murphyif {
#if defined(WITH_DOMCTL) || defined(WITH_RESOURCES)
    mrp_mainloop_t *ml;
#endif
    domctl_interface domctl;
    resource_interface resource;
};

#ifdef WITH_RESOURCES
typedef struct {
    const char *id;
    bool   autorel;
    int         state;
    bool   grant;
    const char *policy;
} rset_data;

typedef struct {
    char       *pid;
    mir_node   *node;
    rset_data  *rset;
} pid_hash;

typedef struct {
    size_t      nnode;
    mir_node  **nodes;
    rset_data  *rset;
} rset_hash;

#endif


#ifdef WITH_DOMCTL
static void domctl_connect_notify(mrp_domctl_t *,int,int,const char *,void *);
static void domctl_watch_notify(mrp_domctl_t *,mrp_domctl_data_t *,int,void *);
static void domctl_dump_data(mrp_domctl_data_t *);
#endif

#ifdef WITH_RESOURCES
static void       resource_attribute_destroy(resource_interface *,
                                             resource_attribute *);
static int        resource_transport_connect(resource_interface *);
static void       resource_xport_closed_evt(mrp_transport_t *, int, void *);

static mrp_msg_t *resource_create_request(uint32_t, mrp_resproto_request_t);
static bool  resource_send_message(resource_interface *, mrp_msg_t *,
                                        uint32_t, uint16_t, uint32_t);
static bool  resource_set_create_node(struct userdata *, mir_node *,
                                           pa_nodeset_resdef *, bool);
static bool  resource_set_create_all(struct userdata *);
static bool  resource_set_destroy_node(struct userdata *, uint32_t);
static bool  resource_set_destroy_all(struct userdata *);
static void       resource_set_notification(struct userdata *, const char *,
                                            int, mrp_domctl_value_t **);

static bool  resource_push_attributes(mrp_msg_t *, resource_interface *,
                                           pa_proplist *);

static void       resource_recv_msg(mrp_transport_t *, mrp_msg_t *, void *);
static void       resource_recvfrom_msg(mrp_transport_t *, mrp_msg_t *,
                                        mrp_sockaddr_t *, socklen_t, void *);
static void       resource_set_create_response(struct userdata *, mir_node *,
                                               mrp_msg_t *, void **);
static void       resource_set_create_response_abort(struct userdata *,
                                                     mrp_msg_t *, void **);

static bool  resource_fetch_seqno(mrp_msg_t *, void **, uint32_t *);
static bool  resource_fetch_request(mrp_msg_t *, void **, uint16_t *);
static bool  resource_fetch_status(mrp_msg_t *, void **, int *);
static bool  resource_fetch_rset_id(mrp_msg_t *, void **, uint32_t*);

/*
static bool  resource_fetch_rset_state(mrp_msg_t *, void **,
                                            mrp_resproto_state_t *);

static bool  resource_fetch_rset_mask(mrp_msg_t *, void **,
                                           mrp_resproto_state_t *);
*/

static bool  resource_transport_create(struct userdata *, pa_murphyif *);
static void       resource_transport_destroy(pa_murphyif *);

static void connect_attempt(pa_mainloop_api *, pa_time_event *,
                             const struct timeval *, void *);
static void schedule_connect(struct userdata *, resource_interface *);
static void cancel_schedule(struct userdata *, resource_interface *);

static rset_hash *node_put_rset(struct userdata *, mir_node *, rset_data *);
static void node_enforce_resource_policy(struct userdata *, mir_node *,
                                         rset_data *);
static rset_data *rset_data_dup(rset_data *);
static void rset_data_copy(rset_data *, rset_data *);
static void rset_data_update(rset_data *, rset_data *);
static void rset_data_free(rset_data *);

static void        pid_hashmap_free(void *, void *);
static int         pid_hashmap_put(struct userdata *, const char *,
                                   mir_node *, rset_data *);
static mir_node   *pid_hashmap_get_node(struct userdata *, const char *);
static rset_data  *pid_hashmap_get_rset(struct userdata *, const char *);
static mir_node   *pid_hashmap_remove_node(struct userdata *, const char *);
static rset_data  *pid_hashmap_remove_rset(struct userdata *, const char *);

static void       rset_hashmap_free(void *, void *);
static rset_hash *rset_hashmap_put(struct userdata *, const char *, mir_node *);
static rset_hash *rset_hashmap_get(struct userdata *u, const char *rsetid);
static int        rset_hashmap_remove(struct userdata *,const char *,mir_node*);

#endif

static pa_proplist *get_node_proplist(struct userdata *, mir_node *);
static const char *get_node_pid(struct userdata *, mir_node *);


pa_murphyif *pa_murphyif_init(struct userdata *u,
                              const char *ctl_addr,
                              const char *res_addr)
{
    pa_murphyif *murphyif;
    domctl_interface *dif;
    resource_interface *rif;
#if defined(WITH_DOMCTL) || defined(WITH_RESOURCES)
    mrp_mainloop_t *ml;

    if (!(ml = mrp_mainloop_pulse_get(u->core->mainloop))) {
        pa_log_error("Failed to set up murphy mainloop.");
        return NULL;
    }
#endif
#ifdef WITH_RESOURCES
#endif

    murphyif = pa_xnew0(pa_murphyif, 1);
    dif = &murphyif->domctl;
    rif = &murphyif->resource;

#if defined(WITH_DOMCTL) || defined(WITH_RESOURCES)
    murphyif->ml = ml;
#endif

    dif->addr = pa_xstrdup(ctl_addr ? ctl_addr:MRP_DEFAULT_DOMCTL_ADDRESS);
#ifdef WITH_DOMCTL
#endif

    rif->addr = pa_xstrdup(res_addr ? res_addr:RESPROTO_DEFAULT_ADDRESS);
#ifdef WITH_RESOURCES
    rif->alen = mrp_transport_resolve(NULL, rif->addr, &rif->saddr,
                                      sizeof(rif->saddr), &rif->atype);
    if (rif->alen <= 0) {
        pa_log("can't resolve resource transport address '%s'", rif->addr);
    }
    else {
        rif->inpres.tblidx = -1;
        rif->outres.tblidx = -1;
        rif->connect.period = 1 * PA_USEC_PER_SEC;

        if (!resource_transport_create(u, murphyif)) {
            pa_log("failed to create resource transport");
            schedule_connect(u, rif);
        }
        else {
            if (resource_transport_connect(rif) == DISCONNECTED)
                schedule_connect(u, rif);
        }
    }

    rif->seqno.request = 1;
    rif->nodes.rsetid = pa_hashmap_new(pa_idxset_string_hash_func,
                                       pa_idxset_string_compare_func);
    rif->nodes.pid = pa_hashmap_new(pa_idxset_string_hash_func,
                                    pa_idxset_string_compare_func);
    PA_LLIST_HEAD_INIT(resource_attribute, rif->attrs);
    PA_LLIST_HEAD_INIT(resource_request, rif->reqs);
#endif

    return murphyif;
}


void pa_murphyif_done(struct userdata *u)
{
    pa_murphyif *murphyif;
    domctl_interface *dif;
    resource_interface *rif;
#ifdef WITH_RESOURCES
    resource_attribute *attr, *a;
    resource_request *req, *r;
    void *state;
    rset_hash *rh;
    pid_hash *ph;
#endif

    if (u && (murphyif = u->murphyif)) {
#ifdef WITH_DOMCTL
        mrp_domctl_table_t *t;
        mrp_domctl_watch_t *w;
        int i;

        dif = &murphyif->domctl;

        mrp_domctl_destroy(dif->ctl);

        if (dif->ntable > 0 && dif->tables) {
            for (i = 0;  i < dif->ntable;  i++) {
                t = dif->tables + i;
                pa_xfree((void *)t->table);
                pa_xfree((void *)t->mql_columns);
                pa_xfree((void *)t->mql_index);
            }
            pa_xfree(dif->tables);
        }

        if (dif->nwatch > 0 && dif->watches) {
            for (i = 0;  i < dif->nwatch;  i++) {
                w = dif->watches + i;
                pa_xfree((void *)w->table);
                pa_xfree((void *)w->mql_columns);
                pa_xfree((void *)w->mql_where);
            }
            pa_xfree(dif->watches);
        }

        pa_xfree((void *)dif->addr);
#endif

#ifdef WITH_RESOURCES
        rif = &murphyif->resource;

        resource_transport_destroy(murphyif);

        PA_HASHMAP_FOREACH(rh, rif->nodes.rsetid, state) {
            if (rh) {
                pa_xfree(rh->nodes);
                rset_data_free(rh->rset);
                pa_xfree(rh);
            }
        }

        PA_HASHMAP_FOREACH(ph, rif->nodes.pid, state) {
            if (ph) {
                pa_xfree((void *)ph->pid);
                rset_data_free(ph->rset);
                pa_xfree(ph);
            }
        }

        pa_hashmap_free(rif->nodes.rsetid);
        pa_hashmap_free(rif->nodes.pid);

        PA_LLIST_FOREACH_SAFE(attr, a, rif->attrs)
            resource_attribute_destroy(rif, attr);

        PA_LLIST_FOREACH_SAFE(req, r, rif->reqs)
            pa_xfree(req);

        cancel_schedule(u, rif);

        pa_xfree((void *)rif->addr);
        pa_xfree((void *)rif->inpres.name);
        pa_xfree((void *)rif->outres.name);
#endif
        mrp_mainloop_destroy(murphyif->ml);

        pa_xfree(murphyif);
    }
}


void pa_murphyif_add_table(struct userdata *u,
                           const char *table,
                           const char *columns,
                           const char *index)
{
    pa_murphyif *murphyif;
    domctl_interface *dif;
    mrp_domctl_table_t *t;
    size_t size;
    size_t idx;

    pa_assert(u);
    pa_assert(table);
    pa_assert(columns);
    pa_assert_se((murphyif = u->murphyif));

    dif = &murphyif->domctl;

    idx = dif->ntable++;
    size = sizeof(mrp_domctl_table_t) * dif->ntable;
    t = (dif->tables = pa_xrealloc(dif->tables, size)) + idx;

    t->table = pa_xstrdup(table);
    t->mql_columns = pa_xstrdup(columns);
    t->mql_index = index ? pa_xstrdup(index) : NULL;
}

int pa_murphyif_add_watch(struct userdata *u,
                          const char *table,
                          const char *columns,
                          const char *where,
                          int max_rows)
{
    pa_murphyif *murphyif;
    domctl_interface *dif;
    mrp_domctl_watch_t *w;
    size_t size;
    size_t idx;

    pa_assert(u);
    pa_assert(table);
    pa_assert(columns);
    pa_assert(max_rows > 0 && max_rows < MQI_QUERY_RESULT_MAX);
    pa_assert_se((murphyif = u->murphyif));

    dif = &murphyif->domctl;

    idx = dif->nwatch++;
    size = sizeof(mrp_domctl_watch_t) * dif->nwatch;
    w = (dif->watches = pa_xrealloc(dif->watches, size)) + idx;

    w->table = pa_xstrdup(table);
    w->mql_columns = pa_xstrdup(columns);
    w->mql_where = where ? pa_xstrdup(where) : NULL;
    w->max_rows = max_rows;

    return idx;
}

void pa_murphyif_setup_domainctl(struct userdata *u, pa_murphyif_watch_cb wcb)
{
    static const char *name = "pulse";

    pa_murphyif *murphyif;
    domctl_interface *dif;

    pa_assert(u);
    pa_assert(wcb);
    pa_assert_se((murphyif = u->murphyif));

    dif = &murphyif->domctl;

#ifdef WITH_DOMCTL
    if (dif->ntable || dif->nwatch) {
        dif->ctl = mrp_domctl_create(name, murphyif->ml,
                                     dif->tables, dif->ntable,
                                     dif->watches, dif->nwatch,
                                     domctl_connect_notify,
                                     domctl_watch_notify, u);
        if (!dif->ctl) {
            pa_log("failed to create '%s' domain controller", name);
            return;
        }

        if (!mrp_domctl_connect(dif->ctl, dif->addr, 0)) {
            pa_log("failed to conect to murphyd");
            return;
        }

        dif->watchcb = wcb;
        pa_log_info("'%s' domain controller sucessfully created", name);
    }
#endif
}

void  pa_murphyif_add_audio_resource(struct userdata *u,
                                     mir_direction dir,
                                     const char *name)
{
#ifdef WITH_DOMCTL
    static const char *columns = RESCOL_NAMES;
    static int maxrow = MQI_QUERY_RESULT_MAX - 1;
#endif
    pa_murphyif *murphyif;
    resource_interface *rif;
    audio_resource_t *res;
    char table[1024];

    pa_assert(u);
    pa_assert(dir == mir_input || dir == mir_output);
    pa_assert(name);

    pa_assert_se((murphyif = u->murphyif));
    rif = &murphyif->resource;
    res = NULL;

    if (dir == mir_input) {
        if (rif->inpres.name)
            pa_log("attempt to register playback resource multiple time");
        else
            res = &rif->inpres;
    }
    else {
        if (rif->outres.name)
            pa_log("attempt to register recording resource multiple time");
        else
            res = &rif->outres;
    }

    if (res) {
        res->name = pa_xstrdup(name);
#ifdef WITH_DOMCTL
        snprintf(table, sizeof(table), "%s_users", name);
        res->tblidx = pa_murphyif_add_watch(u, table, columns, NULL, maxrow);
#endif
    }
}

void pa_murphyif_add_audio_attribute(struct userdata *u,
                                     const char *propnam,
                                     const char *attrnam,
                                     mqi_data_type_t type,
                                     ... ) /* default value */
{
#ifdef WITH_RESOURCES
    pa_murphyif *murphyif;
    resource_interface *rif;
    resource_attribute *attr;
    mrp_attr_value_t *val;
    va_list ap;

    pa_assert(u);
    pa_assert(propnam);
    pa_assert(attrnam);
    pa_assert(type == mqi_string  || type == mqi_integer ||
              type == mqi_unsignd || type == mqi_floating);

    pa_assert_se((murphyif = u->murphyif));
    rif = &murphyif->resource;

    attr = pa_xnew0(resource_attribute, 1);
    val  = &attr->def.value;

    attr->prop = pa_xstrdup(propnam);
    attr->def.name = pa_xstrdup(attrnam);
    attr->def.type = type;

    va_start(ap, type);

    switch (type){
    case mqi_string:   val->string    = pa_xstrdup(va_arg(ap, char *));  break;
    case mqi_integer:  val->integer   = va_arg(ap, int32_t);             break;
    case mqi_unsignd:  val->unsignd   = va_arg(ap, uint32_t);            break;
    case mqi_floating: val->floating  = va_arg(ap, double);              break;
    default:           attr->def.type = mqi_error;                       break;
    }

    va_end(ap);

     if (attr->def.type == mqi_error)
         resource_attribute_destroy(rif, attr);
     else
         PA_LLIST_PREPEND(resource_attribute, rif->attrs, attr);
#endif
}

void pa_murphyif_create_resource_set(struct userdata *u,
                                     mir_node *node,
                                     pa_nodeset_resdef *resdef)
{
    pa_core *core;
    pa_murphyif *murphyif;
    resource_interface *rif;
    int state;

    pa_assert(u);
    pa_assert(node);
    pa_assert((!node->loop && node->implement == mir_stream) ||
              ( node->loop && node->implement == mir_device)   );
    pa_assert(node->direction == mir_input || node->direction == mir_output);
    pa_assert(node->zone);
    pa_assert(!node->rsetid);

    pa_assert_se((core = u->core));

    pa_assert_se((murphyif = u->murphyif));
    rif = &murphyif->resource;

    state = resource_transport_connect(rif);

    switch (state) {

    case CONNECTING:
        resource_set_create_all(u);
        break;

    case CONNECTED:
        node->localrset = resource_set_create_node(u, node, resdef, true);
        break;

    case DISCONNECTED:
        break;
    }
}

void pa_murphyif_destroy_resource_set(struct userdata *u, mir_node *node)
{
    pa_murphyif *murphyif;
    uint32_t rsetid;
    char *e;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((murphyif = u->murphyif));

    if (node->localrset && node->rsetid) {

        pa_murphyif_delete_node(u, node);

        rsetid = strtoul(node->rsetid, &e, 10);

        if (e == node->rsetid || *e) {
            pa_log("can't destroy resource set: invalid rsetid '%s'",
                   node->rsetid);
        }
        else {
            if (rset_hashmap_remove(u, node->rsetid, node) < 0) {
                pa_log_debug("failed to remove resource set %s from hashmap",
                             node->rsetid);
            }

            if (resource_set_destroy_node(u, rsetid))
                pa_log_debug("sent resource set %u destruction request", rsetid);
            else {
                pa_log("failed to destroy resourse set %u for node '%s'",
                       rsetid, node->amname);
            }

            pa_xfree(node->rsetid);

            node->localrset = false;
            node->rsetid = NULL;
        }
    }
}

int pa_murphyif_add_node(struct userdata *u, mir_node *node)
{
#ifdef WITH_RESOURCES
    pa_murphyif *murphyif;
    const char *pid;
    rset_data *rset;
    rset_hash *rh;

    pa_assert(u);
    pa_assert(node);

    pa_assert_se((murphyif = u->murphyif));

    if (!node->rsetid) {
        pa_log("can't register resource set for node %u '%s'.: missing rsetid",
               node->paidx, node->amname);
    }
    else if (pa_streq(node->rsetid, PA_RESOURCE_SET_ID_PID)) {
        if (!(pid = get_node_pid(u,node)))
            pa_log("can't obtain PID for node '%s'", node->amname);
        else {
            if (pid_hashmap_put(u, pid, node, NULL) == 0)
                return 0;

            if ((rset = pid_hashmap_remove_rset(u, pid))) {
                pa_log_debug("found resource-set %s for node '%s'",
                             rset->id, node->amname);

                if (node_put_rset(u, node, rset)) {
                    node_enforce_resource_policy(u, node, rset);
                    rset_data_free(rset);
                    return 0;
                }

                pa_log("can't register resource set for node '%s': "
                       "failed to set rsetid", node->amname);

                rset_data_free(rset);
            }
            else {
                pa_log("can't register resource set for node '%s': "
                       "conflicting pid", node->amname);
            }
        }
    }
    else {
        if ((rh = rset_hashmap_put(u, node->rsetid, node))) {
            rset = rh->rset;

            pa_log_debug("enforce policies on node %u '%s' rsetid:%s autorel:%s "
                         "state:%s grant:%s policy:%s", node->paidx, node->amname,
                         rset->id, rset->autorel ? "yes":"no",
                         rset->state == RSET_ACQUIRE ? "acquire":"release",
                         rset->grant ? "yes":"no", rset->policy);

            node_enforce_resource_policy(u, node, rset);
            return 0;
        }
    }

    return -1;
#else
    return 0;
#endif
}

void pa_murphyif_delete_node(struct userdata *u, mir_node *node)
{
#ifdef WITH_RESOURCES
    pa_murphyif *murphyif;
    const char *pid;

    pa_assert(u);
    pa_assert(node);

    pa_assert_se((murphyif = u->murphyif));

    if (node->rsetid) {
        if (pa_streq(node->rsetid, PA_RESOURCE_SET_ID_PID)) {
            if ((pid = get_node_pid(u, node))) {
                if (node == pid_hashmap_get_node(u, pid))
                    pid_hashmap_remove_node(u, pid);
                else {
                    pa_log("pid %s seems to have multiple resource sets. "
                           "Refuse to delete node %u (%s) from hashmap",
                           pid, node->index, node->amname);
                }
            }
        }
        else {
            if (rset_hashmap_remove(u, node->rsetid, node) < 0) {
                pa_log("failed to remove node '%s' from rset hash", node->amname);
            }
        }
    }
#endif
}


#ifdef WITH_DOMCTL
static void domctl_connect_notify(mrp_domctl_t *dc, int connected, int errcode,
                                  const char *errmsg, void *user_data)
{
    MRP_UNUSED(dc);
    MRP_UNUSED(user_data);

    if (connected)
        pa_log_info("Successfully registered to Murphy.");
    else {
        pa_log_error("Domain control Connection to Murphy failed (%d: %s).",
                     errcode, errmsg);
    }
}

static void domctl_watch_notify(mrp_domctl_t *dc, mrp_domctl_data_t *tables,
                                int ntable, void *user_data)
{
    struct userdata *u = (struct userdata *)user_data;
    pa_murphyif *murphyif;
    domctl_interface *dif;
    resource_interface *rif;
    mrp_domctl_data_t *t;
    mrp_domctl_watch_t *w;
    int i;

    MRP_UNUSED(dc);

    pa_assert(tables);
    pa_assert(ntable > 0);
    pa_assert(u);
    pa_assert_se((murphyif = u->murphyif));

    dif = &murphyif->domctl;
    rif = &murphyif->resource;

    pa_log_info("Received change notification for %d tables.", ntable);

    for (i = 0; i < ntable; i++) {
        t = tables + i;

        domctl_dump_data(t);

        pa_assert(t->id >= 0);
        pa_assert(t->id < dif->nwatch);

        w = dif->watches + t->id;

#ifdef WITH_RESOURCES
        if (t->id == rif->inpres.tblidx || t->id == rif->outres.tblidx) {
            resource_set_notification(u, w->table, t->nrow, t->rows);
            continue;
        }
#endif

        dif->watchcb(u, w->table, t->nrow, t->rows);
    }
}

static void domctl_dump_data(mrp_domctl_data_t *table)
{
    mrp_domctl_value_t *row;
    int                 i, j;
    char                buf[1024], *p;
    const char         *t;
    int                 n, l;

    pa_log_debug("Table #%d: %d rows x %d columns", table->id,
           table->nrow, table->ncolumn);

    for (i = 0; i < table->nrow; i++) {
        row = table->rows[i];
        p   = buf;
        n   = sizeof(buf);

        for (j = 0, t = ""; j < table->ncolumn; j++, t = ", ") {
            switch (row[j].type) {
            case MRP_DOMCTL_STRING:
                l  = snprintf(p, n, "%s'%s'", t, row[j].str);
                p += l;
                n -= l;
                break;
            case MRP_DOMCTL_INTEGER:
                l  = snprintf(p, n, "%s%d", t, row[j].s32);
                p += l;
                n -= l;
                break;
            case MRP_DOMCTL_UNSIGNED:
                l  = snprintf(p, n, "%s%u", t, row[j].u32);
                p += l;
                n -= l;
                break;
            case MRP_DOMCTL_DOUBLE:
                l  = snprintf(p, n, "%s%f", t, row[j].dbl);
                p += l;
                n -= l;
                break;
            default:
                l  = snprintf(p, n, "%s<invalid column 0x%x>",
                              t, row[j].type);
                p += l;
                n -= l;
            }
        }

        pa_log_debug("row #%d: { %s }", i, buf);
    }
}
#endif

#ifdef WITH_RESOURCES
static void resource_attribute_destroy(resource_interface *rif,
                                       resource_attribute *attr)
{
    if (attr) {
       if (rif)
           PA_LLIST_REMOVE(resource_attribute, rif->attrs, attr);

       pa_xfree((void *)attr->prop);
       pa_xfree((void *)attr->def.name);

       if (attr->def.type == mqi_string)
           pa_xfree((void *)attr->def.value.string);

       pa_xfree(attr);
    }
}

static int resource_transport_connect(resource_interface *rif)
{
    int status;

    pa_assert(rif);

    if (rif->connected)
        status = CONNECTED;
    else {
        if (!mrp_transport_connect(rif->transp, &rif->saddr, rif->alen))
            status = DISCONNECTED;
        else {
            pa_log_info("resource transport connected to '%s'", rif->addr);
            rif->connected = true;
            status = CONNECTING;
        }
    }

    return status;
}

static void resource_xport_closed_evt(mrp_transport_t *transp, int error,
                                      void *void_u)
{
    struct userdata *u = (struct userdata *)void_u;
    pa_murphyif *murphyif;
    resource_interface *rif;

    MRP_UNUSED(transp);

    pa_assert(u);
    pa_assert_se((murphyif = u->murphyif));

    rif = &murphyif->resource;

    if (!error)
        pa_log("Resource transport connection closed by peer");
    else {
        pa_log("Resource transport connection closed with error %d (%s)",
               error, strerror(error));
    }

    resource_transport_destroy(murphyif);
    resource_set_destroy_all(u);
    schedule_connect(u, rif);
}

static mrp_msg_t *resource_create_request(uint32_t seqno,
                                          mrp_resproto_request_t req)
{
    uint16_t   type  = req;
    mrp_msg_t *msg;

    msg = mrp_msg_create(RESPROTO_SEQUENCE_NO , MRP_MSG_FIELD_UINT32, seqno,
                         RESPROTO_REQUEST_TYPE, MRP_MSG_FIELD_UINT16, type ,
                         RESPROTO_MESSAGE_END                               );

    if (!msg)
        pa_log("can't to create new resource message");

    return msg;
}

static bool resource_send_message(resource_interface *rif,
                                       mrp_msg_t          *msg,
                                       uint32_t            nodidx,
                                       uint16_t            reqid,
                                       uint32_t            seqno)
{
    resource_request *req;
    bool success = true;

    if (!mrp_transport_send(rif->transp, msg)) {
        pa_log("failed to send resource message");
        success = false;
    }
    else {
        req = pa_xnew0(resource_request, 1);
        req->nodidx = nodidx;
        req->reqid  = reqid;
        req->seqno  = seqno;

        PA_LLIST_PREPEND(resource_request, rif->reqs, req);
    }

    mrp_msg_unref(msg);

    return success;
}

static bool resource_set_create_node(struct userdata *u,
                                          mir_node *node,
                                          pa_nodeset_resdef *resdef,
                                          bool acquire)
{
    pa_core *core;
    pa_murphyif *murphyif;
    resource_interface *rif;
    mrp_msg_t *msg;
    uint16_t reqid;
    uint32_t seqno;
    uint32_t rset_flags;
    const char *role;
    pa_nodeset_map *map;
    const char *class;
    pa_loopnode *loop;
    pa_sink_input *sinp;
    pa_source_output *sout;
    audio_resource_t *res;
    const char *resnam;
    mir_node_type type = 0;
    uint32_t audio_flags = 0;
    uint32_t priority;
    pa_proplist *proplist = NULL;
    bool success = true;

    pa_assert(u);
    pa_assert(node);
    pa_assert(node->index != PA_IDXSET_INVALID);
    pa_assert((!node->loop && node->implement == mir_stream) ||
              ( node->loop && node->implement == mir_device)   );
    pa_assert(node->direction == mir_input || node->direction == mir_output);
    pa_assert(node->zone);
    pa_assert(!node->rsetid);

    pa_assert_se((core = u->core));

    if ((loop = node->loop)) {
        if (node->direction == mir_input) {
            sout = pa_idxset_get_by_index(core->source_outputs,
                                          loop->source_output_index);
            if (sout)
                proplist = sout->proplist;
        }
        else {
            sinp = pa_idxset_get_by_index(core->sink_inputs,
                                          loop->sink_input_index);
            if (sinp)
                proplist = sinp->proplist;
        }
        if (proplist && (role = pa_proplist_gets(proplist, PA_PROP_MEDIA_ROLE))) {
            if ((map = pa_nodeset_get_map_by_role(u, role)))
                type = map->type;
        }
    }
    else {
        if (node->direction == mir_output) {
            if ((sout = pa_idxset_get_by_index(core->source_outputs, node->paidx)))
                proplist = sout->proplist;
        }
        else {
            if ((sinp = pa_idxset_get_by_index(core->sink_inputs, node->paidx)))
                proplist = sinp->proplist;
        }
        type = node->type;
    }

    pa_assert_se((class = pa_nodeset_get_class(u, type)));
    pa_assert_se((murphyif = u->murphyif));
    rif = &murphyif->resource;

    reqid = RESPROTO_CREATE_RESOURCE_SET;
    seqno = rif->seqno.request++;
    res   = (node->direction == mir_input) ? &rif->inpres : &rif->outres;

    pa_assert_se((resnam = res->name));

    rset_flags = RESPROTO_RSETFLAG_NOEVENTS;
    rset_flags |= (acquire ? RESPROTO_RSETFLAG_AUTOACQUIRE : 0);
    rset_flags |= (resdef ? resdef->flags.rset : 0);

    audio_flags = (resdef ? resdef->flags.audio : 0);

    priority = (resdef ? resdef->priority : 0);

    if (!(msg = resource_create_request(seqno, reqid)))
        return false;

    if (PUSH_VALUE(msg,   RESOURCE_FLAGS   , UINT32, rset_flags)  &&
        PUSH_VALUE(msg,   RESOURCE_PRIORITY, UINT32, priority)    &&
        PUSH_VALUE(msg,   CLASS_NAME       , STRING, class)       &&
        PUSH_VALUE(msg,   ZONE_NAME        , STRING, node->zone)  &&
        PUSH_VALUE(msg,   RESOURCE_NAME    , STRING, resnam)      &&
        PUSH_VALUE(msg,   RESOURCE_FLAGS   , UINT32, audio_flags) &&
        PUSH_VALUE(msg,   ATTRIBUTE_NAME   , STRING, "policy")    &&
        PUSH_VALUE(msg,   ATTRIBUTE_VALUE  , STRING, "strict")    &&
        PUSH_ATTRS(msg,   rif, proplist)                          &&
        PUSH_VALUE(msg,   SECTION_END      , UINT8 , 0)            )
    {
        success = resource_send_message(rif, msg, node->index, reqid, seqno);
    }
    else {
        success = false;
        mrp_msg_unref(msg);
    }

    if (success)
        pa_log_debug("requested resource set for '%s'", node->amname);
    else
        pa_log_debug("failed to create resource set for '%s'", node->amname);

    return success;
}

static bool resource_set_create_all(struct userdata *u)
{
    uint32_t idx;
    mir_node *node;
    bool success;

    pa_assert(u);

    success = true;

    idx = PA_IDXSET_INVALID;

    while ((node = pa_nodeset_iterate_nodes(u, &idx))) {
        if ((node->implement == mir_stream && !node->loop) ||
            (node->implement == mir_device &&  node->loop)   )
        {
            if (!node->rsetid) {
                node->localrset = resource_set_create_node(u, node, NULL, false);
                success &= node->localrset;
            }
        }
    }

    return success;
}

static bool resource_set_destroy_node(struct userdata *u, uint32_t rsetid)
{
    pa_murphyif *murphyif;
    resource_interface *rif;
    mrp_msg_t *msg;
    uint16_t reqid;
    uint32_t seqno;
    uint32_t nodidx;
    bool success;

    pa_assert(u);

    pa_assert_se((murphyif = u->murphyif));
    rif = &murphyif->resource;

    reqid = RESPROTO_DESTROY_RESOURCE_SET;
    seqno = rif->seqno.request++;
    nodidx = PA_IDXSET_INVALID;
    msg = resource_create_request(seqno, reqid);

    if (PUSH_VALUE(msg, RESOURCE_SET_ID, UINT32, rsetid))
        success = resource_send_message(rif, msg, nodidx, reqid, seqno);
    else {
        success = false;
        mrp_msg_unref(msg);
    }

    return success;
}

static bool resource_set_destroy_all(struct userdata *u)
{
    pa_murphyif *murphyif;
    resource_interface *rif;
    uint32_t idx;
    mir_node *node;
    uint32_t rsetid;
    char *e;
    bool success;

    pa_assert(u);
    pa_assert_se((murphyif = u->murphyif));

    rif = &murphyif->resource;

    success = true;

    idx = PA_IDXSET_INVALID;

    while ((node = pa_nodeset_iterate_nodes(u, &idx))) {
        if (node->implement == mir_stream && node->localrset) {
            pa_log_debug("destroying resource set for '%s'", node->amname);

            if (rif->connected && node->rsetid) {
                rsetid = strtoul(node->rsetid, &e, 10);

                if (e == node->rsetid || *e)
                    success = false;
                else {
                    rset_hashmap_remove(u, node->rsetid, node);
                    success &= resource_set_destroy_node(u, rsetid);
                }
            }

            pa_xfree(node->rsetid);

            node->localrset = false;
            node->rsetid = NULL;
        }
    }

    return success;
}

static void resource_set_notification(struct userdata *u,
                                      const char *table,
                                      int nrow,
                                      mrp_domctl_value_t **values)
{
    pa_murphyif *murphyif;
    int r;
    mrp_domctl_value_t *row;
    mrp_domctl_value_t *crsetid;
    mrp_domctl_value_t *cautorel;
    mrp_domctl_value_t *cstate;
    mrp_domctl_value_t *cgrant;
    mrp_domctl_value_t *cpid;
    mrp_domctl_value_t *cpolicy;
    char rsetid[32];
    const char *pid;
    mir_node *node, **nodes;
    rset_hash *rh;
    rset_data rset, *rs;
    size_t i, size;

    pa_assert(u);
    pa_assert(table);

    pa_assert_se((murphyif = u->murphyif));

    for (r = 0;  r < nrow;  r++) {
        row = values[r];
        crsetid  =  row + RESCOL_RSETID;
        cautorel =  row + RESCOL_AUTOREL;
        cstate   =  row + RESCOL_STATE;
        cgrant   =  row + RESCOL_GRANT;
        cpid     =  row + RESCOL_PID;
        cpolicy  =  row + RESCOL_POLICY;

        if (crsetid->type  != MRP_DOMCTL_UNSIGNED ||
            cautorel->type != MRP_DOMCTL_INTEGER  ||
            cstate->type   != MRP_DOMCTL_INTEGER  ||
            cgrant->type   != MRP_DOMCTL_INTEGER  ||
            cpid->type     != MRP_DOMCTL_STRING   ||
            cpolicy->type  != MRP_DOMCTL_STRING    )
        {
            pa_log("invalid field type in '%s' (%d|%d|%d|%d|%d|%d)", table,
                   crsetid->type, cautorel->type, cstate->type,
                   cgrant->type, cpid->type, cpolicy->type);
            continue;
        }

        snprintf(rsetid, sizeof(rsetid), "%d", crsetid->s32);
        pid = cpid->str;

        rset.id      = rsetid;
        rset.autorel = cautorel->s32;
        rset.state   = cstate->s32;
        rset.grant   = cgrant->s32;
        rset.policy  = cpolicy->str;


        if (rset.autorel < 0 || rset.autorel > 1) {
            pa_log_debug("invalid autorel %d in table '%s'",
                         rset.autorel, table);
            continue;
        }
        if (rset.state != RSET_RELEASE && rset.state != RSET_ACQUIRE) {
            pa_log_debug("invalid state %d in table '%s'", rset.state, table);
            continue;
        }
        if (rset.grant < 0 || rset.grant > 1) {
            pa_log_debug("invalid grant %d in table '%s'", rset.grant, table);
            continue;
        }

        if (!(rh = rset_hashmap_get(u, rset.id))) {
            if (!pid) {
                pa_log_debug("can't find node for resource set %s "
                             "(pid in resource set unknown)", rset.id);
                continue;
            }

            if ((node = pid_hashmap_remove_node(u, pid))) {
                pa_log_debug("found node %s for resource-set '%s'",
                             node->amname, rset.id);

                if (!(rh = node_put_rset(u, node, &rset))) {
                    pa_log("can't register resource set for node '%s': "
                           "failed to set rsetid", node->amname);
                    continue;
                }
            }
            else {
                if (pid_hashmap_put(u, pid, NULL, rset_data_dup(&rset)) < 0) {
                    if (!(rs = pid_hashmap_get_rset(u, pid)))
                        pa_log("failed to add resource set to pid hash");
                    else {
                        if (!pa_streq(rs->id, rset.id)) {
                            pa_log("process %s appears to have multiple resour"
                                   "ce sets (%s and %s)", pid, rs->id,rset.id);
                        }
                        pa_log_debug("update resource-set %s data in "
                                     "pid hash (pid %s)", rs->id, pid);
                        rset_data_copy(rs, &rset);
                    }
                }
                else {
                    pa_log_debug("can't find node for resource set %s. "
                                 "Beleive the stream will appear later on",
                                 rset.id);
                }

                continue;
            }
        }

        rset_data_update(rh->rset, &rset);

        /* we need to make a copy of this as node_enforce_resource_policy()
           will delete/modify it */
        size = sizeof(mir_node *) * (rh->nnode + 1);
        nodes = alloca(size);
        memcpy(nodes, rh->nodes, size);

        for (i = 0;  (node = nodes[i]);  i++) {
            pa_log_debug("%zu: resource notification for node '%s' autorel:%s "
                         "state:%s grant:%s pid:%s policy:%s", i,
                         node->amname, rset.autorel ? "yes":"no",
                         rset.state == RSET_ACQUIRE ? "acquire":"release",
                         rset.grant ? "yes":"no", pid, rset.policy);

            node_enforce_resource_policy(u, node, &rset);
        }
    }
}


static bool resource_push_attributes(mrp_msg_t *msg,
                                          resource_interface *rif,
                                          pa_proplist *proplist)
{
    resource_attribute *attr;
    union {
        const void *ptr;
        const char *str;
        int32_t    *i32;
        uint32_t   *u32;
        double     *dbl;
    } v;
    size_t size;
    int sts;

    pa_assert(msg);
    pa_assert(rif);

    PA_LLIST_FOREACH(attr, rif->attrs) {
        if (!PUSH_VALUE(msg, ATTRIBUTE_NAME, STRING, attr->def.name))
            return false;

        if (proplist)
            sts = pa_proplist_get(proplist, attr->prop, &v.ptr, &size);
        else
            sts = -1;

        switch (attr->def.type) {
        case mqi_string:
            if (sts < 0)
                v.str = attr->def.value.string;
            else if (v.str[size-1] != '\0' || strlen(v.str) != (size-1) ||
                     !pa_utf8_valid(v.str))
                return false;
            if (!PUSH_VALUE(msg, ATTRIBUTE_VALUE, STRING, v.str))
                return false;
            break;

        case mqi_integer:
            if (sts < 0)
                v.i32 = &attr->def.value.integer;
            else if (size != sizeof(*v.i32))
                return false;
            if (!PUSH_VALUE(msg, ATTRIBUTE_VALUE, SINT8, *v.i32))
                return false;
            break;

        case mqi_unsignd:
            if (sts < 0)
                v.u32 = &attr->def.value.unsignd;
            else if (size != sizeof(*v.u32))
                return false;
            if (!PUSH_VALUE(msg, ATTRIBUTE_VALUE, SINT8, *v.u32))
                return false;
            break;

        case mqi_floating:
            if (sts < 0)
                v.dbl = &attr->def.value.floating;
            else if (size != sizeof(*v.dbl))
                return false;
            if (!PUSH_VALUE(msg, ATTRIBUTE_VALUE, SINT8, *v.dbl))
                return false;
            break;

        default: /* we should never get here */
            return false;
        }
    }

    return true;
}



static void resource_recv_msg(mrp_transport_t *t, mrp_msg_t *msg, void *void_u)
{
    return resource_recvfrom_msg(t, msg, NULL, 0, void_u);
}

static void resource_recvfrom_msg(mrp_transport_t *transp, mrp_msg_t *msg,
                                  mrp_sockaddr_t *addr, socklen_t addrlen,
                                  void *void_u)
{
    struct userdata *u = (struct userdata *)void_u;
    pa_core *core;
    pa_murphyif *murphyif;
    resource_interface *rif;
    void     *curs = NULL;
    uint32_t  seqno;
    uint16_t  reqid;
    uint32_t  nodidx;
    resource_request *req, *n;
    mir_node *node;

    MRP_UNUSED(transp);
    MRP_UNUSED(addr);
    MRP_UNUSED(addrlen);

    pa_assert(u);
    pa_assert_se((core = u->core));
    pa_assert_se((murphyif = u->murphyif));

    rif = &murphyif->resource;

    if (!resource_fetch_seqno   (msg, &curs, &seqno) ||
        !resource_fetch_request (msg, &curs, &reqid)   )
    {
        pa_log("ignoring malformed message");
        return;
    }

    PA_LLIST_FOREACH_SAFE(req, n, rif->reqs) {
        if (req->seqno <= seqno) {
            nodidx = req->nodidx;

            if (req->reqid == reqid) {
                PA_LLIST_REMOVE(resource_request, rif->reqs, req);
                pa_xfree(req);
            }

            if (!(node = mir_node_find_by_index(u, nodidx))) {
                if (reqid != RESPROTO_DESTROY_RESOURCE_SET) {
                    pa_log("got response (reqid:%u seqno:%u) but can't "
                           "find the corresponding node", reqid, seqno);
                    resource_set_create_response_abort(u, msg, &curs);
                }
            }
            else {
                if (req->seqno < seqno) {
                    pa_log("unanswered request %d", req->seqno);
                }
                else {
                    pa_log_debug("got response (reqid:%u seqno:%u "
                                 "node:'%s')", reqid, seqno,
                                 node ? node->amname : "<unknown>");

                    switch (reqid) {
                    case RESPROTO_CREATE_RESOURCE_SET:
                        resource_set_create_response(u, node, msg, &curs);
                        break;
                    case RESPROTO_DESTROY_RESOURCE_SET:
                        break;
                    default:
                        pa_log("ignoring unsupported resource request "
                               "type %u", reqid);
                        break;
                    }
                }
            }
        } /* PA_LLIST_FOREACH_SAFE */
    }
}


static void resource_set_create_response(struct userdata *u, mir_node *node,
                                         mrp_msg_t *msg, void **pcursor)
{
    int status;
    uint32_t rsetid;
    char buf[4096];

    pa_assert(u);
    pa_assert(node);
    pa_assert(msg);
    pa_assert(pcursor);

    if (!resource_fetch_status(msg, pcursor, &status) || (status == 0 &&
        !resource_fetch_rset_id(msg, pcursor, &rsetid)))
    {
        pa_log("ignoring malformed response to resource set creation");
        return;
    }

    if (status) {
        pa_log("creation of resource set failed. error code %u", status);
        return;
    }

    node->rsetid = pa_sprintf_malloc("%d", rsetid);

    if (pa_murphyif_add_node(u, node) == 0) {
        pa_log_debug("resource set was successfully created");
        mir_node_print(node, buf, sizeof(buf));
        pa_log_debug("modified node:\n%s", buf);
    }
    else {
        pa_log("failed to create resource set: "
                   "conflicting resource set id");
    }
}

static void resource_set_create_response_abort(struct userdata *u,
                                               mrp_msg_t *msg, void **pcursor)
{
    int status;
    uint32_t rsetid;

    pa_assert(u);
    pa_assert(msg);
    pa_assert(pcursor);

    if (!resource_fetch_status(msg, pcursor, &status) || (status == 0 &&
        !resource_fetch_rset_id(msg, pcursor, &rsetid)))
    {
        pa_log("ignoring malformed response to resource set creation");
        return;
    }

    if (status) {
        pa_log("creation of resource set failed. error code %u", status);
        return;
    }

    if (resource_set_destroy_node(u, rsetid))
        pa_log_debug("destroying resource set %u", rsetid);
    else
        pa_log("attempt to destroy resource set %u failed", rsetid);
}


static bool resource_fetch_seqno(mrp_msg_t *msg,
                                      void **pcursor,
                                      uint32_t *pseqno)
{
    uint16_t tag;
    uint16_t type;
    mrp_msg_value_t value;
    size_t size;

    if (!mrp_msg_iterate(msg, pcursor, &tag, &type, &value, &size) ||
        tag != RESPROTO_SEQUENCE_NO || type != MRP_MSG_FIELD_UINT32)
    {
        *pseqno = INVALID_SEQNO;
        return false;
    }

    *pseqno = value.u32;
    return true;
}


static bool resource_fetch_request(mrp_msg_t *msg,
                                        void **pcursor,
                                        uint16_t *preqtype)
{
    uint16_t tag;
    uint16_t type;
    mrp_msg_value_t value;
    size_t size;

    if (!mrp_msg_iterate(msg, pcursor, &tag, &type, &value, &size) ||
        tag != RESPROTO_REQUEST_TYPE || type != MRP_MSG_FIELD_UINT16)
    {
        *preqtype = INVALID_REQUEST;
        return false;
    }

    *preqtype = value.u16;
    return true;
}

static bool resource_fetch_status(mrp_msg_t *msg,
                                       void **pcursor,
                                       int *pstatus)
{
    uint16_t tag;
    uint16_t type;
    mrp_msg_value_t value;
    size_t size;

    if (!mrp_msg_iterate(msg, pcursor, &tag, &type, &value, &size) ||
        tag != RESPROTO_REQUEST_STATUS || type != MRP_MSG_FIELD_SINT16)
    {
        *pstatus = EINVAL;
        return false;
    }

    *pstatus = value.s16;
    return true;
}

static bool resource_fetch_rset_id(mrp_msg_t *msg,
                                        void **pcursor,
                                        uint32_t *pid)
{
    uint16_t tag;
    uint16_t type;
    mrp_msg_value_t value;
    size_t size;

    if (!mrp_msg_iterate(msg, pcursor, &tag, &type, &value, &size) ||
        tag != RESPROTO_RESOURCE_SET_ID || type != MRP_MSG_FIELD_UINT32)
    {
        *pid = INVALID_ID;
        return false;
    }

    *pid = value.u32;
    return true;
}

#if 0
static bool resource_fetch_rset_state(mrp_msg_t *msg,
                                           void **pcursor,
                                           mrp_resproto_state_t *pstate)
{
    uint16_t tag;
    uint16_t type;
    mrp_msg_value_t value;
    size_t size;

    if (!mrp_msg_iterate(msg, pcursor, &tag, &type, &value, &size) ||
        tag != RESPROTO_RESOURCE_STATE || type != MRP_MSG_FIELD_UINT16)
    {
        *pstate = 0;
        return false;
    }

    *pstate = value.u16;
    return true;
}

static bool resource_fetch_rset_mask(mrp_msg_t *msg,
                                          void **pcursor,
                                          mrp_resproto_state_t *pmask)
{
    uint16_t tag;
    uint16_t type;
    mrp_msg_value_t value;
    size_t size;

    if (!mrp_msg_iterate(msg, pcursor, &tag, &type, &value, &size) ||
        tag != RESPROTO_RESOURCE_GRANT || type != MRP_MSG_FIELD_UINT32)
    {
        *pmask = 0;
        return false;
    }

    *pmask = value.u32;
    return true;
}
#endif

static bool resource_transport_create(struct userdata *u,
                                           pa_murphyif *murphyif)
{
    static mrp_transport_evt_t ev = {
        { .recvmsg     = resource_recv_msg },
        { .recvmsgfrom = resource_recvfrom_msg },
        .closed        = resource_xport_closed_evt,
        .connection    = NULL
    };

    resource_interface *rif;

    pa_assert(u);
    pa_assert(murphyif);

    rif = &murphyif->resource;

    if (!rif->transp)
        rif->transp = mrp_transport_create(murphyif->ml, rif->atype, &ev, u,0);

    return rif->transp ? true : false;
}

static void resource_transport_destroy(pa_murphyif *murphyif)
{
    resource_interface *rif;

    pa_assert(murphyif);
    rif = &murphyif->resource;

    if (rif->transp)
        mrp_transport_destroy(rif->transp);

    rif->transp = NULL;
    rif->connected = false;
}

static void connect_attempt(pa_mainloop_api *a,
                             pa_time_event *e,
                             const struct timeval *t,
                             void *data)
{
    struct userdata *u = (struct userdata *)data;
    pa_murphyif *murphyif;
    resource_interface *rif;

    int state;

    pa_assert(u);
    pa_assert_se((murphyif = u->murphyif));

    rif = &murphyif->resource;

    if (!resource_transport_create(u, murphyif))
        schedule_connect(u, rif);
    else {
        state = resource_transport_connect(rif);

        switch (state) {

        case CONNECTING:
            resource_set_create_all(u);
            cancel_schedule(u, rif);
            break;

        case CONNECTED:
            cancel_schedule(u, rif);
            break;

        case DISCONNECTED:
            schedule_connect(u, rif);
            break;
        }
    }
}

static void schedule_connect(struct userdata *u, resource_interface *rif)
{
    pa_core *core;
    pa_mainloop_api *mainloop;
    struct timeval when;
    pa_time_event *tev;

    pa_assert(u);
    pa_assert(rif);
    pa_assert_se((core = u->core));
    pa_assert_se((mainloop = core->mainloop));

    pa_gettimeofday(&when);
    pa_timeval_add(&when, rif->connect.period);

    if ((tev = rif->connect.evt))
        mainloop->time_restart(tev, &when);
    else {
        rif->connect.evt = mainloop->time_new(mainloop, &when,
                                              connect_attempt, u);
    }
}

static void cancel_schedule(struct userdata *u, resource_interface *rif)
{
    pa_core *core;
    pa_mainloop_api *mainloop;
    pa_time_event *tev;

    pa_assert(u);
    pa_assert(rif);
    pa_assert_se((core = u->core));
    pa_assert_se((mainloop = core->mainloop));

    if ((tev = rif->connect.evt)) {
        mainloop->time_free(tev);
        rif->connect.evt = NULL;
    }
}

static rset_hash *node_put_rset(struct userdata *u, mir_node *node, rset_data *rset)
{
    pa_murphyif *murphyif;
    pa_proplist *pl;
    rset_hash *rh;

    pa_assert(u);
    pa_assert(node);
    pa_assert(rset);
    pa_assert(rset->id);

    pa_assert(node->implement == mir_stream);
    pa_assert(node->direction == mir_input || node->direction == mir_output);

    pa_assert_se((murphyif = u->murphyif));

    pa_log_debug("setting rsetid %s for node %s", rset->id, node->amname);

    if (node->rsetid) {
        pa_xfree(node->rsetid);
    }
    node->rsetid = pa_xstrdup(rset->id);

    if (!(pl = get_node_proplist(u, node))) {
        pa_log("can't obtain property list for node %s", node->amname);
        return NULL;
    }

    if ((pa_proplist_sets(pl, PA_PROP_RESOURCE_SET_ID, node->rsetid) < 0)) {
        pa_log("failed to set '" PA_PROP_RESOURCE_SET_ID "' property "
               "of '%s' node", node->amname);
        return NULL;
    }

    if (!(rh = rset_hashmap_put(u, node->rsetid, node))) {
        pa_log("conflicting rsetid %s for %s", node->rsetid, node->amname);
        return NULL;
    }

    return rh;
}

static void node_enforce_resource_policy(struct userdata *u,
                                         mir_node *node,
                                         rset_data *rset)
{
    int req;

    pa_assert(node);
    pa_assert(rset);
    pa_assert(rset->policy);


    if (pa_streq(rset->policy, "relaxed"))
        req = PA_STREAM_RUN;
    else if (pa_streq(rset->policy, "strict")) {
        if (rset->state == RSET_RELEASE && rset->autorel)
            req = PA_STREAM_KILL;
        else {
            if (rset->grant)
                req = PA_STREAM_RUN;
            else
                req = PA_STREAM_BLOCK;
        }
    }
    else {
        req = PA_STREAM_BLOCK;
    }

    pa_stream_state_change(u, node, req);
}

static rset_data *rset_data_dup(rset_data *orig)
{
    rset_data *dup;

    pa_assert(orig);
    pa_assert(orig->id);
    pa_assert(orig->policy);

    dup = pa_xnew0(rset_data, 1);

    dup->id      = pa_xstrdup(orig->id);
    dup->autorel = orig->autorel;
    dup->state   = orig->state;
    dup->grant   = orig->grant;
    dup->policy  = pa_xstrdup(orig->policy);

    return dup;
}

static void rset_data_copy(rset_data *dst, rset_data *src)
{
    pa_assert(dst);
    pa_assert(src);
    pa_assert(src->id);
    pa_assert(src->policy);

    pa_xfree((void *)dst->id);
    pa_xfree((void *)dst->policy);

    dst->id      = pa_xstrdup(src->id);
    dst->autorel = src->autorel;
    dst->state   = src->state;
    dst->grant   = src->grant;
    dst->policy  = pa_xstrdup(src->policy);
}


static void rset_data_update(rset_data *dst, rset_data *src)
{
    pa_assert(dst);
    pa_assert(dst->id);
    pa_assert(src);
    pa_assert(src->id);
    pa_assert(src->policy);

    pa_assert_se(pa_streq(src->id, dst->id));

    pa_xfree((void *)dst->policy);

    dst->autorel = src->autorel;
    dst->state   = src->state;
    dst->grant   = src->grant;
    dst->policy  = pa_xstrdup(src->policy);
}


static void rset_data_free(rset_data *rset)
{
    if (rset) {
        pa_xfree((void *)rset->id);
        pa_xfree((void *)rset->policy);
        pa_xfree(rset);
    }
}

static void pid_hashmap_free(void *p, void *userdata)
{
    pid_hash *ph = (pid_hash *)p;

    (void)userdata;

    if (ph) {
        pa_xfree((void *)ph->pid);
        rset_data_free(ph->rset);
        pa_xfree(ph);
    }
}

static int pid_hashmap_put(struct userdata *u, const char *pid,
                           mir_node *node, rset_data *rset)
{
    pa_murphyif *murphyif;
    resource_interface *rif;
    pid_hash *ph;

    pa_assert(u);
    pa_assert(pid);
    pa_assert(node || rset);
    pa_assert_se((murphyif = u->murphyif));

    rif = &murphyif->resource;

    ph = pa_xnew0(pid_hash, 1);
    ph->pid = pa_xstrdup(pid);
    ph->node = node;
    ph->rset = rset;

    if (pa_hashmap_put(rif->nodes.pid, (void *)ph->pid, ph) == 0)
        return 0;
    else
        pid_hashmap_free(ph, NULL);

    return -1;
}

static mir_node *pid_hashmap_get_node(struct userdata *u, const char *pid)
{
    pa_murphyif *murphyif;
    resource_interface *rif;
    pid_hash *ph;

    pa_assert(u);
    pa_assert(pid);
    pa_assert(murphyif = u->murphyif);

    rif = &murphyif->resource;

    if ((ph = pa_hashmap_get(rif->nodes.pid, pid)))
        return ph->node;

    return NULL;
}

static rset_data *pid_hashmap_get_rset(struct userdata *u, const char *pid)
{
    pa_murphyif *murphyif;
    resource_interface *rif;
    pid_hash *ph;

    pa_assert(u);
    pa_assert(pid);
    pa_assert(murphyif = u->murphyif);

    rif = &murphyif->resource;

    if ((ph = pa_hashmap_get(rif->nodes.pid, pid)))
        return ph->rset;

    return NULL;
}

static mir_node *pid_hashmap_remove_node(struct userdata *u, const char *pid)
{
    pa_murphyif *murphyif;
    resource_interface *rif;
    mir_node *node;
    pid_hash *ph;

    pa_assert(u);
    pa_assert_se((murphyif = u->murphyif));

    rif = &murphyif->resource;

    if (!(ph = pa_hashmap_remove(rif->nodes.pid, pid)))
        node = NULL;
    else if (!(node = ph->node))
        pa_hashmap_put(rif->nodes.pid, (void *)ph->pid, ph);
    else
        pid_hashmap_free(ph, NULL);

    return node;
}

static rset_data *pid_hashmap_remove_rset(struct userdata *u, const char *pid)
{
    pa_murphyif *murphyif;
    resource_interface *rif;
    rset_data *rset;
    pid_hash *ph;

    pa_assert(u);
    pa_assert(pid);

    pa_assert_se((murphyif = u->murphyif));

    rif = &murphyif->resource;

    if (!(ph = pa_hashmap_remove(rif->nodes.pid, pid)))
        rset = NULL;
    else if (!(rset = ph->rset))
        pa_hashmap_put(rif->nodes.pid, (void *)ph->pid, ph);
    else {
        ph->rset = NULL;
        pid_hashmap_free(ph, NULL);
    }

    return rset;
}


static void rset_hashmap_free(void *r, void *userdata)
{
    rset_hash *rh = (rset_hash *)r;

    (void)userdata;

    if (rh) {
        pa_xfree(rh->nodes);
        rset_data_free(rh->rset);
        pa_xfree(rh);
    }
}

static rset_hash *rset_hashmap_put(struct userdata *u,
                                   const char *rsetid,
                                   mir_node *node)
{
    pa_murphyif *murphyif;
    resource_interface *rif;
    rset_hash *rh;
    rset_data *rset;
    size_t i;

    pa_assert(u);
    pa_assert(rsetid);
    pa_assert(node);
    pa_assert_se((murphyif = u->murphyif));

    rif = &murphyif->resource;

    if ((rh = pa_hashmap_get(rif->nodes.rsetid, rsetid))) {
        for (i = 0;  i < rh->nnode;  i++) {
            if (rh->nodes[i] == node)
                return NULL;
        }

        i = rh->nnode++;
        rh->nodes = pa_xrealloc(rh->nodes, sizeof(mir_node *) * (rh->nnode+1));
    }
    else {
        rset = pa_xnew0(rset_data, 1);

        rset->id = pa_xstrdup(rsetid);
        rset->policy = pa_xstrdup("unknown");

        rh = pa_xnew0(rset_hash, 1);

        rh->nnode = 1;
        rh->nodes = pa_xnew0(mir_node *, 2);
        rh->rset  = rset;

        pa_hashmap_put(rif->nodes.rsetid, (void *)rh->rset->id, rh);

        i = 0;
    }


    rh->nodes[i+0] = node;
    rh->nodes[i+1] = NULL;

    return rh;
}

static rset_hash *rset_hashmap_get(struct userdata *u, const char *rsetid)
{
    pa_murphyif *murphyif;
    resource_interface *rif;
    rset_hash *rh;

    pa_assert(u);
    pa_assert(rsetid);
    pa_assert(murphyif = u->murphyif);

    rif = &murphyif->resource;

    if ((rh = pa_hashmap_get(rif->nodes.rsetid, rsetid)))
        return rh;

    return NULL;
}

static int rset_hashmap_remove(struct userdata *u,
                               const char *rsetid,
                               mir_node *node)
{
    pa_murphyif *murphyif;
    resource_interface *rif;
    rset_hash *rh;
    size_t i,j;

    pa_assert(u);
    pa_assert_se((murphyif = u->murphyif));

    rif = &murphyif->resource;

    if ((rh = pa_hashmap_get(rif->nodes.rsetid, rsetid))) {

        for (i = 0;  i < rh->nnode;  i++) {
            if (node == rh->nodes[i]) {
                if (rh->nnode <= 1) {
                    pa_hashmap_remove(rif->nodes.rsetid, rsetid);
                    rset_hashmap_free(rh, NULL);
                    return 0;
                }
                else {
                    for (j = i;  j < rh->nnode;  j++)
                        rh->nodes[j] = rh->nodes[j+1];

                    rh->nnode--;

                    return 0;
                }
            }
        }
    }

    return -1;
}

#endif

static pa_proplist *get_node_proplist(struct userdata *u, mir_node *node)
{
    pa_core *core;
    pa_sink_input *i;
    pa_source_output *o;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((core = u->core));

    if (node->implement == mir_stream && node->paidx != PA_IDXSET_INVALID) {
        if (node->direction == mir_input) {
            if ((i = pa_idxset_get_by_index(core->sink_inputs, node->paidx)))
                return i->proplist;
        }
        else if (node->direction == mir_output) {
            if ((o = pa_idxset_get_by_index(core->source_outputs,node->paidx)))
                return o->proplist;
        }
    }

    return NULL;
}

static const char *get_node_pid(struct userdata *u, mir_node *node)
{
    pa_proplist *pl;

    pa_assert(u);

    if (node && (pl = get_node_proplist(u, node)))
        return pa_proplist_gets(pl, PA_PROP_APPLICATION_PROCESS_ID);

    return NULL;
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
