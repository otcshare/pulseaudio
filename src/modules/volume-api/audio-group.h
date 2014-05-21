#ifndef fooaudiogrouphfoo
#define fooaudiogrouphfoo

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

#include <modules/volume-api/binding.h>
#include <modules/volume-api/mute-control.h>
#include <modules/volume-api/volume-control.h>

#include <pulse/proplist.h>

#include <inttypes.h>

typedef struct pa_audio_group pa_audio_group;

#define PA_AUDIO_GROUP_BINDING_TARGET_TYPE "AudioGroup"
#define PA_AUDIO_GROUP_BINDING_TARGET_FIELD_VOLUME_CONTROL "volume_control"
#define PA_AUDIO_GROUP_BINDING_TARGET_FIELD_MUTE_CONTROL "mute_control"

struct pa_audio_group {
    pa_volume_api *volume_api;
    uint32_t index;
    const char *name;
    char *description;
    pa_proplist *proplist;
    pa_volume_control *volume_control;
    pa_mute_control *mute_control;
    bool have_own_volume_control;
    bool have_own_mute_control;
    pa_volume_control *own_volume_control;
    pa_mute_control *own_mute_control;

    pa_binding *volume_control_binding;
    pa_binding *mute_control_binding;
    pa_hashmap *volume_streams; /* pas_stream -> pas_stream (hashmap-as-a-set) */
    pa_hashmap *mute_streams; /* pas_stream -> pas_stream (hashmap-as-a-set) */

    bool linked;
    bool unlinked;
};

int pa_audio_group_new(pa_volume_api *api, const char *name, const char *description, pa_audio_group **group);
void pa_audio_group_put(pa_audio_group *group);
void pa_audio_group_unlink(pa_audio_group *group);
void pa_audio_group_free(pa_audio_group *group);

const char *pa_audio_group_get_name(pa_audio_group *group);

/* Called by policy modules. */
void pa_audio_group_set_have_own_volume_control(pa_audio_group *group, bool have);
void pa_audio_group_set_have_own_mute_control(pa_audio_group *group, bool have);
void pa_audio_group_set_volume_control(pa_audio_group *group, pa_volume_control *control);
void pa_audio_group_set_mute_control(pa_audio_group *group, pa_mute_control *control);
void pa_audio_group_bind_volume_control(pa_audio_group *group, pa_binding_target_info *target_info);
void pa_audio_group_bind_mute_control(pa_audio_group *group, pa_binding_target_info *target_info);

/* Called from sstream.c only. */
void pa_audio_group_add_volume_stream(pa_audio_group *group, pas_stream *stream);
void pa_audio_group_remove_volume_stream(pa_audio_group *group, pas_stream *stream);
void pa_audio_group_add_mute_stream(pa_audio_group *group, pas_stream *stream);
void pa_audio_group_remove_mute_stream(pa_audio_group *group, pas_stream *stream);

/* Called from volume-api.c only. */
pa_binding_target_type *pa_audio_group_create_binding_target_type(pa_volume_api *api);

#endif
