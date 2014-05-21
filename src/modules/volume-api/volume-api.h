#ifndef foovolumeapihfoo
#define foovolumeapihfoo

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

#include <pulsecore/core.h>

#define PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_DESCRIPTION "description"
#define PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_VOLUME "volume"
#define PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_BALANCE "balance"
#define PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_CONVERTIBLE_TO_DB "convertible-to-dB"
#define PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_MUTE "mute"

typedef struct pa_volume_api pa_volume_api;

/* Avoid circular dependencies... */
typedef struct pa_audio_group pa_audio_group;
typedef struct pa_device pa_device;
typedef struct pa_device_creator pa_device_creator;
typedef struct pa_inidb pa_inidb;
typedef struct pa_inidb_table pa_inidb_table;
typedef struct pa_mute_control pa_mute_control;
typedef struct pas_stream pas_stream;
typedef struct pa_stream_creator pa_stream_creator;
typedef struct pa_volume_control pa_volume_control;

enum {
    /* This is fired after the volume control implementation has done its part
     * of the volume control initialization, but before policy modules have
     * done their part of the initialization. Hook users are expected to not
     * modify the volume control state in this hook. */
    PA_VOLUME_API_HOOK_VOLUME_CONTROL_IMPLEMENTATION_INITIALIZED,

    /* Policy modules can use this hook to initialize the volume control
     * volume. This is fired before PUT. If a policy module sets the volume, it
     * should return PA_HOOK_STOP to prevent lower-priority policy modules from
     * modifying the volume. */
    PA_VOLUME_API_HOOK_VOLUME_CONTROL_SET_INITIAL_VOLUME,

    PA_VOLUME_API_HOOK_VOLUME_CONTROL_PUT,
    PA_VOLUME_API_HOOK_VOLUME_CONTROL_UNLINK,
    PA_VOLUME_API_HOOK_VOLUME_CONTROL_DESCRIPTION_CHANGED,
    PA_VOLUME_API_HOOK_VOLUME_CONTROL_VOLUME_CHANGED,
    PA_VOLUME_API_HOOK_VOLUME_CONTROL_CONVERTIBLE_TO_DB_CHANGED,

    /* This is fired after the mute control implementation has done its part of
     * the mute control initialization, but before policy modules have done
     * their part of the initialization. Hook users are expected to not modify
     * the mute control state in this hook. */
    PA_VOLUME_API_HOOK_MUTE_CONTROL_IMPLEMENTATION_INITIALIZED,

    /* Policy modules can use this hook to initialize the mute control mute.
     * This is fired before PUT. If a policy module sets the mute, it should
     * return PA_HOOK_STOP to prevent lower-priority policy modules from
     * modifying the mute. */
    PA_VOLUME_API_HOOK_MUTE_CONTROL_SET_INITIAL_MUTE,

    PA_VOLUME_API_HOOK_MUTE_CONTROL_PUT,
    PA_VOLUME_API_HOOK_MUTE_CONTROL_UNLINK,
    PA_VOLUME_API_HOOK_MUTE_CONTROL_DESCRIPTION_CHANGED,
    PA_VOLUME_API_HOOK_MUTE_CONTROL_MUTE_CHANGED,
    PA_VOLUME_API_HOOK_DEVICE_PUT,
    PA_VOLUME_API_HOOK_DEVICE_UNLINK,
    PA_VOLUME_API_HOOK_DEVICE_DESCRIPTION_CHANGED,
    PA_VOLUME_API_HOOK_DEVICE_VOLUME_CONTROL_CHANGED,
    PA_VOLUME_API_HOOK_DEVICE_MUTE_CONTROL_CHANGED,
    PA_VOLUME_API_HOOK_STREAM_PUT,
    PA_VOLUME_API_HOOK_STREAM_UNLINK,
    PA_VOLUME_API_HOOK_STREAM_DESCRIPTION_CHANGED,
    PA_VOLUME_API_HOOK_STREAM_PROPLIST_CHANGED,
    PA_VOLUME_API_HOOK_STREAM_VOLUME_CONTROL_CHANGED,
    PA_VOLUME_API_HOOK_STREAM_RELATIVE_VOLUME_CONTROL_CHANGED,
    PA_VOLUME_API_HOOK_STREAM_MUTE_CONTROL_CHANGED,
    PA_VOLUME_API_HOOK_AUDIO_GROUP_PUT,
    PA_VOLUME_API_HOOK_AUDIO_GROUP_UNLINK,
    PA_VOLUME_API_HOOK_AUDIO_GROUP_DESCRIPTION_CHANGED,
    PA_VOLUME_API_HOOK_AUDIO_GROUP_VOLUME_CONTROL_CHANGED,
    PA_VOLUME_API_HOOK_AUDIO_GROUP_MUTE_CONTROL_CHANGED,
    PA_VOLUME_API_HOOK_MAIN_OUTPUT_VOLUME_CONTROL_CHANGED,
    PA_VOLUME_API_HOOK_MAIN_INPUT_VOLUME_CONTROL_CHANGED,
    PA_VOLUME_API_HOOK_MAIN_OUTPUT_MUTE_CONTROL_CHANGED,
    PA_VOLUME_API_HOOK_MAIN_INPUT_MUTE_CONTROL_CHANGED,
    PA_VOLUME_API_HOOK_MAX
};

struct pa_volume_api {
    pa_core *core;
    unsigned refcnt;
    pa_hashmap *names; /* object name -> object name (hashmap-as-a-set) */
    pa_hashmap *volume_controls; /* name -> pa_volume_control */
    pa_hashmap *mute_controls; /* name -> pa_mute_control */
    pa_hashmap *devices; /* name -> pa_device */
    pa_hashmap *streams; /* name -> pas_stream */
    pa_hashmap *audio_groups; /* name -> pa_audio_group */
    pa_volume_control *main_output_volume_control;
    pa_volume_control *main_input_volume_control;
    pa_mute_control *main_output_mute_control;
    pa_mute_control *main_input_mute_control;

    uint32_t next_volume_control_index;
    uint32_t next_mute_control_index;
    uint32_t next_device_index;
    uint32_t next_stream_index;
    uint32_t next_audio_group_index;
    pa_hook hooks[PA_VOLUME_API_HOOK_MAX];

    struct {
        pa_inidb *db;
        pa_inidb_table *volume_controls;
        pa_inidb_table *mute_controls;
    } control_db;

    pa_defer_event *create_objects_defer_event;
    pa_device_creator *device_creator;
    pa_stream_creator *stream_creator;

    pa_hashmap *volume_controls_from_db; /* control name -> pa_volume_control, only used during initialization. */
    pa_hashmap *mute_controls_from_db; /* control name -> pa_mute_control, only used during initialization. */
};

pa_volume_api *pa_volume_api_get(pa_core *core);
pa_volume_api *pa_volume_api_ref(pa_volume_api *api);
void pa_volume_api_unref(pa_volume_api *api);

int pa_volume_api_register_name(pa_volume_api *api, const char *requested_name, bool fail_if_already_registered,
                                const char **registered_name);
void pa_volume_api_unregister_name(pa_volume_api *api, const char *name);

uint32_t pa_volume_api_allocate_volume_control_index(pa_volume_api *api);
void pa_volume_api_add_volume_control(pa_volume_api *api, pa_volume_control *control);
int pa_volume_api_remove_volume_control(pa_volume_api *api, pa_volume_control *control);
pa_volume_control *pa_volume_api_get_volume_control_by_index(pa_volume_api *api, uint32_t idx);

uint32_t pa_volume_api_allocate_mute_control_index(pa_volume_api *api);
void pa_volume_api_add_mute_control(pa_volume_api *api, pa_mute_control *control);
int pa_volume_api_remove_mute_control(pa_volume_api *api, pa_mute_control *control);
pa_mute_control *pa_volume_api_get_mute_control_by_index(pa_volume_api *api, uint32_t idx);

uint32_t pa_volume_api_allocate_device_index(pa_volume_api *api);
void pa_volume_api_add_device(pa_volume_api *api, pa_device *device);
int pa_volume_api_remove_device(pa_volume_api *api, pa_device *device);
pa_device *pa_volume_api_get_device_by_index(pa_volume_api *api, uint32_t idx);

uint32_t pa_volume_api_allocate_stream_index(pa_volume_api *api);
void pa_volume_api_add_stream(pa_volume_api *api, pas_stream *stream);
int pa_volume_api_remove_stream(pa_volume_api *api, pas_stream *stream);
pas_stream *pa_volume_api_get_stream_by_index(pa_volume_api *api, uint32_t idx);

uint32_t pa_volume_api_allocate_audio_group_index(pa_volume_api *api);
void pa_volume_api_add_audio_group(pa_volume_api *api, pa_audio_group *group);
int pa_volume_api_remove_audio_group(pa_volume_api *api, pa_audio_group *group);
pa_audio_group *pa_volume_api_get_audio_group_by_index(pa_volume_api *api, uint32_t idx);

void pa_volume_api_set_main_output_volume_control(pa_volume_api *api, pa_volume_control *control);
void pa_volume_api_set_main_input_volume_control(pa_volume_api *api, pa_volume_control *control);
void pa_volume_api_set_main_output_mute_control(pa_volume_api *api, pa_mute_control *control);
void pa_volume_api_set_main_input_mute_control(pa_volume_api *api, pa_mute_control *control);

#endif
