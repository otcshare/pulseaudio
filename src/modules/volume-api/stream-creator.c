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
    pa_hook_slot *sink_input_fixate_slot;
    pa_hook_slot *sink_input_unlink_slot;
    pa_hook_slot *source_output_fixate_slot;
    pa_hook_slot *source_output_unlink_slot;
};

enum stream_type {
    STREAM_TYPE_SINK_INPUT,
    STREAM_TYPE_SOURCE_OUTPUT,
};

struct stream {
    pa_core *core;
    pa_stream_creator *creator;
    enum stream_type type;
    pa_sink_input_new_data *sink_input_new_data;
    pa_sink_input *sink_input;
    pa_source_output_new_data *source_output_new_data;
    pa_source_output *source_output;
    pa_client *client;
    pa_volume_control *volume_control;
    pa_volume_control *relative_volume_control;
    pa_mute_control *mute_control;
    pas_stream *stream;

    pa_hook_slot *proplist_changed_slot;
    pa_hook_slot *volume_changed_slot;
    pa_hook_slot *reference_ratio_changed_slot;
    pa_hook_slot *mute_changed_slot;
};

static void stream_free(struct stream *stream);

static int volume_control_set_volume_cb(pa_volume_control *control, const pa_bvolume *original_volume,
                                        const pa_bvolume *remapped_volume, bool set_volume, bool set_balance) {
    struct stream *stream;
    pa_bvolume bvolume;
    pa_cvolume cvolume;

    pa_assert(control);
    pa_assert(original_volume);
    pa_assert(remapped_volume);

    stream = control->userdata;
    bvolume = control->volume;

    if (set_volume)
        bvolume.volume = remapped_volume->volume;

    if (set_balance)
        pa_bvolume_copy_balance(&bvolume, remapped_volume);

    pa_bvolume_to_cvolume(&bvolume, &cvolume);

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            if (stream->sink_input->state == PA_SINK_INPUT_INIT)
                pa_sink_input_new_data_set_volume(stream->sink_input_new_data, &cvolume, false);
            else
                pa_sink_input_set_volume(stream->sink_input, &cvolume, true, true);
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            if (stream->source_output->state == PA_SOURCE_OUTPUT_INIT)
                pa_source_output_new_data_set_volume(stream->source_output_new_data, &cvolume, false);
            else
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

    if (!stream->volume_control)
        return PA_HOOK_OK;

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
    else if (output)
        pa_bvolume_from_cvolume(&bvolume, &output->volume, &output->channel_map);
    else
        pa_assert_not_reached();

    pa_volume_control_set_volume(stream->volume_control, &bvolume, true, true);

    return PA_HOOK_OK;
}

static int relative_volume_control_set_volume_cb(pa_volume_control *control, const pa_bvolume *original_volume,
                                                 const pa_bvolume *remapped_volume, bool set_volume, bool set_balance) {
    struct stream *stream;
    pa_bvolume bvolume;
    pa_cvolume cvolume;

    pa_assert(control);
    pa_assert(original_volume);
    pa_assert(remapped_volume);

    stream = control->userdata;
    bvolume = control->volume;

    if (set_volume)
        bvolume.volume = remapped_volume->volume;

    if (set_balance)
        pa_bvolume_copy_balance(&bvolume, remapped_volume);

    pa_bvolume_to_cvolume(&bvolume, &cvolume);

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            if (stream->sink_input->state == PA_SINK_INPUT_INIT) {
                pa_sink_input_new_data_set_volume(stream->sink_input_new_data, &cvolume, true);

                /* XXX: This is a bit ugly. This is needed, because when we
                 * call pa_sink_input_new_data_set_volume(), there's no
                 * automatic notification to the primary volume control object
                 * about the changed volume. This problem should go away once
                 * stream volume controls are moved into the core. */
                if (stream->volume_control) {
                    pa_bvolume absolute_volume;

                    pa_bvolume_from_cvolume(&absolute_volume, &stream->sink_input_new_data->volume,
                                            &stream->sink_input_new_data->channel_map);
                    pa_volume_control_set_volume(stream->volume_control, &absolute_volume, true, true);
                }
            } else
                pa_sink_input_set_volume(stream->sink_input, &cvolume, true, false);
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            if (stream->source_output->state == PA_SOURCE_OUTPUT_INIT) {
                pa_source_output_new_data_set_volume(stream->source_output_new_data, &cvolume, true);

                /* XXX: This is a bit ugly. This is needed, because when we
                 * call pa_source_output_new_data_set_volume(), there's no
                 * automatic notification to the primary volume control object
                 * about the changed volume. This problem should go away once
                 * stream volume controls are moved into the core. */
                if (stream->volume_control) {
                    pa_bvolume absolute_volume;

                    pa_bvolume_from_cvolume(&absolute_volume, &stream->source_output_new_data->volume,
                                            &stream->source_output_new_data->channel_map);
                    pa_volume_control_set_volume(stream->volume_control, &absolute_volume, true, true);
                }
            } else
                pa_source_output_set_volume(stream->source_output, &cvolume, true, false);
            break;
    }

    return 0;
}

static pa_hook_result_t sink_input_or_source_output_reference_ratio_changed_cb(void *hook_data, void *call_data,
                                                                               void *userdata) {
    struct stream *stream = userdata;
    pa_sink_input *input = NULL;
    pa_source_output *output = NULL;
    pa_bvolume bvolume;

    pa_assert(stream);
    pa_assert(call_data);

    if (!stream->relative_volume_control)
        return PA_HOOK_OK;

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
        pa_bvolume_from_cvolume(&bvolume, &input->reference_ratio, &input->channel_map);
    else if (output)
        pa_bvolume_from_cvolume(&bvolume, &output->reference_ratio, &output->channel_map);
    else
        pa_assert_not_reached();

    pa_volume_control_set_volume(stream->relative_volume_control, &bvolume, true, true);

    return PA_HOOK_OK;
}

static int mute_control_set_mute_cb(pa_mute_control *control, bool mute) {
    struct stream *stream;

    pa_assert(control);

    stream = control->userdata;

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            if (stream->sink_input->state == PA_SINK_INPUT_INIT)
                pa_sink_input_new_data_set_muted(stream->sink_input_new_data, mute);
            else
                pa_sink_input_set_mute(stream->sink_input, mute, true);
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            if (stream->source_output->state == PA_SOURCE_OUTPUT_INIT)
                pa_source_output_new_data_set_muted(stream->source_output_new_data, mute);
            else
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

    if (!stream->mute_control)
        return PA_HOOK_OK;

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

    pa_mute_control_set_mute(stream->mute_control, mute);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_or_source_output_proplist_changed_cb(void *hook_data, void *call_data, void *userdata) {
    struct stream *stream = userdata;
    pa_sink_input *input = NULL;
    pa_source_output *output = NULL;
    pa_proplist *proplist = NULL;
    const char *description = NULL;

    pa_assert(stream);
    pa_assert(call_data);

    switch (stream->type) {
        case STREAM_TYPE_SINK_INPUT:
            input = call_data;

            if (input != stream->sink_input)
                return PA_HOOK_OK;

            proplist = stream->sink_input->proplist;
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            output = call_data;

            if (output != stream->source_output)
                return PA_HOOK_OK;

            proplist = stream->source_output->proplist;
            break;
    }

    description = pa_proplist_gets(proplist, PA_PROP_MEDIA_NAME);
    if (!description)
        description = stream->stream->name;

    pas_stream_set_description(stream->stream, description);

    return PA_HOOK_OK;
}

static int stream_new(pa_stream_creator *creator, enum stream_type type, void *new_data, void *core_stream,
                      struct stream **_r) {
    struct stream *stream = NULL;
    pa_proplist *proplist = NULL;
    pa_channel_map *channel_map = NULL;
    bool volume_available = false;
    pa_bvolume volume;
    pa_bvolume relative_volume;
    bool mute = false;
    const char *stream_name = NULL;
    const char *description = NULL;
    const char *volume_control_name = NULL;
    const char *relative_volume_control_name = NULL;
    const char *mute_control_name = NULL;
    pa_direction_t direction = PA_DIRECTION_OUTPUT;
    int r;
    const char *prop_key;
    void *state = NULL;

    pa_assert(creator);
    pa_assert(core_stream);
    pa_assert(_r);

    pa_bvolume_init_invalid(&volume);
    pa_bvolume_init_invalid(&relative_volume);

    stream = pa_xnew0(struct stream, 1);
    stream->core = creator->volume_api->core;
    stream->creator = creator;
    stream->type = type;

    switch (type) {
        case STREAM_TYPE_SINK_INPUT:
            stream->sink_input_new_data = new_data;
            stream->sink_input = core_stream;

            if (new_data) {
                stream->client = stream->sink_input_new_data->client;
                proplist = stream->sink_input_new_data->proplist;
                channel_map = &stream->sink_input_new_data->channel_map;
                volume_available = stream->sink_input_new_data->volume_writable;

                if (volume_available) {
                    if (!stream->sink_input_new_data->volume_is_set) {
                        pa_cvolume cvolume;

                        pa_cvolume_reset(&cvolume, channel_map->channels);
                        pa_sink_input_new_data_set_volume(stream->sink_input_new_data, &cvolume, true);
                    }

                    pa_bvolume_from_cvolume(&volume, &stream->sink_input_new_data->volume, channel_map);
                    pa_bvolume_from_cvolume(&relative_volume, &stream->sink_input_new_data->reference_ratio, channel_map);
                }

                if (!stream->sink_input_new_data->muted_is_set)
                    pa_sink_input_new_data_set_muted(stream->sink_input_new_data, false);

                mute = stream->sink_input_new_data->muted;
            } else {
                stream->client = stream->sink_input->client;
                proplist = stream->sink_input->proplist;
                channel_map = &stream->sink_input->channel_map;
                pa_bvolume_from_cvolume(&volume, &stream->sink_input->volume, channel_map);
                pa_bvolume_from_cvolume(&relative_volume, &stream->sink_input->reference_ratio, channel_map);
                mute = stream->sink_input->muted;
            }

            stream_name = "sink-input-stream";
            volume_control_name = "sink-input-volume-control";
            relative_volume_control_name = "sink-input-relative-volume-control";
            mute_control_name = "sink-input-mute-control";

            direction = PA_DIRECTION_OUTPUT;

            stream->proplist_changed_slot =
                    pa_hook_connect(&stream->core->hooks[PA_CORE_HOOK_SINK_INPUT_PROPLIST_CHANGED], PA_HOOK_NORMAL,
                                    sink_input_or_source_output_proplist_changed_cb, stream);
            stream->volume_changed_slot =
                    pa_hook_connect(&stream->core->hooks[PA_CORE_HOOK_SINK_INPUT_VOLUME_CHANGED], PA_HOOK_NORMAL,
                                    sink_input_or_source_output_volume_changed_cb, stream);
            stream->reference_ratio_changed_slot =
                    pa_hook_connect(&stream->core->hooks[PA_CORE_HOOK_SINK_INPUT_REFERENCE_RATIO_CHANGED], PA_HOOK_NORMAL,
                                    sink_input_or_source_output_reference_ratio_changed_cb, stream);
            stream->mute_changed_slot =
                    pa_hook_connect(&stream->core->hooks[PA_CORE_HOOK_SINK_INPUT_MUTE_CHANGED], PA_HOOK_NORMAL,
                                    sink_input_or_source_output_mute_changed_cb, stream);
            break;

        case STREAM_TYPE_SOURCE_OUTPUT:
            stream->source_output_new_data = new_data;
            stream->source_output = core_stream;

            if (new_data) {
                stream->client = stream->source_output_new_data->client;
                proplist = stream->source_output_new_data->proplist;
                channel_map = &stream->source_output_new_data->channel_map;
                volume_available = stream->source_output_new_data->volume_writable;

                if (volume_available) {
                    if (!stream->source_output_new_data->volume_is_set) {
                        pa_cvolume cvolume;

                        pa_cvolume_reset(&cvolume, channel_map->channels);
                        pa_source_output_new_data_set_volume(stream->source_output_new_data, &cvolume, true);
                    }

                    pa_bvolume_from_cvolume(&volume, &stream->source_output_new_data->volume, channel_map);
                    pa_bvolume_from_cvolume(&relative_volume, &stream->source_output_new_data->reference_ratio, channel_map);
                }

                if (!stream->source_output_new_data->muted_is_set)
                    pa_source_output_new_data_set_muted(stream->source_output_new_data, false);

                mute = stream->source_output_new_data->muted;
            } else {
                stream->client = stream->source_output->client;
                proplist = stream->source_output->proplist;
                channel_map = &stream->source_output->channel_map;
                pa_bvolume_from_cvolume(&volume, &stream->source_output->volume, channel_map);
                pa_bvolume_from_cvolume(&relative_volume, &stream->source_output->reference_ratio, channel_map);
                mute = stream->source_output->muted;
            }

            stream_name = "source-output-stream";
            volume_control_name = "source-output-volume-control";
            relative_volume_control_name = "source-output-relative-volume-control";
            mute_control_name = "source-output-mute-control";

            direction = PA_DIRECTION_INPUT;

            stream->proplist_changed_slot =
                    pa_hook_connect(&stream->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_PROPLIST_CHANGED], PA_HOOK_NORMAL,
                                    sink_input_or_source_output_proplist_changed_cb, stream);

            if (volume_available) {
                stream->volume_changed_slot =
                        pa_hook_connect(&stream->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_VOLUME_CHANGED], PA_HOOK_NORMAL,
                                        sink_input_or_source_output_volume_changed_cb, stream);
                stream->reference_ratio_changed_slot =
                        pa_hook_connect(&stream->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_REFERENCE_RATIO_CHANGED],
                                        PA_HOOK_NORMAL, sink_input_or_source_output_reference_ratio_changed_cb, stream);
            }

            stream->mute_changed_slot =
                    pa_hook_connect(&stream->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_MUTE_CHANGED], PA_HOOK_NORMAL,
                                    sink_input_or_source_output_mute_changed_cb, stream);
            break;
    }

    r = pas_stream_new(creator->volume_api, stream_name, &stream->stream);
    if (r < 0)
        goto fail;

    description = pa_proplist_gets(proplist, PA_PROP_MEDIA_NAME);
    if (!description)
        description = stream->stream->name;

    pas_stream_set_description(stream->stream, description);

    while ((prop_key = pa_proplist_iterate(proplist, &state)))
        pas_stream_set_property(stream->stream, prop_key, pa_proplist_gets(proplist, prop_key));

    pas_stream_set_direction(stream->stream, direction);
    stream->stream->userdata = stream;

    if (volume_available) {
        r = pa_volume_control_new(stream->creator->volume_api, volume_control_name, false,
                                  &stream->volume_control);
        if (r >= 0) {
            pa_volume_control_set_description(stream->volume_control, _("Volume"));
            pa_volume_control_set_channel_map(stream->volume_control, channel_map);
            pa_volume_control_set_volume(stream->volume_control, &volume, true, true);
            pa_volume_control_set_convertible_to_dB(stream->volume_control, true);
            stream->volume_control->set_volume = volume_control_set_volume_cb;
            stream->volume_control->userdata = stream;

            pas_stream_set_volume_control(stream->stream, stream->volume_control);
        }

        r = pa_volume_control_new(stream->creator->volume_api, relative_volume_control_name, false,
                                  &stream->relative_volume_control);
        if (r >= 0) {
            pa_volume_control_set_description(stream->relative_volume_control, _("Relative volume"));
            pa_volume_control_set_channel_map(stream->relative_volume_control, channel_map);
            pa_volume_control_set_volume(stream->relative_volume_control, &relative_volume, true, true);
            pa_volume_control_set_convertible_to_dB(stream->relative_volume_control, true);
            pa_volume_control_set_purpose(stream->relative_volume_control, PA_VOLUME_CONTROL_PURPOSE_STREAM_RELATIVE_VOLUME,
                                          stream->stream);
            stream->relative_volume_control->set_volume = relative_volume_control_set_volume_cb;
            stream->relative_volume_control->userdata = stream;

            pas_stream_set_relative_volume_control(stream->stream, stream->relative_volume_control);
        }
    }

    r = pa_mute_control_new(stream->creator->volume_api, mute_control_name, false, &stream->mute_control);
    if (r >= 0) {
        pa_mute_control_set_description(stream->mute_control, _("Mute"));
        pa_mute_control_set_mute(stream->mute_control, mute);
        pa_mute_control_set_purpose(stream->mute_control, PA_MUTE_CONTROL_PURPOSE_STREAM_MUTE, stream->stream);
        stream->mute_control->set_mute = mute_control_set_mute_cb;
        stream->mute_control->userdata = stream;

        pas_stream_set_mute_control(stream->stream, stream->mute_control);
    }

    pas_stream_put(stream->stream);

    if (stream->volume_control)
        pa_volume_control_put(stream->volume_control);

    if (stream->relative_volume_control)
        pa_volume_control_put(stream->relative_volume_control);

    if (stream->mute_control)
        pa_mute_control_put(stream->mute_control);

    *_r = stream;
    return 0;

fail:
    if (stream)
        stream_free(stream);

    return r;
}

static void stream_free(struct stream *stream) {
    pa_assert(stream);

    if (stream->mute_changed_slot)
        pa_hook_slot_free(stream->mute_changed_slot);

    if (stream->reference_ratio_changed_slot)
        pa_hook_slot_free(stream->reference_ratio_changed_slot);

    if (stream->volume_changed_slot)
        pa_hook_slot_free(stream->volume_changed_slot);

    if (stream->proplist_changed_slot)
        pa_hook_slot_free(stream->proplist_changed_slot);

    if (stream->mute_control)
        pa_mute_control_free(stream->mute_control);

    if (stream->relative_volume_control)
        pa_volume_control_free(stream->relative_volume_control);

    if (stream->volume_control)
        pa_volume_control_free(stream->volume_control);

    if (stream->stream)
        pas_stream_free(stream->stream);

    pa_xfree(stream);
}

static pa_hook_result_t sink_input_fixate_cb(void *hook_data, void *call_data, void *userdata) {
    pa_stream_creator *creator = userdata;
    pa_sink_input_new_data *data = call_data;
    int r;
    struct stream *stream;

    pa_assert(creator);
    pa_assert(data);

    r = stream_new(creator, STREAM_TYPE_SINK_INPUT, data, data->sink_input, &stream);
    if (r < 0)
        return PA_HOOK_OK;

    pa_hashmap_put(creator->streams, stream->sink_input, stream);

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

static pa_hook_result_t source_output_fixate_cb(void *hook_data, void *call_data, void *userdata) {
    pa_stream_creator *creator = userdata;
    pa_source_output_new_data *data = call_data;
    int r;
    struct stream *stream;

    pa_assert(creator);
    pa_assert(data);

    r = stream_new(creator, STREAM_TYPE_SOURCE_OUTPUT, data, data->source_output, &stream);
    if (r < 0)
        return PA_HOOK_OK;

    pa_hashmap_put(creator->streams, stream->source_output, stream);

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
    int r;
    struct stream *stream;

    pa_assert(api);

    creator = pa_xnew0(pa_stream_creator, 1);
    creator->volume_api = api;
    creator->streams = pa_hashmap_new_full(NULL, NULL, NULL, (pa_free_cb_t) stream_free);
    creator->sink_input_fixate_slot = pa_hook_connect(&api->core->hooks[PA_CORE_HOOK_SINK_INPUT_FIXATE], PA_HOOK_NORMAL,
                                                      sink_input_fixate_cb, creator);
    creator->sink_input_unlink_slot = pa_hook_connect(&api->core->hooks[PA_CORE_HOOK_SINK_INPUT_UNLINK], PA_HOOK_NORMAL,
                                                      sink_input_unlink_cb, creator);
    creator->source_output_fixate_slot = pa_hook_connect(&api->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_FIXATE], PA_HOOK_NORMAL,
                                                         source_output_fixate_cb, creator);
    creator->source_output_unlink_slot = pa_hook_connect(&api->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_UNLINK], PA_HOOK_NORMAL,
                                                         source_output_unlink_cb, creator);

    PA_IDXSET_FOREACH(input, api->core->sink_inputs, idx) {
        r = stream_new(creator, STREAM_TYPE_SINK_INPUT, NULL, input, &stream);
        if (r >= 0)
            pa_hashmap_put(creator->streams, stream->sink_input, stream);
    }

    PA_IDXSET_FOREACH(output, api->core->source_outputs, idx) {
        r = stream_new(creator, STREAM_TYPE_SOURCE_OUTPUT, NULL, output, &stream);
        if (r >= 0)
            pa_hashmap_put(creator->streams, stream->source_output, stream);
    }

    return creator;
}

void pa_stream_creator_free(pa_stream_creator *creator) {
    pa_assert(creator);

    if (creator->streams)
        pa_hashmap_remove_all(creator->streams);

    if (creator->source_output_unlink_slot)
        pa_hook_slot_free(creator->source_output_unlink_slot);

    if (creator->source_output_fixate_slot)
        pa_hook_slot_free(creator->source_output_fixate_slot);

    if (creator->sink_input_unlink_slot)
        pa_hook_slot_free(creator->sink_input_unlink_slot);

    if (creator->sink_input_fixate_slot)
        pa_hook_slot_free(creator->sink_input_fixate_slot);

    if (creator->streams)
        pa_hashmap_free(creator->streams);

    pa_xfree(creator);
}
