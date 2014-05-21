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

#include "device.h"

#include <modules/volume-api/mute-control.h>
#include <modules/volume-api/volume-control.h>

#include <pulse/direction.h>

#include <pulsecore/core-util.h>

int pa_device_new(pa_volume_api *api, const char *name, const char *description, pa_direction_t direction,
                  const char * const *device_types, unsigned n_device_types, pa_device **_r) {
    pa_device *device = NULL;
    int r;
    unsigned i;

    pa_assert(api);
    pa_assert(name);
    pa_assert(description);
    pa_assert(device_types || n_device_types == 0);
    pa_assert(_r);

    device = pa_xnew0(pa_device, 1);
    device->volume_api = api;
    device->index = pa_volume_api_allocate_device_index(api);

    r = pa_volume_api_register_name(api, name, false, &device->name);
    if (r < 0)
        goto fail;

    device->description = pa_xstrdup(description);
    device->direction = direction;
    device->device_types = pa_dynarray_new(pa_xfree);

    for (i = 0; i < n_device_types; i++)
        pa_dynarray_append(device->device_types, pa_xstrdup(device_types[i]));

    device->proplist = pa_proplist_new();
    device->use_default_volume_control = true;
    device->use_default_mute_control = true;

    *_r = device;
    return 0;

fail:
    if (device)
        pa_device_free(device);

    return r;
}

void pa_device_put(pa_device *device, pa_volume_control *default_volume_control, pa_mute_control *default_mute_control) {
    char *device_types_str;
    const char *prop_key;
    void *state = NULL;

    pa_assert(device);

    if (default_volume_control) {
        device->default_volume_control = default_volume_control;
        pa_volume_control_add_default_for_device(default_volume_control, device);

        device->volume_control = default_volume_control;
        pa_volume_control_add_device(default_volume_control, device);
    }

    if (default_mute_control) {
        device->default_mute_control = default_mute_control;
        pa_mute_control_add_default_for_device(default_mute_control, device);

        device->mute_control = default_mute_control;
        pa_mute_control_add_device(default_mute_control, device);
    }

    pa_volume_api_add_device(device->volume_api, device);
    device->linked = true;

    device_types_str = pa_join((const char * const *) pa_dynarray_get_raw_array(device->device_types),
                               pa_dynarray_size(device->device_types), ", ");

    pa_log_debug("Created device #%u.", device->index);
    pa_log_debug("    Name: %s", device->name);
    pa_log_debug("    Description: %s", device->description);
    pa_log_debug("    Direction: %s", pa_direction_to_string(device->direction));
    pa_log_debug("    Device Types: %s", *device_types_str ? device_types_str : "(none)");
    pa_log_debug("    Volume control: %s", device->volume_control ? device->volume_control->name : "(unset)");
    pa_log_debug("    Mute control: %s", device->mute_control ? device->mute_control->name : "(unset)");
    pa_log_debug("    Properties:");

    while ((prop_key = pa_proplist_iterate(device->proplist, &state)))
        pa_log_debug("        %s = %s", prop_key, pa_strnull(pa_proplist_gets(device->proplist, prop_key)));

    pa_xfree(device_types_str);

    pa_hook_fire(&device->volume_api->hooks[PA_VOLUME_API_HOOK_DEVICE_PUT], device);
}

void pa_device_unlink(pa_device *device) {
    pa_assert(device);

    if (device->unlinked) {
        pa_log_debug("Unlinking device %s (already unlinked, this is a no-op).", device->name);
        return;
    }

    device->unlinked = true;

    pa_log_debug("Unlinking device %s.", device->name);

    if (device->linked)
        pa_volume_api_remove_device(device->volume_api, device);

    pa_hook_fire(&device->volume_api->hooks[PA_VOLUME_API_HOOK_DEVICE_UNLINK], device);

    pa_device_set_mute_control(device, NULL);
    pa_device_set_default_mute_control(device, NULL);
    pa_device_set_volume_control(device, NULL);
    pa_device_set_default_volume_control(device, NULL);
}

void pa_device_free(pa_device *device) {
    pa_assert(device);

    /* unlink() expects name to be set. */
    if (!device->unlinked && device->name)
        pa_device_unlink(device);

    if (device->proplist)
        pa_proplist_free(device->proplist);

    if (device->device_types)
        pa_dynarray_free(device->device_types);

    pa_xfree(device->description);

    if (device->name)
        pa_volume_api_unregister_name(device->volume_api, device->name);

    pa_xfree(device);
}

static void set_volume_control_internal(pa_device *device, pa_volume_control *control) {
    pa_volume_control *old_control;

    pa_assert(device);

    old_control = device->volume_control;

    if (control == old_control)
        return;

    if (old_control)
        pa_volume_control_remove_device(old_control, device);

    device->volume_control = control;

    if (control)
        pa_volume_control_add_device(control, device);

    if (!device->linked || device->unlinked)
        return;

    pa_log_debug("The volume control of device %s changed from %s to %s.", device->name,
                 old_control ? old_control->name : "(unset)", control ? control->name : "(unset)");

    pa_hook_fire(&device->volume_api->hooks[PA_VOLUME_API_HOOK_DEVICE_VOLUME_CONTROL_CHANGED], device);
}

void pa_device_set_volume_control(pa_device *device, pa_volume_control *control) {
    pa_assert(device);

    device->use_default_volume_control = false;
    set_volume_control_internal(device, control);
}

static void set_mute_control_internal(pa_device *device, pa_mute_control *control) {
    pa_mute_control *old_control;

    pa_assert(device);

    old_control = device->mute_control;

    if (control == old_control)
        return;

    if (old_control)
        pa_mute_control_remove_device(old_control, device);

    device->mute_control = control;

    if (control)
        pa_mute_control_add_device(control, device);

    pa_log_debug("The mute control of device %s changed from %s to %s.", device->name,
                 old_control ? old_control->name : "(unset)", control ? control->name : "(unset)");

    pa_hook_fire(&device->volume_api->hooks[PA_VOLUME_API_HOOK_DEVICE_MUTE_CONTROL_CHANGED], device);
}

void pa_device_set_mute_control(pa_device *device, pa_mute_control *control) {
    pa_assert(device);

    device->use_default_mute_control = false;
    set_mute_control_internal(device, control);
}

void pa_device_description_changed(pa_device *device, const char *new_description) {
    char *old_description;

    pa_assert(device);
    pa_assert(new_description);

    old_description = device->description;

    if (pa_streq(new_description, old_description))
        return;

    device->description = pa_xstrdup(new_description);
    pa_log_debug("The description of device %s changed from \"%s\" to \"%s\".", device->name, old_description,
                 new_description);
    pa_xfree(old_description);
    pa_hook_fire(&device->volume_api->hooks[PA_VOLUME_API_HOOK_DEVICE_DESCRIPTION_CHANGED], device);
}

void pa_device_set_default_volume_control(pa_device *device, pa_volume_control *control) {
    pa_volume_control *old_control;

    pa_assert(device);

    old_control = device->default_volume_control;

    if (control == old_control)
        return;

    if (old_control)
        pa_volume_control_remove_default_for_device(old_control, device);

    device->default_volume_control = control;

    if (control)
        pa_volume_control_add_default_for_device(control, device);

    if (device->use_default_volume_control)
        set_volume_control_internal(device, control);
}

void pa_device_set_default_mute_control(pa_device *device, pa_mute_control *control) {
    pa_mute_control *old_control;

    pa_assert(device);

    old_control = device->default_mute_control;

    if (control == old_control)
        return;

    if (old_control)
        pa_mute_control_remove_default_for_device(old_control, device);

    device->default_mute_control = control;

    if (control)
        pa_mute_control_add_default_for_device(control, device);

    if (device->use_default_mute_control)
        set_mute_control_internal(device, control);
}
