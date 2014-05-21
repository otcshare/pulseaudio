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

#include "audio-group.h"

#include <modules/volume-api/sstream.h>

#include <pulsecore/core-util.h>

int pa_audio_group_new(pa_volume_api *api, const char *name, const char *description, pa_audio_group **group) {
    pa_audio_group *group_local;
    int r;

    pa_assert(api);
    pa_assert(name);
    pa_assert(description);
    pa_assert(group);

    group_local = pa_xnew0(pa_audio_group, 1);
    group_local->volume_api = api;
    group_local->index = pa_volume_api_allocate_audio_group_index(api);

    r = pa_volume_api_register_name(api, name, true, &group_local->name);
    if (r < 0)
        goto fail;

    group_local->description = pa_xstrdup(description);
    group_local->proplist = pa_proplist_new();
    group_local->volume_streams = pa_hashmap_new(NULL, NULL);
    group_local->mute_streams = pa_hashmap_new(NULL, NULL);

    *group = group_local;

    return 0;

fail:
    pa_audio_group_free(group_local);

    return r;
}

void pa_audio_group_put(pa_audio_group *group) {
    const char *prop_key;
    void *state = NULL;

    pa_assert(group);

    pa_volume_api_add_audio_group(group->volume_api, group);

    group->linked = true;

    pa_log_debug("Created audio group #%u.", group->index);
    pa_log_debug("    Name: %s", group->name);
    pa_log_debug("    Description: %s", group->description);
    pa_log_debug("    Volume control: %s", group->volume_control ? group->volume_control->name : "(unset)");
    pa_log_debug("    Mute control: %s", group->mute_control ? group->mute_control->name : "(unset)");
    pa_log_debug("    Properties:");

    while ((prop_key = pa_proplist_iterate(group->proplist, &state)))
        pa_log_debug("        %s = %s", prop_key, pa_strnull(pa_proplist_gets(group->proplist, prop_key)));

    pa_hook_fire(&group->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_PUT], group);
}

void pa_audio_group_unlink(pa_audio_group *group) {
    pas_stream *stream;

    pa_assert(group);

    if (group->unlinked) {
        pa_log_debug("Unlinking audio group %s (already unlinked, this is a no-op).", group->name);
        return;
    }

    group->unlinked = true;

    pa_log_debug("Unlinking audio group %s.", group->name);

    if (group->linked)
        pa_hook_fire(&group->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_UNLINK], group);

    pa_volume_api_remove_audio_group(group->volume_api, group);

    while ((stream = pa_hashmap_first(group->mute_streams)))
        pas_stream_set_audio_group_for_mute(stream, NULL);

    while ((stream = pa_hashmap_first(group->volume_streams)))
        pas_stream_set_audio_group_for_volume(stream, NULL);

    if (group->mute_control_binding) {
        pa_binding_free(group->mute_control_binding);
        group->mute_control_binding = NULL;
    }

    if (group->volume_control_binding) {
        pa_binding_free(group->volume_control_binding);
        group->volume_control_binding = NULL;
    }

    pa_audio_group_set_have_own_mute_control(group, false);
    pa_audio_group_set_have_own_volume_control(group, false);

    if (group->mute_control) {
        pa_mute_control_remove_audio_group(group->mute_control, group);
        group->mute_control = NULL;
    }

    if (group->volume_control) {
        pa_volume_control_remove_audio_group(group->volume_control, group);
        group->volume_control = NULL;
    }
}

void pa_audio_group_free(pa_audio_group *group) {
    pa_assert(group);

    if (!group->unlinked)
        pa_audio_group_unlink(group);

    if (group->mute_streams)
        pa_hashmap_free(group->mute_streams);

    if (group->volume_streams)
        pa_hashmap_free(group->volume_streams);

    if (group->proplist)
        pa_proplist_free(group->proplist);

    pa_xfree(group->description);

    if (group->name)
        pa_volume_api_unregister_name(group->volume_api, group->name);

    pa_xfree(group);
}

const char *pa_audio_group_get_name(pa_audio_group *group) {
    pa_assert(group);

    return group->name;
}

static int volume_control_set_volume_cb(pa_volume_control *control, const pa_bvolume *volume, bool set_volume,
                                        bool set_balance) {
    pa_audio_group *group;
    pas_stream *stream;
    void *state;

    pa_assert(control);
    pa_assert(volume);

    group = control->userdata;

    PA_HASHMAP_FOREACH(stream, group->volume_streams, state) {
        if (stream->own_volume_control)
            pa_volume_control_set_volume(stream->own_volume_control, volume, set_volume, set_balance);
    }

    return 0;
}

static void volume_control_set_initial_volume_cb(pa_volume_control *control) {
    pa_audio_group *group;
    pas_stream *stream;
    void *state;

    pa_assert(control);

    group = control->userdata;

    PA_HASHMAP_FOREACH(stream, group->volume_streams, state) {
        if (stream->own_volume_control)
            pa_volume_control_set_volume(stream->own_volume_control, &control->volume, true, true);
    }
}

void pa_audio_group_set_have_own_volume_control(pa_audio_group *group, bool have) {
    pa_assert(group);

    if (have == group->have_own_volume_control)
        return;

    if (have) {
        pa_bvolume initial_volume;

        if (group->volume_api->core->flat_volumes)
            /* Usually the initial volume should get overridden by some module
             * that manages audio group volume levels, but if there's no such
             * module, let's try to avoid too high volume in flat volume
             * mode. */
            pa_bvolume_init_mono(&initial_volume, 0.3 * PA_VOLUME_NORM);
        else
            pa_bvolume_init_mono(&initial_volume, PA_VOLUME_NORM);

        pa_assert(!group->own_volume_control);
        group->own_volume_control = pa_volume_control_new(group->volume_api, "audio-group-volume-control",
                                                          group->description, false, false);
        pa_volume_control_set_owner_audio_group(group->own_volume_control, group);
        group->own_volume_control->set_volume = volume_control_set_volume_cb;
        group->own_volume_control->userdata = group;
        pa_volume_control_put(group->own_volume_control, &initial_volume, volume_control_set_initial_volume_cb);
    } else {
        pa_volume_control_free(group->own_volume_control);
        group->own_volume_control = NULL;
    }

    group->have_own_volume_control = have;
}

static int mute_control_set_mute_cb(pa_mute_control *control, bool mute) {
    pa_audio_group *group;
    pas_stream *stream;
    void *state;

    pa_assert(control);

    group = control->userdata;

    PA_HASHMAP_FOREACH(stream, group->mute_streams, state) {
        if (stream->own_mute_control)
            pa_mute_control_set_mute(stream->own_mute_control, mute);
    }

    return 0;
}

static void mute_control_set_initial_mute_cb(pa_mute_control *control) {
    pa_audio_group *group;
    pas_stream *stream;
    void *state;

    pa_assert(control);

    group = control->userdata;

    PA_HASHMAP_FOREACH(stream, group->mute_streams, state) {
        if (stream->own_mute_control)
            pa_mute_control_set_mute(stream->own_mute_control, control->mute);
    }
}

void pa_audio_group_set_have_own_mute_control(pa_audio_group *group, bool have) {
    pa_assert(group);

    if (have == group->have_own_mute_control)
        return;

    group->have_own_mute_control = have;

    if (have) {
        pa_assert(!group->own_mute_control);
        group->own_mute_control = pa_mute_control_new(group->volume_api, "audio-group-mute-control", group->description);
        pa_mute_control_set_owner_audio_group(group->own_mute_control, group);
        group->own_mute_control->set_mute = mute_control_set_mute_cb;
        group->own_mute_control->userdata = group;
        pa_mute_control_put(group->own_mute_control, false, true, mute_control_set_initial_mute_cb);
    } else {
        pa_mute_control_free(group->own_mute_control);
        group->own_mute_control = NULL;
    }
}

static void set_volume_control_internal(pa_audio_group *group, pa_volume_control *control) {
    pa_volume_control *old_control;

    pa_assert(group);

    old_control = group->volume_control;

    if (control == old_control)
        return;

    if (old_control)
        pa_volume_control_remove_audio_group(old_control, group);

    group->volume_control = control;

    if (control)
        pa_volume_control_add_audio_group(control, group);

    if (!group->linked || group->unlinked)
        return;

    pa_log_debug("The volume control of audio group %s changed from %s to %s.", group->name,
                 old_control ? old_control->name : "(unset)", control ? control->name : "(unset)");

    pa_hook_fire(&group->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_VOLUME_CONTROL_CHANGED], group);
}

void pa_audio_group_set_volume_control(pa_audio_group *group, pa_volume_control *control) {
    pa_assert(group);

    if (group->volume_control_binding) {
        pa_binding_free(group->volume_control_binding);
        group->volume_control_binding = NULL;
    }

    set_volume_control_internal(group, control);
}

static void set_mute_control_internal(pa_audio_group *group, pa_mute_control *control) {
    pa_mute_control *old_control;

    pa_assert(group);

    old_control = group->mute_control;

    if (control == old_control)
        return;

    if (old_control)
        pa_mute_control_remove_audio_group(old_control, group);

    group->mute_control = control;

    if (control)
        pa_mute_control_add_audio_group(control, group);

    if (!group->linked || group->unlinked)
        return;

    pa_log_debug("The mute control of audio group %s changed from %s to %s.", group->name,
                 old_control ? old_control->name : "(unset)", control ? control->name : "(unset)");

    pa_hook_fire(&group->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_MUTE_CONTROL_CHANGED], group);
}

void pa_audio_group_set_mute_control(pa_audio_group *group, pa_mute_control *control) {
    pa_assert(group);

    if (group->mute_control_binding) {
        pa_binding_free(group->mute_control_binding);
        group->mute_control_binding = NULL;
    }

    set_mute_control_internal(group, control);
}

void pa_audio_group_bind_volume_control(pa_audio_group *group, pa_binding_target_info *target_info) {
    pa_binding_owner_info owner_info = {
        .userdata = group,
        .set_value = (pa_binding_set_value_cb_t) set_volume_control_internal,
    };

    pa_assert(group);
    pa_assert(target_info);

    if (group->volume_control_binding)
        pa_binding_free(group->volume_control_binding);

    group->volume_control_binding = pa_binding_new(group->volume_api, &owner_info, target_info);
}

void pa_audio_group_bind_mute_control(pa_audio_group *group, pa_binding_target_info *target_info) {
    pa_binding_owner_info owner_info = {
        .userdata = group,
        .set_value = (pa_binding_set_value_cb_t) set_mute_control_internal,
    };

    pa_assert(group);
    pa_assert(target_info);

    if (group->mute_control_binding)
        pa_binding_free(group->mute_control_binding);

    group->mute_control_binding = pa_binding_new(group->volume_api, &owner_info, target_info);
}

void pa_audio_group_add_volume_stream(pa_audio_group *group, pas_stream *stream) {
    pa_assert(group);
    pa_assert(stream);

    pa_assert_se(pa_hashmap_put(group->volume_streams, stream, stream) >= 0);

    if (stream->own_volume_control && group->own_volume_control)
        pa_volume_control_set_volume(stream->own_volume_control, &group->own_volume_control->volume, true, true);

    pa_log_debug("Stream %s added to audio group %s (volume).", stream->name, group->name);
}

void pa_audio_group_remove_volume_stream(pa_audio_group *group, pas_stream *stream) {
    pa_assert(group);
    pa_assert(stream);

    pa_assert_se(pa_hashmap_remove(group->volume_streams, stream));

    pa_log_debug("Stream %s removed from audio group %s (volume).", stream->name, group->name);
}

void pa_audio_group_add_mute_stream(pa_audio_group *group, pas_stream *stream) {
    pa_assert(group);
    pa_assert(stream);

    pa_assert_se(pa_hashmap_put(group->mute_streams, stream, stream) >= 0);

    if (stream->own_mute_control && group->own_mute_control)
        pa_mute_control_set_mute(stream->own_mute_control, group->own_mute_control->mute);

    pa_log_debug("Stream %s added to audio group %s (mute).", stream->name, group->name);
}

void pa_audio_group_remove_mute_stream(pa_audio_group *group, pas_stream *stream) {
    pa_assert(group);
    pa_assert(stream);

    pa_assert_se(pa_hashmap_remove(group->mute_streams, stream));

    pa_log_debug("Stream %s removed from audio group %s (mute).", stream->name, group->name);
}

pa_binding_target_type *pa_audio_group_create_binding_target_type(pa_volume_api *api) {
    pa_binding_target_type *type;

    pa_assert(api);

    type = pa_binding_target_type_new(PA_AUDIO_GROUP_BINDING_TARGET_TYPE, api->audio_groups,
                                      &api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_PUT],
                                      &api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_UNLINK],
                                      (pa_binding_target_type_get_name_cb_t) pa_audio_group_get_name);
    pa_binding_target_type_add_field(type, PA_AUDIO_GROUP_BINDING_TARGET_FIELD_VOLUME_CONTROL,
                                     PA_BINDING_CALCULATE_FIELD_OFFSET(pa_audio_group, volume_control));
    pa_binding_target_type_add_field(type, PA_AUDIO_GROUP_BINDING_TARGET_FIELD_MUTE_CONTROL,
                                     PA_BINDING_CALCULATE_FIELD_OFFSET(pa_audio_group, mute_control));

    return type;
}
