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

pa_mute_control *pa_mute_control_new(pa_volume_api *api, const char *name, const char *description) {
    pa_mute_control *control;

    pa_assert(api);
    pa_assert(name);
    pa_assert(description);

    control = pa_xnew0(pa_mute_control, 1);
    control->volume_api = api;
    control->index = pa_volume_api_allocate_mute_control_index(api);
    pa_assert_se(pa_volume_api_register_name(api, name, false, &control->name) >= 0);
    control->description = pa_xstrdup(description);
    control->proplist = pa_proplist_new();
    control->devices = pa_hashmap_new(NULL, NULL);
    control->default_for_devices = pa_hashmap_new(NULL, NULL);
    control->streams = pa_hashmap_new(NULL, NULL);
    control->audio_groups = pa_hashmap_new(NULL, NULL);

    return control;
}

void pa_mute_control_put(pa_mute_control *control, bool initial_mute, bool initial_mute_is_set,
                         pa_mute_control_set_initial_mute_cb_t set_initial_mute_cb) {
    const char *prop_key;
    void *state = NULL;

    pa_assert(control);
    pa_assert(initial_mute_is_set || control->set_mute);
    pa_assert(set_initial_mute_cb || !control->set_mute);

    if (initial_mute_is_set)
        control->mute = initial_mute;
    else
        control->mute = false;

    if (set_initial_mute_cb)
        set_initial_mute_cb(control);

    pa_volume_api_add_mute_control(control->volume_api, control);

    control->linked = true;

    pa_log_debug("Created mute control #%u.", control->index);
    pa_log_debug("    Name: %s", control->name);
    pa_log_debug("    Description: %s", control->description);
    pa_log_debug("    Mute: %s", pa_yes_no(control->mute));
    pa_log_debug("    Properties:");

    while ((prop_key = pa_proplist_iterate(control->proplist, &state)))
        pa_log_debug("        %s = %s", prop_key, pa_strnull(pa_proplist_gets(control->proplist, prop_key)));

    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_PUT], control);
}

void pa_mute_control_unlink(pa_mute_control *control) {
    pa_audio_group *group;
    pa_device *device;
    pas_stream *stream;

    pa_assert(control);

    if (control->unlinked) {
        pa_log_debug("Unlinking mute control %s (already unlinked, this is a no-op).", control->name);
        return;
    }

    control->unlinked = true;

    pa_log_debug("Unlinking mute control %s.", control->name);

    if (control->linked)
        pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_UNLINK], control);

    pa_volume_api_remove_mute_control(control->volume_api, control);

    while ((group = pa_hashmap_first(control->audio_groups)))
        pa_audio_group_set_mute_control(group, NULL);

    while ((stream = pa_hashmap_first(control->streams)))
        pas_stream_set_mute_control(stream, NULL);

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

    if (!control->unlinked)
        pa_mute_control_unlink(control);

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

void pa_mute_control_set_owner_audio_group(pa_mute_control *control, pa_audio_group *group) {
    pa_assert(control);
    pa_assert(group);

    control->owner_audio_group = group;
}

static void set_mute_internal(pa_mute_control *control, bool mute) {
    bool old_mute;

    pa_assert(control);

    old_mute = control->mute;

    if (mute == old_mute)
        return;

    control->mute = mute;

    if (!control->linked || control->unlinked)
        return;

    pa_log_debug("The mute of mute control %s changed from %s to %s.", control->name, pa_yes_no(old_mute),
                 pa_yes_no(control->mute));

    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_MUTE_CHANGED], control);
}

int pa_mute_control_set_mute(pa_mute_control *control, bool mute) {
    int r;

    pa_assert(control);

    if (!control->set_mute) {
        pa_log_info("Tried to set the mute of mute control %s, but the mute control doesn't support the operation.",
                    control->name);
        return -PA_ERR_NOTSUPPORTED;
    }

    if (mute == control->mute)
        return 0;

    control->set_mute_in_progress = true;
    r = control->set_mute(control, mute);
    control->set_mute_in_progress = false;

    if (r >= 0)
        set_mute_internal(control, mute);

    return r;
}

void pa_mute_control_description_changed(pa_mute_control *control, const char *new_description) {
    char *old_description;

    pa_assert(control);
    pa_assert(new_description);

    old_description = control->description;

    if (pa_streq(new_description, old_description))
        return;

    control->description = pa_xstrdup(new_description);
    pa_log_debug("The description of mute control %s changed from \"%s\" to \"%s\".", control->name, old_description,
                 new_description);
    pa_xfree(old_description);
    pa_hook_fire(&control->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_DESCRIPTION_CHANGED], control);
}

void pa_mute_control_mute_changed(pa_mute_control *control, bool new_mute) {
    pa_assert(control);

    if (!control->linked)
        return;

    if (control->set_mute_in_progress)
        return;

    set_mute_internal(control, new_mute);
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

void pa_mute_control_add_stream(pa_mute_control *control, pas_stream *stream) {
    pa_assert(control);
    pa_assert(stream);

    pa_assert_se(pa_hashmap_put(control->streams, stream, stream) >= 0);
}

void pa_mute_control_remove_stream(pa_mute_control *control, pas_stream *stream) {
    pa_assert(control);
    pa_assert(stream);

    pa_assert_se(pa_hashmap_remove(control->streams, stream));
}

void pa_mute_control_add_audio_group(pa_mute_control *control, pa_audio_group *group) {
    pa_assert(control);
    pa_assert(group);

    pa_assert_se(pa_hashmap_put(control->audio_groups, group, group) >= 0);
}

void pa_mute_control_remove_audio_group(pa_mute_control *control, pa_audio_group *group) {
    pa_assert(control);
    pa_assert(group);

    pa_assert_se(pa_hashmap_remove(control->audio_groups, group));
}
