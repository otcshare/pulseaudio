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

#include "volume-api.h"

#include <modules/volume-api/audio-group.h>
#include <modules/volume-api/device.h>
#include <modules/volume-api/device-creator.h>
#include <modules/volume-api/inidb.h>
#include <modules/volume-api/sstream.h>
#include <modules/volume-api/stream-creator.h>
#include <modules/volume-api/volume-control.h>

#include <pulsecore/core-util.h>
#include <pulsecore/namereg.h>
#include <pulsecore/shared.h>

#define CONTROL_DB_TABLE_NAME_VOLUME_CONTROL "VolumeControl"
#define CONTROL_DB_TABLE_NAME_MUTE_CONTROL "MuteControl"

static pa_volume_api *volume_api_new(pa_core *core);
static void volume_api_free(pa_volume_api *api);

pa_volume_api *pa_volume_api_get(pa_core *core) {
    pa_volume_api *api;

    pa_assert(core);

    api = pa_shared_get(core, "volume-api");

    if (api)
        pa_volume_api_ref(api);
    else {
        api = volume_api_new(core);
        pa_assert_se(pa_shared_set(core, "volume-api", api) >= 0);
    }

    return api;
}

pa_volume_api *pa_volume_api_ref(pa_volume_api *api) {
    pa_assert(api);

    api->refcnt++;

    return api;
}

void pa_volume_api_unref(pa_volume_api *api) {
    pa_assert(api);
    pa_assert(api->refcnt > 0);

    api->refcnt--;

    if (api->refcnt == 0) {
        pa_assert_se(pa_shared_remove(api->core, "volume-api") >= 0);
        volume_api_free(api);
    }
}

static int control_db_get_volume_control_cb(pa_inidb *db, const char *name, void **_r) {
    pa_volume_api *api;
    pa_volume_control *control;

    pa_assert(db);
    pa_assert(name);
    pa_assert(_r);

    api = pa_inidb_get_userdata(db);

    control = pa_hashmap_get(api->volume_controls_from_db, name);
    if (!control) {
        int r;

        r = pa_volume_control_new(api, name, true, &control);
        if (r < 0)
            return r;

        pa_hashmap_put(api->volume_controls_from_db, (void *) control->name, control);
    }

    *_r = control;
    return 0;
}

static int control_db_parse_volume_control_description_cb(pa_inidb *db, const char *value, void *object) {
    pa_volume_control *control = object;

    pa_assert(db);
    pa_assert(value);
    pa_assert(control);

    pa_volume_control_set_description(control, value);

    return 0;
}

static int control_db_parse_volume_control_volume_cb(pa_inidb *db, const char *value, void *object) {
    pa_volume_control *control = object;
    int r;
    pa_bvolume bvolume;

    pa_assert(db);
    pa_assert(value);
    pa_assert(control);

    r = pa_atou(value, &bvolume.volume);
    if (r < 0)
        return -PA_ERR_INVALID;

    if (!PA_VOLUME_IS_VALID(bvolume.volume))
        return -PA_ERR_INVALID;

    pa_volume_control_set_volume(control, &bvolume, true, false);

    return 0;
}

static int control_db_parse_volume_control_balance_cb(pa_inidb *db, const char *value, void *object) {
    pa_volume_control *control = object;
    int r;
    pa_bvolume bvolume;

    pa_assert(db);
    pa_assert(value);
    pa_assert(control);

    r = pa_bvolume_parse_balance(value, &bvolume);
    if (r < 0)
        return -PA_ERR_INVALID;

    pa_volume_control_set_channel_map(control, &bvolume.channel_map);
    pa_volume_control_set_volume(control, &bvolume, false, true);

    return 0;
}

static int control_db_parse_volume_control_convertible_to_dB_cb(pa_inidb *db, const char *value, void *object) {
    pa_volume_control *control = object;
    int r;

    pa_assert(db);
    pa_assert(value);
    pa_assert(control);

    r = pa_parse_boolean(value);
    if (r < 0)
        return -PA_ERR_INVALID;

    pa_volume_control_set_convertible_to_dB(control, r);

    return 0;
}

static int control_db_get_mute_control_cb(pa_inidb *db, const char *name, void **_r) {
    pa_volume_api *api;
    pa_mute_control *control;

    pa_assert(db);
    pa_assert(name);
    pa_assert(_r);

    api = pa_inidb_get_userdata(db);

    control = pa_hashmap_get(api->mute_controls_from_db, name);
    if (!control) {
        int r;

        r = pa_mute_control_new(api, name, true, &control);
        if (r < 0)
            return r;

        pa_hashmap_put(api->mute_controls_from_db, (void *) control->name, control);
    }

    *_r = control;
    return 0;
}

static int control_db_parse_mute_control_description_cb(pa_inidb *db, const char *value, void *object) {
    pa_mute_control *control = object;

    pa_assert(db);
    pa_assert(value);
    pa_assert(control);

    pa_mute_control_set_description(control, value);

    return 0;
}

static int control_db_parse_mute_control_mute_cb(pa_inidb *db, const char *value, void *object) {
    pa_mute_control *control = object;
    int mute;

    pa_assert(db);
    pa_assert(value);
    pa_assert(control);

    mute = pa_parse_boolean(value);
    if (mute < 0)
        return -PA_ERR_INVALID;

    pa_mute_control_set_mute(control, mute);

    return 0;
}

static void create_control_db(pa_volume_api *api) {
    pa_volume_control *volume_control;
    pa_mute_control *mute_control;

    pa_assert(api);
    pa_assert(!api->control_db.db);

    api->control_db.db = pa_inidb_new(api->core, "controls", api);

    api->control_db.volume_controls = pa_inidb_add_table(api->control_db.db, CONTROL_DB_TABLE_NAME_VOLUME_CONTROL,
                                                         control_db_get_volume_control_cb);
    pa_inidb_table_add_column(api->control_db.volume_controls, PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_DESCRIPTION,
                              control_db_parse_volume_control_description_cb);
    pa_inidb_table_add_column(api->control_db.volume_controls, PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_VOLUME,
                              control_db_parse_volume_control_volume_cb);
    pa_inidb_table_add_column(api->control_db.volume_controls, PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_BALANCE,
                              control_db_parse_volume_control_balance_cb);
    pa_inidb_table_add_column(api->control_db.volume_controls, PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_CONVERTIBLE_TO_DB,
                              control_db_parse_volume_control_convertible_to_dB_cb);

    api->control_db.mute_controls = pa_inidb_add_table(api->control_db.db, CONTROL_DB_TABLE_NAME_MUTE_CONTROL,
                                                       control_db_get_mute_control_cb);
    pa_inidb_table_add_column(api->control_db.mute_controls, PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_DESCRIPTION,
                              control_db_parse_mute_control_description_cb);
    pa_inidb_table_add_column(api->control_db.mute_controls, PA_VOLUME_API_CONTROL_DB_COLUMN_NAME_MUTE,
                              control_db_parse_mute_control_mute_cb);

    api->volume_controls_from_db = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    api->mute_controls_from_db = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    pa_inidb_load(api->control_db.db);

    while ((volume_control = pa_hashmap_steal_first(api->volume_controls_from_db)))
        pa_volume_control_put(volume_control);

    pa_hashmap_free(api->volume_controls_from_db);
    api->volume_controls_from_db = NULL;

    while ((mute_control = pa_hashmap_steal_first(api->mute_controls_from_db)))
        pa_mute_control_put(mute_control);

    pa_hashmap_free(api->mute_controls_from_db);
    api->mute_controls_from_db = NULL;
}

static void delete_control_db(pa_volume_api *api) {
    pa_assert(api);

    if (!api->control_db.db)
        return;

    pa_inidb_free(api->control_db.db);
    api->control_db.mute_controls = NULL;
    api->control_db.volume_controls = NULL;
    api->control_db.db = NULL;
}

static void create_objects_defer_event_cb(pa_mainloop_api *mainloop_api, pa_defer_event *event, void *userdata) {
    pa_volume_api *volume_api = userdata;

    pa_assert(volume_api);
    pa_assert(event == volume_api->create_objects_defer_event);

    mainloop_api->defer_free(event);
    volume_api->create_objects_defer_event = NULL;

    volume_api->device_creator = pa_device_creator_new(volume_api);
    volume_api->stream_creator = pa_stream_creator_new(volume_api);
}

static pa_volume_api *volume_api_new(pa_core *core) {
    pa_volume_api *api;
    unsigned i;

    pa_assert(core);

    api = pa_xnew0(pa_volume_api, 1);
    api->core = core;
    api->refcnt = 1;
    api->names = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL, pa_xfree);
    api->volume_controls = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    api->mute_controls = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    api->devices = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    api->streams = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    api->audio_groups = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    for (i = 0; i < PA_VOLUME_API_HOOK_MAX; i++)
        pa_hook_init(&api->hooks[i], api);

    create_control_db(api);

    /* We delay the object creation to ensure that policy modules have a chance
     * to affect the initialization of the objects. If we created the objects
     * immediately, policy modules wouldn't have a chance of connecting to the
     * object creation hooks before the objects are created. */
    api->create_objects_defer_event = core->mainloop->defer_new(core->mainloop, create_objects_defer_event_cb, api);

    pa_log_debug("Created a pa_volume_api object.");

    return api;
}

static void volume_api_free(pa_volume_api *api) {
    unsigned i;

    pa_assert(api);
    pa_assert(api->refcnt == 0);

    pa_log_debug("Freeing the pa_volume_api object.");

    pa_assert(!api->mute_controls_from_db);
    pa_assert(!api->volume_controls_from_db);

    if (api->stream_creator)
        pa_stream_creator_free(api->stream_creator);

    if (api->device_creator)
        pa_device_creator_free(api->device_creator);

    if (api->create_objects_defer_event)
        api->core->mainloop->defer_free(api->create_objects_defer_event);

    delete_control_db(api);

    for (i = 0; i < PA_VOLUME_API_HOOK_MAX; i++)
        pa_hook_done(&api->hooks[i]);

    if (api->audio_groups) {
        pa_assert(pa_hashmap_isempty(api->audio_groups));
        pa_hashmap_free(api->audio_groups);
    }

    if (api->streams) {
        pa_assert(pa_hashmap_isempty(api->streams));
        pa_hashmap_free(api->streams);
    }

    if (api->devices) {
        pa_assert(pa_hashmap_isempty(api->devices));
        pa_hashmap_free(api->devices);
    }

    if (api->mute_controls) {
        pa_mute_control *control;

        while ((control = pa_hashmap_first(api->mute_controls))) {
            pa_assert(!control->present);
            pa_mute_control_free(control);
        }

        pa_hashmap_free(api->mute_controls);
    }

    if (api->volume_controls) {
        pa_volume_control *control;

        while ((control = pa_hashmap_first(api->volume_controls))) {
            pa_assert(!control->present);
            pa_volume_control_free(control);
        }

        pa_hashmap_free(api->volume_controls);
    }

    if (api->names) {
        pa_assert(pa_hashmap_isempty(api->names));
        pa_hashmap_free(api->names);
    }

    pa_xfree(api);
}

int pa_volume_api_register_name(pa_volume_api *api, const char *requested_name, bool fail_if_already_registered,
                                const char **registered_name) {
    char *n;

    pa_assert(api);
    pa_assert(requested_name);
    pa_assert(registered_name);

    if (!pa_namereg_is_valid_name(requested_name)) {
        pa_log("Invalid name: \"%s\"", requested_name);
        return -PA_ERR_INVALID;
    }

    n = pa_xstrdup(requested_name);

    if (pa_hashmap_put(api->names, n, n) < 0) {
        unsigned i = 1;

        if (fail_if_already_registered) {
            pa_xfree(n);
            pa_log("Name %s already registered.", requested_name);
            return -PA_ERR_EXIST;
        }

        do {
            pa_xfree(n);
            i++;
            n = pa_sprintf_malloc("%s.%u", requested_name, i);
        } while (pa_hashmap_put(api->names, n, n) < 0);
    }

    *registered_name = n;

    return 0;
}

void pa_volume_api_unregister_name(pa_volume_api *api, const char *name) {
    pa_assert(api);
    pa_assert(name);

    pa_assert_se(pa_hashmap_remove_and_free(api->names, name) >= 0);
}

uint32_t pa_volume_api_allocate_volume_control_index(pa_volume_api *api) {
    uint32_t idx;

    pa_assert(api);

    idx = api->next_volume_control_index++;

    return idx;
}

void pa_volume_api_add_volume_control(pa_volume_api *api, pa_volume_control *control) {
    pa_assert(api);
    pa_assert(control);

    pa_assert_se(pa_hashmap_put(api->volume_controls, (void *) control->name, control) >= 0);
}

int pa_volume_api_remove_volume_control(pa_volume_api *api, pa_volume_control *control) {
    pa_assert(api);
    pa_assert(control);

    if (!pa_hashmap_remove(api->volume_controls, control->name))
        return -1;

    if (control == api->main_output_volume_control)
        pa_volume_api_set_main_output_volume_control(api, NULL);

    if (control == api->main_input_volume_control)
        pa_volume_api_set_main_input_volume_control(api, NULL);

    return 0;
}

pa_volume_control *pa_volume_api_get_volume_control_by_index(pa_volume_api *api, uint32_t idx) {
    pa_volume_control *control;
    void *state;

    pa_assert(api);

    PA_HASHMAP_FOREACH(control, api->volume_controls, state) {
        if (control->index == idx)
            return control;
    }

    return NULL;
}

uint32_t pa_volume_api_allocate_mute_control_index(pa_volume_api *api) {
    uint32_t idx;

    pa_assert(api);

    idx = api->next_mute_control_index++;

    return idx;
}

void pa_volume_api_add_mute_control(pa_volume_api *api, pa_mute_control *control) {
    pa_assert(api);
    pa_assert(control);

    pa_assert_se(pa_hashmap_put(api->mute_controls, (void *) control->name, control) >= 0);
}

int pa_volume_api_remove_mute_control(pa_volume_api *api, pa_mute_control *control) {
    pa_assert(api);
    pa_assert(control);

    if (!pa_hashmap_remove(api->mute_controls, control->name))
        return -1;

    if (control == api->main_output_mute_control)
        pa_volume_api_set_main_output_mute_control(api, NULL);

    if (control == api->main_input_mute_control)
        pa_volume_api_set_main_input_mute_control(api, NULL);

    return 0;
}

pa_mute_control *pa_volume_api_get_mute_control_by_index(pa_volume_api *api, uint32_t idx) {
    pa_mute_control *control;
    void *state;

    pa_assert(api);

    PA_HASHMAP_FOREACH(control, api->mute_controls, state) {
        if (control->index == idx)
            return control;
    }

    return NULL;
}

uint32_t pa_volume_api_allocate_device_index(pa_volume_api *api) {
    uint32_t idx;

    pa_assert(api);

    idx = api->next_device_index++;

    return idx;
}

void pa_volume_api_add_device(pa_volume_api *api, pa_device *device) {
    pa_assert(api);
    pa_assert(device);

    pa_assert_se(pa_hashmap_put(api->devices, (void *) device->name, device) >= 0);
}

int pa_volume_api_remove_device(pa_volume_api *api, pa_device *device) {
    pa_assert(api);
    pa_assert(device);

    if (!pa_hashmap_remove(api->devices, device->name))
        return -1;

    return 0;
}

pa_device *pa_volume_api_get_device_by_index(pa_volume_api *api, uint32_t idx) {
    pa_device *device;
    void *state;

    pa_assert(api);

    PA_HASHMAP_FOREACH(device, api->devices, state) {
        if (device->index == idx)
            return device;
    }

    return NULL;
}

uint32_t pa_volume_api_allocate_stream_index(pa_volume_api *api) {
    uint32_t idx;

    pa_assert(api);

    idx = api->next_stream_index++;

    return idx;
}

void pa_volume_api_add_stream(pa_volume_api *api, pas_stream *stream) {
    pa_assert(api);
    pa_assert(stream);

    pa_assert_se(pa_hashmap_put(api->streams, (void *) stream->name, stream) >= 0);
}

int pa_volume_api_remove_stream(pa_volume_api *api, pas_stream *stream) {
    pa_assert(api);
    pa_assert(stream);

    if (!pa_hashmap_remove(api->streams, stream->name))
        return -1;

    return 0;
}

pas_stream *pa_volume_api_get_stream_by_index(pa_volume_api *api, uint32_t idx) {
    pas_stream *stream;
    void *state;

    pa_assert(api);

    PA_HASHMAP_FOREACH(stream, api->streams, state) {
        if (stream->index == idx)
            return stream;
    }

    return NULL;
}

uint32_t pa_volume_api_allocate_audio_group_index(pa_volume_api *api) {
    uint32_t idx;

    pa_assert(api);

    idx = api->next_audio_group_index++;

    return idx;
}

void pa_volume_api_add_audio_group(pa_volume_api *api, pa_audio_group *group) {
    pa_assert(api);
    pa_assert(group);

    pa_assert_se(pa_hashmap_put(api->audio_groups, (void *) group->name, group) >= 0);
}

int pa_volume_api_remove_audio_group(pa_volume_api *api, pa_audio_group *group) {
    pa_assert(api);
    pa_assert(group);

    if (!pa_hashmap_remove(api->audio_groups, group->name))
        return -1;

    return 0;
}

pa_audio_group *pa_volume_api_get_audio_group_by_index(pa_volume_api *api, uint32_t idx) {
    pa_audio_group *group;
    void *state;

    pa_assert(api);

    PA_HASHMAP_FOREACH(group, api->audio_groups, state) {
        if (group->index == idx)
            return group;
    }

    return NULL;
}

void pa_volume_api_set_main_output_volume_control(pa_volume_api *api, pa_volume_control *control) {
    pa_volume_control *old_control;

    pa_assert(api);

    old_control = api->main_output_volume_control;

    if (control == old_control)
        return;

    api->main_output_volume_control = control;

    pa_log_debug("Main output volume control changed from %s to %s.", old_control ? old_control->name : "(unset)",
                 control ? control->name : "(unset)");

    pa_hook_fire(&api->hooks[PA_VOLUME_API_HOOK_MAIN_OUTPUT_VOLUME_CONTROL_CHANGED], api);
}

void pa_volume_api_set_main_input_volume_control(pa_volume_api *api, pa_volume_control *control) {
    pa_volume_control *old_control;

    pa_assert(api);

    old_control = api->main_input_volume_control;

    if (control == old_control)
        return;

    api->main_input_volume_control = control;

    pa_log_debug("Main input volume control changed from %s to %s.", old_control ? old_control->name : "(unset)",
                 control ? control->name : "(unset)");

    pa_hook_fire(&api->hooks[PA_VOLUME_API_HOOK_MAIN_INPUT_VOLUME_CONTROL_CHANGED], api);
}

void pa_volume_api_set_main_output_mute_control(pa_volume_api *api, pa_mute_control *control) {
    pa_mute_control *old_control;

    pa_assert(api);

    old_control = api->main_output_mute_control;

    if (control == old_control)
        return;

    api->main_output_mute_control = control;

    pa_log_debug("Main output mute control changed from %s to %s.", old_control ? old_control->name : "(unset)",
                 control ? control->name : "(unset)");

    pa_hook_fire(&api->hooks[PA_VOLUME_API_HOOK_MAIN_OUTPUT_MUTE_CONTROL_CHANGED], api);
}

void pa_volume_api_set_main_input_mute_control(pa_volume_api *api, pa_mute_control *control) {
    pa_mute_control *old_control;

    pa_assert(api);

    old_control = api->main_input_mute_control;

    if (control == old_control)
        return;

    api->main_input_mute_control = control;

    pa_log_debug("Main input mute control changed from %s to %s.", old_control ? old_control->name : "(unset)",
                 control ? control->name : "(unset)");

    pa_hook_fire(&api->hooks[PA_VOLUME_API_HOOK_MAIN_INPUT_MUTE_CONTROL_CHANGED], api);
}
