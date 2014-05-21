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

#include "ext-volume-api.h"

#include <modules/volume-api/volume-api-common.h>

#include <pulse/direction.h>
#include <pulse/extension.h>
#include <pulse/internal.h>
#include <pulse/xmalloc.h>

#include <pulsecore/core-util.h>
#include <pulsecore/i18n.h>
#include <pulsecore/macro.h>
#include <pulsecore/pstream-util.h>
#include <pulsecore/strbuf.h>

#include <math.h>

struct userdata {
    pa_extension *extension;
    pa_context *context;
    pa_ext_volume_api_state_t state;
    bool state_notification_needed;
    pa_ext_volume_api_state_cb_t state_callback;
    void *state_callback_userdata;
    pa_ext_volume_api_subscription_mask_t subscription_mask;
    pa_ext_volume_api_subscribe_cb_t subscribe_callback;
    void *subscribe_callback_userdata;
};

static struct userdata *get_userdata(pa_context *context, bool create);
static void userdata_free(struct userdata *u);
static void set_state(struct userdata *u, pa_ext_volume_api_state_t state, bool notify);

int pa_ext_volume_api_balance_valid(double balance) {
    return balance >= 0.0 && balance <= 1.0;
}

int pa_ext_volume_api_bvolume_valid(const pa_ext_volume_api_bvolume *volume, int check_volume, int check_balance) {
    unsigned channel;

    pa_assert(volume);

    if (check_volume && !PA_VOLUME_IS_VALID(volume->volume))
        return 0;

    if (!check_balance)
        return 1;

    if (!pa_channel_map_valid(&volume->channel_map))
        return 0;

    for (channel = 0; channel < volume->channel_map.channels; channel++) {
        if (!pa_ext_volume_api_balance_valid(volume->balance[channel]))
            return 0;
    }

    return 1;
}

void pa_ext_volume_api_bvolume_init_invalid(pa_ext_volume_api_bvolume *volume) {
    unsigned i;

    pa_assert(volume);

    volume->volume = PA_VOLUME_INVALID;

    for (i = 0; i < PA_CHANNELS_MAX; i++)
        volume->balance[i] = -1.0;

    pa_channel_map_init(&volume->channel_map);
}

void pa_ext_volume_api_bvolume_init(pa_ext_volume_api_bvolume *bvolume, pa_volume_t volume, pa_channel_map *map) {
    unsigned i;

    pa_assert(bvolume);
    pa_assert(PA_VOLUME_IS_VALID(volume));
    pa_assert(map);
    pa_assert(pa_channel_map_valid(map));

    bvolume->volume = volume;
    bvolume->channel_map = *map;

    for (i = 0; i < map->channels; i++)
        bvolume->balance[i] = 1.0;
}

void pa_ext_volume_api_bvolume_init_mono(pa_ext_volume_api_bvolume *bvolume, pa_volume_t volume) {
    pa_assert(bvolume);
    pa_assert(PA_VOLUME_IS_VALID(volume));

    bvolume->volume = volume;
    bvolume->balance[0] = 1.0;
    pa_channel_map_init_mono(&bvolume->channel_map);
}

int pa_ext_volume_api_bvolume_parse_balance(const char *str, pa_ext_volume_api_bvolume *_r) {
    pa_ext_volume_api_bvolume bvolume;

    pa_assert(str);
    pa_assert(_r);

    bvolume.channel_map.channels = 0;

    for (;;) {
        const char *colon;
        size_t channel_name_len;
        char *channel_name;
        pa_channel_position_t position;
        const char *space;
        size_t balance_str_len;
        char *balance_str;
        int r;
        double balance;

        colon = strchr(str, ':');
        if (!colon)
            return -PA_ERR_INVALID;

        channel_name_len = colon - str;
        channel_name = pa_xstrndup(str, channel_name_len);

        position = pa_channel_position_from_string(channel_name);
        pa_xfree(channel_name);
        if (position == PA_CHANNEL_POSITION_INVALID)
            return -PA_ERR_INVALID;

        bvolume.channel_map.map[bvolume.channel_map.channels] = position;
        str = colon + 1;

        space = strchr(str, ' ');
        if (space)
            balance_str_len = space - str;
        else
            balance_str_len = strlen(str);

        balance_str = pa_xstrndup(str, balance_str_len);

        r = pa_atod(balance_str, &balance);
        if (r < 0)
            return -PA_ERR_INVALID;

        if (!pa_ext_volume_api_balance_valid(balance))
            return -PA_ERR_INVALID;

        bvolume.balance[bvolume.channel_map.channels++] = balance;

        if (space)
            str = space + 1;
        else
            break;
    }

    pa_ext_volume_api_bvolume_copy_balance(_r, &bvolume);
    return 0;
}

int pa_ext_volume_api_bvolume_equal(const pa_ext_volume_api_bvolume *a, const pa_ext_volume_api_bvolume *b,
                                    int check_volume, int check_balance) {
    unsigned i;

    pa_assert(a);
    pa_assert(b);

    if (check_volume && a->volume != b->volume)
        return 0;

    if (!check_balance)
        return 1;

    if (!pa_channel_map_equal(&a->channel_map, &b->channel_map))
        return 0;

    for (i = 0; i < a->channel_map.channels; i++) {
        if (fabs(a->balance[i] - b->balance[i]) > 0.00001)
            return 0;
    }

    return 1;
}

void pa_ext_volume_api_bvolume_from_cvolume(pa_ext_volume_api_bvolume *bvolume, const pa_cvolume *cvolume,
                                            const pa_channel_map *map) {
    unsigned i;

    pa_assert(bvolume);
    pa_assert(cvolume);
    pa_assert(map);
    pa_assert(cvolume->channels == map->channels);

    bvolume->volume = pa_cvolume_max(cvolume);
    bvolume->channel_map = *map;

    for (i = 0; i < map->channels; i++) {
        if (bvolume->volume != PA_VOLUME_MUTED)
            bvolume->balance[i] = ((double) cvolume->values[i]) / ((double) bvolume->volume);
        else
            bvolume->balance[i] = 1.0;
    }
}

void pa_ext_volume_api_bvolume_to_cvolume(const pa_ext_volume_api_bvolume *bvolume, pa_cvolume *cvolume) {
    unsigned i;

    pa_assert(bvolume);
    pa_assert(cvolume);
    pa_assert(pa_ext_volume_api_bvolume_valid(bvolume, true, true));

    cvolume->channels = bvolume->channel_map.channels;

    for (i = 0; i < bvolume->channel_map.channels; i++)
        cvolume->values[i] = bvolume->volume * bvolume->balance[i];
}

void pa_ext_volume_api_bvolume_copy_balance(pa_ext_volume_api_bvolume *to,
                                            const pa_ext_volume_api_bvolume *from) {
    pa_assert(to);
    pa_assert(from);

    memcpy(to->balance, from->balance, sizeof(from->balance));
    to->channel_map = from->channel_map;
}

void pa_ext_volume_api_bvolume_reset_balance(pa_ext_volume_api_bvolume *volume, const pa_channel_map *map) {
    unsigned i;

    pa_assert(volume);
    pa_assert(map);
    pa_assert(pa_channel_map_valid(map));

    for (i = 0; i < map->channels; i++)
        volume->balance[i] = 1.0;

    volume->channel_map = *map;
}

void pa_ext_volume_api_bvolume_remap(pa_ext_volume_api_bvolume *volume, const pa_channel_map *to) {
    unsigned i;
    pa_cvolume cvolume;

    pa_assert(volume);
    pa_assert(to);
    pa_assert(pa_ext_volume_api_bvolume_valid(volume, false, true));
    pa_assert(pa_channel_map_valid(to));

    cvolume.channels = volume->channel_map.channels;

    for (i = 0; i < cvolume.channels; i++)
        cvolume.values[i] = volume->balance[i] * (double) PA_VOLUME_NORM;

    pa_cvolume_remap(&cvolume, &volume->channel_map, to);

    for (i = 0; i < to->channels; i++)
        volume->balance[i] = (double) cvolume.values[i] / (double) PA_VOLUME_NORM;

    volume->channel_map = *to;
}

double pa_ext_volume_api_bvolume_get_left_right_balance(const pa_ext_volume_api_bvolume *volume) {
    pa_ext_volume_api_bvolume bvolume;
    pa_cvolume cvolume;
    double ret;

    pa_assert(volume);

    bvolume.volume = PA_VOLUME_NORM;
    pa_ext_volume_api_bvolume_copy_balance(&bvolume, volume);
    pa_ext_volume_api_bvolume_to_cvolume(&bvolume, &cvolume);
    ret = pa_cvolume_get_balance(&cvolume, &volume->channel_map);

    return ret;
}

void pa_ext_volume_api_bvolume_set_left_right_balance(pa_ext_volume_api_bvolume *volume, double balance) {
    pa_cvolume cvolume;
    pa_volume_t old_volume;

    pa_assert(volume);

    if (!pa_channel_map_can_balance(&volume->channel_map))
        return;

    pa_cvolume_reset(&cvolume, volume->channel_map.channels);
    pa_cvolume_set_balance(&cvolume, &volume->channel_map, balance);
    old_volume = volume->volume;
    pa_ext_volume_api_bvolume_from_cvolume(volume, &cvolume, &volume->channel_map);
    volume->volume = old_volume;
}

double pa_ext_volume_api_bvolume_get_rear_front_balance(const pa_ext_volume_api_bvolume *volume) {
    pa_ext_volume_api_bvolume bvolume;
    pa_cvolume cvolume;
    double ret;

    pa_assert(volume);

    bvolume.volume = PA_VOLUME_NORM;
    pa_ext_volume_api_bvolume_copy_balance(&bvolume, volume);
    pa_ext_volume_api_bvolume_to_cvolume(&bvolume, &cvolume);
    ret = pa_cvolume_get_fade(&cvolume, &volume->channel_map);

    return ret;
}

void pa_ext_volume_api_bvolume_set_rear_front_balance(pa_ext_volume_api_bvolume *volume, double balance) {
    pa_cvolume cvolume;
    pa_volume_t old_volume;

    pa_assert(volume);

    if (!pa_channel_map_can_fade(&volume->channel_map))
        return;

    pa_cvolume_reset(&cvolume, volume->channel_map.channels);
    pa_cvolume_set_fade(&cvolume, &volume->channel_map, balance);
    old_volume = volume->volume;
    pa_ext_volume_api_bvolume_from_cvolume(volume, &cvolume, &volume->channel_map);
    volume->volume = old_volume;
}

int pa_ext_volume_api_bvolume_balance_to_string(const pa_ext_volume_api_bvolume *volume, char **_r) {
    pa_strbuf *buf;
    unsigned i;

    pa_assert(volume);
    pa_assert(_r);

    if (!pa_ext_volume_api_bvolume_valid(volume, false, true))
        return -PA_ERR_INVALID;

    buf = pa_strbuf_new();

    for (i = 0; i < volume->channel_map.channels; i++) {
        if (i != 0)
            pa_strbuf_putc(buf, ' ');

        pa_strbuf_printf(buf, "%s:%.2f", pa_channel_position_to_string(volume->channel_map.map[i]), volume->balance[i]);
    }

    *_r = pa_strbuf_tostring_free(buf);
    return 0;
}

char *pa_ext_volume_api_bvolume_snprint_balance(char *buf, size_t buf_len,
                                                const pa_ext_volume_api_bvolume *volume) {
    char *e;
    unsigned channel;
    bool first = true;

    pa_assert(buf);
    pa_assert(buf_len > 0);
    pa_assert(volume);

    pa_init_i18n();

    if (!pa_ext_volume_api_bvolume_valid(volume, true, true)) {
        pa_snprintf(buf, buf_len, _("(invalid)"));
        return buf;
    }

    *(e = buf) = 0;

    for (channel = 0; channel < volume->channel_map.channels && buf_len > 1; channel++) {
        buf_len -= pa_snprintf(e, buf_len, "%s%s: %u%%",
                               first ? "" : ", ",
                               pa_channel_position_to_string(volume->channel_map.map[channel]),
                               (unsigned) (volume->balance[channel] * 100 + 0.5));

        e = strchr(e, 0);
        first = false;
    }

    return buf;
}

static void extension_context_state_changed_cb(pa_extension *extension, unsigned phase) {
    struct userdata *u;
    pa_context_state_t context_state;
    pa_ext_volume_api_state_t api_state;

    pa_assert(extension);
    pa_assert(phase == 1 || phase == 2);

    u = get_userdata(extension->context, false);
    pa_assert(u);

    api_state = u->state;

    if (phase == 2) {
        if (u->state_notification_needed && u->state_callback)
            u->state_callback(u->context, u->state_callback_userdata);

        u->state_notification_needed = false;
        return;
    }

    context_state = pa_context_get_state(u->context);

    switch (context_state) {
        case PA_CONTEXT_UNCONNECTED:
        case PA_CONTEXT_CONNECTING:
        case PA_CONTEXT_AUTHORIZING:
        case PA_CONTEXT_SETTING_NAME:
        case PA_CONTEXT_READY:
            /* The volume api connection can only be initiated after the
             * context state becomes READY. */
            pa_assert(u->state == PA_EXT_VOLUME_API_STATE_UNCONNECTED);
            return;

        case PA_CONTEXT_FAILED:
            api_state = PA_EXT_VOLUME_API_STATE_FAILED;
            break;

        case PA_CONTEXT_TERMINATED:
            api_state = PA_EXT_VOLUME_API_STATE_TERMINATED;
            break;
    }

    if (api_state != u->state) {
        set_state(u, api_state, false);
        u->state_notification_needed = true;
    }
}

static void extension_kill_cb(pa_extension *extension) {
    pa_assert(extension);

    userdata_free(extension->userdata);
}

static void command_disconnect(struct userdata *u, pa_tagstruct *tagstruct) {
    pa_assert(u);
    pa_assert(tagstruct);

    if (!pa_tagstruct_eof(tagstruct)) {
        pa_log("Failed to parse the parameters of a DISCONNECT command.");
        pa_context_fail(u->context, PA_ERR_PROTOCOL);
        return;
    }

    if (u->state == PA_EXT_VOLUME_API_STATE_UNCONNECTED
            || u->state == PA_EXT_VOLUME_API_STATE_TERMINATED)
        return;

    /* We set the error to NOEXTENSION, because the assumption is that we only
     * receive a DISCONNECT command when the extension module is unloaded. */
    pa_context_set_error(u->context, PA_ERR_NOEXTENSION);
    set_state(u, PA_EXT_VOLUME_API_STATE_FAILED, true);
}

static void command_subscribe_event(struct userdata *u, pa_tagstruct *tagstruct) {
    pa_ext_volume_api_subscription_event_type_t event_type;
    pa_ext_volume_api_subscription_event_type_t facility;
    uint32_t idx;

    pa_assert(u);
    pa_assert(tagstruct);

    if (pa_tagstruct_getu32(tagstruct, &event_type) < 0
            || pa_tagstruct_getu32(tagstruct, &idx) < 0
            || !pa_tagstruct_eof(tagstruct)) {
        pa_log("Failed to parse the parameters of a SUBSCRIBE_EVENT command.");
        pa_context_fail(u->context, PA_ERR_PROTOCOL);
        return;
    }

    facility = event_type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;

    if (u->subscription_mask & (1 << facility)) {
        if (u->subscribe_callback)
            u->subscribe_callback(u->context, event_type, idx, u->subscribe_callback_userdata);
    }
}

static void extension_process_command_cb(pa_extension *extension, uint32_t command, uint32_t tag, pa_tagstruct *tagstruct) {
    struct userdata *u;

    pa_assert(extension);
    pa_assert(tagstruct);

    u = extension->userdata;

    if (u->state != PA_EXT_VOLUME_API_STATE_READY) {
        pa_pstream_send_error(extension->context->pstream, tag, PA_ERR_BADSTATE);
        return;
    }

    switch (command) {
        case PA_VOLUME_API_COMMAND_DISCONNECT:
            command_disconnect(u, tagstruct);
            break;

        case PA_VOLUME_API_COMMAND_SUBSCRIBE_EVENT:
            command_subscribe_event(u, tagstruct);
            break;

        default:
            pa_log("Received unrecognized command for the volume API extension: %u", command);
            pa_context_fail(u->context, PA_ERR_PROTOCOL);
            break;
    }
}

static struct userdata *userdata_new(pa_context *context) {
    struct userdata *u = NULL;

    pa_assert(context);

    u = pa_xnew0(struct userdata, 1);
    u->extension = pa_extension_new(context, PA_VOLUME_API_EXTENSION_NAME);
    u->extension->context_state_changed = extension_context_state_changed_cb;
    u->extension->kill = extension_kill_cb;
    u->extension->process_command = extension_process_command_cb;
    u->extension->userdata = u;
    u->context = context;
    u->state = PA_EXT_VOLUME_API_STATE_UNCONNECTED;

    pa_extension_put(u->extension);

    return u;
}

static void userdata_free(struct userdata *u) {
    pa_assert(u);

    if (u->extension)
        pa_extension_free(u->extension);

    pa_xfree(u);
}

static struct userdata *get_userdata(pa_context *context, bool create) {
    pa_extension *extension;

    pa_assert(context);

    extension = pa_context_get_extension(context, PA_VOLUME_API_EXTENSION_NAME);

    if (extension) {
        pa_assert(extension->userdata);
        return extension->userdata;
    }

    if (!create)
        return NULL;

    return userdata_new(context);
}

static void set_state(struct userdata *u, pa_ext_volume_api_state_t state, bool notify) {
    pa_assert(u);

    if (state == u->state)
        return;

    u->state = state;

    if (notify && u->state_callback)
        u->state_callback(u->context, u->state_callback_userdata);
}

static void connect_cb(pa_pdispatch *pdispatch, uint32_t command, uint32_t tag, pa_tagstruct *tagstruct, void *userdata) {
    struct userdata *u = userdata;
    uint32_t version;

    pa_assert(u);

    if (command != PA_COMMAND_REPLY) {
        pa_context_handle_error(u->context, command, tagstruct, false);
        set_state(u, PA_EXT_VOLUME_API_STATE_FAILED, true);
        return;
    }

    pa_assert(tagstruct);
    pa_assert(u->state == PA_EXT_VOLUME_API_STATE_CONNECTING);

    if (pa_tagstruct_getu32(tagstruct, &version) < 0
            || version < 1)
        goto fail_parse;

    set_state(u, PA_EXT_VOLUME_API_STATE_READY, true);

    return;

fail_parse:
    pa_log("Failed to parse the reply parameters of a CONNECT command.");
    pa_context_fail(u->context, PA_ERR_PROTOCOL);
}

int pa_ext_volume_api_connect(pa_context *context) {
    struct userdata *u;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);

    PA_CHECK_VALIDITY_RETURN_ANY(context, context->state == PA_CONTEXT_READY, PA_ERR_BADSTATE, -1);
    PA_CHECK_VALIDITY_RETURN_ANY(context, context->version >= 14, PA_ERR_NOTSUPPORTED, -1);

    u = get_userdata(context, true);

    PA_CHECK_VALIDITY_RETURN_ANY(context, u->state == PA_EXT_VOLUME_API_STATE_UNCONNECTED
                                          || u->state == PA_EXT_VOLUME_API_STATE_TERMINATED, PA_ERR_BADSTATE, -1);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_CONNECT);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_VERSION);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, connect_cb, u, NULL);

    set_state(u, PA_EXT_VOLUME_API_STATE_CONNECTING, true);

    return 0;
}

void pa_ext_volume_api_disconnect(pa_context *context) {
    struct userdata *u;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);

    u = get_userdata(context, false);
    if (!u)
        return;

    if (u->state == PA_EXT_VOLUME_API_STATE_UNCONNECTED
            || u->state == PA_EXT_VOLUME_API_STATE_FAILED
            || u->state == PA_EXT_VOLUME_API_STATE_TERMINATED)
        return;

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_DISCONNECT);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);

    set_state(u, PA_EXT_VOLUME_API_STATE_TERMINATED, true);
}

void pa_ext_volume_api_set_state_callback(pa_context *context, pa_ext_volume_api_state_cb_t cb, void *userdata) {
    struct userdata *u;

    pa_assert(context);

    u = get_userdata(context, true);
    u->state_callback = cb;
    u->state_callback_userdata = userdata;
}

pa_ext_volume_api_state_t pa_ext_volume_api_get_state(pa_context *context) {
    struct userdata *u;

    pa_assert(context);

    u = get_userdata(context, false);
    if (!u)
        return PA_EXT_VOLUME_API_STATE_UNCONNECTED;

    return u->state;
}

pa_operation *pa_ext_volume_api_subscribe(pa_context *context, pa_ext_volume_api_subscription_mask_t mask,
                                          pa_context_success_cb_t cb, void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_SUBSCRIBE);
    pa_tagstruct_putu32(tagstruct, mask);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback,
                                pa_operation_ref(operation), (pa_free_cb_t) pa_operation_unref);

    u->subscription_mask = mask;

    return operation;
}

void pa_ext_volume_api_set_subscribe_callback(pa_context *context, pa_ext_volume_api_subscribe_cb_t cb,
                                              void *userdata) {
    struct userdata *u;

    pa_assert(context);

    u = get_userdata(context, true);
    u->subscribe_callback = cb;
    u->subscribe_callback_userdata = userdata;
}

static void get_server_info_cb(pa_pdispatch *pdispatch, uint32_t command, uint32_t tag, pa_tagstruct *tagstruct,
                               void *userdata) {
    pa_operation *operation = userdata;
    pa_ext_volume_api_server_info info, *p = &info;

    pa_assert(pdispatch);
    pa_assert(operation);

    if (!operation->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(operation->context, command, tagstruct, false) < 0)
            goto finish;

        p = NULL;
    } else {
        pa_assert(tagstruct);

        if (pa_tagstruct_getu32(tagstruct, &info.main_output_volume_control) < 0
                || pa_tagstruct_getu32(tagstruct, &info.main_input_volume_control) < 0
                || pa_tagstruct_getu32(tagstruct, &info.main_output_mute_control) < 0
                || pa_tagstruct_getu32(tagstruct, &info.main_input_mute_control) < 0
                || !pa_tagstruct_eof(tagstruct))
            goto fail_parse;
    }

    if (operation->callback) {
        pa_ext_volume_api_server_info_cb_t cb = (pa_ext_volume_api_server_info_cb_t) operation->callback;
        cb(operation->context, p, operation->userdata);
    }

finish:
    pa_operation_done(operation);
    pa_operation_unref(operation);
    return;

fail_parse:
    pa_log("Failed to parse the reply parameters of a GET_SERVER_INFO command.");
    pa_context_fail(operation->context, PA_ERR_PROTOCOL);
    goto finish;
}

pa_operation *pa_ext_volume_api_get_server_info(pa_context *context, pa_ext_volume_api_server_info_cb_t cb,
                                                void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);
    pa_assert(cb);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_GET_SERVER_INFO);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, get_server_info_cb, pa_operation_ref(operation),
                                (pa_free_cb_t) pa_operation_unref);

    return operation;
}

static void volume_control_info_free(pa_ext_volume_api_volume_control_info *info) {
    pa_assert(info);

    if (info->proplist)
        pa_proplist_free(info->proplist);

    /* Description and name don't need to be freed, because they should point
     * to memory owned by pa_tagstruct, so they'll be freed when the tagstruct
     * is freed. */
}

static void get_volume_control_info_cb(pa_pdispatch *pdispatch, uint32_t command, uint32_t tag, pa_tagstruct *tagstruct,
                                       void *userdata) {
    pa_operation *operation = userdata;
    int eol = 1;
    pa_ext_volume_api_volume_control_info info;

    pa_assert(pdispatch);
    pa_assert(operation);

    if (!operation->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(operation->context, command, tagstruct, false) < 0)
            goto finish;

        eol = -1;
    } else {
        pa_assert(tagstruct);

        while (!pa_tagstruct_eof(tagstruct)) {
            unsigned channel;
            bool convertible_to_dB;

            pa_zero(info);
            info.proplist = pa_proplist_new();

            if (pa_tagstruct_getu32(tagstruct, &info.index) < 0
                    || info.index == PA_INVALID_INDEX
                    || pa_tagstruct_gets(tagstruct, &info.name) < 0
                    || !info.name || !*info.name
                    || pa_tagstruct_gets(tagstruct, &info.description) < 0
                    || !info.description
                    || pa_tagstruct_get_proplist(tagstruct, info.proplist) < 0
                    || pa_tagstruct_get_volume(tagstruct, &info.volume.volume) < 0
                    || !PA_VOLUME_IS_VALID(info.volume.volume)
                    || pa_tagstruct_get_channel_map(tagstruct, &info.volume.channel_map) < 0
                    || !pa_channel_map_valid(&info.volume.channel_map))
                goto fail_parse;

            for (channel = 0; channel < info.volume.channel_map.channels; channel++) {
                uint64_t balance;

                if (pa_tagstruct_getu64(tagstruct, &balance) < 0)
                    goto fail_parse;

                memcpy(&info.volume.balance[channel], &balance, sizeof(double));

                if (!pa_ext_volume_api_balance_valid(info.volume.balance[channel]))
                    goto fail_parse;
            }

            if (pa_tagstruct_get_boolean(tagstruct, &convertible_to_dB) < 0)
                goto fail_parse;

            info.convertible_to_dB = convertible_to_dB;

            if (operation->callback) {
                pa_ext_volume_api_volume_control_info_cb_t cb =
                        (pa_ext_volume_api_volume_control_info_cb_t) operation->callback;
                cb(operation->context, &info, 0, operation->userdata);
            }

            volume_control_info_free(&info);
        }
    }

    if (operation->callback) {
        pa_ext_volume_api_volume_control_info_cb_t cb =
                (pa_ext_volume_api_volume_control_info_cb_t) operation->callback;
        cb(operation->context, NULL, eol, operation->userdata);
    }

finish:
    pa_operation_done(operation);
    pa_operation_unref(operation);
    return;

fail_parse:
    pa_log("Failed to parse the reply parameters of a GET_VOLUME_CONTROL_INFO(_LIST) command.");
    pa_context_fail(operation->context, PA_ERR_PROTOCOL);
    volume_control_info_free(&info);
    goto finish;
}

pa_operation *pa_ext_volume_api_get_volume_control_info_by_index(pa_context *context, uint32_t idx,
                                                                 pa_ext_volume_api_volume_control_info_cb_t cb,
                                                                 void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);
    pa_assert(cb);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(context, idx != PA_INVALID_INDEX, PA_ERR_INVALID);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_GET_VOLUME_CONTROL_INFO);
    pa_tagstruct_putu32(tagstruct, idx);
    pa_tagstruct_puts(tagstruct, NULL);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, get_volume_control_info_cb,
                                pa_operation_ref(operation), (pa_free_cb_t) pa_operation_unref);

    return operation;
}

pa_operation *pa_ext_volume_api_get_volume_control_info_by_name(pa_context *context, const char *name,
                                                                pa_ext_volume_api_volume_control_info_cb_t cb,
                                                                void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);
    pa_assert(cb);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(context, name && *name, PA_ERR_INVALID);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_GET_VOLUME_CONTROL_INFO);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, name);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, get_volume_control_info_cb,
                                pa_operation_ref(operation), (pa_free_cb_t) pa_operation_unref);

    return operation;
}

pa_operation *pa_ext_volume_api_get_volume_control_info_list(pa_context *context,
                                                             pa_ext_volume_api_volume_control_info_cb_t cb,
                                                             void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);
    pa_assert(cb);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_GET_VOLUME_CONTROL_INFO_LIST);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, get_volume_control_info_cb,
                                pa_operation_ref(operation), (pa_free_cb_t) pa_operation_unref);

    return operation;
}

pa_operation *pa_ext_volume_api_set_volume_control_volume_by_index(pa_context *context, uint32_t idx,
                                                                   pa_ext_volume_api_bvolume *volume,
                                                                   int set_volume, int set_balance,
                                                                   pa_context_success_cb_t cb, void *userdata) {
    struct userdata *u;
    pa_volume_t v;
    pa_channel_map channel_map;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;
    unsigned i;

    pa_assert(context);
    pa_assert(volume);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(context, idx != PA_INVALID_INDEX, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(context, set_volume || set_balance, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(context, pa_ext_volume_api_bvolume_valid(volume, set_volume, set_balance),
                                  PA_ERR_INVALID);

    v = volume->volume;

    if (!set_volume)
        v = PA_VOLUME_INVALID;

    channel_map = volume->channel_map;

    if (!set_balance)
        pa_channel_map_init(&channel_map);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_SET_VOLUME_CONTROL_VOLUME);
    pa_tagstruct_putu32(tagstruct, idx);
    pa_tagstruct_puts(tagstruct, NULL);
    pa_tagstruct_put_volume(tagstruct, v);
    pa_tagstruct_put_channel_map(tagstruct, &channel_map);

    for (i = 0; i < channel_map.channels; i++) {
        uint64_t balance;

        memcpy(&balance, &volume->balance[i], sizeof(uint64_t));
        pa_tagstruct_putu64(tagstruct, balance);
    }

    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback,
                                pa_operation_ref(operation), (pa_free_cb_t) pa_operation_unref);

    return operation;
}

pa_operation *pa_ext_volume_api_set_volume_control_volume_by_name(pa_context *context, const char *name,
                                                                  pa_ext_volume_api_bvolume *volume,
                                                                  int set_volume, int set_balance,
                                                                  pa_context_success_cb_t cb, void *userdata) {
    struct userdata *u;
    pa_volume_t v;
    pa_channel_map channel_map;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;
    unsigned i;

    pa_assert(context);
    pa_assert(volume);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(context, name && *name, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(context, set_volume || set_balance, PA_ERR_INVALID);
    PA_CHECK_VALIDITY_RETURN_NULL(context, pa_ext_volume_api_bvolume_valid(volume, set_volume, set_balance),
                                  PA_ERR_INVALID);

    v = volume->volume;

    if (!set_volume)
        v = PA_VOLUME_INVALID;

    channel_map = volume->channel_map;

    if (!set_balance)
        pa_channel_map_init(&channel_map);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_SET_VOLUME_CONTROL_VOLUME);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, name);
    pa_tagstruct_put_volume(tagstruct, v);
    pa_tagstruct_put_channel_map(tagstruct, &channel_map);

    for (i = 0; i < channel_map.channels; i++) {
        uint64_t balance;

        memcpy(&balance, &volume->balance[i], sizeof(uint64_t));
        pa_tagstruct_putu64(tagstruct, balance);
    }

    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback,
                                pa_operation_ref(operation), (pa_free_cb_t) pa_operation_unref);

    return operation;
}

static void mute_control_info_free(pa_ext_volume_api_mute_control_info *info) {
    pa_assert(info);

    if (info->proplist)
        pa_proplist_free(info->proplist);

    /* Description and name don't need to be freed, because they should point
     * to memory owned by pa_tagstruct, so they'll be freed when the tagstruct
     * is freed. */
}

static void get_mute_control_info_cb(pa_pdispatch *pdispatch, uint32_t command, uint32_t tag, pa_tagstruct *tagstruct,
                                     void *userdata) {
    pa_operation *operation = userdata;
    int eol = 1;
    pa_ext_volume_api_mute_control_info info;

    pa_assert(pdispatch);
    pa_assert(operation);

    if (!operation->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(operation->context, command, tagstruct, false) < 0)
            goto finish;

        eol = -1;
    } else {
        pa_assert(tagstruct);

        while (!pa_tagstruct_eof(tagstruct)) {
            bool mute;

            pa_zero(info);
            info.proplist = pa_proplist_new();

            if (pa_tagstruct_getu32(tagstruct, &info.index) < 0
                    || info.index == PA_INVALID_INDEX
                    || pa_tagstruct_gets(tagstruct, &info.name) < 0
                    || !info.name || !*info.name
                    || pa_tagstruct_gets(tagstruct, &info.description) < 0
                    || !info.description
                    || pa_tagstruct_get_proplist(tagstruct, info.proplist) < 0
                    || pa_tagstruct_get_boolean(tagstruct, &mute) < 0)
                goto fail_parse;

            info.mute = mute;

            if (operation->callback) {
                pa_ext_volume_api_mute_control_info_cb_t cb =
                        (pa_ext_volume_api_mute_control_info_cb_t) operation->callback;
                cb(operation->context, &info, 0, operation->userdata);
            }

            mute_control_info_free(&info);
        }
    }

    if (operation->callback) {
        pa_ext_volume_api_mute_control_info_cb_t cb =
                (pa_ext_volume_api_mute_control_info_cb_t) operation->callback;
        cb(operation->context, NULL, eol, operation->userdata);
    }

finish:
    pa_operation_done(operation);
    pa_operation_unref(operation);
    return;

fail_parse:
    pa_log("Failed to parse the reply parameters of a GET_MUTE_CONTROL_INFO(_LIST) command.");
    pa_context_fail(operation->context, PA_ERR_PROTOCOL);
    mute_control_info_free(&info);
    goto finish;
}

pa_operation *pa_ext_volume_api_get_mute_control_info_by_index(pa_context *context, uint32_t idx,
                                                               pa_ext_volume_api_mute_control_info_cb_t cb,
                                                               void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);
    pa_assert(cb);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(context, idx != PA_INVALID_INDEX, PA_ERR_INVALID);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_GET_MUTE_CONTROL_INFO);
    pa_tagstruct_putu32(tagstruct, idx);
    pa_tagstruct_puts(tagstruct, NULL);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, get_mute_control_info_cb,
                                pa_operation_ref(operation), (pa_free_cb_t) pa_operation_unref);

    return operation;
}

pa_operation *pa_ext_volume_api_get_mute_control_info_by_name(pa_context *context, const char *name,
                                                              pa_ext_volume_api_mute_control_info_cb_t cb,
                                                              void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);
    pa_assert(cb);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(context, name && *name, PA_ERR_INVALID);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_GET_MUTE_CONTROL_INFO);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, name);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, get_mute_control_info_cb,
                                pa_operation_ref(operation), (pa_free_cb_t) pa_operation_unref);

    return operation;
}

pa_operation *pa_ext_volume_api_get_mute_control_info_list(pa_context *context,
                                                           pa_ext_volume_api_mute_control_info_cb_t cb,
                                                           void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);
    pa_assert(cb);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_GET_MUTE_CONTROL_INFO_LIST);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, get_mute_control_info_cb,
                                pa_operation_ref(operation), (pa_free_cb_t) pa_operation_unref);

    return operation;
}

pa_operation *pa_ext_volume_api_set_mute_control_mute_by_index(pa_context *context, uint32_t idx, int mute,
                                                               pa_context_success_cb_t cb, void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(context, idx != PA_INVALID_INDEX, PA_ERR_INVALID);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_SET_MUTE_CONTROL_MUTE);
    pa_tagstruct_putu32(tagstruct, idx);
    pa_tagstruct_puts(tagstruct, NULL);
    pa_tagstruct_put_boolean(tagstruct, mute);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback,
                                pa_operation_ref(operation), (pa_free_cb_t) pa_operation_unref);

    return operation;
}

pa_operation *pa_ext_volume_api_set_mute_control_mute_by_name(pa_context *context, const char *name, int mute,
                                                              pa_context_success_cb_t cb, void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(context, name && *name, PA_ERR_INVALID);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_SET_MUTE_CONTROL_MUTE);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, name);
    pa_tagstruct_put_boolean(tagstruct, mute);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback,
                                pa_operation_ref(operation), (pa_free_cb_t) pa_operation_unref);

    return operation;
}

static void device_info_free(pa_ext_volume_api_device_info *info) {
    pa_assert(info);

    if (info->proplist)
        pa_proplist_free(info->proplist);

    /* The strings in device_types point to memory owned by pa_tagstruct, so we
     * only need to free the device_types array. */
    pa_xfree(info->device_types);

    /* Description and name don't need to be freed, because they should point
     * to memory owned by pa_tagstruct, so they'll be freed when the tagstruct
     * is freed. */
}

static void get_device_info_cb(pa_pdispatch *pdispatch, uint32_t command, uint32_t tag, pa_tagstruct *tagstruct,
                               void *userdata) {
    pa_operation *operation = userdata;
    int eol = 1;
    pa_ext_volume_api_device_info info;

    pa_assert(pdispatch);
    pa_assert(operation);

    if (!operation->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(operation->context, command, tagstruct, false) < 0)
            goto finish;

        eol = -1;
    } else {
        pa_assert(tagstruct);

        while (!pa_tagstruct_eof(tagstruct)) {
            uint8_t direction;
            unsigned i;

            pa_zero(info);
            info.proplist = pa_proplist_new();

            if (pa_tagstruct_getu32(tagstruct, &info.index) < 0
                    || info.index == PA_INVALID_INDEX
                    || pa_tagstruct_gets(tagstruct, &info.name) < 0
                    || !info.name || !*info.name
                    || pa_tagstruct_gets(tagstruct, &info.description) < 0
                    || !info.description
                    || pa_tagstruct_getu8(tagstruct, &direction) < 0
                    || !pa_direction_valid(direction)
                    || pa_tagstruct_getu32(tagstruct, &info.n_device_types) < 0
                    || info.n_device_types > 1000)
                goto fail_parse;

            info.direction = direction;

            if (info.n_device_types > 0)
                info.device_types = pa_xnew0(const char *, info.n_device_types);

            for (i = 0; i < info.n_device_types; i++) {
                if (pa_tagstruct_gets(tagstruct, &info.device_types[i]) < 0
                        || !info.device_types[i] || !*info.device_types[i])
                    goto fail_parse;
            }

            if (pa_tagstruct_get_proplist(tagstruct, info.proplist) < 0
                    || pa_tagstruct_getu32(tagstruct, &info.volume_control) < 0
                    || pa_tagstruct_getu32(tagstruct, &info.mute_control) < 0)
                goto fail_parse;

            if (operation->callback) {
                pa_ext_volume_api_device_info_cb_t cb = (pa_ext_volume_api_device_info_cb_t) operation->callback;
                cb(operation->context, &info, 0, operation->userdata);
            }

            device_info_free(&info);
        }
    }

    if (operation->callback) {
        pa_ext_volume_api_device_info_cb_t cb = (pa_ext_volume_api_device_info_cb_t) operation->callback;
        cb(operation->context, NULL, eol, operation->userdata);
    }

finish:
    pa_operation_done(operation);
    pa_operation_unref(operation);
    return;

fail_parse:
    pa_log("Failed to parse the reply parameters of a GET_DEVICE_INFO(_LIST) command.");
    pa_context_fail(operation->context, PA_ERR_PROTOCOL);
    device_info_free(&info);
    goto finish;
}

pa_operation *pa_ext_volume_api_get_device_info_by_index(pa_context *context, uint32_t idx,
                                                         pa_ext_volume_api_device_info_cb_t cb, void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);
    pa_assert(cb);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(context, idx != PA_INVALID_INDEX, PA_ERR_INVALID);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_GET_DEVICE_INFO);
    pa_tagstruct_putu32(tagstruct, idx);
    pa_tagstruct_puts(tagstruct, NULL);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, get_device_info_cb, pa_operation_ref(operation),
                                (pa_free_cb_t) pa_operation_unref);

    return operation;
}

pa_operation *pa_ext_volume_api_get_device_info_by_name(pa_context *context, const char *name,
                                                        pa_ext_volume_api_device_info_cb_t cb, void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);
    pa_assert(cb);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(context, name && *name, PA_ERR_INVALID);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_GET_DEVICE_INFO);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, name);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, get_device_info_cb, pa_operation_ref(operation),
                                (pa_free_cb_t) pa_operation_unref);

    return operation;
}

pa_operation *pa_ext_volume_api_get_device_info_list(pa_context *context, pa_ext_volume_api_device_info_cb_t cb,
                                                     void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);
    pa_assert(cb);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_GET_DEVICE_INFO_LIST);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, get_device_info_cb, pa_operation_ref(operation),
                                (pa_free_cb_t) pa_operation_unref);

    return operation;
}

static void stream_info_free(pa_ext_volume_api_stream_info *info) {
    pa_assert(info);

    if (info->proplist)
        pa_proplist_free(info->proplist);

    /* Description and name don't need to be freed, because they should point
     * to memory owned by pa_tagstruct, so they'll be freed when the tagstruct
     * is freed. */
}

static void get_stream_info_cb(pa_pdispatch *pdispatch, uint32_t command, uint32_t tag, pa_tagstruct *tagstruct,
                               void *userdata) {
    pa_operation *operation = userdata;
    int eol = 1;
    pa_ext_volume_api_stream_info info;

    pa_assert(pdispatch);
    pa_assert(operation);

    if (!operation->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(operation->context, command, tagstruct, false) < 0)
            goto finish;

        eol = -1;
    } else {
        pa_assert(tagstruct);

        while (!pa_tagstruct_eof(tagstruct)) {
            uint8_t direction;

            pa_zero(info);
            info.proplist = pa_proplist_new();

            if (pa_tagstruct_getu32(tagstruct, &info.index) < 0
                    || info.index == PA_INVALID_INDEX
                    || pa_tagstruct_gets(tagstruct, &info.name) < 0
                    || !info.name || !*info.name
                    || pa_tagstruct_gets(tagstruct, &info.description) < 0
                    || !info.description
                    || pa_tagstruct_getu8(tagstruct, &direction) < 0
                    || !pa_direction_valid(direction)
                    || pa_tagstruct_get_proplist(tagstruct, info.proplist) < 0
                    || pa_tagstruct_getu32(tagstruct, &info.volume_control) < 0
                    || pa_tagstruct_getu32(tagstruct, &info.mute_control) < 0)
                goto fail_parse;

            info.direction = direction;

            if (operation->callback) {
                pa_ext_volume_api_stream_info_cb_t cb = (pa_ext_volume_api_stream_info_cb_t) operation->callback;
                cb(operation->context, &info, 0, operation->userdata);
            }

            stream_info_free(&info);
        }
    }

    if (operation->callback) {
        pa_ext_volume_api_stream_info_cb_t cb = (pa_ext_volume_api_stream_info_cb_t) operation->callback;
        cb(operation->context, NULL, eol, operation->userdata);
    }

finish:
    pa_operation_done(operation);
    pa_operation_unref(operation);
    return;

fail_parse:
    pa_log("Failed to parse the reply parameters of a GET_STREAM_INFO(_LIST) command.");
    pa_context_fail(operation->context, PA_ERR_PROTOCOL);
    stream_info_free(&info);
    goto finish;
}

pa_operation *pa_ext_volume_api_get_stream_info_by_index(pa_context *context, uint32_t idx,
                                                         pa_ext_volume_api_stream_info_cb_t cb, void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);
    pa_assert(cb);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(context, idx != PA_INVALID_INDEX, PA_ERR_INVALID);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_GET_STREAM_INFO);
    pa_tagstruct_putu32(tagstruct, idx);
    pa_tagstruct_puts(tagstruct, NULL);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, get_stream_info_cb, pa_operation_ref(operation),
                                (pa_free_cb_t) pa_operation_unref);

    return operation;
}

pa_operation *pa_ext_volume_api_get_stream_info_by_name(pa_context *context, const char *name,
                                                        pa_ext_volume_api_stream_info_cb_t cb, void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);
    pa_assert(cb);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(context, name && *name, PA_ERR_INVALID);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_GET_STREAM_INFO);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, name);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, get_stream_info_cb, pa_operation_ref(operation),
                                (pa_free_cb_t) pa_operation_unref);

    return operation;
}

pa_operation *pa_ext_volume_api_get_stream_info_list(pa_context *context, pa_ext_volume_api_stream_info_cb_t cb,
                                                     void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);
    pa_assert(cb);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_GET_STREAM_INFO_LIST);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, get_stream_info_cb, pa_operation_ref(operation),
                                (pa_free_cb_t) pa_operation_unref);

    return operation;
}

static void audio_group_info_free(pa_ext_volume_api_audio_group_info *info) {
    pa_assert(info);

    if (info->proplist)
        pa_proplist_free(info->proplist);

    /* Description and name don't need to be freed, because they should point
     * to memory owned by pa_tagstruct, so they'll be freed when the tagstruct
     * is freed. */
}

static void get_audio_group_info_cb(pa_pdispatch *pdispatch, uint32_t command, uint32_t tag, pa_tagstruct *tagstruct,
                                    void *userdata) {
    pa_operation *operation = userdata;
    int eol = 1;
    pa_ext_volume_api_audio_group_info info;

    pa_assert(pdispatch);
    pa_assert(operation);

    if (!operation->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(operation->context, command, tagstruct, false) < 0)
            goto finish;

        eol = -1;
    } else {
        pa_assert(tagstruct);

        while (!pa_tagstruct_eof(tagstruct)) {
            pa_zero(info);
            info.proplist = pa_proplist_new();

            if (pa_tagstruct_getu32(tagstruct, &info.index) < 0
                    || info.index == PA_INVALID_INDEX
                    || pa_tagstruct_gets(tagstruct, &info.name) < 0
                    || !info.name || !*info.name
                    || pa_tagstruct_gets(tagstruct, &info.description) < 0
                    || !info.description
                    || pa_tagstruct_get_proplist(tagstruct, info.proplist) < 0
                    || pa_tagstruct_getu32(tagstruct, &info.volume_control) < 0
                    || pa_tagstruct_getu32(tagstruct, &info.mute_control) < 0)
                goto fail_parse;

            if (operation->callback) {
                pa_ext_volume_api_audio_group_info_cb_t cb =
                        (pa_ext_volume_api_audio_group_info_cb_t) operation->callback;
                cb(operation->context, &info, 0, operation->userdata);
            }

            audio_group_info_free(&info);
        }
    }

    if (operation->callback) {
        pa_ext_volume_api_audio_group_info_cb_t cb = (pa_ext_volume_api_audio_group_info_cb_t) operation->callback;
        cb(operation->context, NULL, eol, operation->userdata);
    }

finish:
    pa_operation_done(operation);
    pa_operation_unref(operation);
    return;

fail_parse:
    pa_log("Failed to parse the reply parameters of a GET_AUDIO_GROUP_INFO(_LIST) command.");
    pa_context_fail(operation->context, PA_ERR_PROTOCOL);
    audio_group_info_free(&info);
    goto finish;
}

pa_operation *pa_ext_volume_api_get_audio_group_info_by_index(pa_context *context, uint32_t idx,
                                                              pa_ext_volume_api_audio_group_info_cb_t cb,
                                                              void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);
    pa_assert(cb);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(context, idx != PA_INVALID_INDEX, PA_ERR_INVALID);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_GET_AUDIO_GROUP_INFO);
    pa_tagstruct_putu32(tagstruct, idx);
    pa_tagstruct_puts(tagstruct, NULL);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, get_audio_group_info_cb,
                                pa_operation_ref(operation), (pa_free_cb_t) pa_operation_unref);

    return operation;
}

pa_operation *pa_ext_volume_api_get_audio_group_info_by_name(pa_context *context, const char *name,
                                                             pa_ext_volume_api_audio_group_info_cb_t cb,
                                                             void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);
    pa_assert(cb);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(context, name && *name, PA_ERR_INVALID);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_GET_AUDIO_GROUP_INFO);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, name);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, get_audio_group_info_cb,
                                pa_operation_ref(operation), (pa_free_cb_t) pa_operation_unref);

    return operation;
}

pa_operation *pa_ext_volume_api_get_audio_group_info_list(pa_context *context,
                                                          pa_ext_volume_api_audio_group_info_cb_t cb,
                                                          void *userdata) {
    struct userdata *u;
    pa_operation *operation;
    pa_tagstruct *tagstruct;
    uint32_t tag;

    pa_assert(context);
    pa_assert(cb);

    u = get_userdata(context, false);
    PA_CHECK_VALIDITY_RETURN_NULL(context, u && u->state == PA_EXT_VOLUME_API_STATE_READY, PA_ERR_BADSTATE);

    operation = pa_operation_new(context, NULL, (pa_operation_cb_t) cb, userdata);

    tagstruct = pa_tagstruct_command(context, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(tagstruct, PA_INVALID_INDEX);
    pa_tagstruct_puts(tagstruct, PA_VOLUME_API_EXTENSION_NAME);
    pa_tagstruct_putu32(tagstruct, PA_VOLUME_API_COMMAND_GET_AUDIO_GROUP_INFO_LIST);
    pa_pstream_send_tagstruct(context->pstream, tagstruct);
    pa_pdispatch_register_reply(context->pdispatch, tag, DEFAULT_TIMEOUT, get_audio_group_info_cb,
                                pa_operation_ref(operation), (pa_free_cb_t) pa_operation_unref);

    return operation;
}
