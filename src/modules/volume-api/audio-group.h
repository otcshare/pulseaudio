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

#include <modules/volume-api/mute-control.h>
#include <modules/volume-api/volume-control.h>

#include <pulse/proplist.h>

#include <inttypes.h>

typedef struct pa_audio_group pa_audio_group;

struct pa_audio_group {
    pa_volume_api *volume_api;
    uint32_t index;
    const char *name;
    char *description;
    pa_proplist *proplist;
    pa_volume_control *volume_control;
    pa_mute_control *mute_control;

    pa_hashmap *volume_streams; /* pas_stream -> pas_stream (hashmap-as-a-set) */
    pa_hashmap *mute_streams; /* pas_stream -> pas_stream (hashmap-as-a-set) */

    bool linked;
    bool unlinked;
};

int pa_audio_group_new(pa_volume_api *api, const char *name, pa_audio_group **_r);
void pa_audio_group_put(pa_audio_group *group);
void pa_audio_group_unlink(pa_audio_group *group);
void pa_audio_group_free(pa_audio_group *group);

/* Called by the audio group implementation. */
void pa_audio_group_set_description(pa_audio_group *group, const char *description);
void pa_audio_group_set_volume_control(pa_audio_group *group, pa_volume_control *control);
void pa_audio_group_set_mute_control(pa_audio_group *group, pa_mute_control *control);

/* Called by sstream.c only. If you want to assign a stream to an audio group, use
 * pas_stream_set_audio_group_for_volume() and
 * pas_stream_set_audio_group_for_mute(). */
void pa_audio_group_add_volume_stream(pa_audio_group *group, pas_stream *stream);
void pa_audio_group_remove_volume_stream(pa_audio_group *group, pas_stream *stream);
void pa_audio_group_add_mute_stream(pa_audio_group *group, pas_stream *stream);
void pa_audio_group_remove_mute_stream(pa_audio_group *group, pas_stream *stream);

#endif
