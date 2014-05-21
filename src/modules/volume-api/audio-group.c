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

int pa_audio_group_new(pa_volume_api *api, const char *name, pa_audio_group **_r) {
    pa_audio_group *group = NULL;
    int r;

    pa_assert(api);
    pa_assert(name);
    pa_assert(_r);

    group = pa_xnew0(pa_audio_group, 1);
    group->volume_api = api;
    group->index = pa_volume_api_allocate_audio_group_index(api);

    r = pa_volume_api_register_name(api, name, true, &group->name);
    if (r < 0)
        goto fail;

    group->description = pa_xstrdup(group->name);
    group->proplist = pa_proplist_new();
    group->volume_streams = pa_hashmap_new(NULL, NULL);
    group->mute_streams = pa_hashmap_new(NULL, NULL);

    *_r = group;
    return 0;

fail:
    if (group)
        pa_audio_group_free(group);

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
        pa_volume_api_remove_audio_group(group->volume_api, group);

    pa_hook_fire(&group->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_UNLINK], group);

    while ((stream = pa_hashmap_first(group->mute_streams)))
        pas_stream_set_audio_group_for_mute(stream, NULL);

    while ((stream = pa_hashmap_first(group->volume_streams)))
        pas_stream_set_audio_group_for_volume(stream, NULL);

    pa_audio_group_set_mute_control(group, NULL);
    pa_audio_group_set_volume_control(group, NULL);
}

void pa_audio_group_free(pa_audio_group *group) {
    pa_assert(group);

    /* unlink() expects name to be set. */
    if (!group->unlinked && group->name)
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

void pa_audio_group_set_description(pa_audio_group *group, const char *description) {
    char *old_description;

    pa_assert(group);
    pa_assert(description);

    old_description = group->description;

    if (pa_streq(description, old_description))
        return;

    group->description = pa_xstrdup(description);

    if (!group->linked || group->unlinked) {
        pa_xfree(old_description);
        return;
    }

    pa_log_debug("The description of audio group %s changed from \"%s\" to \"%s\".", group->name, old_description,
                 description);
    pa_xfree(old_description);

    pa_hook_fire(&group->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_DESCRIPTION_CHANGED], group);

}

void pa_audio_group_set_volume_control(pa_audio_group *group, pa_volume_control *control) {
    pa_volume_control *old_control;

    pa_assert(group);

    old_control = group->volume_control;

    if (control == old_control)
        return;

    group->volume_control = control;

    if (!group->linked || group->unlinked)
        return;

    pa_log_debug("The volume control of audio group %s changed from %s to %s.", group->name,
                 old_control ? old_control->name : "(unset)", control ? control->name : "(unset)");

    pa_hook_fire(&group->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_VOLUME_CONTROL_CHANGED], group);
}

void pa_audio_group_set_mute_control(pa_audio_group *group, pa_mute_control *control) {
    pa_mute_control *old_control;

    pa_assert(group);

    old_control = group->mute_control;

    if (control == old_control)
        return;

    group->mute_control = control;

    if (!group->linked || group->unlinked)
        return;

    pa_log_debug("The mute control of audio group %s changed from %s to %s.", group->name,
                 old_control ? old_control->name : "(unset)", control ? control->name : "(unset)");

    pa_hook_fire(&group->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_MUTE_CONTROL_CHANGED], group);
}

void pa_audio_group_add_volume_stream(pa_audio_group *group, pas_stream *stream) {
    pa_assert(group);
    pa_assert(stream);

    pa_assert_se(pa_hashmap_put(group->volume_streams, stream, stream) >= 0);
}

void pa_audio_group_remove_volume_stream(pa_audio_group *group, pas_stream *stream) {
    pa_assert(group);
    pa_assert(stream);

    pa_assert_se(pa_hashmap_remove(group->volume_streams, stream));
}

void pa_audio_group_add_mute_stream(pa_audio_group *group, pas_stream *stream) {
    pa_assert(group);
    pa_assert(stream);

    pa_assert_se(pa_hashmap_put(group->mute_streams, stream, stream) >= 0);
}

void pa_audio_group_remove_mute_stream(pa_audio_group *group, pas_stream *stream) {
    pa_assert(group);
    pa_assert(stream);

    pa_assert_se(pa_hashmap_remove(group->mute_streams, stream));
}
