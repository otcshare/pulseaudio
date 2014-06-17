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
#include <modules/volume-api/mute-control.h>
#include <modules/volume-api/volume-control.h>

#include <pulse/direction.h>

#include <pulsecore/core-util.h>

int pas_stream_new(pa_volume_api *api, const char *name, pas_stream **_r) {
    pas_stream *stream = NULL;
    int r;

    pa_assert(api);
    pa_assert(name);
    pa_assert(_r);

    stream = pa_xnew0(pas_stream, 1);
    stream->volume_api = api;
    stream->index = pa_volume_api_allocate_stream_index(api);

    r = pa_volume_api_register_name(api, name, false, &stream->name);
    if (r < 0)
        goto fail;

    stream->description = pa_xstrdup(stream->name);
    stream->direction = PA_DIRECTION_OUTPUT;
    stream->proplist = pa_proplist_new();

    *_r = stream;
    return 0;

fail:
    if (stream)
        pas_stream_free(stream);

    return r;
}

void pas_stream_put(pas_stream *stream) {
    const char *prop_key;
    void *state = NULL;

    pa_assert(stream);

    pa_volume_api_add_stream(stream->volume_api, stream);
    stream->linked = true;

    pa_log_debug("Created stream #%u.", stream->index);
    pa_log_debug("    Name: %s", stream->name);
    pa_log_debug("    Description: %s", stream->description);
    pa_log_debug("    Direction: %s", pa_direction_to_string(stream->direction));
    pa_log_debug("    Volume control: %s", stream->volume_control ? stream->volume_control->name : "(unset)");
    pa_log_debug("    Mute control: %s", stream->mute_control ? stream->mute_control->name : "(unset)");
    pa_log_debug("    Audio group for volume: %s",
                 stream->audio_group_for_volume ? stream->audio_group_for_volume->name : "(unset)");
    pa_log_debug("    Audio group for mute: %s",
                 stream->audio_group_for_mute ? stream->audio_group_for_mute->name : "(unset)");
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
        pa_volume_api_remove_stream(stream->volume_api, stream);

    pa_hook_fire(&stream->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_UNLINK], stream);

    pas_stream_set_audio_group_for_mute(stream, NULL);
    pas_stream_set_audio_group_for_volume(stream, NULL);
    pas_stream_set_mute_control(stream, NULL);
    pas_stream_set_relative_volume_control(stream, NULL);
    pas_stream_set_volume_control(stream, NULL);
}

void pas_stream_free(pas_stream *stream) {
    pa_assert(stream);

    /* unlink() expects name to be set. */
    if (!stream->unlinked && stream->name)
        pas_stream_unlink(stream);

    if (stream->proplist)
        pa_proplist_free(stream->proplist);

    pa_xfree(stream->description);

    if (stream->name)
        pa_volume_api_unregister_name(stream->volume_api, stream->name);

    pa_xfree(stream);
}

void pas_stream_set_direction(pas_stream *stream, pa_direction_t direction) {
    pa_assert(stream);
    pa_assert(!stream->linked);

    stream->direction = direction;
}

void pas_stream_set_description(pas_stream *stream, const char *description) {
    char *old_description;

    pa_assert(stream);
    pa_assert(description);

    old_description = stream->description;

    if (pa_streq(description, old_description))
        return;

    stream->description = pa_xstrdup(description);

    if (!stream->linked || stream->unlinked) {
        pa_xfree(old_description);
        return;
    }

    pa_log_debug("Stream %s description changed from \"%s\" to \"%s\".", stream->name, old_description,
                 description);
    pa_xfree(old_description);

    pa_hook_fire(&stream->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_DESCRIPTION_CHANGED], stream);
}

void pas_stream_set_property(pas_stream *stream, const char *key, const char *value) {
    const char *old_value;

    pa_assert(stream);
    pa_assert(key);

    old_value = pa_proplist_gets(stream->proplist, key);

    if (pa_safe_streq(value, old_value))
        return;

    if (value)
        pa_proplist_sets(stream->proplist, key, value);
    else
        pa_proplist_unset(stream->proplist, key);

    if (!stream->linked || stream->unlinked)
        return;

    pa_log_debug("Stream %s property \"%s\" changed from \"%s\" to \"%s\".", stream->name, key,
                 old_value ? old_value : "(unset)", value ? value : "(unset)");

    pa_hook_fire(&stream->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_PROPLIST_CHANGED], stream);
}

void pas_stream_set_volume_control(pas_stream *stream, pa_volume_control *control) {
    pa_volume_control *old_control;

    pa_assert(stream);

    old_control = stream->volume_control;

    if (control == old_control)
        return;

    stream->volume_control = control;

    if (!stream->linked || stream->unlinked)
        return;

    pa_log_debug("The volume control of stream %s changed from %s to %s.", stream->name,
                 old_control ? old_control->name : "(unset)", control ? control->name : "(unset)");

    pa_hook_fire(&stream->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_VOLUME_CONTROL_CHANGED], stream);
}

void pas_stream_set_relative_volume_control(pas_stream *stream, pa_volume_control *control) {
    pa_volume_control *old_control;

    pa_assert(stream);

    old_control = stream->relative_volume_control;

    if (control == old_control)
        return;

    stream->relative_volume_control = control;

    if (!stream->linked || stream->unlinked)
        return;

    pa_log_debug("The relative volume control of stream %s changed from %s to %s.", stream->name,
                 old_control ? old_control->name : "(unset)", control ? control->name : "(unset)");

    pa_hook_fire(&stream->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_RELATIVE_VOLUME_CONTROL_CHANGED], stream);
}

void pas_stream_set_mute_control(pas_stream *stream, pa_mute_control *control) {
    pa_mute_control *old_control;

    pa_assert(stream);

    old_control = stream->mute_control;

    if (control == old_control)
        return;

    stream->mute_control = control;

    if (!stream->linked || stream->unlinked)
        return;

    pa_log_debug("The mute control of stream %s changed from %s to %s.", stream->name,
                 old_control ? old_control->name : "(unset)", control ? control->name : "(unset)");

    pa_hook_fire(&stream->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_MUTE_CONTROL_CHANGED], stream);
}

void pas_stream_set_audio_group_for_volume(pas_stream *stream, pa_audio_group *group) {
    pa_audio_group *old_group;

    pa_assert(stream);

    old_group = stream->audio_group_for_volume;

    if (group == old_group)
        return;

    if (old_group)
        pa_audio_group_remove_volume_stream(old_group, stream);

    stream->audio_group_for_volume = group;

    if (group)
        pa_audio_group_add_volume_stream(group, stream);

    if (!stream->linked || stream->unlinked)
        return;

    pa_log_debug("Stream %s audio group for volume changed from %s to %s.", stream->name,
                 old_group ? old_group->name : "(unset)", group ? group->name : "(unset)");
}

void pas_stream_set_audio_group_for_mute(pas_stream *stream, pa_audio_group *group) {
    pa_audio_group *old_group;

    pa_assert(stream);

    old_group = stream->audio_group_for_mute;

    if (group == old_group)
        return;

    if (old_group)
        pa_audio_group_remove_mute_stream(old_group, stream);

    stream->audio_group_for_mute = group;

    if (group)
        pa_audio_group_add_mute_stream(group, stream);

    if (!stream->linked || stream->unlinked)
        return;

    pa_log_debug("Stream %s audio group for mute changed from %s to %s.", stream->name,
                 old_group ? old_group->name : "(unset)", group ? group->name : "(unset)");
}
