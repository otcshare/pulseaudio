/***
  This file is part of PulseAudio.

  Copyright 2013 João Paulo Rechi Vita

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>
#include <pulsecore/module.h>

#include "module-bluetooth-discover-symdef.h"

PA_MODULE_AUTHOR("João Paulo Rechi Vita");
PA_MODULE_DESCRIPTION("Detect available Bluetooth daemon and load the corresponding discovery module");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);

struct userdata {
    pa_module *bluez5_module;
    pa_module *bluez4_module;
};

/* This function is heavily inspired in the implementation of ".ifexists" */
static bool exists(const char *filename) {
    const char *paths, *state = NULL;
    char *p, *pathname;
    bool result;

    if (!(paths = lt_dlgetsearchpath()))
        return false;

    while ((p = pa_split(paths, ":", &state))) {
        pathname = pa_sprintf_malloc("%s" PA_PATH_SEP "%s", p, filename);
        result = access(pathname, F_OK) == 0 ? true : false;
        pa_log_debug("Checking for existence of '%s': %s", pathname, result ? "success" : "failure");
        pa_xfree(pathname);
        pa_xfree(p);
        if (result)
            return true;
    }

    state = NULL;
    if (PA_UNLIKELY(pa_run_from_build_tree())) {
        while ((p = pa_split(paths, ":", &state))) {
            pathname = pa_sprintf_malloc("%s" PA_PATH_SEP ".libs" PA_PATH_SEP "%s", p, filename);
            result = access(pathname, F_OK) == 0 ? true : false;
            pa_log_debug("Checking for existence of '%s': %s", pathname, result ? "success" : "failure");
            pa_xfree(pathname);
            pa_xfree(p);
            if (result)
                return true;
        }
    }
    return false;
}

int pa__init(pa_module* m) {
    struct userdata *u;

    pa_assert(m);

    m->userdata = u = pa_xnew0(struct userdata, 1);

    if (exists("module-bluez5-discover.so"))
        u->bluez5_module = pa_module_load(m->core, "module-bluez5-discover",  NULL);

    if (exists("module-bluez4-discover.so"))
        u->bluez4_module = pa_module_load(m->core, "module-bluez4-discover",  NULL);

    return 0;
}

void pa__done(pa_module* m) {
    struct userdata *u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->bluez5_module)
        pa_module_unload(m->core, u->bluez5_module, false);

    if (u->bluez4_module)
        pa_module_unload(m->core, u->bluez4_module, false);

    pa_xfree(u);
}
