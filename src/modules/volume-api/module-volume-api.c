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

#include "module-volume-api-symdef.h"

#include <modules/volume-api/audio-group.h>
#include <modules/volume-api/bvolume.h>
#include <modules/volume-api/device.h>
#include <modules/volume-api/sstream.h>
#include <modules/volume-api/volume-api.h>
#include <modules/volume-api/volume-api-common.h>
#include <modules/volume-api/volume-control.h>

#include <pulsecore/core-util.h>
#include <pulsecore/i18n.h>
#include <pulsecore/protocol-native.h>
#include <pulsecore/pstream-util.h>

PA_MODULE_AUTHOR("Tanu Kaskinen");
PA_MODULE_DESCRIPTION(_("Volume API"));
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);

struct userdata {
    pa_native_protocol *native_protocol;
    bool extension_installed;
    pa_volume_api *volume_api;
    pa_hook_slot *volume_control_put_slot;
    pa_hook_slot *volume_control_unlink_slot;
    pa_hook_slot *volume_control_description_changed_slot;
    pa_hook_slot *volume_control_volume_changed_slot;
    pa_hook_slot *volume_control_convertible_to_db_changed_slot;
    pa_hook_slot *mute_control_put_slot;
    pa_hook_slot *mute_control_unlink_slot;
    pa_hook_slot *mute_control_description_changed_slot;
    pa_hook_slot *mute_control_mute_changed_slot;
    pa_hook_slot *device_put_slot;
    pa_hook_slot *device_unlink_slot;
    pa_hook_slot *device_description_changed_slot;
    pa_hook_slot *device_volume_control_changed_slot;
    pa_hook_slot *device_mute_control_changed_slot;
    pa_hook_slot *stream_put_slot;
    pa_hook_slot *stream_unlink_slot;
    pa_hook_slot *stream_description_changed_slot;
    pa_hook_slot *stream_proplist_changed_slot;
    pa_hook_slot *stream_volume_control_changed_slot;
    pa_hook_slot *stream_relative_volume_control_changed_slot;
    pa_hook_slot *stream_mute_control_changed_slot;
    pa_hook_slot *audio_group_put_slot;
    pa_hook_slot *audio_group_unlink_slot;
    pa_hook_slot *audio_group_description_changed_slot;
    pa_hook_slot *audio_group_volume_control_changed_slot;
    pa_hook_slot *audio_group_mute_control_changed_slot;
    pa_hook_slot *main_output_volume_control_changed_slot;
    pa_hook_slot *main_input_volume_control_changed_slot;
    pa_hook_slot *main_output_mute_control_changed_slot;
    pa_hook_slot *main_input_mute_control_changed_slot;
    pa_hashmap *connections; /* pa_native_connection -> volume_api_connection */
    pa_hook_slot *native_connection_unlink_slot;
};

struct volume_api_connection {
    pa_native_connection *native_connection;
    bool dead;
    pa_ext_volume_api_subscription_mask_t subscription_mask;
};

static struct volume_api_connection *volume_api_connection_new(pa_native_connection *native_connection) {
    struct volume_api_connection *api_connection;

    pa_assert(native_connection);

    api_connection = pa_xnew0(struct volume_api_connection, 1);
    api_connection->native_connection = native_connection;

    return api_connection;
}

static void volume_api_connection_free(struct volume_api_connection *connection) {
    pa_assert(connection);

    pa_xfree(connection);
}

static void add_connection(struct userdata *u, struct volume_api_connection *connection) {
    pa_assert(u);
    pa_assert(connection);

    pa_assert_se(pa_hashmap_put(u->connections, connection->native_connection, connection) >= 0);
}

static void remove_connection(struct userdata *u, struct volume_api_connection *connection) {
    pa_assert(u);
    pa_assert(connection);

    if (!connection->dead) {
        pa_tagstruct *tagstruct;

        tagstruct = pa_tagstruct_new(NULL, 0);
        pa_tagstruct_putu32(tagstruct, PA_COMMAND_EXTENSION);
        pa_tagstruct_putu32(tagstruct, (uint32_t) -1);
        pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
        pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
        pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_DISCONNECT);
        pa_pstream_send_tagstruct(pa_native_connection_get_pstream(connection->native_connection), tagstruct);
    }

    pa_assert_se(pa_hashmap_remove_and_free(u->connections, connection->native_connection) >= 0);
}

static pa_tagstruct *reply_new(uint32_t tag) {
    pa_tagstruct *reply;

    reply = pa_tagstruct_new(NULL, 0);
    pa_tagstruct_putu32(reply, PA_COMMAND_REPLY);
    pa_tagstruct_putu32(reply, tag);

    return reply;
}

static int command_connect(struct userdata *u, pa_native_connection *native_connection, uint32_t tag,
                           pa_tagstruct *tagstruct) {
    uint32_t version;
    pa_tagstruct *reply;
    struct volume_api_connection *api_connection;

    pa_assert(u);
    pa_assert(native_connection);
    pa_assert(tagstruct);

    if (pa_tagstruct_getu32(tagstruct, &version) < 0
            || version < 1
            || !pa_tagstruct_eof(tagstruct)) {
        pa_log_info("Failed to parse the parameters of a CONNECT command.");
        return -1;
    }

    if (pa_hashmap_get(u->connections, native_connection)) {
        pa_log_info("Tried to connect an already connected client.");
        return -1;
    }

    reply = reply_new(tag);
    pa_tagstruct_putu32(reply, PA_VOLUME_API_VERSION);
    pa_pstream_send_tagstruct(pa_native_connection_get_pstream(native_connection), reply);

    api_connection = volume_api_connection_new(native_connection);
    add_connection(u, api_connection);

    return 0;
}

static int command_disconnect(struct userdata *u, pa_native_connection *native_connection, uint32_t tag,
                              pa_tagstruct *tagstruct) {
    struct volume_api_connection *api_connection;

    pa_assert(u);
    pa_assert(native_connection);
    pa_assert(tagstruct);

    if (!pa_tagstruct_eof(tagstruct)) {
        pa_log_info("Failed to parse the parameters of a DISCONNECT command.");
        return -1;
    }

    api_connection = pa_hashmap_get(u->connections, native_connection);
    if (!api_connection) {
        pa_log_info("Tried to disconnect an unconnected client.");
        return -1;
    }

    remove_connection(u, api_connection);

    return 0;
}

static int command_subscribe(struct userdata *u, pa_native_connection *native_connection, uint32_t tag,
                             pa_tagstruct *tagstruct) {
    uint32_t mask;
    struct volume_api_connection *api_connection;

    pa_assert(u);
    pa_assert(native_connection);
    pa_assert(tagstruct);

    if (pa_tagstruct_getu32(tagstruct, &mask) < 0
            || !pa_tagstruct_eof(tagstruct)) {
        pa_log_info("Failed to parse the parameters of a SUBSCRIBE command.");
        return -1;
    }

    api_connection = pa_hashmap_get(u->connections, native_connection);
    if (!api_connection) {
        pa_log_info("SUBSCRIBE command received from an unconnected client.");
        return -1;
    }

    api_connection->subscription_mask = mask;

    return 0;
}

static void fill_volume_control_info(pa_tagstruct *tagstruct, pa_volume_control *control) {
    unsigned i;

    pa_assert(tagstruct);
    pa_assert(control);

    pa_tagstruct_putu32(tagstruct, control->index);
    pa_tagstruct_puts(tagstruct, control->name);
    pa_tagstruct_puts(tagstruct, control->description);
    pa_tagstruct_put_proplist(tagstruct, control->proplist);
    pa_tagstruct_put_volume(tagstruct, control->volume.volume);
    pa_tagstruct_put_channel_map(tagstruct, &control->volume.channel_map);

    for (i = 0; i < control->volume.channel_map.channels; i++) {
        uint64_t u;

        memcpy(&u, &control->volume.balance[i], sizeof(uint64_t));
        pa_tagstruct_putu64(tagstruct, u);
    }

    pa_tagstruct_put_boolean(tagstruct, control->convertible_to_dB);
}

static int command_get_server_info(struct userdata *u, pa_native_connection *native_connection, uint32_t tag,
                                   pa_tagstruct *tagstruct) {
    struct volume_api_connection *api_connection;
    pa_tagstruct *reply;

    pa_assert(u);
    pa_assert(native_connection);
    pa_assert(tagstruct);

    if (!pa_tagstruct_eof(tagstruct)) {
        pa_log_info("Failed to parse the parameters of a GET_SERVER_INFO command.");
        return -1;
    }

    api_connection = pa_hashmap_get(u->connections, native_connection);
    if (!api_connection) {
        pa_log_info("GET_SERVER_INFO command received from an unconnected client.");
        return -1;
    }

    reply = reply_new(tag);
    pa_tagstruct_putu32(reply, u->volume_api->main_output_volume_control
                               ? u->volume_api->main_output_volume_control->index
                               : PA_INVALID_INDEX);
    pa_tagstruct_putu32(reply, u->volume_api->main_input_volume_control
                               ? u->volume_api->main_input_volume_control->index
                               : PA_INVALID_INDEX);
    pa_tagstruct_putu32(reply, u->volume_api->main_output_mute_control
                               ? u->volume_api->main_output_mute_control->index
                               : PA_INVALID_INDEX);
    pa_tagstruct_putu32(reply, u->volume_api->main_input_mute_control
                               ? u->volume_api->main_input_mute_control->index
                               : PA_INVALID_INDEX);
    pa_pstream_send_tagstruct(pa_native_connection_get_pstream(native_connection), reply);

    return 0;
}

static int command_get_volume_control_info(struct userdata *u, pa_native_connection *native_connection, uint32_t tag,
                                           pa_tagstruct *tagstruct) {
    pa_pstream *pstream;
    uint32_t idx;
    const char *name;
    struct volume_api_connection *api_connection;
    pa_volume_control *control = NULL;
    pa_tagstruct *reply;

    pa_assert(u);
    pa_assert(native_connection);
    pa_assert(tagstruct);

    pstream = pa_native_connection_get_pstream(native_connection);

    if (pa_tagstruct_getu32(tagstruct, &idx) < 0
            || pa_tagstruct_gets(tagstruct, &name)
            || (idx == PA_INVALID_INDEX && !name)
            || (idx != PA_INVALID_INDEX && name)
            || (name && !*name)
            || !pa_tagstruct_eof(tagstruct)) {
        pa_log_info("Failed to parse the parameters of a GET_VOLUME_CONTROL_INFO command.");
        return -1;
    }

    api_connection = pa_hashmap_get(u->connections, native_connection);
    if (!api_connection) {
        pa_log_info("GET_VOLUME_CONTROL_INFO command received from an unconnected client.");
        return -1;
    }

    if (name) {
        control = pa_hashmap_get(u->volume_api->volume_controls, name);

        if (!control)
            pa_atou(name, &idx);
    }

    if (idx != PA_INVALID_INDEX)
        control = pa_volume_api_get_volume_control_by_index(u->volume_api, idx);

    if (!control) {
        pa_log_info("Tried to get volume control info for a non-existing volume control.");
        pa_pstream_send_error(pstream, tag, PA_ERR_NOENTITY);
        return 0;
    }

    reply = reply_new(tag);
    fill_volume_control_info(reply, control);
    pa_pstream_send_tagstruct(pstream, reply);

    return 0;
}

static int command_get_volume_control_info_list(struct userdata *u, pa_native_connection *native_connection, uint32_t tag,
                                                pa_tagstruct *tagstruct) {
    struct volume_api_connection *api_connection;
    pa_tagstruct *reply;
    pa_volume_control *control;
    void *state;

    pa_assert(u);
    pa_assert(native_connection);
    pa_assert(tagstruct);

    if (!pa_tagstruct_eof(tagstruct)) {
        pa_log_info("Failed to parse the parameters of a GET_VOLUME_CONTROL_INFO_LIST command.");
        return -1;
    }

    api_connection = pa_hashmap_get(u->connections, native_connection);
    if (!api_connection) {
        pa_log_info("GET_VOLUME_CONTROL_INFO_LIST command received from an unconnected client.");
        return -1;
    }

    reply = reply_new(tag);

    PA_HASHMAP_FOREACH(control, u->volume_api->volume_controls, state)
        fill_volume_control_info(reply, control);

    pa_pstream_send_tagstruct(pa_native_connection_get_pstream(native_connection), reply);

    return 0;
}

static int command_set_volume_control_volume(struct userdata *u, pa_native_connection *native_connection, uint32_t tag,
                                             pa_tagstruct *tagstruct) {
    pa_pstream *pstream;
    uint32_t idx;
    const char *name;
    pa_bvolume bvolume;
    bool set_volume;
    bool set_balance;
    struct volume_api_connection *api_connection;
    pa_volume_control *control = NULL;
    int r;

    pa_assert(u);
    pa_assert(native_connection);
    pa_assert(tagstruct);

    pstream = pa_native_connection_get_pstream(native_connection);

    if (pa_tagstruct_getu32(tagstruct, &idx) < 0
            || pa_tagstruct_gets(tagstruct, &name) < 0
            || (idx == PA_INVALID_INDEX && !name)
            || (idx != PA_INVALID_INDEX && name)
            || (name && !*name)
            || pa_tagstruct_get_volume(tagstruct, &bvolume.volume) < 0
            || pa_tagstruct_get_channel_map(tagstruct, &bvolume.channel_map) < 0)
        goto fail_parse;

    set_volume = PA_VOLUME_IS_VALID(bvolume.volume);
    set_balance = pa_channel_map_valid(&bvolume.channel_map);

    if (set_balance) {
        unsigned i;

        for (i = 0; i < bvolume.channel_map.channels; i++) {
            uint64_t balance;

            if (pa_tagstruct_getu64(tagstruct, &balance) < 0)
                goto fail_parse;

            memcpy(&bvolume.balance[i], &balance, sizeof(double));

            if (!pa_balance_valid(bvolume.balance[i]))
                goto fail_parse;
        }
    }

    if (!pa_tagstruct_eof(tagstruct))
        goto fail_parse;

    api_connection = pa_hashmap_get(u->connections, native_connection);
    if (!api_connection) {
        pa_log_info("SET_VOLUME_CONTROL_VOLUME received from an unconnected client.");
        return -1;
    }

    if (name) {
        control = pa_hashmap_get(u->volume_api->volume_controls, name);

        if (!control)
            pa_atou(name, &idx);
    }

    if (idx != PA_INVALID_INDEX)
        control = pa_volume_api_get_volume_control_by_index(u->volume_api, idx);

    if (!control) {
        pa_log_info("Tried to set volume of a non-existing volume control.");
        pa_pstream_send_error(pstream, tag, PA_ERR_NOENTITY);
        return 0;
    }

    r = pa_volume_control_set_volume(control, &bvolume, set_volume, set_balance);
    if (r < 0) {
        pa_pstream_send_error(pstream, tag, -r);
        return 0;
    }

    pa_pstream_send_simple_ack(pstream, tag);

    return 0;

fail_parse:
    pa_log_info("Failed to parse the parameters of a SET_VOLUME_CONTROL_VOLUME command.");

    return -1;
}

static void fill_mute_control_info(pa_tagstruct *tagstruct, pa_mute_control *control) {
    pa_assert(tagstruct);
    pa_assert(control);

    pa_tagstruct_putu32(tagstruct, control->index);
    pa_tagstruct_puts(tagstruct, control->name);
    pa_tagstruct_puts(tagstruct, control->description);
    pa_tagstruct_put_proplist(tagstruct, control->proplist);
    pa_tagstruct_put_boolean(tagstruct, control->mute);
}

static int command_get_mute_control_info(struct userdata *u, pa_native_connection *native_connection, uint32_t tag,
                                         pa_tagstruct *tagstruct) {
    pa_pstream *pstream;
    uint32_t idx;
    const char *name;
    struct volume_api_connection *api_connection;
    pa_mute_control *control = NULL;
    pa_tagstruct *reply;

    pa_assert(u);
    pa_assert(native_connection);
    pa_assert(tagstruct);

    pstream = pa_native_connection_get_pstream(native_connection);

    if (pa_tagstruct_getu32(tagstruct, &idx) < 0
            || pa_tagstruct_gets(tagstruct, &name)
            || (idx == PA_INVALID_INDEX && !name)
            || (idx != PA_INVALID_INDEX && name)
            || (name && !*name)
            || !pa_tagstruct_eof(tagstruct)) {
        pa_log_info("Failed to parse the parameters of a GET_MUTE_CONTROL_INFO command.");
        return -1;
    }

    api_connection = pa_hashmap_get(u->connections, native_connection);
    if (!api_connection) {
        pa_log_info("GET_MUTE_CONTROL_INFO command received from an unconnected client.");
        return -1;
    }

    if (name) {
        control = pa_hashmap_get(u->volume_api->mute_controls, name);

        if (!control)
            pa_atou(name, &idx);
    }

    if (idx != PA_INVALID_INDEX)
        control = pa_volume_api_get_mute_control_by_index(u->volume_api, idx);

    if (!control) {
        pa_log_info("Tried to get mute control info for a non-existing mute control.");
        pa_pstream_send_error(pstream, tag, PA_ERR_NOENTITY);
        return 0;
    }

    reply = reply_new(tag);
    fill_mute_control_info(reply, control);
    pa_pstream_send_tagstruct(pstream, reply);

    return 0;
}

static int command_get_mute_control_info_list(struct userdata *u, pa_native_connection *native_connection, uint32_t tag,
                                              pa_tagstruct *tagstruct) {
    struct volume_api_connection *api_connection;
    pa_tagstruct *reply;
    pa_mute_control *control;
    void *state;

    pa_assert(u);
    pa_assert(native_connection);
    pa_assert(tagstruct);

    if (!pa_tagstruct_eof(tagstruct)) {
        pa_log_info("Failed to parse the parameters of a GET_MUTE_CONTROL_INFO_LIST command.");
        return -1;
    }

    api_connection = pa_hashmap_get(u->connections, native_connection);
    if (!api_connection) {
        pa_log_info("GET_MUTE_CONTROL_INFO_LIST command received from an unconnected client.");
        return -1;
    }

    reply = reply_new(tag);

    PA_HASHMAP_FOREACH(control, u->volume_api->mute_controls, state)
        fill_mute_control_info(reply, control);

    pa_pstream_send_tagstruct(pa_native_connection_get_pstream(native_connection), reply);

    return 0;
}

static int command_set_mute_control_mute(struct userdata *u, pa_native_connection *native_connection, uint32_t tag,
                                         pa_tagstruct *tagstruct) {
    pa_pstream *pstream;
    uint32_t idx;
    const char *name;
    bool mute;
    struct volume_api_connection *api_connection;
    pa_mute_control *control = NULL;
    int r;

    pa_assert(u);
    pa_assert(native_connection);
    pa_assert(tagstruct);

    pstream = pa_native_connection_get_pstream(native_connection);

    if (pa_tagstruct_getu32(tagstruct, &idx) < 0
            || pa_tagstruct_gets(tagstruct, &name) < 0
            || (idx == PA_INVALID_INDEX && !name)
            || (idx != PA_INVALID_INDEX && name)
            || (name && !*name)
            || pa_tagstruct_get_boolean(tagstruct, &mute) < 0
            || !pa_tagstruct_eof(tagstruct))
        goto fail_parse;

    api_connection = pa_hashmap_get(u->connections, native_connection);
    if (!api_connection) {
        pa_log_info("SET_MUTE_CONTROL_MUTE received from an unconnected client.");
        return -1;
    }

    if (name) {
        control = pa_hashmap_get(u->volume_api->mute_controls, name);

        if (!control)
            pa_atou(name, &idx);
    }

    if (idx != PA_INVALID_INDEX)
        control = pa_volume_api_get_mute_control_by_index(u->volume_api, idx);

    if (!control) {
        pa_log_info("Tried to set mute of a non-existing mute control.");
        pa_pstream_send_error(pstream, tag, PA_ERR_NOENTITY);
        return 0;
    }

    r = pa_mute_control_set_mute(control, mute);
    if (r < 0) {
        pa_pstream_send_error(pstream, tag, -r);
        return 0;
    }

    pa_pstream_send_simple_ack(pstream, tag);

    return 0;

fail_parse:
    pa_log_info("Failed to parse the parameters of a SET_MUTE_CONTROL_MUTE command.");

    return -1;
}

static void fill_device_info(pa_tagstruct *tagstruct, pa_device *device) {
    unsigned i;

    pa_assert(tagstruct);
    pa_assert(device);

    pa_tagstruct_putu32(tagstruct, device->index);
    pa_tagstruct_puts(tagstruct, device->name);
    pa_tagstruct_puts(tagstruct, device->description);
    pa_tagstruct_putu8(tagstruct, device->direction);
    pa_tagstruct_putu32(tagstruct, pa_dynarray_size(device->device_types));

    for (i = 0; i < pa_dynarray_size(device->device_types); i++)
        pa_tagstruct_puts(tagstruct, pa_dynarray_get(device->device_types, i));

    pa_tagstruct_put_proplist(tagstruct, device->proplist);
    pa_tagstruct_putu32(tagstruct, device->volume_control ? device->volume_control->index : PA_INVALID_INDEX);
    pa_tagstruct_putu32(tagstruct, device->mute_control ? device->mute_control->index : PA_INVALID_INDEX);
}

static int command_get_device_info(struct userdata *u, pa_native_connection *native_connection, uint32_t tag,
                                   pa_tagstruct *tagstruct) {
    pa_pstream *pstream;
    uint32_t idx;
    const char *name;
    struct volume_api_connection *api_connection;
    pa_device *device = NULL;
    pa_tagstruct *reply;

    pa_assert(u);
    pa_assert(native_connection);
    pa_assert(tagstruct);

    pstream = pa_native_connection_get_pstream(native_connection);

    if (pa_tagstruct_getu32(tagstruct, &idx) < 0
            || pa_tagstruct_gets(tagstruct, &name)
            || (idx == PA_INVALID_INDEX && !name)
            || (idx != PA_INVALID_INDEX && name)
            || (name && !*name)
            || !pa_tagstruct_eof(tagstruct)) {
        pa_log_info("Failed to parse the parameters of a GET_DEVICE_INFO command.");
        return -1;
    }

    api_connection = pa_hashmap_get(u->connections, native_connection);
    if (!api_connection) {
        pa_log_info("GET_DEVICE_INFO command received from an unconnected client.");
        return -1;
    }

    if (name) {
        device = pa_hashmap_get(u->volume_api->devices, name);

        if (!device)
            pa_atou(name, &idx);
    }

    if (idx != PA_INVALID_INDEX)
        device = pa_volume_api_get_device_by_index(u->volume_api, idx);

    if (!device) {
        pa_log_info("Tried to get device info for a non-existing device.");
        pa_pstream_send_error(pstream, tag, PA_ERR_NOENTITY);
        return 0;
    }

    reply = reply_new(tag);
    fill_device_info(reply, device);
    pa_pstream_send_tagstruct(pstream, reply);

    return 0;
}

static int command_get_device_info_list(struct userdata *u, pa_native_connection *native_connection, uint32_t tag,
                                        pa_tagstruct *tagstruct) {
    struct volume_api_connection *api_connection;
    pa_tagstruct *reply;
    pa_device *device;
    void *state;

    pa_assert(u);
    pa_assert(native_connection);
    pa_assert(tagstruct);

    if (!pa_tagstruct_eof(tagstruct)) {
        pa_log_info("Failed to parse the parameters of a GET_DEVICE_INFO_LIST command.");
        return -1;
    }

    api_connection = pa_hashmap_get(u->connections, native_connection);
    if (!api_connection) {
        pa_log_info("GET_DEVICE_INFO_LIST command received from an unconnected client.");
        return -1;
    }

    reply = reply_new(tag);

    PA_HASHMAP_FOREACH(device, u->volume_api->devices, state)
        fill_device_info(reply, device);

    pa_pstream_send_tagstruct(pa_native_connection_get_pstream(native_connection), reply);

    return 0;
}

static void fill_stream_info(pa_tagstruct *tagstruct, pas_stream *stream) {
    pa_assert(tagstruct);
    pa_assert(stream);

    pa_tagstruct_putu32(tagstruct, stream->index);
    pa_tagstruct_puts(tagstruct, stream->name);
    pa_tagstruct_puts(tagstruct, stream->description);
    pa_tagstruct_putu8(tagstruct, stream->direction);
    pa_tagstruct_put_proplist(tagstruct, stream->proplist);
    pa_tagstruct_putu32(tagstruct, stream->volume_control ? stream->volume_control->index : PA_INVALID_INDEX);
    pa_tagstruct_putu32(tagstruct, stream->mute_control ? stream->mute_control->index : PA_INVALID_INDEX);
}

static int command_get_stream_info(struct userdata *u, pa_native_connection *native_connection, uint32_t tag,
                                   pa_tagstruct *tagstruct) {
    pa_pstream *pstream;
    uint32_t idx;
    const char *name;
    struct volume_api_connection *api_connection;
    pas_stream *stream = NULL;
    pa_tagstruct *reply;

    pa_assert(u);
    pa_assert(native_connection);
    pa_assert(tagstruct);

    pstream = pa_native_connection_get_pstream(native_connection);

    if (pa_tagstruct_getu32(tagstruct, &idx) < 0
            || pa_tagstruct_gets(tagstruct, &name)
            || (idx == PA_INVALID_INDEX && !name)
            || (idx != PA_INVALID_INDEX && name)
            || (name && !*name)
            || !pa_tagstruct_eof(tagstruct)) {
        pa_log_info("Failed to parse the parameters of a GET_STREAM_INFO command.");
        return -1;
    }

    api_connection = pa_hashmap_get(u->connections, native_connection);
    if (!api_connection) {
        pa_log_info("GET_STREAM_INFO command received from an unconnected client.");
        return -1;
    }

    if (name) {
        stream = pa_hashmap_get(u->volume_api->streams, name);

        if (!stream)
            pa_atou(name, &idx);
    }

    if (idx != PA_INVALID_INDEX)
        stream = pa_volume_api_get_stream_by_index(u->volume_api, idx);

    if (!stream) {
        pa_log_info("Tried to get stream info for a non-existing stream.");
        pa_pstream_send_error(pstream, tag, PA_ERR_NOENTITY);
        return 0;
    }

    reply = reply_new(tag);
    fill_stream_info(reply, stream);
    pa_pstream_send_tagstruct(pstream, reply);

    return 0;
}

static int command_get_stream_info_list(struct userdata *u, pa_native_connection *native_connection, uint32_t tag,
                                        pa_tagstruct *tagstruct) {
    struct volume_api_connection *api_connection;
    pa_tagstruct *reply;
    pas_stream *stream;
    void *state;

    pa_assert(u);
    pa_assert(native_connection);
    pa_assert(tagstruct);

    if (!pa_tagstruct_eof(tagstruct)) {
        pa_log_info("Failed to parse the parameters of a GET_STREAM_INFO_LIST command.");
        return -1;
    }

    api_connection = pa_hashmap_get(u->connections, native_connection);
    if (!api_connection) {
        pa_log_info("GET_STREAM_INFO_LIST command received from an unconnected client.");
        return -1;
    }

    reply = reply_new(tag);

    PA_HASHMAP_FOREACH(stream, u->volume_api->streams, state)
        fill_stream_info(reply, stream);

    pa_pstream_send_tagstruct(pa_native_connection_get_pstream(native_connection), reply);

    return 0;
}

static void fill_audio_group_info(pa_tagstruct *tagstruct, pa_audio_group *group) {
    pa_assert(tagstruct);
    pa_assert(group);

    pa_tagstruct_putu32(tagstruct, group->index);
    pa_tagstruct_puts(tagstruct, group->name);
    pa_tagstruct_puts(tagstruct, group->description);
    pa_tagstruct_put_proplist(tagstruct, group->proplist);
    pa_tagstruct_putu32(tagstruct, group->volume_control ? group->volume_control->index : PA_INVALID_INDEX);
    pa_tagstruct_putu32(tagstruct, group->mute_control ? group->mute_control->index : PA_INVALID_INDEX);
}

static int command_get_audio_group_info(struct userdata *u, pa_native_connection *native_connection, uint32_t tag,
                                        pa_tagstruct *tagstruct) {
    pa_pstream *pstream;
    uint32_t idx;
    const char *name;
    struct volume_api_connection *api_connection;
    pa_audio_group *group = NULL;
    pa_tagstruct *reply;

    pa_assert(u);
    pa_assert(native_connection);
    pa_assert(tagstruct);

    pstream = pa_native_connection_get_pstream(native_connection);

    if (pa_tagstruct_getu32(tagstruct, &idx) < 0
            || pa_tagstruct_gets(tagstruct, &name)
            || (idx == PA_INVALID_INDEX && !name)
            || (idx != PA_INVALID_INDEX && name)
            || (name && !*name)
            || !pa_tagstruct_eof(tagstruct)) {
        pa_log_info("Failed to parse the parameters of a GET_AUDIO_GROUP_INFO command.");
        return -1;
    }

    api_connection = pa_hashmap_get(u->connections, native_connection);
    if (!api_connection) {
        pa_log_info("GET_AUDIO_GROUP_INFO command received from an unconnected client.");
        return -1;
    }

    if (name) {
        group = pa_hashmap_get(u->volume_api->audio_groups, name);

        if (!group)
            pa_atou(name, &idx);
    }

    if (idx != PA_INVALID_INDEX)
        group = pa_volume_api_get_audio_group_by_index(u->volume_api, idx);

    if (!group) {
        pa_log_info("Tried to get audio group info for a non-existing audio group.");
        pa_pstream_send_error(pstream, tag, PA_ERR_NOENTITY);
        return 0;
    }

    reply = reply_new(tag);
    fill_audio_group_info(reply, group);
    pa_pstream_send_tagstruct(pstream, reply);

    return 0;
}

static int command_get_audio_group_info_list(struct userdata *u, pa_native_connection *native_connection, uint32_t tag,
                                             pa_tagstruct *tagstruct) {
    struct volume_api_connection *api_connection;
    pa_tagstruct *reply;
    pa_audio_group *group;
    void *state;

    pa_assert(u);
    pa_assert(native_connection);
    pa_assert(tagstruct);

    if (!pa_tagstruct_eof(tagstruct)) {
        pa_log_info("Failed to parse the parameters of a GET_AUDIO_GROUP_INFO_LIST command.");
        return -1;
    }

    api_connection = pa_hashmap_get(u->connections, native_connection);
    if (!api_connection) {
        pa_log_info("GET_AUDIO_GROUP_INFO_LIST command received from an unconnected client.");
        return -1;
    }

    reply = reply_new(tag);

    PA_HASHMAP_FOREACH(group, u->volume_api->audio_groups, state)
        fill_audio_group_info(reply, group);

    pa_pstream_send_tagstruct(pa_native_connection_get_pstream(native_connection), reply);

    return 0;
}

static int extension_cb(pa_native_protocol *protocol, pa_module *module, pa_native_connection *connection, uint32_t tag,
                        pa_tagstruct *tagstruct) {
    struct userdata *u;
    uint32_t command;

    pa_assert(protocol);
    pa_assert(module);
    pa_assert(connection);
    pa_assert(tagstruct);

    u = module->userdata;

    if (pa_tagstruct_getu32(tagstruct, &command) < 0)
        return -1;

    switch (command) {
        case PA_VOLUME_API_COMMAND_CONNECT:
            return command_connect(u, connection, tag, tagstruct);

        case PA_VOLUME_API_COMMAND_DISCONNECT:
            return command_disconnect(u, connection, tag, tagstruct);

        case PA_VOLUME_API_COMMAND_SUBSCRIBE:
            return command_subscribe(u, connection, tag, tagstruct);

        case PA_VOLUME_API_COMMAND_GET_SERVER_INFO:
            return command_get_server_info(u, connection, tag, tagstruct);

        case PA_VOLUME_API_COMMAND_GET_VOLUME_CONTROL_INFO:
            return command_get_volume_control_info(u, connection, tag, tagstruct);

        case PA_VOLUME_API_COMMAND_GET_VOLUME_CONTROL_INFO_LIST:
            return command_get_volume_control_info_list(u, connection, tag, tagstruct);

        case PA_VOLUME_API_COMMAND_SET_VOLUME_CONTROL_VOLUME:
            return command_set_volume_control_volume(u, connection, tag, tagstruct);

        case PA_VOLUME_API_COMMAND_GET_MUTE_CONTROL_INFO:
            return command_get_mute_control_info(u, connection, tag, tagstruct);

        case PA_VOLUME_API_COMMAND_GET_MUTE_CONTROL_INFO_LIST:
            return command_get_mute_control_info_list(u, connection, tag, tagstruct);

        case PA_VOLUME_API_COMMAND_SET_MUTE_CONTROL_MUTE:
            return command_set_mute_control_mute(u, connection, tag, tagstruct);

        case PA_VOLUME_API_COMMAND_GET_DEVICE_INFO:
            return command_get_device_info(u, connection, tag, tagstruct);

        case PA_VOLUME_API_COMMAND_GET_DEVICE_INFO_LIST:
            return command_get_device_info_list(u, connection, tag, tagstruct);

        case PA_VOLUME_API_COMMAND_GET_STREAM_INFO:
            return command_get_stream_info(u, connection, tag, tagstruct);

        case PA_VOLUME_API_COMMAND_GET_STREAM_INFO_LIST:
            return command_get_stream_info_list(u, connection, tag, tagstruct);

        case PA_VOLUME_API_COMMAND_GET_AUDIO_GROUP_INFO:
            return command_get_audio_group_info(u, connection, tag, tagstruct);

        case PA_VOLUME_API_COMMAND_GET_AUDIO_GROUP_INFO_LIST:
            return command_get_audio_group_info_list(u, connection, tag, tagstruct);

        default:
            pa_log_info("Received unrecognized command: %u", command);
            return -1;
    }
}

static void send_subscribe_event(struct userdata *u, pa_ext_volume_api_subscription_event_type_t event_type,
                                 uint32_t idx) {
    pa_ext_volume_api_subscription_event_type_t facility;
    struct volume_api_connection *connection;
    void *state;

    pa_assert(u);

    facility = event_type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;

    PA_HASHMAP_FOREACH(connection, u->connections, state) {
        pa_tagstruct *tagstruct;

        if (!(connection->subscription_mask & (1 << facility)))
            continue;

        tagstruct = pa_tagstruct_new(NULL, 0);
        pa_tagstruct_putu32(tagstruct, PA_COMMAND_EXTENSION);
        pa_tagstruct_putu32(tagstruct, (uint32_t) -1);
        pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
        pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
        pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_SUBSCRIBE_EVENT);
        pa_tagstruct_putu32(tagstruct, event_type);
        pa_tagstruct_putu32(tagstruct, idx);
        pa_pstream_send_tagstruct(pa_native_connection_get_pstream(connection->native_connection), tagstruct);
    }
}

static pa_hook_result_t volume_control_put_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_volume_control *control = call_data;

    pa_assert(u);
    pa_assert(control);

    send_subscribe_event(u, PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_VOLUME_CONTROL | PA_SUBSCRIPTION_EVENT_NEW,
                         control->index);

    return PA_HOOK_OK;
}

static pa_hook_result_t volume_control_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_volume_control *control = call_data;

    pa_assert(u);
    pa_assert(control);

    send_subscribe_event(u, PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_VOLUME_CONTROL | PA_SUBSCRIPTION_EVENT_REMOVE,
                         control->index);

    return PA_HOOK_OK;
}

static pa_hook_result_t volume_control_event_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_volume_control *control = call_data;

    pa_assert(u);
    pa_assert(control);

    send_subscribe_event(u, PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_VOLUME_CONTROL | PA_SUBSCRIPTION_EVENT_CHANGE,
                         control->index);

    return PA_HOOK_OK;
}

static pa_hook_result_t mute_control_put_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_mute_control *control = call_data;

    pa_assert(u);
    pa_assert(control);

    send_subscribe_event(u, PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_MUTE_CONTROL | PA_SUBSCRIPTION_EVENT_NEW,
                         control->index);

    return PA_HOOK_OK;
}

static pa_hook_result_t mute_control_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_mute_control *control = call_data;

    pa_assert(u);
    pa_assert(control);

    send_subscribe_event(u, PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_MUTE_CONTROL | PA_SUBSCRIPTION_EVENT_REMOVE,
                         control->index);

    return PA_HOOK_OK;
}

static pa_hook_result_t mute_control_event_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_mute_control *control = call_data;

    pa_assert(u);
    pa_assert(control);

    send_subscribe_event(u, PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_MUTE_CONTROL | PA_SUBSCRIPTION_EVENT_CHANGE,
                         control->index);

    return PA_HOOK_OK;
}

static pa_hook_result_t device_put_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_device *device = call_data;

    pa_assert(u);
    pa_assert(device);

    send_subscribe_event(u, PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_DEVICE | PA_SUBSCRIPTION_EVENT_NEW, device->index);

    return PA_HOOK_OK;
}

static pa_hook_result_t device_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_device *device = call_data;

    pa_assert(u);
    pa_assert(device);

    send_subscribe_event(u, PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_DEVICE | PA_SUBSCRIPTION_EVENT_REMOVE, device->index);

    return PA_HOOK_OK;
}

static pa_hook_result_t device_event_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_device *device = call_data;

    pa_assert(u);
    pa_assert(device);

    send_subscribe_event(u, PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_DEVICE | PA_SUBSCRIPTION_EVENT_CHANGE, device->index);

    return PA_HOOK_OK;
}

static pa_hook_result_t stream_put_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pas_stream *stream = call_data;

    pa_assert(u);
    pa_assert(stream);

    send_subscribe_event(u, PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_STREAM | PA_SUBSCRIPTION_EVENT_NEW, stream->index);

    return PA_HOOK_OK;
}

static pa_hook_result_t stream_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pas_stream *stream = call_data;

    pa_assert(u);
    pa_assert(stream);

    send_subscribe_event(u, PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_STREAM | PA_SUBSCRIPTION_EVENT_REMOVE, stream->index);

    return PA_HOOK_OK;
}

static pa_hook_result_t stream_event_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pas_stream *stream = call_data;

    pa_assert(u);
    pa_assert(stream);

    send_subscribe_event(u, PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_STREAM | PA_SUBSCRIPTION_EVENT_CHANGE, stream->index);

    return PA_HOOK_OK;
}

static pa_hook_result_t audio_group_put_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_audio_group *group = call_data;

    pa_assert(u);
    pa_assert(group);

    send_subscribe_event(u, PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_AUDIO_GROUP | PA_SUBSCRIPTION_EVENT_NEW, group->index);

    return PA_HOOK_OK;
}

static pa_hook_result_t audio_group_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_audio_group *group = call_data;

    pa_assert(u);
    pa_assert(group);

    send_subscribe_event(u, PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_AUDIO_GROUP | PA_SUBSCRIPTION_EVENT_REMOVE,
                         group->index);

    return PA_HOOK_OK;
}

static pa_hook_result_t audio_group_event_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_audio_group *group = call_data;

    pa_assert(u);
    pa_assert(group);

    send_subscribe_event(u, PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_AUDIO_GROUP | PA_SUBSCRIPTION_EVENT_CHANGE,
                         group->index);

    return PA_HOOK_OK;
}

static pa_hook_result_t server_event_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;

    pa_assert(u);

    send_subscribe_event(u, PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_SERVER | PA_SUBSCRIPTION_EVENT_CHANGE,
                         PA_INVALID_INDEX);

    return PA_HOOK_OK;
}

static pa_hook_result_t native_connection_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    pa_native_connection *native_connection = call_data;
    struct userdata *u = userdata;
    struct volume_api_connection *api_connection;

    pa_assert(native_connection);
    pa_assert(u);

    api_connection = pa_hashmap_get(u->connections, native_connection);
    if (!api_connection)
        return PA_HOOK_OK;

    api_connection->dead = true;
    remove_connection(u, api_connection);

    return PA_HOOK_OK;
}

int pa__init(pa_module *module) {
    struct userdata *u;

    pa_assert(module);

    u = module->userdata = pa_xnew0(struct userdata, 1);
    u->native_protocol = pa_native_protocol_get(module->core);
    pa_native_protocol_install_ext(u->native_protocol, module, extension_cb);
    u->extension_installed = true;
    u->volume_api = pa_volume_api_get(module->core);
    u->volume_control_put_slot = pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_PUT],
                                                 PA_HOOK_NORMAL, volume_control_put_cb, u);
    u->volume_control_unlink_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_UNLINK], PA_HOOK_NORMAL,
                            volume_control_unlink_cb, u);
    u->volume_control_description_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_DESCRIPTION_CHANGED],
                            PA_HOOK_NORMAL, volume_control_event_cb, u);
    u->volume_control_volume_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_VOLUME_CHANGED],
                            PA_HOOK_NORMAL, volume_control_event_cb, u);
    u->volume_control_convertible_to_db_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_CONVERTIBLE_TO_DB_CHANGED], PA_HOOK_NORMAL,
                            volume_control_event_cb, u);
    u->mute_control_put_slot = pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_PUT],
                                               PA_HOOK_NORMAL, mute_control_put_cb, u);
    u->mute_control_unlink_slot = pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_UNLINK],
                                                  PA_HOOK_NORMAL, mute_control_unlink_cb, u);
    u->mute_control_description_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_DESCRIPTION_CHANGED],
                            PA_HOOK_NORMAL, mute_control_event_cb, u);
    u->mute_control_mute_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_MUTE_CHANGED], PA_HOOK_NORMAL,
                            mute_control_event_cb, u);
    u->device_put_slot = pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_DEVICE_PUT], PA_HOOK_NORMAL,
                                         device_put_cb, u);
    u->device_unlink_slot = pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_DEVICE_UNLINK],
                                            PA_HOOK_NORMAL, device_unlink_cb, u);
    u->device_description_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_DEVICE_DESCRIPTION_CHANGED], PA_HOOK_NORMAL,
                            device_event_cb, u);
    u->device_volume_control_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_DEVICE_VOLUME_CONTROL_CHANGED],
                            PA_HOOK_NORMAL, device_event_cb, u);
    u->device_mute_control_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_DEVICE_MUTE_CONTROL_CHANGED], PA_HOOK_NORMAL,
                            device_event_cb, u);
    u->stream_put_slot = pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_PUT], PA_HOOK_NORMAL,
                                         stream_put_cb, u);
    u->stream_unlink_slot = pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_UNLINK],
                                            PA_HOOK_NORMAL, stream_unlink_cb, u);
    u->stream_description_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_DESCRIPTION_CHANGED], PA_HOOK_NORMAL,
                            stream_event_cb, u);
    u->stream_proplist_changed_slot = pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_PROPLIST_CHANGED],
                                                      PA_HOOK_NORMAL, stream_event_cb, u);
    u->stream_volume_control_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_VOLUME_CONTROL_CHANGED],
                            PA_HOOK_NORMAL, stream_event_cb, u);
    u->stream_relative_volume_control_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_RELATIVE_VOLUME_CONTROL_CHANGED],
                            PA_HOOK_NORMAL, stream_event_cb, u);
    u->stream_mute_control_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_MUTE_CONTROL_CHANGED], PA_HOOK_NORMAL,
                            stream_event_cb, u);
    u->audio_group_put_slot = pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_PUT],
                                              PA_HOOK_NORMAL, audio_group_put_cb, u);
    u->audio_group_unlink_slot = pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_UNLINK],
                                                 PA_HOOK_NORMAL, audio_group_unlink_cb, u);
    u->audio_group_description_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_DESCRIPTION_CHANGED], PA_HOOK_NORMAL,
                            audio_group_event_cb, u);
    u->audio_group_volume_control_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_VOLUME_CONTROL_CHANGED],
                            PA_HOOK_NORMAL, audio_group_event_cb, u);
    u->audio_group_mute_control_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_MUTE_CONTROL_CHANGED],
                            PA_HOOK_NORMAL, audio_group_event_cb, u);
    u->main_output_volume_control_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_MAIN_OUTPUT_VOLUME_CONTROL_CHANGED],
                            PA_HOOK_NORMAL, server_event_cb, u);
    u->main_input_volume_control_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_MAIN_INPUT_VOLUME_CONTROL_CHANGED],
                            PA_HOOK_NORMAL, server_event_cb, u);
    u->main_output_mute_control_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_MAIN_OUTPUT_MUTE_CONTROL_CHANGED],
                            PA_HOOK_NORMAL, server_event_cb, u);
    u->main_input_mute_control_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_MAIN_INPUT_MUTE_CONTROL_CHANGED],
                            PA_HOOK_NORMAL, server_event_cb, u);
    u->connections = pa_hashmap_new_full(NULL, NULL, NULL, (pa_free_cb_t) volume_api_connection_free);
    u->native_connection_unlink_slot =
            pa_hook_connect(&pa_native_protocol_hooks(u->native_protocol)[PA_NATIVE_HOOK_CONNECTION_UNLINK], PA_HOOK_NORMAL,
                            native_connection_unlink_cb, u);

    return 0;
}

void pa__done(pa_module *module) {
    struct userdata *u;

    pa_assert(module);

    u = module->userdata;
    if (!u)
        return;

    if (u->native_connection_unlink_slot)
        pa_hook_slot_free(u->native_connection_unlink_slot);

    if (u->connections) {
        struct volume_api_connection *connection;

        while ((connection = pa_hashmap_first(u->connections)))
            remove_connection(u, connection);

        pa_hashmap_free(u->connections);
    }

    if (u->main_input_mute_control_changed_slot)
        pa_hook_slot_free(u->main_input_mute_control_changed_slot);

    if (u->main_output_mute_control_changed_slot)
        pa_hook_slot_free(u->main_output_mute_control_changed_slot);

    if (u->main_input_volume_control_changed_slot)
        pa_hook_slot_free(u->main_input_volume_control_changed_slot);

    if (u->main_output_volume_control_changed_slot)
        pa_hook_slot_free(u->main_output_volume_control_changed_slot);

    if (u->audio_group_mute_control_changed_slot)
        pa_hook_slot_free(u->audio_group_mute_control_changed_slot);

    if (u->audio_group_volume_control_changed_slot)
        pa_hook_slot_free(u->audio_group_volume_control_changed_slot);

    if (u->audio_group_description_changed_slot)
        pa_hook_slot_free(u->audio_group_description_changed_slot);

    if (u->audio_group_unlink_slot)
        pa_hook_slot_free(u->audio_group_unlink_slot);

    if (u->audio_group_put_slot)
        pa_hook_slot_free(u->audio_group_put_slot);

    if (u->stream_mute_control_changed_slot)
        pa_hook_slot_free(u->stream_mute_control_changed_slot);

    if (u->stream_relative_volume_control_changed_slot)
        pa_hook_slot_free(u->stream_relative_volume_control_changed_slot);

    if (u->stream_volume_control_changed_slot)
        pa_hook_slot_free(u->stream_volume_control_changed_slot);

    if (u->stream_proplist_changed_slot)
        pa_hook_slot_free(u->stream_proplist_changed_slot);

    if (u->stream_description_changed_slot)
        pa_hook_slot_free(u->stream_description_changed_slot);

    if (u->stream_unlink_slot)
        pa_hook_slot_free(u->stream_unlink_slot);

    if (u->stream_put_slot)
        pa_hook_slot_free(u->stream_put_slot);

    if (u->device_mute_control_changed_slot)
        pa_hook_slot_free(u->device_mute_control_changed_slot);

    if (u->device_volume_control_changed_slot)
        pa_hook_slot_free(u->device_volume_control_changed_slot);

    if (u->device_description_changed_slot)
        pa_hook_slot_free(u->device_description_changed_slot);

    if (u->device_unlink_slot)
        pa_hook_slot_free(u->device_unlink_slot);

    if (u->device_put_slot)
        pa_hook_slot_free(u->device_put_slot);

    if (u->mute_control_mute_changed_slot)
        pa_hook_slot_free(u->mute_control_mute_changed_slot);

    if (u->mute_control_description_changed_slot)
        pa_hook_slot_free(u->mute_control_description_changed_slot);

    if (u->mute_control_unlink_slot)
        pa_hook_slot_free(u->mute_control_unlink_slot);

    if (u->mute_control_put_slot)
        pa_hook_slot_free(u->mute_control_put_slot);

    if (u->volume_control_convertible_to_db_changed_slot)
        pa_hook_slot_free(u->volume_control_convertible_to_db_changed_slot);

    if (u->volume_control_volume_changed_slot)
        pa_hook_slot_free(u->volume_control_volume_changed_slot);

    if (u->volume_control_description_changed_slot)
        pa_hook_slot_free(u->volume_control_description_changed_slot);

    if (u->volume_control_unlink_slot)
        pa_hook_slot_free(u->volume_control_unlink_slot);

    if (u->volume_control_put_slot)
        pa_hook_slot_free(u->volume_control_put_slot);

    if (u->volume_api)
        pa_volume_api_unref(u->volume_api);

    if (u->extension_installed)
        pa_native_protocol_remove_ext(u->native_protocol, module);

    if (u->native_protocol)
        pa_native_protocol_unref(u->native_protocol);

    pa_xfree(u);
}
