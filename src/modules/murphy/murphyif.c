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
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <alloca.h>
#include <errno.h>

#include <pulse/utf8.h>
#include <pulse/timeval.h>
#include <pulsecore/module.h>
#include <pulsecore/llist.h>
#include <pulsecore/idxset.h>
#include <pulsecore/hashmap.h>
#include <pulsecore/core-util.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>

#ifdef HAVE_MURPHY
#include <murphy/config.h>
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
#include "resource.h"
#include "node.h"
#include "stream-state.h"
#include "fader.h"
#include "utils.h"

#ifdef WITH_RESOURCES
#define INVALID_ID       (~(uint32_t)0)
#define INVALID_INDEX    (~(uint32_t)0)
#define INVALID_SEQNO    (~(uint32_t)0)
#define INVALID_REQUEST  (~(uint16_t)0)

#define DISCONNECTED    -1
#define CONNECTED        0
#define CONNECTING       1

#define RESCOL_NAMES     "rsetid,autorel,state,grant,pid,policy,name"
#define RESCOL_RSETID    0
#define RESCOL_AUTOREL   1
#define RESCOL_STATE     2
#define RESCOL_GRANT     3
#define RESCOL_PID       4
#define RESCOL_POLICY    5
#define RESCOL_RSETNAME  6

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
    const char *tblnam;
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
static bool       resource_send_message(resource_interface *, mrp_msg_t *,
                                        uint32_t, uint16_t, uint32_t);
static bool       resource_set_create_node(struct userdata *, mir_node *,
                                           pa_nodeset_resdef *, bool);
static bool       resource_set_create_all(struct userdata *);
static bool       resource_set_destroy_node(struct userdata *, uint32_t);
static bool       resource_set_destroy_all(struct userdata *);
static void       resource_set_notification(struct userdata *, const char *,
                                            int, mrp_domctl_value_t **);

static bool  resource_push_attributes(mrp_msg_t *, resource_interface *,
                                           pa_proplist *);

static void  resource_recv_msg(mrp_transport_t *, mrp_msg_t *, void *);
static void  resource_recvfrom_msg(mrp_transport_t *, mrp_msg_t *,
                                   mrp_sockaddr_t *, socklen_t, void *);
static void  resource_set_create_response(struct userdata *, mir_node *,
                                          mrp_msg_t *, void **);
static void  resource_set_create_response_abort(struct userdata *,
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

#endif

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

    murphyif = pa_xnew0(pa_murphyif, 1);
    dif = &murphyif->domctl;
    rif = &murphyif->resource;

#if defined(WITH_DOMCTL) || defined(WITH_RESOURCES)
    murphyif->ml = ml;
#endif

    dif->addr = pa_xstrdup(ctl_addr ? ctl_addr:MRP_DEFAULT_DOMCTL_ADDRESS);

    rif->addr = pa_xstrdup(res_addr ? res_addr:RESPROTO_DEFAULT_ADDRESS);
#ifdef WITH_RESOURCES
    rif->alen = mrp_transport_resolve(NULL, rif->addr, &rif->saddr,
                                      sizeof(rif->saddr), &rif->atype);
    if (rif->alen <= 0) {
        pa_log_debug("can't resolve resource transport address '%s'", rif->addr);
    }
    else {
        rif->inpres.tblidx = -1;
        rif->outres.tblidx = -1;
        rif->connect.period = 1 * PA_USEC_PER_SEC;

        if (!resource_transport_create(u, murphyif)) {
            pa_log_debug("failed to create resource transport");
            schedule_connect(u, rif);
        }
        else {
            if (resource_transport_connect(rif) == DISCONNECTED)
                schedule_connect(u, rif);
        }
    }

    rif->seqno.request = 1;
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
    int idx;

    pa_assert(u);
    pa_assert(table);
    pa_assert(columns);
    pa_assert_se((murphyif = u->murphyif));

    dif = &murphyif->domctl;

    idx = dif->ntable++;
    size = sizeof(mrp_domctl_table_t) * (size_t)(dif->ntable);
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
    int idx;

    pa_assert(u);
    pa_assert(table);
    pa_assert(columns);
    pa_assert(max_rows > 0 && max_rows < MQI_QUERY_RESULT_MAX);
    pa_assert_se((murphyif = u->murphyif));

    dif = &murphyif->domctl;

    idx = dif->nwatch++;
    size = sizeof(mrp_domctl_watch_t) * (size_t)(dif->nwatch);
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
            pa_log_debug("failed to create '%s' domain controller", name);
            return;
        }

        if (!mrp_domctl_connect(dif->ctl, dif->addr, 0)) {
            pa_log_debug("failed to conect to murphyd");
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
            pa_log_debug("attempt to register playback resource multiple time");
        else
            res = &rif->inpres;
    }
    else {
        if (rif->outres.name)
            pa_log_debug("attempt to register recording resource multiple time");
        else
            res = &rif->outres;
    }

    if (res) {
        res->name = pa_xstrdup(name);
#ifdef WITH_DOMCTL
        snprintf(table, sizeof(table), "%s_users", name);
        res->tblnam = pa_xstrdup(table);
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
    pa_assert(!node->rset.id);

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

    if (node->localrset && node->rset.id) {

        pa_murphyif_delete_node(u, node);

        rsetid = strtoul(node->rset.id, &e, 10);

        if (e == node->rset.id || *e) {
            pa_log_debug("can't destroy resource set: invalid rsetid '%s'",
                         node->rset.id);
        }
        else {
            if (resource_set_destroy_node(u, rsetid))
                pa_log_debug("sent resource set %u destruction request", rsetid);
            else {
                pa_log_debug("failed to destroy resourse set %u for node '%s'",
                             rsetid, node->amname);
            }

            pa_xfree(node->rset.id);

            node->localrset = false;
            node->rset.id = NULL;
        }
    }
}

int pa_murphyif_add_node(struct userdata *u, mir_node *node)
{
#ifdef WITH_RESOURCES
    pa_murphyif *murphyif;
    char *id;
    char *name;
    int type;

    pa_assert(u);
    pa_assert(node);

    pa_assert_se((murphyif = u->murphyif));

    if (!node->rset.id) {
        pa_log_debug("can't register resource set for node %u '%s'.: missing rsetid",
                     node->paidx, node->amname);
    }
    else if (pa_streq(node->rset.id, PA_RESOURCE_SET_ID_PID)) {
    }
    else {
        if (node->rset.id[0] == '#') {
            name = node->rset.id;
            id = NULL;
        }
        else {
            name = NULL;
            id = node->rset.id;
        }


        if (pa_resource_stream_update(u, name, id, node) == 0) {
            type = (node->direction == mir_input) ?
                       PA_RESOURCE_PLAYBACK : PA_RESOURCE_RECORDING;
            pa_resource_enforce_policies(u, type);
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

    pa_assert(u);
    pa_assert(node);

    pa_assert_se((murphyif = u->murphyif));

    if (node->rset.id) {
        if (pa_streq(node->rset.id, PA_RESOURCE_SET_ID_PID)) {
        }
        else {
            pa_resource_stream_remove(u, node);
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
    int                 n;
    int                 l;

    pa_log_debug("Table #%d: %d rows x %d columns", table->id,
                 table->nrow, table->ncolumn);

    for (i = 0; i < table->nrow; i++) {
        row = table->rows[i];
        p   = buf;
        n   = sizeof(buf);

        for (j = 0, t = ""; j < table->ncolumn; j++, t = ", ") {
            switch (row[j].type) {
            case MRP_DOMCTL_STRING:
                l  = snprintf(p, (size_t)n, "%s'%s'", t, row[j].str);
                p += l;
                n -= l;
                break;
            case MRP_DOMCTL_INTEGER:
                l  = snprintf(p, (size_t)n, "%s%d", t, row[j].s32);
                p += l;
                n -= l;
                break;
            case MRP_DOMCTL_UNSIGNED:
                l  = snprintf(p, (size_t)n, "%s%u", t, row[j].u32);
                p += l;
                n -= l;
                break;
            case MRP_DOMCTL_DOUBLE:
                l  = snprintf(p, (size_t)n, "%s%f", t, row[j].dbl);
                p += l;
                n -= l;
                break;
            default:
                l  = snprintf(p, (size_t)n, "%s<invalid column 0x%x>",
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
        pa_log_debug("Resource transport connection closed by peer");
    else {
        pa_log_debug("Resource transport connection closed with error %d (%s)",
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
        pa_log_debug("can't to create new resource message");

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
        pa_log_debug("failed to send resource message");
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
    pa_assert(!node->rset.id);

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
            if (!node->rset.id) {
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

            if (rif->connected && node->rset.id) {
                rsetid = strtoul(node->rset.id, &e, 10);

                if (e == node->rset.id || *e)
                    success = false;
                else {
                    pa_resource_rset_remove(u, NULL, node->rset.id);
                    success &= resource_set_destroy_node(u, rsetid);
                }
            }

            pa_xfree(node->rset.id);

            node->localrset = false;
            node->rset.id = NULL;
        }
    }

    return success;
}

static void resource_set_notification(struct userdata *u,
                                      const char *table,
                                      int nrow,
                                      mrp_domctl_value_t **values)
{
    static uint32_t updid;

    pa_murphyif *murphyif;
    resource_interface *rif;
    int type;
    int r;
    mrp_domctl_value_t *row;
    mrp_domctl_value_t *crsetid;
    mrp_domctl_value_t *cautorel;
    mrp_domctl_value_t *cstate;
    mrp_domctl_value_t *cgrant;
    mrp_domctl_value_t *cpid;
    mrp_domctl_value_t *cpolicy;
    mrp_domctl_value_t *crsetname;
    char rsetid[32];
    char name[256];
    pa_resource_rset_data rset;
    unsigned nrset;

    pa_assert(u);
    pa_assert(table);

    pa_assert_se((murphyif = u->murphyif));
    rif = &murphyif->resource;

    if (pa_streq(table, rif->inpres.tblnam))
        type = PA_RESOURCE_PLAYBACK;
    else if (pa_streq(table, rif->outres.tblnam))
        type = PA_RESOURCE_RECORDING;
    else {
        pa_log_debug("ignoring unregistered table '%s'", table);
        return;
    }

    updid++;

    for (r = 0, nrset = 0;  r < nrow;  r++) {
        row = values[r];
        crsetid    =  row + RESCOL_RSETID;
        cautorel   =  row + RESCOL_AUTOREL;
        cstate     =  row + RESCOL_STATE;
        cgrant     =  row + RESCOL_GRANT;
        cpid       =  row + RESCOL_PID;
        cpolicy    =  row + RESCOL_POLICY;
        crsetname  =  row + RESCOL_RSETNAME;

        if (crsetid->type   != MRP_DOMCTL_UNSIGNED ||
            cautorel->type  != MRP_DOMCTL_INTEGER  ||
            cstate->type    != MRP_DOMCTL_INTEGER  ||
            cgrant->type    != MRP_DOMCTL_INTEGER  ||
            cpid->type      != MRP_DOMCTL_STRING   ||
            cpolicy->type   != MRP_DOMCTL_STRING   ||
            crsetname->type != MRP_DOMCTL_STRING    )
        {
            pa_log_debug("invalid field type in '%s' (%d|%d|%d|%d|%d|%d)", table,
                         crsetid->type, cautorel->type, cstate->type,
                         cgrant->type, cpid->type, cpolicy->type);
            continue;
        }

        snprintf(rsetid, sizeof(rsetid), "%d" , crsetid->s32);
        if (crsetname->str[0] && !pa_streq(crsetname->str, "<unknown>"))
            snprintf(name,  sizeof(name),  "#%s", crsetname->str);
        else
            name[0] = 0;

        memset(&rset, 0, sizeof(rset));
        rset.id      = rsetid;
        rset.autorel = cautorel->s32;
        rset.state   = cstate->s32;
        rset.name    = name;
        rset.pid     = (char *)cpid->str;

        rset.policy[type] = (char *)cpolicy->str;
        rset.grant[type]  = cgrant->s32;

        pa_log_debug("cgrant is %d and grant type is %s",
                     cgrant->s32, rset.grant[type] ? "yes" : "no");

        if (cautorel->s32 < 0 || cautorel->s32 > 1) {
            pa_log_debug("invalid autorel %d in table '%s'",
                         cautorel->s32, table);
            continue;
        }
        if (rset.state != PA_RESOURCE_RELEASE && rset.state != PA_RESOURCE_ACQUIRE) {
            pa_log_debug("invalid state %d in table '%s'", rset.state, table);
            continue;
        }
        if (cgrant->s32 < 0 || cgrant->s32 > 1) {
            pa_log_debug("invalid grant %d in table '%s'", cgrant->s32, table);
            continue;
        }
        if (!rset.policy[type]) {
            pa_log_debug("invalid 'policy' string in table '%s'", table);
            continue;
        }

        pa_resource_rset_update(u, rset.name, rset.id, type, &rset, updid);

    } /* for each row */

    pa_log_debug("*** nrset=%u pa_resource_get_number_of_resources()=%u",
                 nrset, pa_resource_get_number_of_resources(u, type));

    // if (nrset != pa_resource_get_number_of_resources(u, type))
        pa_resource_purge(u, updid, type);

    pa_resource_enforce_policies(u, type);
    pa_fader_apply_volume_limits(u, pa_utils_get_stamp());
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
        pa_log_debug("ignoring malformed message");
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
                    pa_log_debug("got response (reqid:%u seqno:%u) but can't "
                                 "find the corresponding node", reqid, seqno);
                    resource_set_create_response_abort(u, msg, &curs);
                }
            }
            else {
                if (req->seqno < seqno) {
                    pa_log_debug("unanswered request %d", req->seqno);
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
                        pa_log_debug("ignoring unsupported resource request "
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
        pa_log_debug("ignoring malformed response to resource set creation");
        return;
    }

    if (status) {
        pa_log_debug("creation of resource set failed. error code %u", status);
        return;
    }

    node->rset.id = pa_sprintf_malloc("%d", rsetid);

    if (pa_murphyif_add_node(u, node) == 0) {
        pa_log_debug("resource set was successfully created");
        mir_node_print(node, buf, sizeof(buf));
        pa_log_debug("modified node:\n%s", buf);
    }
    else {
        pa_log_debug("failed to create resource set: "
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
        pa_log_debug("ignoring malformed response to resource set creation");
        return;
    }

    if (status) {
        pa_log_debug("creation of resource set failed. error code %u", status);
        return;
    }

    if (resource_set_destroy_node(u, rsetid))
        pa_log_debug("destroying resource set %u", rsetid);
    else
        pa_log_debug("attempt to destroy resource set %u failed", rsetid);
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
        *preqtype = (uint16_t)INVALID_REQUEST;
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

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
