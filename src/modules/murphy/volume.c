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

#include "modules/main-volume-policy/main-volume-policy.h"

#include "volume.h"
#include "fader.h"
#include "node.h"
#include "utils.h"

#define VLIM_CLASS_ALLOC_BUCKET  16

typedef struct vlim_entry  vlim_entry;
typedef struct vlim_table  vlim_table;


struct vlim_entry {
    mir_volume_func_t func;      /**< volume limit function */
    void             *arg;       /**< arg given at registration time */
};

struct vlim_table {
    size_t       nentry;
    vlim_entry  *entries;
};

struct pa_mir_volume {
    int          classlen;       /**< class table length  */
    vlim_table  *classlim;       /**< class indexed table */
    vlim_table   genlim;         /**< generic limit */
    double       maxlim[mir_application_class_end];  /**< per class max. limit */
    pa_main_volume_policy *main_volume_policy;
};


static void add_to_table(vlim_table *, mir_volume_func_t, void *);
static void destroy_table(vlim_table *);
static double apply_table(double, vlim_table *, struct userdata *, int,
                          mir_node *, const char *);

static void reset_volume_limit(struct userdata *, mir_node *, uint32_t);
static void add_volume_limit(struct userdata *, mir_node *, int);


pa_mir_volume *pa_mir_volume_init(struct userdata *u)
{
    pa_mir_volume *volume = pa_xnew0(pa_mir_volume, 1);
    int i;

    (void)u;

    for (i = 0;  i < mir_application_class_end;  i++)
        volume->maxlim[i] = MIR_VOLUME_MAX_ATTENUATION;

    volume->main_volume_policy = pa_main_volume_policy_get(u->core);

    return volume;
}

void pa_mir_volume_done(struct userdata *u)
{
    pa_mir_volume *volume;
    int i;

    if (u && (volume = u->volume)) {
        if (volume->main_volume_policy)
            pa_main_volume_policy_unref(volume->main_volume_policy);

        for (i = 0;   i < volume->classlen;   i++) {
            destroy_table(volume->classlim + i);
        }
        free(volume->classlim);

        destroy_table(&volume->genlim);

        pa_xfree(volume);

        u->volume = NULL;
    }
}

void mir_volume_add_class_limit(struct userdata  *u,
                                int               class,
                                mir_volume_func_t func,
                                void             *arg)
{
    pa_mir_volume *volume;
    vlim_table    *classlim;
    vlim_table    *table;
    size_t         newlen;
    size_t         size;
    size_t         diff;

    pa_assert(u);
    pa_assert(func);
    pa_assert(class > 0);
    pa_assert_se((volume = u->volume));

    if (class < volume->classlen)
        table = volume->classlim + class;
    else {
        newlen = class + 1;
        size = sizeof(vlim_table) * newlen;
        diff = sizeof(vlim_table) * (newlen - volume->classlen);

        pa_assert_se((classlim = realloc(volume->classlim, size)));
        memset(classlim + volume->classlen, 0, diff);


        volume->classlen = newlen;
        volume->classlim = classlim;

        table = classlim + class;
    }

    add_to_table(table, func, arg);
}


void mir_volume_add_generic_limit(struct userdata  *u,
                                  mir_volume_func_t func,
                                  void             *arg)
{
    pa_mir_volume *volume;

    pa_assert(u);
    pa_assert(func);
    pa_assert_se((volume = u->volume));

    add_to_table(&volume->genlim, func, arg);
}


void mir_volume_add_maximum_limit(struct userdata *u,
                                  double maxlim,
                                  size_t nclass,
                                  int *classes)
{
    pa_mir_volume *volume;
    int class;
    size_t i;

    pa_assert(u);
    pa_assert_se((volume = u->volume));

    for (i = 0;  i < nclass;  i++) {
        if ((class = classes[i]) < mir_application_class_end)
            volume->maxlim[class] = maxlim;
    }
}


void mir_volume_make_limiting(struct userdata *u)
{
    uint32_t stamp;

    pa_assert(u);

    stamp = pa_utils_new_stamp();

    pa_fader_apply_volume_limits(u, stamp);
}

void mir_volume_add_limiting_class(struct userdata *u,
                                   mir_node        *node,
                                   int              class,
                                   uint32_t         stamp)
{
    pa_assert(u);
    pa_assert(node);
    pa_assert(class >= 0);

    if (node->implement == mir_device && node->direction == mir_output) {
        if (stamp > node->vlim.stamp)
            reset_volume_limit(u, node, stamp);

        add_volume_limit(u, node, class);
    }
}


double mir_volume_apply_limits(struct userdata *u,
                               mir_node *node,
                               int class,
                               uint32_t stamp)
{
    pa_mir_volume *volume;
    double attenuation = 0.0;
    double devlim, classlim;
    vlim_table *tbl;
    double maxlim;

    pa_assert(u);
    pa_assert_se((volume = u->volume));

    if (class < 0 || class >= volume->classlen) {
        if (class < 0 || class >= mir_application_class_end)
            attenuation = maxlim = MIR_VOLUME_MAX_ATTENUATION;
        else {
            attenuation = apply_table(0.0, &volume->genlim,
                                      u,class,node, "device");
        }
    }
    else {
        devlim = apply_table(0.0, &volume->genlim, u,class,node, "device");
        classlim = 0.0;

        if (class && node) {
            pa_assert(class >= mir_application_class_begin);
            pa_assert(class <  mir_application_class_end);

            maxlim = volume->maxlim[class];

            if (class < volume->classlen && (tbl = volume->classlim + class))
                classlim = apply_table(classlim, tbl, u, class, node, "class");

            if (classlim <= MIR_VOLUME_MAX_ATTENUATION)
                classlim = MIR_VOLUME_MAX_ATTENUATION;
            else if (classlim < maxlim)
                classlim = maxlim;
        }

        attenuation = devlim + classlim;
    }

    return attenuation;
}


double mir_volume_suppress(struct userdata *u, int class, mir_node *node,
                           void *arg)
{
    mir_volume_suppress_arg *suppress = arg;
    uint32_t clmask, trigmask;

    pa_assert(u);
    pa_assert(class >= mir_application_class_begin);
    pa_assert(class <  mir_application_class_end);
    pa_assert(node);
    pa_assert(node->direction == mir_output);
    pa_assert(node->implement == mir_device);

    clmask = ((uint32_t)1) << (class - mir_application_class_begin);

    if (suppress && (trigmask = suppress->trigger.clmask)) {
        pa_log_debug("        volume_supress(class=%d, clmask=0x%x, "
                     "trigmask=0x%x nodemask=0x%x)",
                     class, clmask, trigmask, node->vlim.clmask);

        if (!(trigmask & clmask) && (trigmask & node->vlim.clmask))
            return *suppress->attenuation;
    }

    return 0.0;
}

double mir_volume_correction(struct userdata *u, int class, mir_node *node,
                             void *arg)
{
    pa_assert(u);
    pa_assert(node);

    if (arg && *(double **)arg &&
        node->implement == mir_device &&
        node->privacy == mir_public)
    {
        return **(double **)arg;
    }

    return 0.0;
}

void mir_volume_change_context(struct userdata *u, const char *volume_class)
{
    pa_main_volume_policy *policy;
    pa_main_volume_context *ctx;

    pa_assert(u);

    if (!volume_class) {
        pa_log_error("no volume class set");
        return;
    }

    policy = u->volume->main_volume_policy;

    /* see if there is a context available that maps to the volume class */

    ctx = (pa_main_volume_context *) pa_hashmap_get(policy->main_volume_contexts, volume_class);

    if (ctx) {
        pa_main_volume_policy_set_active_main_volume_context(policy, ctx);
        pa_log_debug("volume context changed to: '%s'", volume_class);
    }

    /* TODO: change volume class here */
}

static void add_to_table(vlim_table *tbl, mir_volume_func_t func, void *arg)
{
    size_t      size;
    vlim_entry *entries;
    vlim_entry *entry;

    pa_assert(tbl);
    pa_assert(func);

    size = sizeof(vlim_entry) * (tbl->nentry + 1);
    pa_assert_se((entries = realloc(tbl->entries,  size)));
    entry = entries + tbl->nentry;

    entry->func = func;
    entry->arg  = arg;

    tbl->nentry += 1;
    tbl->entries = entries;
}

static void destroy_table(vlim_table *tbl)
{
    pa_assert(tbl);

    free(tbl->entries);
}

static double apply_table(double attenuation,
                          vlim_table *tbl,
                          struct userdata *u,
                          int class,
                          mir_node *node,
                          const char *type)
{
    static mir_node fake_node;

    double a;
    vlim_entry *e;
    size_t i;

    pa_assert(tbl);
    pa_assert(u);
    pa_assert(type);

    if (!node)
        node = &fake_node;

    for (i = 0;   i < tbl->nentry;  i++) {
        e = tbl->entries + i;
        a = e->func(u, class, node, e->arg);

        pa_log_debug("        %s limit = %.2lf", type, a);

        if (a < attenuation)
            attenuation = a;
    }

    return attenuation;
}



static void reset_volume_limit(struct userdata *u,
                               mir_node        *node,
                               uint32_t         stamp)
{
    mir_vlim      *vlim = &node->vlim;
    pa_core       *core;
    pa_sink       *sink;
    pa_sink_input *sinp;
    int            class;
    uint32_t       i;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((core = u->core));

    pa_log_debug("reset volume classes on node '%s'", node->amname);

    vlim->nclass = 0;
    vlim->clmask = 0;
    vlim->stamp  = stamp;

    if ((sink = pa_idxset_get_by_index(core->sinks, node->paidx))) {
        PA_IDXSET_FOREACH(sinp, sink->inputs, i) {
            class = pa_utils_get_stream_class(sinp->proplist);
            add_volume_limit(u, node, class);
        }
    }
}


static void add_volume_limit(struct userdata *u, mir_node *node, int class)
{
    mir_vlim      *vlim = &node->vlim;
    pa_mir_volume *volume;
    int           *classes;
    size_t         classes_size;
    uint32_t       mask;

    pa_assert(u);
    pa_assert(node);
    pa_assert_se((volume = u->volume));
    pa_assert(class >= 0);

    if (class <  mir_application_class_begin ||
        class >= mir_application_class_end      )
    {
        pa_log_debug("refusing to add unknown volume class %d to node '%s'",
                     class, node->amname);
    }
    else {
        mask = ((uint32_t)1) << (class - mir_application_class_begin);

        if (class < volume->classlen && volume->classlim[class].nentry > 0) {
            if (!(vlim->clmask & mask)) {

                if (vlim->nclass < vlim->maxentry)
                    classes = vlim->classes;
                else {
                    vlim->maxentry += VLIM_CLASS_ALLOC_BUCKET;
                    classes_size    = sizeof(int *) * vlim->maxentry;
                    vlim->classes   = realloc(vlim->classes, classes_size);

                    pa_assert_se((classes = vlim->classes));
                }

                vlim->classes[vlim->nclass++] = class;
            }
        }

        if (!(vlim->clmask & mask)) {
            pa_log_debug("add volume class %d (%s) to node '%s' (clmask 0x%x)",
                         class, mir_node_type_str(class), node->amname,
                         vlim->clmask);
        }

        vlim->clmask |= mask;

    }
}




/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
