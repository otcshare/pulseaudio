/***
  This file is part of PulseAudio.

  Copyright 2014 Intel Corporation

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

#include "mute-control.h"

#include <modules/volume-api/audio-group.h>
#include <modules/volume-api/device.h>
#include <modules/volume-api/sstream.h>

#include <pulsecore/core-util.h>

int pa_mute_control_new(pa_volume_api *api, const char *name, bool persistent, pa_mute_control **_r) {
    pa_mute_control *control = NULL;
    int r;

    pa_assert(api);
    pa_assert(name);
    pa_assert(_r);

    control = pa_xnew0(pa_mute_control, 1);
    control->volume_api = api;
    control->index = pa_volume_api_allocate_mute_control_index(api);

    r = pa_volume_api_register_name(api, name, false, &control->name);
    if (r < 0)
        goto fail;

    control->description = pa_xstrdup(control->name);
    control->proplist = pa_proplist_new();
    control->present = !persistent;
    control->persistent = persistent;
    control->purpose = PA_MUTE_CONTROL_PURPOSE_OTHER;
    control->devices = pa_hashmap_new(NULL, NULL);
    control->default_for_devices = pa_hashmap_new(NULL, NULL);

    if (persistent) {
        pa_inidb_row *row;

        row = pa_inidb_table_add_row(api->control_db.mute_controls, control->name);
        control->db_cells.description = pa_inidb_row_get_cell(row, PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_DESCRIPTION);
        control->db_cells.mute = pa_inidb_row_get_cell(row, PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_MUTE);
    }

    *_r = control;
    return 0;

fail:
    if (control)
        pa_mute_control_free(control);

    return r;
}

void pa_mute_control_put(pa_mute_control *control) {
    const char *prop_key;
    void *state = NULL;

    pa_assert(control);
    pa_assert(control->set_mute || !control->present);

    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_IMPLEMENTATION_INITIALIZED], control);
    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_SET_INITIAL_MUTE], control);

    if (control->set_mute) {
        control->set_mute_in_progress = true;
        control->set_mute(control, control->mute);
        control->set_mute_in_progress = false;
    }

    pa_volume_api_add_mute_control(control->volume_api, control);
    control->linked = true;

    pa_log_debug("Created mute control #%u.", control->index);
    pa_log_debug("    Name: %s", control->name);
    pa_log_debug("    Description: %s", control->description);
    pa_log_debug("    Mute: %s", pa_yes_no(control->mute));
    pa_log_debug("    Present: %s", pa_yes_no(control->present));
    pa_log_debug("    Persistent: %s", pa_yes_no(control->persistent));
    pa_log_debug("    Properties:");

    while ((prop_key = pa_proplist_iterate(control->proplist, &state)))
        pa_log_debug("        %s = %s", prop_key, pa_strnull(pa_proplist_gets(control->proplist, prop_key)));

    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_PUT], control);
}

void pa_mute_control_unlink(pa_mute_control *control) {
    pa_device *device;

    pa_assert(control);

    if (control->unlinked) {
        pa_log_debug("Unlinking mute control %s (already unlinked, this is a no-op).", control->name);
        return;
    }

    control->unlinked = true;

    pa_log_debug("Unlinking mute control %s.", control->name);

    if (control->linked)
        pa_volume_api_remove_mute_control(control->volume_api, control);

    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_UNLINK], control);

    while ((device = pa_hashmap_first(control->default_for_devices)))
        pa_device_set_default_mute_control(device, NULL);

    while ((device = pa_hashmap_first(control->devices))) {
        /* Why do we have this assertion here? The concern is that if we call
         * pa_device_set_mute_control() for some device that has the
         * use_default_mute_control flag set, then that flag will be unset as
         * a side effect, and we don't want that side effect. This assertion
         * should be safe, because we just called
         * pa_device_set_default_mute_control(NULL) for each device that this
         * control was the default for, and that should ensure that we don't
         * any more hold any references to devices that used to use this
         * control as the default. */
        pa_assert(!device->use_default_mute_control);
        pa_device_set_mute_control(device, NULL);
    }
}

void pa_mute_control_free(pa_mute_control *control) {
    pa_assert(control);

    /* unlink() expects name to be set. */
    if (!control->unlinked && control->name)
        pa_mute_control_unlink(control);

    if (control->default_for_devices) {
        pa_assert(pa_hashmap_isempty(control->default_for_devices));
        pa_hashmap_free(control->default_for_devices);
    }

    if (control->devices) {
        pa_assert(pa_hashmap_isempty(control->devices));
        pa_hashmap_free(control->devices);
    }

    if (control->proplist)
        pa_proplist_free(control->proplist);

    pa_xfree(control->description);

    if (control->name)
        pa_volume_api_unregister_name(control->volume_api, control->name);

    pa_xfree(control);
}

void pa_mute_control_set_purpose(pa_mute_control *control, pa_mute_control_purpose_t purpose, void *owner) {
    pa_assert(control);
    pa_assert(!control->linked);

    control->purpose = purpose;
    control->owner = owner;
}

int pa_mute_control_acquire_for_audio_group(pa_mute_control *control, pa_audio_group *group,
                                            pa_mute_control_set_mute_cb_t set_mute_cb, void *userdata) {
    pa_assert(control);
    pa_assert(group);
    pa_assert(set_mute_cb);

    if (control->present) {
        pa_log("Can't acquire mute control %s, it's already present.", control->name);
        return -PA_ERR_BUSY;
    }

    control->owner_audio_group = group;
    control->set_mute = set_mute_cb;
    control->userdata = userdata;

    control->set_mute_in_progress = true;
    control->set_mute(control, control->mute);
    control->set_mute_in_progress = false;

    control->present = true;

    if (!control->linked || control->unlinked)
        return 0;

    pa_log_debug("Mute control %s became present.", control->name);

    return 0;
}

void pa_mute_control_release(pa_mute_control *control) {
    pa_assert(control);

    if (!control->present)
        return;

    control->present = false;

    control->userdata = NULL;
    control->set_mute = NULL;
    control->owner_audio_group = NULL;

    if (!control->linked || control->unlinked)
        return;

    pa_log_debug("Mute control %s became not present.", control->name);
}

void pa_mute_control_set_description(pa_mute_control *control, const char *description) {
    char *old_description;

    pa_assert(control);
    pa_assert(description);

    old_description = control->description;

    if (pa_streq(description, old_description))
        return;

    control->description = pa_xstrdup(description);

    if (control->persistent)
        pa_inidb_cell_set_value(control->db_cells.description, description);

    if (!control->linked || control->unlinked) {
        pa_xfree(old_description);
        return;
    }

    pa_log_debug("The description of mute control %s changed from \"%s\" to \"%s\".", control->name, old_description,
                 description);
    pa_xfree(old_description);
    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_DESCRIPTION_CHANGED], control);
}

static void set_mute_internal(pa_mute_control *control, bool mute) {
    bool old_mute;

    pa_assert(control);

    old_mute = control->mute;

    if (mute == old_mute)
        return;

    control->mute = mute;

    if (control->persistent)
        pa_inidb_cell_set_value(control->db_cells.mute, pa_boolean_to_string(mute));

    if (!control->linked || control->unlinked)
        return;

    pa_log_debug("The mute of mute control %s changed from %s to %s.", control->name, pa_boolean_to_string(old_mute),
                 pa_boolean_to_string(control->mute));

    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_MUTE_CHANGED], control);
}

int pa_mute_control_set_mute(pa_mute_control *control, bool mute) {
    int r;

    pa_assert(control);

    if (control->set_mute_in_progress)
        return 0;

    if (mute == control->mute)
        return 0;

    if (control->linked && control->present) {
        control->set_mute_in_progress = true;
        r = control->set_mute(control, mute);
        control->set_mute_in_progress = false;

        if (r < 0) {
            pa_log("Setting the mute of mute control %s failed.", control->name);
            return r;
        }
    }

    set_mute_internal(control, mute);

    return 0;
}

void pa_mute_control_add_device(pa_mute_control *control, pa_device *device) {
    pa_assert(control);
    pa_assert(device);

    pa_assert_se(pa_hashmap_put(control->devices, device, device) >= 0);
}

void pa_mute_control_remove_device(pa_mute_control *control, pa_device *device) {
    pa_assert(control);
    pa_assert(device);

    pa_assert_se(pa_hashmap_remove(control->devices, device));
}

void pa_mute_control_add_default_for_device(pa_mute_control *control, pa_device *device) {
    pa_assert(control);
    pa_assert(device);

    pa_assert_se(pa_hashmap_put(control->default_for_devices, device, device) >= 0);
}

void pa_mute_control_remove_default_for_device(pa_mute_control *control, pa_device *device) {
    pa_assert(control);
    pa_assert(device);

    pa_assert_se(pa_hashmap_remove(control->default_for_devices, device));
}
