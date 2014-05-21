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
    pa_mute_control *mute_control;
    bool use_default_volume_control;
    bool use_default_mute_control;
    bool have_own_volume_control;
    bool have_own_mute_control;
    pa_volume_control *own_volume_control;
    pa_mute_control *own_mute_control;

    pa_binding *volume_control_binding;
    pa_binding *mute_control_binding;
    pa_audio_group *audio_group_for_volume;
    pa_audio_group *audio_group_for_mute;

    bool linked;
    bool unlinked;

    /* Called when the own volume control is enabled. The callback
     * implementation should return a new linked volume control object. The
     * callback may be NULL, in which case the own volume control can't be
     * enabled. */
    pa_volume_control *(*create_own_volume_control)(pas_stream *stream);

    /* Called when the own volume control is disabled. The implementation
     * should free stream->own_volume_control. The callback may be NULL only if
     * create_own_volume_control is NULL also. */
    void (*delete_own_volume_control)(pas_stream *stream);

    /* Called when the own mute control is enabled. The callback implementation
     * should return a new linked mute control object. The callback may be
     * NULL, in which case the own mute control can't be enabled. */
    pa_mute_control *(*create_own_mute_control)(pas_stream *stream);

    /* Called when the own mute control is disabled. The implementation should
     * free stream->own_mute_control. The callback may be NULL only if
     * create_own_mute_control is NULL also. */
    void (*delete_own_mute_control)(pas_stream *stream);

    void *userdata;
};

pas_stream *pas_stream_new(pa_volume_api *api, const char *name, const char *description, pa_direction_t direction);
void pas_stream_put(pas_stream *stream, pa_proplist *initial_properties);
void pas_stream_unlink(pas_stream *stream);
void pas_stream_free(pas_stream *stream);

/* Called by the stream implementation and possibly by policy modules.
 * Enabling own controls may fail (the stream may not support own controls),
 * disabling will never fail. */
int pas_stream_set_have_own_volume_control(pas_stream *stream, bool have);
int pas_stream_set_have_own_mute_control(pas_stream *stream, bool have);

/* Called by policy modules. */
void pas_stream_set_volume_control(pas_stream *stream, pa_volume_control *control);
void pas_stream_set_mute_control(pas_stream *stream, pa_mute_control *control);
void pas_stream_bind_volume_control(pas_stream *stream, pa_binding_target_info *target_info);
void pas_stream_bind_mute_control(pas_stream *stream, pa_binding_target_info *target_info);

/* Called by the stream implementation. */
void pas_stream_description_changed(pas_stream *stream, const char *new_description);

/* Called by audio-group.c only. Adding a stream to an audio group happens
 * implicitly when the volume or mute control of a stream is set to point to
 * the own control of an audio group. */
void pas_stream_set_audio_group_for_volume(pas_stream *stream, pa_audio_group *group);
void pas_stream_set_audio_group_for_mute(pas_stream *stream, pa_audio_group *group);

#endif
