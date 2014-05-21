#ifndef foosstreamhfoo
#define foosstreamhfoo

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

#include <modules/volume-api/volume-api.h>

/* We use the "pas_" prefix in pas_stream, because there's already pa_stream in
 * the client API, and there's no good alternative term for streams. The 's' in
 * "pas" means "server", i.e. the point is that this stuff is for servers,
 * while pa_stream is for clients. */

typedef struct pas_stream pas_stream;

struct pas_stream {
    pa_volume_api *volume_api;
    uint32_t index;
    const char *name;
    char *description;
    pa_direction_t direction;
    pa_proplist *proplist;
    pa_volume_control *volume_control;
    pa_volume_control *relative_volume_control;
    pa_mute_control *mute_control;
    pa_audio_group *audio_group_for_volume;
    pa_audio_group *audio_group_for_mute;

    bool linked;
    bool unlinked;

    void *userdata;
};

int pas_stream_new(pa_volume_api *api, const char *name, pas_stream **_r);
void pas_stream_put(pas_stream *stream);
void pas_stream_unlink(pas_stream *stream);
void pas_stream_free(pas_stream *stream);

/* Called by the stream implementation, only during initialization. */
void pas_stream_set_direction(pas_stream *stream, pa_direction_t direction);

/* Called by the stream implementation. */
void pas_stream_set_description(pas_stream *stream, const char *description);
void pas_stream_set_property(pas_stream *stream, const char *key, const char *value);
void pas_stream_set_volume_control(pas_stream *stream, pa_volume_control *control);
void pas_stream_set_relative_volume_control(pas_stream *stream, pa_volume_control *control);
void pas_stream_set_mute_control(pas_stream *stream, pa_mute_control *control);

/* Called by anyone. */
void pas_stream_set_audio_group_for_volume(pas_stream *stream, pa_audio_group *group);
void pas_stream_set_audio_group_for_mute(pas_stream *stream, pa_audio_group *group);

#endif
