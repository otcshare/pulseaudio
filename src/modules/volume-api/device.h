#ifndef foodevicehfoo
#define foodevicehfoo

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

#include <modules/volume-api/volume-api.h>

#include <pulsecore/dynarray.h>

typedef struct pa_device pa_device;

struct pa_device {
    pa_volume_api *volume_api;
    uint32_t index;
    const char *name;
    char *description;
    pa_direction_t direction;
    pa_dynarray *device_types;
    pa_proplist *proplist;
    pa_volume_control *volume_control;
    pa_mute_control *mute_control;

    /* The device implementation can provide default volume and mute controls,
     * which are used in case there's no policy module that wants to override
     * the defaults. */
    pa_volume_control *default_volume_control;
    bool use_default_volume_control;
    pa_mute_control *default_mute_control;
    bool use_default_mute_control;

    bool linked;
    bool unlinked;
};

int pa_device_new(pa_volume_api *api, const char *name, const char *description, pa_direction_t direction,
                  const char * const *device_types, unsigned n_device_types, pa_device **_r);
void pa_device_put(pa_device *device, pa_volume_control *default_volume_control, pa_mute_control *default_mute_control);
void pa_device_unlink(pa_device *device);
void pa_device_free(pa_device *device);

/* Called by policy modules. */
void pa_device_set_volume_control(pa_device *device, pa_volume_control *control);
void pa_device_set_mute_control(pa_device *device, pa_mute_control *control);

/* Called by policy modules. Note that pa_device_set_volume_control() and
 * pa_device_set_mute_control() automatically disable the corresponding
 * use_default flags, so these functions are mainly useful for re-enabling the
 * flags. */
void pa_device_set_use_default_volume_control(pa_device *device, bool use);
void pa_device_set_use_default_mute_control(pa_device *device, bool use);

/* Called by the device implementation. */
void pa_device_description_changed(pa_device *device, const char *new_description);
void pa_device_set_default_volume_control(pa_device *device, pa_volume_control *control);
void pa_device_set_default_mute_control(pa_device *device, pa_mute_control *control);

#endif
