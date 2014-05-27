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

#include "stream-creator.h"

#include <modules/volume-api/sstream.h>
#include <modules/volume-api/mute-control.h>
#include <modules/volume-api/volume-control.h>

#include <pulsecore/core-util.h>
#include <pulsecore/i18n.h>

struct pa_stream_creator {
    pa_volume_api *volume_api;
    pa_hashmap *streams; /* pa_sink_input/pa_source_output -> struct stream */
    pa_hook_slot *sink_input_put_slot;
    pa_hook_slot *sink_input_unlink_slot;
    pa_hook_slot *source_output_put_slot;
    pa_hook_slot *source_output_unlink_slot;
};

enum stream_type {
    STREAM_TYPE_SINK_INPUT,
    STREAM_TYPE_SOURCE_OUTPUT,
};

struct stream {
    pa_stream_creator *creator;
    enum stream_type type;
    pa_sink_input *sink_input;
    pa_source_output *source_output;
    pa_client *client;
    pas_stream *stream;

    bool unlinked;

    pa_hook_slot *proplist_changed_slot;
    pa_hook_slot *client_proplist_changed_slot;
    pa_hook_slot *volume_changed_slot;
    pa_hook_slot *mute_changed_slot;
};

static char *get_stream_volume_and_mute_control_description_malloc(struct stream *stream) {
    const char *application_name = NULL;
    char *description;

    pa_assert(stream);

    if (stream->client)
        application_name = pa_proplist_gets(stream->client->proplist, PA_PROP_APPLICATION_NAME);

    if (application_name)
        description = pa_sprintf_malloc("%s: %s", application_name, stream->stream->description);
    else
        description = pa_xstrdup(stream->stream->description);

    return description;
}

static int volume_control_set_volume_cb(pa_volume_control *control, const pa_bvolume *volume, bool set_volume, bool set_balance) {
    struct stream *stream;
    pa_bvolume bvolume;
    pa_cvolume cvolume;

    pa_assert(control);
    pa_assert(volume);

    stream = control->userdata;

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            pa_bvolume_from_cvolume(&bvolume, &stream->sink_input->volume, &stream->sink_input->channel_map);
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            pa_bvolume_from_cvolume(&bvolume, &stream->source_output->volume, &stream->source_output->channel_map);
            break;
    }

    if (set_volume)
        bvolume.volume = volume->volume;

    if (set_balance)
        pa_bvolume_copy_balance(&bvolume, volume);

    pa_bvolume_to_cvolume(&bvolume, &cvolume);

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            pa_sink_input_set_volume(stream->sink_input, &cvolume, true, true);
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            pa_source_output_set_volume(stream->source_output, &cvolume, true, true);
            break;
    }

    return 0;
}

static pa_hook_result_t sink_input_or_source_output_volume_changed_cb(void *hook_data, void *call_data, void *userdata) {
    struct stream *stream = userdata;
    pa_sink_input *input = NULL;
    pa_source_output *output = NULL;
    pa_bvolume bvolume;

    pa_assert(stream);
    pa_assert(call_data);

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            input = call_data;
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            output = call_data;
            break;
    }

    if ((input && input != stream->sink_input) || (output && output != stream->source_output))
        return PA_HOOK_OK;

    if (input)
        pa_bvolume_from_cvolume(&bvolume, &input->volume, &input->channel_map);
    else
        pa_bvolume_from_cvolume(&bvolume, &output->volume, &output->channel_map);

    pa_volume_control_volume_changed(stream->stream->own_volume_control, &bvolume, true, true);

    return PA_HOOK_OK;
}

static void volume_control_set_initial_volume_cb(pa_volume_control *control) {
    struct stream *stream;
    pa_cvolume cvolume;

    pa_assert(control);

    stream = control->userdata;
    pa_bvolume_to_cvolume(&control->volume, &cvolume);

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            pa_sink_input_set_volume(stream->sink_input, &cvolume, true, true);
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            pa_source_output_set_volume(stream->source_output, &cvolume, true, true);
            break;
    }
}

static int mute_control_set_mute_cb(pa_mute_control *control, bool mute) {
    struct stream *stream;

    pa_assert(control);

    stream = control->userdata;

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            pa_sink_input_set_mute(stream->sink_input, mute, true);
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            pa_source_output_set_mute(stream->source_output, mute, true);
            break;
    }

    return 0;
}

static pa_hook_result_t sink_input_or_source_output_mute_changed_cb(void *hook_data, void *call_data, void *userdata) {
    struct stream *stream = userdata;
    pa_sink_input *input = NULL;
    pa_source_output *output = NULL;
    bool mute;

    pa_assert(stream);
    pa_assert(call_data);

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            input = call_data;
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            output = call_data;
            break;
    }

    if ((input && input != stream->sink_input) || (output && output != stream->source_output))
        return PA_HOOK_OK;

    if (input)
        mute = input->muted;
    else if (output)
        mute = output->muted;
    else
        pa_assert_not_reached();

    pa_mute_control_mute_changed(stream->stream->own_mute_control, mute);

    return PA_HOOK_OK;
}

static void mute_control_set_initial_mute_cb(pa_mute_control *control) {
    struct stream *stream;

    pa_assert(control);

    stream = control->userdata;

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            pa_sink_input_set_mute(stream->sink_input, control->mute, true);
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            pa_source_output_set_mute(stream->source_output, control->mute, true);
            break;
    }
}

static const char *get_sink_input_description(pa_sink_input *input) {
    const char *description;

    pa_assert(input);

    description = pa_proplist_gets(input->proplist, PA_PROP_MEDIA_NAME);
    if (description)
        return description;

    return NULL;
}

static const char *get_source_output_description(pa_source_output *output) {
    const char *description;

    pa_assert(output);

    description = pa_proplist_gets(output->proplist, PA_PROP_MEDIA_NAME);
    if (description)
        return description;

    return NULL;
}

static pa_volume_control *stream_create_own_volume_control_cb(pas_stream *s) {
    struct stream *stream;
    const char *name = NULL;
    char *description;
    pa_volume_control *control;
    pa_bvolume volume;

    pa_assert(s);

    stream = s->userdata;

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            name = "sink-input-volume-control";
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            name = "source-output-volume-control";
            break;
    }

    description = get_stream_volume_and_mute_control_description_malloc(stream);
    control = pa_volume_control_new(stream->creator->volume_api, name, description, true, false);
    pa_xfree(description);
    control->set_volume = volume_control_set_volume_cb;
    control->userdata = stream;

    pa_assert(!stream->volume_changed_slot);

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            stream->volume_changed_slot =
                    pa_hook_connect(&stream->sink_input->core->hooks[PA_CORE_HOOK_SINK_INPUT_VOLUME_CHANGED], PA_HOOK_NORMAL,
                                    sink_input_or_source_output_volume_changed_cb, stream);
            pa_bvolume_from_cvolume(&volume, &stream->sink_input->volume, &stream->sink_input->channel_map);
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            stream->volume_changed_slot =
                    pa_hook_connect(&stream->source_output->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_VOLUME_CHANGED],
                                    PA_HOOK_NORMAL, sink_input_or_source_output_volume_changed_cb, stream);
            pa_bvolume_from_cvolume(&volume, &stream->source_output->volume, &stream->source_output->channel_map);
            break;
    }

    pa_volume_control_put(control, &volume, volume_control_set_initial_volume_cb);

    return control;
}

static void stream_delete_own_volume_control_cb(pas_stream *s) {
    struct stream *stream;

    pa_assert(s);

    stream = s->userdata;
    pa_hook_slot_free(stream->volume_changed_slot);
    stream->volume_changed_slot = NULL;
    pa_volume_control_free(s->own_volume_control);
}

static pa_mute_control *stream_create_own_mute_control_cb(pas_stream *s) {
    struct stream *stream;
    const char *name = NULL;
    char *description;
    pa_mute_control *control;
    bool mute = false;

    pa_assert(s);

    stream = s->userdata;

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            name = "sink-input-mute-control";
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            name = "source-output-mute-control";
            break;
    }

    description = get_stream_volume_and_mute_control_description_malloc(stream);
    control = pa_mute_control_new(stream->creator->volume_api, name, description);
    pa_xfree(description);
    control->set_mute = mute_control_set_mute_cb;
    control->userdata = stream;

    pa_assert(!stream->mute_changed_slot);

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            stream->mute_changed_slot =
                    pa_hook_connect(&stream->sink_input->core->hooks[PA_CORE_HOOK_SINK_INPUT_MUTE_CHANGED], PA_HOOK_NORMAL,
                                    sink_input_or_source_output_mute_changed_cb, stream);
            mute = stream->sink_input->muted;
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            stream->mute_changed_slot =
                    pa_hook_connect(&stream->source_output->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_MUTE_CHANGED],
                                    PA_HOOK_NORMAL, sink_input_or_source_output_mute_changed_cb, stream);
            mute = stream->source_output->muted;
            break;
    }

    pa_mute_control_put(control, mute, true, mute_control_set_initial_mute_cb);

    return control;
}

static void stream_delete_own_mute_control_cb(pas_stream *s) {
    struct stream *stream;

    pa_assert(s);

    stream = s->userdata;
    pa_hook_slot_free(stream->mute_changed_slot);
    stream->mute_changed_slot = NULL;
    pa_mute_control_free(s->own_mute_control);
}

static pa_hook_result_t sink_input_or_source_output_proplist_changed_cb(void *hook_data, void *call_data, void *userdata) {
    struct stream *stream = userdata;
    pa_sink_input *input = NULL;
    pa_source_output *output = NULL;
    const char *new_stream_description = NULL;
    char *new_control_description;

    pa_assert(stream);
    pa_assert(call_data);

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            input = call_data;

            if (input != stream->sink_input)
                return PA_HOOK_OK;

            new_stream_description = get_sink_input_description(input);
            if (!new_stream_description)
                new_stream_description = stream->stream->name;
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            output = call_data;

            if (output != stream->source_output)
                return PA_HOOK_OK;

            new_stream_description = get_source_output_description(output);
            if (!new_stream_description)
                new_stream_description = stream->stream->name;
            break;
    }

    pas_stream_description_changed(stream->stream, new_stream_description);

    new_control_description = get_stream_volume_and_mute_control_description_malloc(stream);

    if (stream->stream->own_volume_control)
        pa_volume_control_description_changed(stream->stream->own_volume_control, new_control_description);

    if (stream->stream->own_mute_control)
        pa_mute_control_description_changed(stream->stream->own_mute_control, new_control_description);

    pa_xfree(new_control_description);

    return PA_HOOK_OK;
}

static pa_hook_result_t client_proplist_changed_cb(void *hook_data, void *call_data, void *userdata) {
    struct stream *stream = userdata;
    pa_client *client = call_data;
    char *description;

    pa_assert(stream);
    pa_assert(client);

    if (client != stream->client)
        return PA_HOOK_OK;

    description = get_stream_volume_and_mute_control_description_malloc(stream);

    if (stream->stream->own_volume_control)
        pa_volume_control_description_changed(stream->stream->own_volume_control, description);

    if (stream->stream->own_mute_control)
        pa_mute_control_description_changed(stream->stream->own_mute_control, description);

    pa_xfree(description);

    return PA_HOOK_OK;
}

static struct stream *stream_new(pa_stream_creator *creator, enum stream_type type, void *core_stream) {
    struct stream *stream;
    const char *name = NULL;
    const char *description = NULL;
    pa_direction_t direction = PA_DIRECTION_OUTPUT;

    pa_assert(creator);
    pa_assert(core_stream);

    stream = pa_xnew0(struct stream, 1);
    stream->creator = creator;
    stream->type = type;

    switch (type) {
        case STREAM_TYPE_SINK_INPUT:
            stream->sink_input = core_stream;
            stream->client = stream->sink_input->client;
            name = "sink-input-stream";

            description = get_sink_input_description(stream->sink_input);
            if (!description)
                description = name;

            direction = PA_DIRECTION_OUTPUT;
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            stream->source_output = core_stream;
            stream->client = stream->source_output->client;
            name = "source-output-stream";

            description = get_source_output_description(stream->source_output);
            if (!description)
                description = name;

            direction = PA_DIRECTION_INPUT;
            break;
    }

    stream->stream = pas_stream_new(creator->volume_api, name, description, direction);
    stream->stream->create_own_volume_control = stream_create_own_volume_control_cb;
    stream->stream->delete_own_volume_control = stream_delete_own_volume_control_cb;
    stream->stream->create_own_mute_control = stream_create_own_mute_control_cb;
    stream->stream->delete_own_mute_control = stream_delete_own_mute_control_cb;
    stream->stream->userdata = stream;
    pas_stream_set_have_own_volume_control(stream->stream, true);
    pas_stream_set_have_own_mute_control(stream->stream, true);

    switch (type) {
        case STREAM_TYPE_SINK_INPUT:
            stream->proplist_changed_slot =
                    pa_hook_connect(&stream->sink_input->core->hooks[PA_CORE_HOOK_SINK_INPUT_PROPLIST_CHANGED], PA_HOOK_NORMAL,
                                    sink_input_or_source_output_proplist_changed_cb, stream);
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            stream->proplist_changed_slot =
                    pa_hook_connect(&stream->source_output->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_PROPLIST_CHANGED],
                                    PA_HOOK_NORMAL, sink_input_or_source_output_proplist_changed_cb, stream);
            break;
    }

    stream->client_proplist_changed_slot =
            pa_hook_connect(&stream->creator->volume_api->core->hooks[PA_CORE_HOOK_CLIENT_PROPLIST_CHANGED],
                            PA_HOOK_NORMAL, client_proplist_changed_cb, stream);

    return stream;
}

static void stream_put(struct stream *stream) {
    pa_proplist *proplist = NULL;

    pa_assert(stream);

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            proplist = stream->sink_input->proplist;
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            proplist = stream->source_output->proplist;
            break;
    }

    pas_stream_put(stream->stream, proplist);
}

static void stream_unlink(struct stream *stream) {
    pa_assert(stream);

    if (stream->unlinked)
        return;

    stream->unlinked = true;

    if (stream->stream)
        pas_stream_unlink(stream->stream);
}

static void stream_free(struct stream *stream) {
    pa_assert(stream);

    if (!stream->unlinked)
        stream_unlink(stream);

    if (stream->client_proplist_changed_slot)
        pa_hook_slot_free(stream->client_proplist_changed_slot);

    if (stream->proplist_changed_slot)
        pa_hook_slot_free(stream->proplist_changed_slot);

    if (stream->stream)
        pas_stream_free(stream->stream);

    pa_xfree(stream);
}

static void create_stream(pa_stream_creator *creator, enum stream_type type, void *core_stream) {
    struct stream *stream;

    pa_assert(creator);
    pa_assert(core_stream);

    stream = stream_new(creator, type, core_stream);
    pa_hashmap_put(creator->streams, core_stream, stream);
    stream_put(stream);
}

static pa_hook_result_t sink_input_put_cb(void *hook_data, void *call_data, void *userdata) {
    pa_stream_creator *creator = userdata;
    pa_sink_input *input = call_data;

    pa_assert(creator);
    pa_assert(input);

    create_stream(creator, STREAM_TYPE_SINK_INPUT, input);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    pa_stream_creator *creator = userdata;
    pa_sink_input *input = call_data;

    pa_assert(creator);
    pa_assert(input);

    pa_hashmap_remove_and_free(creator->streams, input);

    return PA_HOOK_OK;
}

static pa_hook_result_t source_output_put_cb(void *hook_data, void *call_data, void *userdata) {
    pa_stream_creator *creator = userdata;
    pa_source_output *output = call_data;

    pa_assert(creator);
    pa_assert(output);

    create_stream(creator, STREAM_TYPE_SOURCE_OUTPUT, output);

    return PA_HOOK_OK;
}

static pa_hook_result_t source_output_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    pa_stream_creator *creator = userdata;
    pa_source_output *output = call_data;

    pa_assert(creator);
    pa_assert(output);

    pa_hashmap_remove_and_free(creator->streams, output);

    return PA_HOOK_OK;
}

pa_stream_creator *pa_stream_creator_new(pa_volume_api *api) {
    pa_stream_creator *creator;
    uint32_t idx;
    pa_sink_input *input;
    pa_source_output *output;

    pa_assert(api);

    creator = pa_xnew0(pa_stream_creator, 1);
    creator->volume_api = api;
    creator->streams = pa_hashmap_new_full(NULL, NULL, NULL, (pa_free_cb_t) stream_free);
    creator->sink_input_put_slot = pa_hook_connect(&api->core->hooks[PA_CORE_HOOK_SINK_INPUT_PUT], PA_HOOK_NORMAL,
                                                   sink_input_put_cb, creator);
    creator->sink_input_unlink_slot = pa_hook_connect(&api->core->hooks[PA_CORE_HOOK_SINK_INPUT_UNLINK], PA_HOOK_NORMAL,
                                                      sink_input_unlink_cb, creator);
    creator->source_output_put_slot = pa_hook_connect(&api->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_PUT], PA_HOOK_NORMAL,
                                                      source_output_put_cb, creator);
    creator->source_output_unlink_slot = pa_hook_connect(&api->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_UNLINK], PA_HOOK_NORMAL,
                                                         source_output_unlink_cb, creator);

    PA_IDXSET_FOREACH(input, api->core->sink_inputs, idx)
        create_stream(creator, STREAM_TYPE_SINK_INPUT, input);

    PA_IDXSET_FOREACH(output, api->core->source_outputs, idx)
        create_stream(creator, STREAM_TYPE_SOURCE_OUTPUT, output);

    return creator;
}

void pa_stream_creator_free(pa_stream_creator *creator) {
    pa_assert(creator);

    if (creator->streams)
        pa_hashmap_remove_all(creator->streams);

    if (creator->source_output_unlink_slot)
        pa_hook_slot_free(creator->source_output_unlink_slot);

    if (creator->source_output_put_slot)
        pa_hook_slot_free(creator->source_output_put_slot);

    if (creator->sink_input_unlink_slot)
        pa_hook_slot_free(creator->sink_input_unlink_slot);

    if (creator->sink_input_put_slot)
        pa_hook_slot_free(creator->sink_input_put_slot);

    if (creator->streams)
        pa_hashmap_free(creator->streams);

    pa_xfree(creator);
}
