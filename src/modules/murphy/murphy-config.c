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
#include <unistd.h>
#include <string.h>
#include <errno.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "murphy-config.h"
#include "zone.h"
#include "node.h"
#include "router.h"
#include "volume.h"
#include "scripting.h"

typedef struct {
    const char *name;
} zone_def;

typedef struct {
    mir_direction          type;
    const char            *name;
    mir_rtgroup_accept_t   accept;
    mir_rtgroup_compare_t  compare;
} rtgroup_def;

typedef struct {
    mir_node_type  class;
    uint32_t       zone;
    mir_direction  type;
    const char    *rtgroup;
} classmap_def;

typedef struct {
    const char    *id;
    mir_node_type  type;
} typemap_def;

typedef struct {
    mir_node_type  class;
    int            priority;
} prior_def;



static zone_def zones[] = {
    {"driver"},
    {"passanger1"},
    {"passanger2"},
    {"passanger3"},
    {"passanger4"},
    {NULL}
};

static rtgroup_def  rtgroups[] = {
    {mir_input,
     "phone",
     mir_router_phone_accept,
     mir_router_phone_compare
    },

    {mir_output,
     "default",
     mir_router_default_accept,
     mir_router_default_compare
    },

    {mir_output,
     "phone",
     mir_router_phone_accept,
     mir_router_phone_compare
    },

    {0,NULL,NULL,NULL}
};

static classmap_def classmap[] = {
    {mir_phone    , 0, mir_input , "phone"  },
    {mir_radio    , 0, mir_output, "default"},
    {mir_player   , 0, mir_output, "default"},
    {mir_navigator, 0, mir_output, "default"},
    {mir_game     , 0, mir_output, "default"},
    {mir_browser  , 0, mir_output, "default"},
    {mir_phone    , 0, mir_output, "phone"  },
    {mir_event    , 0, mir_output, "default"},
    {mir_node_type_unknown, 0, mir_direction_unknown, NULL}
};

static typemap_def rolemap[] = {
    {"video"    , mir_player    },
    {"music"    , mir_player    },
    {"game"     , mir_game      },
    {"event"    , mir_event     },
    {"navigator", mir_navigator },
    {"phone"    , mir_phone     },
    {"carkit"   , mir_phone     },
    {"animation", mir_browser   },
    {"test"     , mir_player    },
    {"ringtone" , mir_alert     },
    {"alarm"    , mir_alert     },
    {"camera"   , mir_camera    },
    {"system"   , mir_system    },
    {NULL, mir_node_type_unknown}
};

static typemap_def binmap[] = {
    {"rhytmbox"    , mir_player },
    {"firefox"     , mir_browser},
    {"chrome"      , mir_browser},
    {"sound-juicer", mir_player },
    {NULL, mir_node_type_unknown}
};

static prior_def priormap[] = {
    {mir_radio    , 1},
    {mir_player   , 1},
    {mir_navigator, 2},
    {mir_game     , 3},
    {mir_browser  , 1},
    {mir_phone    , 4},
    {mir_event    , 5},
    {mir_node_type_unknown, 0}
};

static double speedvol;
static double supprvol = -20.0;
static int exception_classes[] = {mir_phone, mir_navigator};
static mir_volume_suppress_arg suppress = {
    &supprvol, {DIM(exception_classes), exception_classes, 0}
};


static bool use_default_configuration(struct userdata *);

#if 0
static bool parse_config_file(struct userdata *, FILE *);
#endif

pa_mir_config *pa_mir_config_init(struct userdata *u)
{
    pa_mir_config *config;

    pa_assert(u);

    config = pa_xnew0(pa_mir_config, 1);

    return config;
}

void pa_mir_config_done(struct userdata *u)
{
    pa_mir_config *config;

    if (u && (config = u->config)) {
        pa_xfree(config);
        u->config = NULL;
    }
}


bool pa_mir_config_parse_file(struct userdata *u, const char *path)
{
    pa_module *module;
    pa_mir_config *config;
    int success;
    char buf[4096];

    pa_assert(u);
    pa_assert_se((module = u->module));
    pa_assert_se((config = u->config));

    if (!path)
        success = false;
    else {
        pa_log_info("%s: configuration file is '%s'", module->name, path);
        success =  pa_scripting_dofile(u, path);
    }

    if (!success) {
        pa_log_info("%s: builtin default configuration applies", module->name);
        success = use_default_configuration(u);
    }

    pa_nodeset_print_maps(u, buf, sizeof(buf));
    pa_log_debug("maps %s", buf);

    return success;
}

static bool use_default_configuration(struct userdata *u)
{
    zone_def     *z;
    rtgroup_def  *r;
    classmap_def *c;
    typemap_def  *t;
    prior_def    *p;

    pa_assert(u);

    for (z = zones;  z->name;  z++)
        pa_zoneset_add_zone(u, z->name, z - zones);

    for (r = rtgroups;  r->name;   r++)
        mir_router_create_rtgroup(u, r->type, r->name, r->accept, r->compare);

    for (c = classmap;  c->rtgroup;  c++) {
        mir_router_assign_class_to_rtgroup(u, c->class, c->zone,
                                           c->type, c->rtgroup);
    }

    for (t = rolemap; t->id; t++)
        pa_nodeset_add_role(u, t->id, t->type, NULL);

    for (t = binmap; t->id; t++)
        pa_nodeset_add_binary(u, t->id, t->type, NULL, NULL);

    for (p = priormap;  p->class;  p++)
        mir_router_assign_class_priority(u, p->class, p->priority);


    mir_volume_add_generic_limit(u, mir_volume_correction, &speedvol);

    mir_volume_add_class_limit(u, mir_phone,mir_volume_suppress, &suppress);
    mir_volume_add_class_limit(u, mir_navigator,mir_volume_suppress,&suppress);


    return true;
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
