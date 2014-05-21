#ifndef foomutecontrolhfoo
#define foomutecontrolhfoo

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

typedef struct pa_mute_control pa_mute_control;

struct pa_mute_control {
    pa_volume_api *volume_api;
    uint32_t index;
    const char *name;
    char *description;
    pa_proplist *proplist;
    bool mute;

    /* If this mute control is the "own mute control" of an audio group, this
     * is set to point to that group, otherwise this is NULL. */
    pa_audio_group *owner_audio_group;

    pa_hashmap *devices; /* pa_device -> pa_device (hashmap-as-a-set) */
    pa_hashmap *default_for_devices; /* pa_device -> pa_device (hashmap-as-a-set) */
    pa_hashmap *streams; /* pas_stream -> pas_stream (hashmap-as-a-set) */
    pa_hashmap *audio_groups; /* pa_audio_group -> pa_audio_group (hashmap-as-a-set) */

    bool linked;
    bool unlinked;
    bool set_mute_in_progress;

    /* Called from pa_mute_control_set_mute(). The implementation is expected
     * to return a negative error code on failure. May be NULL, if the mute
     * control is read-only. */
    int (*set_mute)(pa_mute_control *control, bool mute);

    void *userdata;
};

pa_mute_control *pa_mute_control_new(pa_volume_api *api, const char *name, const char *description);

typedef void (*pa_mute_control_set_initial_mute_cb_t)(pa_mute_control *control);

/* initial_mute is the preferred initial mute of the mute control
 * implementation. It may be unset, if the implementation doesn't care about
 * the initial state of the mute control. Read-only mute controls, however,
 * must always set initial_mute.
 *
 * The implementation's initial mute preference may be overridden by policy, if
 * the mute control isn't read-only. When the final initial mute is known, the
 * the implementation is notified via set_initial_mute_cb (the mute can be read
 * from control->mute). set_initial_mute_cb may be NULL, if the mute control is
 * read-only. */
void pa_mute_control_put(pa_mute_control *control, bool initial_mute, bool initial_mute_is_set,
                         pa_mute_control_set_initial_mute_cb_t set_initial_mute_cb);

void pa_mute_control_unlink(pa_mute_control *control);
void pa_mute_control_free(pa_mute_control *control);

/* Called by audio-group.c only. */
void pa_mute_control_set_owner_audio_group(pa_mute_control *control, pa_audio_group *group);

/* Called by clients and policy modules. */
int pa_mute_control_set_mute(pa_mute_control *control, bool mute);

/* Called by the mute control implementation. */
void pa_mute_control_description_changed(pa_mute_control *control, const char *new_description);
void pa_mute_control_mute_changed(pa_mute_control *control, bool new_mute);

/* Called from device.c only. */
void pa_mute_control_add_device(pa_mute_control *control, pa_device *device);
void pa_mute_control_remove_device(pa_mute_control *control, pa_device *device);
void pa_mute_control_add_default_for_device(pa_mute_control *control, pa_device *device);
void pa_mute_control_remove_default_for_device(pa_mute_control *control, pa_device *device);

/* Called from sstream.c only. */
void pa_mute_control_add_stream(pa_mute_control *control, pas_stream *stream);
void pa_mute_control_remove_stream(pa_mute_control *control, pas_stream *stream);

/* Called from audio-group.c only. */
void pa_mute_control_add_audio_group(pa_mute_control *control, pa_audio_group *group);
void pa_mute_control_remove_audio_group(pa_mute_control *control, pa_audio_group *group);

#endif
