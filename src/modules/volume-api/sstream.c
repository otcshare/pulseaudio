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

#include "sstream.h"

#include <modules/volume-api/audio-group.h>
#include <modules/volume-api/binding.h>
#include <modules/volume-api/mute-control.h>
#include <modules/volume-api/volume-control.h>

#include <pulse/direction.h>

#include <pulsecore/core-util.h>

pas_stream *pas_stream_new(pa_volume_api *api, const char *name, const char *description, pa_direction_t direction) {
    pas_stream *stream;

    pa_assert(api);
    pa_assert(name);
    pa_assert(description);

    stream = pa_xnew0(pas_stream, 1);
    stream->volume_api = api;
    stream->index = pa_volume_api_allocate_stream_index(api);
    pa_assert_se(pa_volume_api_register_name(api, name, false, &stream->name) >= 0);
    stream->description = pa_xstrdup(description);
    stream->direction = direction;
    stream->proplist = pa_proplist_new();
    stream->use_default_volume_control = true;
    stream->use_default_mute_control = true;

    return stream;
}

static void set_volume_control_internal(pas_stream *stream, pa_volume_control *control) {
    pa_volume_control *old_control;

    pa_assert(stream);

    old_control = stream->volume_control;

    if (control == old_control)
        return;

    if (old_control) {
        /* If the old control pointed to the own volume control of an audio
         * group, then the stream's audio group for volume needs to be
         * updated. We set it to NULL here, and if it should be non-NULL, that
         * will be fixed very soon (a few lines down). */
        pas_stream_set_audio_group_for_volume(stream, NULL);

        pa_volume_control_remove_stream(old_control, stream);
    }

    stream->volume_control = control;

    if (control) {
        pa_volume_control_add_stream(control, stream);
        pas_stream_set_audio_group_for_volume(stream, control->owner_audio_group);
    }

    if (!stream->linked || stream->unlinked)
        return;

    pa_log_debug("The volume control of stream %s changed from %s to %s.", stream->name,
                 old_control ? old_control->name : "(unset)", control ? control->name : "(unset)");

    pa_hook_fire(&stream->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_VOLUME_CONTROL_CHANGED], stream);
}

static void set_mute_control_internal(pas_stream *stream, pa_mute_control *control) {
    pa_mute_control *old_control;

    pa_assert(stream);

    old_control = stream->mute_control;

    if (control == old_control)
        return;

    if (old_control) {
        /* If the old control pointed to the own mute control of an audio
         * group, then the stream's audio group for mute needs to be updated.
         * We set it to NULL here, and if it should be non-NULL, that will be
         * fixed very soon (a few lines down). */
        pas_stream_set_audio_group_for_mute(stream, NULL);

        pa_mute_control_remove_stream(old_control, stream);
    }

    stream->mute_control = control;

    if (control) {
        pa_mute_control_add_stream(control, stream);
        pas_stream_set_audio_group_for_mute(stream, control->owner_audio_group);
    }

    if (!stream->linked || stream->unlinked)
        return;

    pa_log_debug("The mute control of stream %s changed from %s to %s.", stream->name,
                 old_control ? old_control->name : "(unset)", control ? control->name : "(unset)");

    pa_hook_fire(&stream->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_MUTE_CONTROL_CHANGED], stream);
}

void pas_stream_put(pas_stream *stream, pa_proplist *initial_properties) {
    const char *prop_key;
    void *state = NULL;

    pa_assert(stream);
    pa_assert(!stream->create_own_volume_control || stream->delete_own_volume_control);
    pa_assert(!stream->create_own_mute_control || stream->delete_own_mute_control);

    if (initial_properties)
        pa_proplist_update(stream->proplist, PA_UPDATE_REPLACE, initial_properties);

    pa_hook_fire(&stream->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_SET_INITIAL_VOLUME_CONTROL], stream);

    if (stream->use_default_volume_control)
        set_volume_control_internal(stream, stream->own_volume_control);

    pa_hook_fire(&stream->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_SET_INITIAL_MUTE_CONTROL], stream);

    if (stream->use_default_mute_control)
        set_mute_control_internal(stream, stream->own_mute_control);

    pa_volume_api_add_stream(stream->volume_api, stream);

    stream->linked = true;

    pa_log_debug("Created stream #%u.", stream->index);
    pa_log_debug("    Name: %s", stream->name);
    pa_log_debug("    Description: %s", stream->description);
    pa_log_debug("    Direction: %s", pa_direction_to_string(stream->direction));
    pa_log_debug("    Volume control: %s", stream->volume_control ? stream->volume_control->name : "(unset)");
    pa_log_debug("    Mute control: %s", stream->mute_control ? stream->mute_control->name : "(unset)");
    pa_log_debug("    Properties:");

    while ((prop_key = pa_proplist_iterate(stream->proplist, &state)))
        pa_log_debug("        %s = %s", prop_key, pa_strnull(pa_proplist_gets(stream->proplist, prop_key)));

    pa_hook_fire(&stream->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_PUT], stream);
}

void pas_stream_unlink(pas_stream *stream) {
    pa_assert(stream);

    if (stream->unlinked) {
        pa_log_debug("Unlinking stream %s (already unlinked, this is a no-op).", stream->name);
        return;
    }

    stream->unlinked = true;

    pa_log_debug("Unlinking stream %s.", stream->name);

    if (stream->linked)
        pa_hook_fire(&stream->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_UNLINK], stream);

    pa_volume_api_remove_stream(stream->volume_api, stream);

    pas_stream_set_audio_group_for_mute(stream, NULL);
    pas_stream_set_audio_group_for_volume(stream, NULL);
    pas_stream_set_mute_control(stream, NULL);
    pas_stream_set_volume_control(stream, NULL);
    pas_stream_set_have_own_mute_control(stream, false);
    pas_stream_set_have_own_volume_control(stream, false);
}

void pas_stream_free(pas_stream *stream) {
    pa_assert(stream);

    if (!stream->unlinked)
        pas_stream_unlink(stream);

    if (stream->proplist)
        pa_proplist_free(stream->proplist);

    pa_xfree(stream->description);

    if (stream->name)
        pa_volume_api_unregister_name(stream->volume_api, stream->name);

    pa_xfree(stream);
}

int pas_stream_set_have_own_volume_control(pas_stream *stream, bool have) {
    pa_assert(stream);

    if (have == stream->have_own_volume_control)
        return 0;

    if (have) {
        pa_assert(!stream->own_volume_control);

        if (!stream->create_own_volume_control) {
            pa_log_debug("Stream %s doesn't support own volume control.", stream->name);
            return -PA_ERR_NOTSUPPORTED;
        }

        stream->own_volume_control = stream->create_own_volume_control(stream);
    } else {
        stream->delete_own_volume_control(stream);
        stream->own_volume_control = NULL;
    }

    stream->have_own_volume_control = have;

    return 0;
}

int pas_stream_set_have_own_mute_control(pas_stream *stream, bool have) {
    pa_assert(stream);

    if (have == stream->have_own_mute_control)
        return 0;

    if (have) {
        pa_assert(!stream->own_mute_control);

        if (!stream->create_own_mute_control) {
            pa_log_debug("Stream %s doesn't support own mute control.", stream->name);
            return -PA_ERR_NOTSUPPORTED;
        }

        stream->own_mute_control = stream->create_own_mute_control(stream);
    } else {
        stream->delete_own_mute_control(stream);
        stream->own_mute_control = NULL;
    }

    stream->have_own_mute_control = have;

    return 0;
}

void pas_stream_set_volume_control(pas_stream *stream, pa_volume_control *control) {
    pa_assert(stream);

    stream->use_default_volume_control = false;

    if (stream->volume_control_binding) {
        pa_binding_free(stream->volume_control_binding);
        stream->volume_control_binding = NULL;
    }

    set_volume_control_internal(stream, control);
}

void pas_stream_set_mute_control(pas_stream *stream, pa_mute_control *control) {
    pa_assert(stream);

    stream->use_default_mute_control = false;

    if (stream->mute_control_binding) {
        pa_binding_free(stream->mute_control_binding);
        stream->mute_control_binding = NULL;
    }

    set_mute_control_internal(stream, control);
}

void pas_stream_bind_volume_control(pas_stream *stream, pa_binding_target_info *target_info) {
    pa_binding_owner_info owner_info = {
        .userdata = stream,
        .set_value = (pa_binding_set_value_cb_t) set_volume_control_internal,
    };

    pa_assert(stream);
    pa_assert(target_info);

    stream->use_default_volume_control = false;

    if (stream->volume_control_binding)
        pa_binding_free(stream->volume_control_binding);

    stream->volume_control_binding = pa_binding_new(stream->volume_api, &owner_info, target_info);
}

void pas_stream_bind_mute_control(pas_stream *stream, pa_binding_target_info *target_info) {
    pa_binding_owner_info owner_info = {
        .userdata = stream,
        .set_value = (pa_binding_set_value_cb_t) set_mute_control_internal,
    };

    pa_assert(stream);
    pa_assert(target_info);

    stream->use_default_mute_control = false;

    if (stream->mute_control_binding)
        pa_binding_free(stream->mute_control_binding);

    stream->mute_control_binding = pa_binding_new(stream->volume_api, &owner_info, target_info);
}

void pas_stream_description_changed(pas_stream *stream, const char *new_description) {
    char *old_description;

    pa_assert(stream);
    pa_assert(new_description);

    old_description = stream->description;

    if (pa_streq(new_description, old_description))
        return;

    stream->description = pa_xstrdup(new_description);
    pa_log_debug("The description of stream %s changed from \"%s\" to \"%s\".", stream->name, old_description,
                 new_description);
    pa_xfree(old_description);
    pa_hook_fire(&stream->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_DESCRIPTION_CHANGED], stream);
}

void pas_stream_set_audio_group_for_volume(pas_stream *stream, pa_audio_group *group) {
    pa_assert(stream);

    if (group == stream->audio_group_for_volume)
        return;

    if (stream->audio_group_for_volume)
        pa_audio_group_remove_volume_stream(stream->audio_group_for_volume, stream);

    stream->audio_group_for_volume = group;

    if (group)
        pa_audio_group_add_volume_stream(group, stream);
}

void pas_stream_set_audio_group_for_mute(pas_stream *stream, pa_audio_group *group) {
    pa_assert(stream);

    if (group == stream->audio_group_for_mute)
        return;

    if (stream->audio_group_for_mute)
        pa_audio_group_remove_mute_stream(stream->audio_group_for_mute, stream);

    stream->audio_group_for_mute = group;

    if (group)
        pa_audio_group_add_mute_stream(group, stream);
}
