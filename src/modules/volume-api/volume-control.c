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

#include "volume-control.h"

#include <modules/volume-api/audio-group.h>
#include <modules/volume-api/device.h>
#include <modules/volume-api/inidb.h>
#include <modules/volume-api/sstream.h>

#include <pulsecore/core-util.h>

int pa_volume_control_new(pa_volume_api *api, const char *name, bool persistent, pa_volume_control **_r) {
    pa_volume_control *control = NULL;
    int r;

    pa_assert(api);
    pa_assert(name);
    pa_assert(_r);

    control = pa_xnew0(pa_volume_control, 1);
    control->volume_api = api;
    control->index = pa_volume_api_allocate_volume_control_index(api);

    r = pa_volume_api_register_name(api, name, persistent, &control->name);
    if (r < 0)
        goto fail;

    control->description = pa_xstrdup(control->name);
    control->proplist = pa_proplist_new();
    pa_bvolume_init_mono(&control->volume, PA_VOLUME_NORM);
    control->present = !persistent;
    control->persistent = persistent;
    control->purpose = PA_VOLUME_CONTROL_PURPOSE_OTHER;
    control->devices = pa_hashmap_new(NULL, NULL);
    control->default_for_devices = pa_hashmap_new(NULL, NULL);

    if (persistent) {
        pa_inidb_row *row;

        row = pa_inidb_table_add_row(api->control_db.volume_controls, control->name);
        control->db_cells.description = pa_inidb_row_get_cell(row, PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_DESCRIPTION);
        control->db_cells.volume = pa_inidb_row_get_cell(row, PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_VOLUME);
        control->db_cells.balance = pa_inidb_row_get_cell(row, PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_BALANCE);
        control->db_cells.convertible_to_dB = pa_inidb_row_get_cell(row,
                                                                    PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_CONVERTIBLE_TO_DB);
    }

    *_r = control;
    return 0;

fail:
    if (control)
        pa_volume_control_free(control);

    return r;
}

void pa_volume_control_put(pa_volume_control *control) {
    const char *prop_key;
    void *state = NULL;
    char volume_str[PA_VOLUME_SNPRINT_VERBOSE_MAX];
    char balance_str[PA_BVOLUME_SNPRINT_BALANCE_MAX];

    pa_assert(control);
    pa_assert(control->set_volume || !control->present);

    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_IMPLEMENTATION_INITIALIZED], control);
    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_SET_INITIAL_VOLUME], control);

    if (control->set_volume) {
        control->set_volume_in_progress = true;
        control->set_volume(control, &control->volume, &control->volume, true, true);
        control->set_volume_in_progress = false;
    }

    pa_volume_api_add_volume_control(control->volume_api, control);
    control->linked = true;

    pa_log_debug("Created volume control #%u.", control->index);
    pa_log_debug("    Name: %s", control->name);
    pa_log_debug("    Description: %s", control->description);
    pa_log_debug("    Volume: %s", pa_volume_snprint_verbose(volume_str, sizeof(volume_str), control->volume.volume,
                 control->convertible_to_dB));
    pa_log_debug("    Balance: %s", pa_bvolume_snprint_balance(balance_str, sizeof(balance_str), &control->volume));
    pa_log_debug("    Present: %s", pa_yes_no(control->present));
    pa_log_debug("    Persistent: %s", pa_yes_no(control->persistent));
    pa_log_debug("    Properties:");

    while ((prop_key = pa_proplist_iterate(control->proplist, &state)))
        pa_log_debug("        %s = %s", prop_key, pa_strnull(pa_proplist_gets(control->proplist, prop_key)));

    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_PUT], control);
}

void pa_volume_control_unlink(pa_volume_control *control) {
    pa_device *device;

    pa_assert(control);

    if (control->unlinked) {
        pa_log_debug("Unlinking volume control %s (already unlinked, this is a no-op).", control->name);
        return;
    }

    control->unlinked = true;

    pa_log_debug("Unlinking volume control %s.", control->name);

    if (control->linked)
        pa_volume_api_remove_volume_control(control->volume_api, control);

    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_UNLINK], control);

    while ((device = pa_hashmap_first(control->default_for_devices)))
        pa_device_set_default_volume_control(device, NULL);

    while ((device = pa_hashmap_first(control->devices))) {
        /* Why do we have this assertion here? The concern is that if we call
         * pa_device_set_volume_control() for some device that has the
         * use_default_volume_control flag set, then that flag will be unset as
         * a side effect, and we don't want that side effect. This assertion
         * should be safe, because we just called
         * pa_device_set_default_volume_control(NULL) for each device that this
         * control was the default for, and that should ensure that we don't
         * any more hold any references to devices that used to use this
         * control as the default. */
        pa_assert(!device->use_default_volume_control);
        pa_device_set_volume_control(device, NULL);
    }
}

void pa_volume_control_free(pa_volume_control *control) {
    pa_assert(control);

    /* unlink() expects name to be set. */
    if (!control->unlinked && control->name)
        pa_volume_control_unlink(control);

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

void pa_volume_control_set_purpose(pa_volume_control *control, pa_volume_control_purpose_t purpose, void *owner) {
    pa_assert(control);
    pa_assert(!control->linked);

    control->purpose = purpose;
    control->owner = owner;
}

int pa_volume_control_acquire_for_audio_group(pa_volume_control *control, pa_audio_group *group,
                                              pa_volume_control_set_volume_cb_t set_volume_cb, void *userdata) {
    pa_assert(control);
    pa_assert(group);
    pa_assert(set_volume_cb);

    if (control->present) {
        pa_log("Can't acquire volume control %s, it's already present.", control->name);
        return -PA_ERR_BUSY;
    }

    control->set_volume = set_volume_cb;
    control->userdata = userdata;

    control->set_volume_in_progress = true;
    control->set_volume(control, &control->volume, &control->volume, true, true);
    control->set_volume_in_progress = false;

    control->present = true;

    if (!control->linked || control->unlinked)
        return 0;

    pa_log_debug("Volume control %s became present.", control->name);

    return 0;
}

void pa_volume_control_release(pa_volume_control *control) {
    pa_assert(control);

    if (!control->present)
        return;

    control->present = false;

    control->userdata = NULL;
    control->set_volume = NULL;

    if (!control->linked || control->unlinked)
        return;

    pa_log_debug("Volume control %s became not present.", control->name);
}

void pa_volume_control_set_description(pa_volume_control *control, const char *description) {
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

    pa_log_debug("The description of volume control %s changed from \"%s\" to \"%s\".", control->name, old_description,
                 description);
    pa_xfree(old_description);
    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_DESCRIPTION_CHANGED], control);
}

static void set_volume_internal(pa_volume_control *control, const pa_bvolume *volume, bool set_volume, bool set_balance) {
    pa_bvolume old_volume;
    bool volume_changed;
    bool balance_changed;
    char *str;

    pa_assert(control);
    pa_assert(volume);

    old_volume = control->volume;
    volume_changed = !pa_bvolume_equal(volume, &old_volume, set_volume, false);
    balance_changed = !pa_bvolume_equal(volume, &old_volume, false, set_balance);

    if (!volume_changed && !balance_changed)
        return;

    if (volume_changed) {
        control->volume.volume = volume->volume;

        if (control->persistent) {
            str = pa_sprintf_malloc("%u", control->volume.volume);
            pa_inidb_cell_set_value(control->db_cells.volume, str);
            pa_xfree(str);
        }
    }

    if (balance_changed) {
        pa_bvolume_copy_balance(&control->volume, volume);

        if (control->persistent) {
            pa_assert_se(pa_bvolume_balance_to_string(&control->volume, &str) >= 0);
            pa_inidb_cell_set_value(control->db_cells.balance, str);
            pa_xfree(str);
        }
    }

    if (!control->linked || control->unlinked)
        return;

    if (volume_changed) {
        char old_volume_str[PA_VOLUME_SNPRINT_VERBOSE_MAX];
        char new_volume_str[PA_VOLUME_SNPRINT_VERBOSE_MAX];

        pa_log_debug("The volume of volume control %s changed from %s to %s.", control->name,
                     pa_volume_snprint_verbose(old_volume_str, sizeof(old_volume_str), old_volume.volume,
                                               control->convertible_to_dB),
                     pa_volume_snprint_verbose(new_volume_str, sizeof(new_volume_str), control->volume.volume,
                                               control->convertible_to_dB));
    }

    if (balance_changed) {
        char old_balance_str[PA_BVOLUME_SNPRINT_BALANCE_MAX];
        char new_balance_str[PA_BVOLUME_SNPRINT_BALANCE_MAX];

        pa_log_debug("The balance of volume control %s changed from %s to %s.", control->name,
                     pa_bvolume_snprint_balance(old_balance_str, sizeof(old_balance_str), &control->volume),
                     pa_bvolume_snprint_balance(new_balance_str, sizeof(new_balance_str), &control->volume));
    }

    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_VOLUME_CHANGED], control);
}

int pa_volume_control_set_volume(pa_volume_control *control, const pa_bvolume *volume, bool set_volume, bool set_balance) {
    pa_bvolume volume_local;
    int r;

    pa_assert(control);
    pa_assert(volume);

    if (control->set_volume_in_progress)
        return 0;

    volume_local = *volume;

    if (set_balance && !pa_channel_map_equal(&volume_local.channel_map, &control->volume.channel_map))
        pa_bvolume_remap(&volume_local, &control->volume.channel_map);

    if (pa_bvolume_equal(&volume_local, &control->volume, set_volume, set_balance))
        return 0;

    if (control->linked && control->present) {
        control->set_volume_in_progress = true;
        r = control->set_volume(control, volume, &volume_local, set_volume, set_balance);
        control->set_volume_in_progress = false;

        if (r < 0) {
            pa_log("Setting the volume of volume control %s failed.", control->name);
            return r;
        }
    }

    set_volume_internal(control, &volume_local, set_volume, set_balance);

    return 0;
}

void pa_volume_control_set_channel_map(pa_volume_control *control, const pa_channel_map *map) {
    pa_bvolume bvolume;

    pa_assert(control);
    pa_assert(map);

    if (pa_channel_map_equal(map, &control->volume.channel_map))
        return;

    pa_bvolume_copy_balance(&bvolume, &control->volume);
    pa_bvolume_remap(&bvolume, map);

    set_volume_internal(control, &bvolume, false, true);
}

void pa_volume_control_set_convertible_to_dB(pa_volume_control *control, bool convertible) {
    bool old_convertible;

    pa_assert(control);

    old_convertible = control->convertible_to_dB;

    if (convertible == old_convertible)
        return;

    control->convertible_to_dB = convertible;

    if (control->persistent)
        pa_inidb_cell_set_value(control->db_cells.convertible_to_dB, pa_boolean_to_string(convertible));

    if (!control->linked || control->unlinked)
        return;

    pa_log_debug("The volume of volume control %s became %sconvertible to dB.", control->name, convertible ? "" : "not ");

    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_CONVERTIBLE_TO_DB_CHANGED], control);
}

void pa_volume_control_add_device(pa_volume_control *control, pa_device *device) {
    pa_assert(control);
    pa_assert(device);

    pa_assert_se(pa_hashmap_put(control->devices, device, device) >= 0);
}

void pa_volume_control_remove_device(pa_volume_control *control, pa_device *device) {
    pa_assert(control);
    pa_assert(device);

    pa_assert_se(pa_hashmap_remove(control->devices, device));
}

void pa_volume_control_add_default_for_device(pa_volume_control *control, pa_device *device) {
    pa_assert(control);
    pa_assert(device);

    pa_assert_se(pa_hashmap_put(control->default_for_devices, device, device) >= 0);
}

void pa_volume_control_remove_default_for_device(pa_volume_control *control, pa_device *device) {
    pa_assert(control);
    pa_assert(device);

    pa_assert_se(pa_hashmap_remove(control->default_for_devices, device));
}
