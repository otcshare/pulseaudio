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
#include <modules/volume-api/inidb.h>
#include <modules/volume-api/volume-api.h>

typedef struct pa_volume_control pa_volume_control;

typedef enum {
    PA_VOLUME_CONTROL_PURPOSE_STREAM_RELATIVE_VOLUME,
    PA_VOLUME_CONTROL_PURPOSE_OTHER,
} pa_volume_control_purpose_t;

/* Usually remapped_volume is the volume to use, because it has a matching
 * channel map with the control, but in case the volume needs to be propagated
 * to another control, original_volume can be used to avoid loss of precision
 * that can result from remapping. */
typedef int (*pa_volume_control_set_volume_cb_t)(pa_volume_control *control, const pa_bvolume *original_volume,
                                                 const pa_bvolume *remapped_volume, bool set_volume, bool set_balance);

struct pa_volume_control {
    pa_volume_api *volume_api;
    uint32_t index;
    const char *name;
    char *description;
    pa_proplist *proplist;
    pa_bvolume volume;
    bool convertible_to_dB;
    bool present;
    bool persistent;

    pa_volume_control_purpose_t purpose;
    union {
        pas_stream *owner_stream;
        void *owner;
    };

    pa_hashmap *devices; /* pa_device -> pa_device (hashmap-as-a-set) */
    pa_hashmap *default_for_devices; /* pa_device -> pa_device (hashmap-as-a-set) */

    struct {
        pa_inidb_cell *description;
        pa_inidb_cell *volume;
        pa_inidb_cell *balance;
        pa_inidb_cell *convertible_to_dB;
    } db_cells;

    bool linked;
    bool unlinked;
    bool set_volume_in_progress;

    /* Called from pa_volume_control_set_volume(). The implementation is
     * expected to return a negative error code on failure. */
    pa_volume_control_set_volume_cb_t set_volume;

    void *userdata;
};

int pa_volume_control_new(pa_volume_api *api, const char *name, bool persistent, pa_volume_control **_r);
void pa_volume_control_put(pa_volume_control *control);
void pa_volume_control_unlink(pa_volume_control *control);
void pa_volume_control_free(pa_volume_control *control);

/* Called by the volume control implementation, before
 * pa_volume_control_put(). */
void pa_volume_control_set_purpose(pa_volume_control *control, pa_volume_control_purpose_t purpose, void *owner);

/* Called by the volume control implementation. */
int pa_volume_control_acquire_for_audio_group(pa_volume_control *control, pa_audio_group *group,
                                              pa_volume_control_set_volume_cb_t set_volume_cb, void *userdata);

/* Called by the volume control implementation. This must only be called for
 * persistent controls; use pa_volume_control_free() for non-persistent
 * controls. */
void pa_volume_control_release(pa_volume_control *control);

/* Called by anyone. */
void pa_volume_control_set_description(pa_volume_control *control, const char *description);
int pa_volume_control_set_volume(pa_volume_control *control, const pa_bvolume *volume, bool set_volume, bool set_balance);

/* Called by the volume control implementation. */
void pa_volume_control_set_channel_map(pa_volume_control *control, const pa_channel_map *map);
void pa_volume_control_set_convertible_to_dB(pa_volume_control *control, bool convertible);

/* Called from device.c only. */
void pa_volume_control_add_device(pa_volume_control *control, pa_device *device);
void pa_volume_control_remove_device(pa_volume_control *control, pa_device *device);
void pa_volume_control_add_default_for_device(pa_volume_control *control, pa_device *device);
void pa_volume_control_remove_default_for_device(pa_volume_control *control, pa_device *device);

#endif
