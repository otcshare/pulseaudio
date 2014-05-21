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

#include <modules/volume-api/inidb.h>
#include <modules/volume-api/volume-api.h>

typedef struct pa_mute_control pa_mute_control;

typedef enum {
    PA_MUTE_CONTROL_PURPOSE_STREAM_MUTE,
    PA_MUTE_CONTROL_PURPOSE_OTHER,
} pa_mute_control_purpose_t;

typedef int (*pa_mute_control_set_mute_cb_t)(pa_mute_control *control, bool mute);

struct pa_mute_control {
    pa_volume_api *volume_api;
    uint32_t index;
    const char *name;
    char *description;
    pa_proplist *proplist;
    bool mute;
    bool present;
    bool persistent;

    pa_mute_control_purpose_t purpose;
    union {
        pas_stream *owner_stream;
        void *owner;
    };

    /* If this mute control is the "own mute control" of an audio group, this
     * is set to point to that group, otherwise this is NULL. */
    pa_audio_group *owner_audio_group;

    pa_hashmap *devices; /* pa_device -> pa_device (hashmap-as-a-set) */
    pa_hashmap *default_for_devices; /* pa_device -> pa_device (hashmap-as-a-set) */

    struct {
        pa_inidb_cell *description;
        pa_inidb_cell *mute;
    } db_cells;

    bool linked;
    bool unlinked;
    bool set_mute_in_progress;

    /* Called from pa_mute_control_set_mute(). The implementation is expected
     * to return a negative error code on failure. */
    pa_mute_control_set_mute_cb_t set_mute;

    void *userdata;
};

int pa_mute_control_new(pa_volume_api *api, const char *name, bool persistent, pa_mute_control **_r);
void pa_mute_control_put(pa_mute_control *control);
void pa_mute_control_unlink(pa_mute_control *control);
void pa_mute_control_free(pa_mute_control *control);

/* Called by the mute control implementation, before pa_mute_control_put(). */
void pa_mute_control_set_purpose(pa_mute_control *control, pa_mute_control_purpose_t purpose, void *owner);

/* Called by the mute control implementation. */
int pa_mute_control_acquire_for_audio_group(pa_mute_control *control, pa_audio_group *group,
                                            pa_mute_control_set_mute_cb_t set_mute_cb, void *userdata);

/* Called by the mute control implementation. This must only be called for
 * persistent controls; use pa_mute_control_free() for non-persistent
 * controls. */
void pa_mute_control_release(pa_mute_control *control);

/* Called by anyone. */
void pa_mute_control_set_description(pa_mute_control *control, const char *description);
int pa_mute_control_set_mute(pa_mute_control *control, bool mute);

/* Called from device.c only. */
void pa_mute_control_add_device(pa_mute_control *control, pa_device *device);
void pa_mute_control_remove_device(pa_mute_control *control, pa_device *device);
void pa_mute_control_add_default_for_device(pa_mute_control *control, pa_device *device);
void pa_mute_control_remove_default_for_device(pa_mute_control *control, pa_device *device);

#endif
