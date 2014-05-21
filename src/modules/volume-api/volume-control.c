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
#include <modules/volume-api/sstream.h>

#include <pulsecore/core-util.h>

pa_volume_control *pa_volume_control_new(pa_volume_api *api, const char *name, const char *description, bool convertible_to_dB,
                                         bool channel_map_is_writable) {
    pa_volume_control *control;

    pa_assert(api);
    pa_assert(name);
    pa_assert(description);

    control = pa_xnew0(pa_volume_control, 1);
    control->volume_api = api;
    control->index = pa_volume_api_allocate_volume_control_index(api);
    pa_assert_se(pa_volume_api_register_name(api, name, false, &control->name) >= 0);
    control->description = pa_xstrdup(description);
    control->proplist = pa_proplist_new();
    pa_bvolume_init_invalid(&control->volume);
    control->convertible_to_dB = convertible_to_dB;
    control->channel_map_is_writable = channel_map_is_writable;
    control->devices = pa_hashmap_new(NULL, NULL);
    control->default_for_devices = pa_hashmap_new(NULL, NULL);
    control->streams = pa_hashmap_new(NULL, NULL);
    control->audio_groups = pa_hashmap_new(NULL, NULL);

    return control;
}

void pa_volume_control_put(pa_volume_control *control, const pa_bvolume *initial_volume,
                           pa_volume_control_set_initial_volume_cb_t set_initial_volume_cb) {
    const char *prop_key;
    void *state = NULL;
    char volume_str[PA_VOLUME_SNPRINT_VERBOSE_MAX];
    char balance_str[PA_BVOLUME_SNPRINT_BALANCE_MAX];

    pa_assert(control);
    pa_assert((initial_volume && pa_bvolume_valid(initial_volume, true, true)) || control->set_volume);
    pa_assert((initial_volume && pa_channel_map_valid(&initial_volume->channel_map)) || control->channel_map_is_writable);
    pa_assert(set_initial_volume_cb || !control->set_volume);

    if (initial_volume && pa_bvolume_valid(initial_volume, true, false))
        control->volume.volume = initial_volume->volume;
    else
        control->volume.volume = PA_VOLUME_NORM / 3;

    if (initial_volume && pa_bvolume_valid(initial_volume, false, true))
        pa_bvolume_copy_balance(&control->volume, initial_volume);
    else if (initial_volume && pa_channel_map_valid(&initial_volume->channel_map))
        pa_bvolume_reset_balance(&control->volume, &initial_volume->channel_map);
    else {
        pa_channel_map_init_mono(&control->volume.channel_map);
        pa_bvolume_reset_balance(&control->volume, &control->volume.channel_map);
    }

    if (set_initial_volume_cb)
        set_initial_volume_cb(control);

    pa_volume_api_add_volume_control(control->volume_api, control);

    control->linked = true;

    pa_log_debug("Created volume control #%u.", control->index);
    pa_log_debug("    Name: %s", control->name);
    pa_log_debug("    Description: %s", control->description);
    pa_log_debug("    Properties:");

    while ((prop_key = pa_proplist_iterate(control->proplist, &state)))
        pa_log_debug("        %s = %s", prop_key, pa_strnull(pa_proplist_gets(control->proplist, prop_key)));

    pa_log_debug("    Volume: %s", pa_volume_snprint_verbose(volume_str, sizeof(volume_str), control->volume.volume,
                 control->convertible_to_dB));
    pa_log_debug("    Balance: %s", pa_bvolume_snprint_balance(balance_str, sizeof(balance_str), &control->volume));
    pa_log_debug("    Channel map is writable: %s", pa_yes_no(control->channel_map_is_writable));

    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_PUT], control);
}

void pa_volume_control_unlink(pa_volume_control *control) {
    pa_audio_group *group;
    pa_device *device;
    pas_stream *stream;

    pa_assert(control);

    if (control->unlinked) {
        pa_log_debug("Unlinking volume control %s (already unlinked, this is a no-op).", control->name);
        return;
    }

    control->unlinked = true;

    pa_log_debug("Unlinking volume control %s.", control->name);

    if (control->linked)
        pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_UNLINK], control);

    pa_volume_api_remove_volume_control(control->volume_api, control);

    while ((group = pa_hashmap_first(control->audio_groups)))
        pa_audio_group_set_volume_control(group, NULL);

    while ((stream = pa_hashmap_first(control->streams)))
        pas_stream_set_volume_control(stream, NULL);

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

    if (!control->unlinked)
        pa_volume_control_unlink(control);

    if (control->audio_groups) {
        pa_assert(pa_hashmap_isempty(control->audio_groups));
        pa_hashmap_free(control->audio_groups);
    }

    if (control->streams) {
        pa_assert(pa_hashmap_isempty(control->streams));
        pa_hashmap_free(control->streams);
    }

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

void pa_volume_control_set_owner_audio_group(pa_volume_control *control, pa_audio_group *group) {
    pa_assert(control);
    pa_assert(group);

    control->owner_audio_group = group;
}

static void set_volume_internal(pa_volume_control *control, const pa_bvolume *volume, bool set_volume, bool set_balance) {
    pa_bvolume old_volume;
    bool volume_changed;
    bool balance_changed;

    pa_assert(control);
    pa_assert(volume);

    old_volume = control->volume;
    volume_changed = !pa_bvolume_equal(volume, &old_volume, set_volume, false);
    balance_changed = !pa_bvolume_equal(volume, &old_volume, false, set_balance);

    if (!volume_changed && !balance_changed)
        return;

    if (volume_changed)
        control->volume.volume = volume->volume;

    if (balance_changed)
        pa_bvolume_copy_balance(&control->volume, volume);

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

    volume_local = *volume;

    if (!control->set_volume) {
        pa_log_info("Tried to set the volume of volume control %s, but the volume control doesn't support the operation.",
                    control->name);
        return -PA_ERR_NOTSUPPORTED;
    }

    if (set_balance
            && !control->channel_map_is_writable
            && !pa_channel_map_equal(&volume_local.channel_map, &control->volume.channel_map))
        pa_bvolume_remap(&volume_local, &control->volume.channel_map);

    if (pa_bvolume_equal(&volume_local, &control->volume, set_volume, set_balance))
        return 0;

    control->set_volume_in_progress = true;
    r = control->set_volume(control, &volume_local, set_volume, set_balance);
    control->set_volume_in_progress = false;

    if (r >= 0)
        set_volume_internal(control, &volume_local, set_volume, set_balance);

    return r;
}

void pa_volume_control_description_changed(pa_volume_control *control, const char *new_description) {
    char *old_description;

    pa_assert(control);
    pa_assert(new_description);

    old_description = control->description;

    if (pa_streq(new_description, old_description))
        return;

    control->description = pa_xstrdup(new_description);
    pa_log_debug("The description of volume control %s changed from \"%s\" to \"%s\".", control->name, old_description,
                 new_description);
    pa_xfree(old_description);
    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_DESCRIPTION_CHANGED], control);
}

void pa_volume_control_volume_changed(pa_volume_control *control, const pa_bvolume *new_volume, bool volume_changed,
                                      bool balance_changed) {
    pa_assert(control);
    pa_assert(new_volume);

    if (!control->linked)
        return;

    if (control->set_volume_in_progress)
        return;

    set_volume_internal(control, new_volume, volume_changed, balance_changed);
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

void pa_volume_control_add_stream(pa_volume_control *control, pas_stream *stream) {
    pa_assert(control);
    pa_assert(stream);

    pa_assert_se(pa_hashmap_put(control->streams, stream, stream) >= 0);
}

void pa_volume_control_remove_stream(pa_volume_control *control, pas_stream *stream) {
    pa_assert(control);
    pa_assert(stream);

    pa_assert_se(pa_hashmap_remove(control->streams, stream));
}

void pa_volume_control_add_audio_group(pa_volume_control *control, pa_audio_group *group) {
    pa_assert(control);
    pa_assert(group);

    pa_assert_se(pa_hashmap_put(control->audio_groups, group, group) >= 0);
}

void pa_volume_control_remove_audio_group(pa_volume_control *control, pa_audio_group *group) {
    pa_assert(control);
    pa_assert(group);

    pa_assert_se(pa_hashmap_remove(control->audio_groups, group));
}
