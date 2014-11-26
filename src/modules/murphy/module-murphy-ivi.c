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
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>

#include <sys/types.h>
#include <sys/socket.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulse/timeval.h>
#include <pulse/xmalloc.h>

#include <pulsecore/macro.h>
#include <pulsecore/module.h>
#include <pulsecore/idxset.h>
#include <pulsecore/client.h>
#include <pulsecore/core-util.h>
#include <pulsecore/core-error.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>

#include "module-murphy-ivi-symdef.h"
#include "userdata.h"
#include "node.h"
#include "zone.h"
#include "tracker.h"
#include "discover.h"
#include "router.h"
#include "constrain.h"
#include "multiplex.h"
#include "loopback.h"
#include "fader.h"
#include "volume.h"
#include "audiomgr.h"
#include "routerif.h"
#include "murphy-config.h"
#include "utils.h"
#include "scripting.h"
#include "extapi.h"
#include "murphyif.h"
#include "classify.h"

#ifndef DEFAULT_CONFIG_DIR
#define DEFAULT_CONFIG_DIR "/etc/pulse"
#endif

#ifndef DEFAULT_CONFIG_FILE
#define DEFAULT_CONFIG_FILE "murphy-ivi.lua"
#endif

#ifdef WITH_MURPHYIF
#define WITH_DOMCTL
#define WITH_RESOURCES
#endif


PA_MODULE_AUTHOR("Janos Kovacs");
PA_MODULE_DESCRIPTION("Murphy and GenIVI compliant audio policy module");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);
PA_MODULE_USAGE(
    "config_dir=<configuration directory>"
    "config_file=<policy configuration file> "
    "fade_out=<stream fade-out time in msec> "
    "fade_in=<stream fade-in time in msec> "
    "enable_multiplex=<boolean for disabling combine creation> "
#ifdef WITH_DOMCTL
    "murphy_domain_controller=<address of Murphy's domain controller service> "
#endif
#ifdef WITH_RESOURCES
    "murphy_resources=<address of Murphy's native resource service> "
#endif
#ifdef WITH_DBUS
    "dbus_bus_type=<system|session> "
    "dbus_if_name=<policy dbus interface> "
    "dbus_murphy_path=<policy daemon's path> "
    "dbus_murphy_name=<policy daemon's name> "
    "dbus_audiomgr_path=<GenIVI audio manager's path> "
    "dbus_audiomgr_name=<GenIVI audio manager's name> "
#else
    "audiomgr_socktype=<tcp|unix> "
    "audiomgr_address=<audiomgr socket address> "
    "audiomgr_port=<audiomgr tcp port> "
#endif
    "null_sink_name=<name of the null sink> "
);

static const char* const valid_modargs[] = {
    "config_dir",
    "config_file",
    "fade_out",
    "fade_in",
    "enable_multiplex",
#ifdef WITH_DOMCTL
    "murphy_domain_controller",
#endif
#ifdef WITH_RESOURCES
    "murphy_resources",
#endif
#ifdef WITH_DBUS
    "dbus_bus_type",
    "dbus_if_name",
    "dbus_murphy_path",
    "dbus_murphy_name",
    "dbus_audiomgr_path",
    "dbus_audiomgr_name",
#else
    "audiomgr_socktype",
    "audiomgr_address",
    "audiomgr_port",
#endif
    "null_sink_name",
    NULL
};


int pa__init(pa_module *m) {
    struct userdata *u = NULL;
    pa_modargs      *ma = NULL;
    const char      *cfgdir;
    const char      *cfgfile;
    const char      *fadeout;
    const char      *fadein;
#ifdef WITH_DOMCTL
    const char      *ctladdr;
#endif
#ifdef WITH_RESOURCES
    const char      *resaddr;
#endif
#ifdef WITH_DBUS
    const char      *dbustype;
    const char      *ampath;
    const char      *amnam;
#else
    const char      *socktype;
    const char      *amaddr;
    const char      *amport;
#endif
    const char      *nsnam;
    const char      *cfgpath;
    char             buf[4096];
    bool             enable_multiplex = true;


    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments.");
        goto fail;
    }

    cfgdir   = pa_modargs_get_value(ma, "config_dir", DEFAULT_CONFIG_DIR);
    cfgfile  = pa_modargs_get_value(ma, "config_file", DEFAULT_CONFIG_FILE);
    fadeout  = pa_modargs_get_value(ma, "fade_out", NULL);
    fadein   = pa_modargs_get_value(ma, "fade_in", NULL);

    if (pa_modargs_get_value_boolean(ma, "enable_multiplex", &enable_multiplex) < 0)
        enable_multiplex = true;

#ifdef WITH_DOMCTL
    ctladdr  = pa_modargs_get_value(ma, "murphy_domain_controller", NULL);
#endif
#ifdef WITH_RESOURCES
    resaddr  = pa_modargs_get_value(ma, "murphy_resources", NULL);
#endif
#ifdef WITH_DBUS
    dbustype = pa_modargs_get_value(ma, "dbus_bus_type", NULL);
    ampath   = pa_modargs_get_value(ma, "dbus_audiomgr_path", NULL);
    amnam    = pa_modargs_get_value(ma, "dbus_audiomgr_name", NULL);
#else
    socktype = pa_modargs_get_value(ma, "audiomgr_socktype", NULL);
    amaddr   = pa_modargs_get_value(ma, "audiomgr_address", NULL);
    amport   = pa_modargs_get_value(ma, "audiomgr_port", NULL);
#endif
    nsnam    = pa_modargs_get_value(ma, "null_sink_name", NULL);

    u = pa_xnew0(struct userdata, 1);
    u->core      = m->core;
    u->module    = m;
    u->nullsink  = pa_utils_create_null_sink(u, nsnam);
    u->zoneset   = pa_zoneset_init(u);
    u->nodeset   = pa_nodeset_init(u);
    u->audiomgr  = pa_audiomgr_init(u);
#ifdef WITH_DBUS
    u->routerif  = pa_routerif_init(u, dbustype, ampath, amnam);
#else
    u->routerif  = pa_routerif_init(u, socktype, amaddr, amport);
#endif
    u->discover  = pa_discover_init(u);
    u->tracker   = pa_tracker_init(u);
    u->router    = pa_router_init(u);
    u->constrain = pa_constrain_init(u);
    u->multiplex = pa_multiplex_init();
    u->loopback  = pa_loopback_init();
    u->fader     = pa_fader_init(fadeout, fadein);
    u->volume    = pa_mir_volume_init(u);
    u->scripting = pa_scripting_init(u);
    u->config    = pa_mir_config_init(u);
    u->extapi    = pa_extapi_init(u);
    u->murphyif  = pa_murphyif_init(u, ctladdr, resaddr);

    u->state.sink   = PA_IDXSET_INVALID;
    u->state.source = PA_IDXSET_INVALID;

    u->enable_multiplex = enable_multiplex;

    if (u->nullsink == NULL || u->routerif == NULL  ||
        u->audiomgr == NULL || u->discover == NULL  ||
        u->murphyif == NULL)
        goto fail;

    m->userdata = u;

    /* register ext api callback */
    u->protocol = pa_native_protocol_get(m->core);
    pa_native_protocol_install_ext(u->protocol, m, extension_cb);

    cfgpath = pa_utils_file_path(cfgdir, cfgfile, buf, sizeof(buf));

    pa_mir_config_parse_file(u, cfgpath);

    pa_tracker_synchronize(u);

    mir_router_print_rtgroups(u, buf, sizeof(buf));
    pa_log_debug("%s", buf);

    pa_modargs_free(ma);

    return 0;

 fail:

    if (ma)
        pa_modargs_free(ma);

    pa__done(m);

    return -1;
}

void pa__done(pa_module *m) {
    struct userdata *u;

    pa_assert(m);

    if ((u = m->userdata)) {
        pa_murphyif_done(u);
        pa_tracker_done(u);
        pa_discover_done(u);
        pa_constrain_done(u);
        pa_router_done(u);
        pa_audiomgr_done(u);
        pa_routerif_done(u);
        pa_fader_done(u);
        pa_mir_volume_done(u);
        pa_mir_config_done(u);
        pa_nodeset_done(u);
        pa_zoneset_done(u);
        pa_scripting_done(u);
        pa_utils_destroy_null_sink(u);

        pa_loopback_done(u->loopback, u->core);
        pa_multiplex_done(u->multiplex, u->core);

        pa_extapi_done(u);

        if (u->protocol) {
            pa_native_protocol_remove_ext(u->protocol, m);
            pa_native_protocol_unref(u->protocol);
        }

        pa_xfree(u);
    }
}



/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
