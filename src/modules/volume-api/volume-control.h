#ifndef foovolumecontrolhfoo
#define foovolumecontrolhfoo

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

#include <modules/volume-api/bvolume.h>
#include <modules/volume-api/volume-api.h>

typedef struct pa_volume_control pa_volume_control;

struct pa_volume_control {
    pa_volume_api *volume_api;
    uint32_t index;
    const char *name;
    char *description;
    pa_proplist *proplist;
    pa_bvolume volume;
    bool convertible_to_dB;
    bool channel_map_is_writable;

    /* If this volume control is the "own volume control" of an audio group,
     * this is set to point to that group, otherwise this is NULL. */
    pa_audio_group *owner_audio_group;

    pa_hashmap *devices; /* pa_device -> pa_device (hashmap-as-a-set) */
    pa_hashmap *default_for_devices; /* pa_device -> pa_device (hashmap-as-a-set) */
    pa_hashmap *streams; /* pas_stream -> pas_stream (hashmap-as-a-set) */
    pa_hashmap *audio_groups; /* pa_audio_group -> pa_audio_group (hashmap-as-a-set) */

    bool linked;
    bool unlinked;
    bool set_volume_in_progress;

    /* Called from pa_volume_control_set_volume(). The implementation is
     * expected to return a negative error code on failure. May be NULL, if the
     * volume control is read-only. */
    int (*set_volume)(pa_volume_control *control, const pa_bvolume *volume, bool set_volume, bool set_balance);

    void *userdata;
};

pa_volume_control *pa_volume_control_new(pa_volume_api *api, const char *name, const char *description, bool convertible_to_dB,
                                         bool channel_map_is_writable);

typedef void (*pa_volume_control_set_initial_volume_cb_t)(pa_volume_control *control);

/* initial_volume is the preferred initial volume of the volume control
 * implementation. It may be NULL or partially invalid, if the implementation
 * doesn't care about the initial state of the volume control, as long as these
 * two rules are followed:
 *
 *   1) Read-only volume controls must always specify fully valid initial
 *      volume.
 *   2) Volume controls with read-only channel map must always specify a valid
 *      channel map in initial_volume.
 *
 * The implementation's initial volume preference may be overridden by policy,
 * if the volume control isn't read-only. When the final initial volume is
 * known, the implementation is notified via set_initial_volume_cb (the volume
 * can be read from control->volume). set_initial_volume_cb may be NULL, if the
 * volume control is read-only. */
void pa_volume_control_put(pa_volume_control *control, const pa_bvolume *initial_volume,
                           pa_volume_control_set_initial_volume_cb_t set_initial_volume_cb);

void pa_volume_control_unlink(pa_volume_control *control);
void pa_volume_control_free(pa_volume_control *control);

/* Called by audio-group.c only. */
void pa_volume_control_set_owner_audio_group(pa_volume_control *control, pa_audio_group *group);

/* Called by clients and policy modules. */
int pa_volume_control_set_volume(pa_volume_control *control, const pa_bvolume *volume, bool set_volume, bool set_balance);

/* Called by the volume control implementation. */
void pa_volume_control_description_changed(pa_volume_control *control, const char *new_description);
void pa_volume_control_volume_changed(pa_volume_control *control, const pa_bvolume *new_volume, bool volume_changed,
                                      bool balance_changed);

/* Called from device.c only. */
void pa_volume_control_add_device(pa_volume_control *control, pa_device *device);
void pa_volume_control_remove_device(pa_volume_control *control, pa_device *device);
void pa_volume_control_add_default_for_device(pa_volume_control *control, pa_device *device);
void pa_volume_control_remove_default_for_device(pa_volume_control *control, pa_device *device);

/* Called from sstream.c only. */
void pa_volume_control_add_stream(pa_volume_control *control, pas_stream *stream);
void pa_volume_control_remove_stream(pa_volume_control *control, pas_stream *stream);

/* Called from audio-group.c only. */
void pa_volume_control_add_audio_group(pa_volume_control *control, pa_audio_group *group);
void pa_volume_control_remove_audio_group(pa_volume_control *control, pa_audio_group *group);

#endif
