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
#include <pulsecore/core-util.h>
#include <pulsecore/module.h>

#include "constrain.h"
#include "router.h"
#include "node.h"


static mir_constr_def *cstrdef_create(struct userdata *, const char *,
                                      mir_constrain_func_t, const char *);
static void cstrdef_destroy(struct userdata *, mir_constr_def *);

static mir_constr_link *cstrlink_create(struct userdata *, mir_constr_def *,
                                        mir_node *);
static void cstrlink_destroy(struct userdata *, mir_constr_link *);

pa_constrain *pa_constrain_init(struct userdata *u)
{
    pa_constrain *constrain = pa_xnew0(pa_constrain, 1);

    constrain->defs = pa_hashmap_new(pa_idxset_string_hash_func,
                                     pa_idxset_string_compare_func);
    return constrain;
}

void pa_constrain_done(struct userdata *u)
{
    pa_constrain   *constrain;
    mir_constr_def *cd;
    void *state;

    if (u && (constrain = u->constrain)) {
        PA_HASHMAP_FOREACH(cd, constrain->defs, state) {
            cstrdef_destroy(u, cd);
        }

        pa_hashmap_free(constrain->defs);

        pa_xfree(constrain);

        u->constrain = NULL;
    }
}


mir_constr_def *mir_constrain_create(struct userdata *u, const char *name,
                                     mir_constrain_func_t func,
                                     const char *key)
{
    pa_constrain *constrain;
    mir_constr_def *cd;

    pa_assert(u);
    pa_assert(name);
    pa_assert(func);
    pa_assert(key);
    pa_assert_se((constrain = u->constrain));

    if ((cd = mir_constrain_find(u, key))) {
        if (pa_streq(name, cd->name) && func == cd->func)
            return cd;
        else {
            pa_log_debug("attempt to redefine constrain %s/%s => %s/%s",
                         cd->name,cd->key, name,key);
            return NULL;
        }
    }

    cd = cstrdef_create(u, name, func, key);

    if (pa_hashmap_put(constrain->defs, cd->key, cd) < 0) {
        pa_xfree(cd->key);
        pa_xfree(cd->name);
        pa_xfree(cd);
        return NULL;
    }

    pa_log_debug("constrain %s/%s created", cd->name, cd->key);

    return cd;
}

void mir_constrain_destroy(struct userdata *u, const char *key)
{
    pa_constrain *constrain;
    mir_constr_def *cd;

    pa_assert(u);
    pa_assert(key);
    pa_assert_se((constrain = u->constrain));

    if ((cd = pa_hashmap_remove(constrain->defs, key))) {
        pa_log_debug("destroying constrain %s/%s", cd->name, cd->key);
        cstrdef_destroy(u, cd);
    }
}

mir_constr_def *mir_constrain_find(struct userdata *u, const char *key)
{
    pa_constrain *constrain;
    mir_constr_def *cd;

    pa_assert(u);
    pa_assert(key);
    pa_assert_se((constrain = u->constrain));

    cd = pa_hashmap_get(constrain->defs, key);

    return cd;
}


void mir_constrain_add_node(struct userdata *u,
                            mir_constr_def  *cd,
                            mir_node        *node)
{
    mir_constr_link *cl;

    pa_assert(u);
    pa_assert(node);

    if (cd) {
        cl = cstrlink_create(u, cd, node);

        MIR_DLIST_APPEND(mir_constr_link, link, cl, &cd->nodes);
        MIR_DLIST_APPEND(mir_constr_link, nodchain, cl, &node->constrains);

        pa_log_debug("node '%s' added to constrain %s/%s",
                     node->amname, cd->name, cd->key);
    }
}

void mir_constrain_remove_node(struct userdata *u, mir_node *node)
{
    mir_constr_def  *cd;
    mir_constr_link *cl, *n;

    pa_assert(u);
    pa_assert(node);

    MIR_DLIST_FOR_EACH_SAFE(mir_constr_link,nodchain, cl,n, &node->constrains){
        pa_assert_se((cd = cl->def));

        pa_log_debug("node '%s' removed from constrain %s/%s",
                     node->amname, cd->name, cd->key);

        cstrlink_destroy(u, cl);
    }
}


void mir_constrain_apply(struct userdata *u, mir_node *node, uint32_t stamp)
{
    mir_constr_link *cl;
    mir_constr_def  *cd;
    mir_constr_link *c;
    mir_node        *n;
    mir_rtentry     *rte;
    mir_rtgroup     *rtg;
    bool        blocked;

    pa_assert(u);
    pa_assert(node);

    MIR_DLIST_FOR_EACH(mir_constr_link, nodchain, cl, &node->constrains) {
        pa_assert(node == cl->node);
        pa_assert_se((cd = cl->def));

        pa_log_debug("applying constrain %s/%s", cd->name, cd->key);

        MIR_DLIST_FOR_EACH(mir_constr_link, link, c, &cd->nodes) {
            n = c->node;
            blocked = cd->func(u, cd, node, n);

            MIR_DLIST_FOR_EACH(mir_rtentry, nodchain, rte, &n->rtentries) {
                pa_assert_se((rtg = rte->group));

                rte->blocked = blocked;
                rte->stamp   = stamp;

                pa_log_debug("   %sblocking '%s' in table '%s'",
                             blocked ? "":"un", n->amname, rtg->name);
            }
        }
    }
}

int mir_constrain_print(mir_node *node, char *buf, int len)
{
    mir_constr_def  *cd;
    mir_constr_link *cl;
    char *p, *e;
    const char *s;

    pa_assert(node);
    pa_assert(buf);
    pa_assert(len > 0);

    buf[0] = '\0';

    e = (p = buf) + len;
    s = "";

    MIR_DLIST_FOR_EACH(mir_constr_link, nodchain, cl, &node->constrains) {
        if (p >= e)
            break;

        cd = cl->def;

        p += snprintf(p, e-p, "%s'%s'", s, cd->name);
        s  = " ";
    }

    return p - buf;
}

bool mir_constrain_port(struct userdata *u,
                             mir_constr_def  *cd,
                             mir_node        *active,
                             mir_node        *node)
{
    char *active_port;
    char *node_port;
    bool block;

    pa_assert(u);
    pa_assert(cd);
    pa_assert(active);
    pa_assert(node);

    pa_assert_se((active_port = active->paport));
    pa_assert_se((node_port = node->paport));

    block = !pa_streq(active_port, node_port);

    return block;
}

bool mir_constrain_profile(struct userdata *u,
                                mir_constr_def  *cd,
                                mir_node        *active,
                                mir_node        *node)
{
    char *active_profile;
    char *node_profile;
    bool block;

    pa_assert(u);
    pa_assert(cd);
    pa_assert(active);
    pa_assert(node);

    pa_assert_se((active_profile = active->pacard.profile));
    pa_assert_se((node_profile = node->pacard.profile));

    block = !pa_streq(active_profile, node_profile);

    return block;
}

static mir_constr_def *cstrdef_create(struct userdata     *u,
                                      const char          *name,
                                      mir_constrain_func_t func,
                                      const char          *key)
{
    mir_constr_def *cd;

    pa_assert(u);
    pa_assert(name);
    pa_assert(func);
    pa_assert(key);

    cd = pa_xnew0(mir_constr_def, 1);
    cd->key  = pa_xstrdup(key);
    cd->name = pa_xstrdup(name);
    cd->func = func;
    MIR_DLIST_INIT(cd->nodes);

    return cd;
}

static void cstrdef_destroy(struct userdata *u, mir_constr_def *cd)
{
    mir_constr_link *cl, *n;

    pa_assert(u);
    pa_assert(cd);

    MIR_DLIST_FOR_EACH_SAFE(mir_constr_link, link, cl,n, &cd->nodes) {
        cstrlink_destroy(u, cl);
    }

    pa_xfree(cd->name);
    pa_xfree(cd);
}


static mir_constr_link *cstrlink_create(struct userdata *u,
                                        mir_constr_def  *cd,
                                        mir_node        *node)
{
    mir_constr_link *cl;

    pa_assert(u);
    pa_assert(cd);
    pa_assert(node);

    cl = pa_xnew0(mir_constr_link, 1);
    cl->def  = cd;
    cl->node = node;
    MIR_DLIST_INIT(cl->link);
    MIR_DLIST_INIT(cl->nodchain);

    return cl;
}

static void cstrlink_destroy(struct userdata *u, mir_constr_link *cl)
{
    pa_assert(u);
    pa_assert(cl);

    MIR_DLIST_UNLINK(mir_constr_link, link, cl);
    MIR_DLIST_UNLINK(mir_constr_link, nodchain, cl);

    pa_xfree(cl);
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
