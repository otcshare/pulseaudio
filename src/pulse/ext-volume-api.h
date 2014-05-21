#ifndef fooextvolumeapihfoo
#define fooextvolumeapihfoo

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

#include <pulse/cdecl.h>
#include <pulse/context.h>
#include <pulse/volume.h>

/* This API is temporary, and has no stability guarantees whatsoever. Think
 * twice before making anything that relies on this API. This is undocumented
 * for a reason. */

PA_C_DECL_BEGIN

typedef struct pa_ext_volume_api_bvolume pa_ext_volume_api_bvolume;
typedef struct pa_ext_volume_api_server_info pa_ext_volume_api_server_info;
typedef struct pa_ext_volume_api_volume_control_info pa_ext_volume_api_volume_control_info;
typedef struct pa_ext_volume_api_mute_control_info pa_ext_volume_api_mute_control_info;
typedef struct pa_ext_volume_api_device_info pa_ext_volume_api_device_info;
typedef struct pa_ext_volume_api_stream_info pa_ext_volume_api_stream_info;
typedef struct pa_ext_volume_api_audio_group_info pa_ext_volume_api_audio_group_info;

struct pa_ext_volume_api_bvolume {
    pa_volume_t volume;
    double balance[PA_CHANNELS_MAX];
    pa_channel_map channel_map;
};

int pa_ext_volume_api_balance_valid(double balance) PA_GCC_CONST;
int pa_ext_volume_api_bvolume_valid(const pa_ext_volume_api_bvolume *volume, int check_volume, int check_balance)
        PA_GCC_PURE;
void pa_ext_volume_api_bvolume_init_invalid(pa_ext_volume_api_bvolume *volume);
void pa_ext_volume_api_bvolume_init(pa_ext_volume_api_bvolume *bvolume, pa_volume_t volume, pa_channel_map *map);
void pa_ext_volume_api_bvolume_init_mono(pa_ext_volume_api_bvolume *bvolume, pa_volume_t volume);
int pa_ext_volume_api_bvolume_parse_balance(const char *str, pa_ext_volume_api_bvolume *bvolume);
int pa_ext_volume_api_bvolume_equal(const pa_ext_volume_api_bvolume *a, const pa_ext_volume_api_bvolume *b,
                                    int check_volume, int check_balance) PA_GCC_PURE;
void pa_ext_volume_api_bvolume_from_cvolume(pa_ext_volume_api_bvolume *bvolume, const pa_cvolume *cvolume,
                                            const pa_channel_map *map);
void pa_ext_volume_api_bvolume_to_cvolume(const pa_ext_volume_api_bvolume *bvolume, pa_cvolume *cvolume);
void pa_ext_volume_api_bvolume_copy_balance(pa_ext_volume_api_bvolume *to,
                                            const pa_ext_volume_api_bvolume *from);
void pa_ext_volume_api_bvolume_reset_balance(pa_ext_volume_api_bvolume *volume, const pa_channel_map *map);
void pa_ext_volume_api_bvolume_remap(pa_ext_volume_api_bvolume *volume, const pa_channel_map *to);
double pa_ext_volume_api_bvolume_get_left_right_balance(const pa_ext_volume_api_bvolume *volume) PA_GCC_PURE;
void pa_ext_volume_api_bvolume_set_left_right_balance(pa_ext_volume_api_bvolume *volume, double balance);
double pa_ext_volume_api_bvolume_get_rear_front_balance(const pa_ext_volume_api_bvolume *volume) PA_GCC_PURE;
void pa_ext_volume_api_bvolume_set_rear_front_balance(pa_ext_volume_api_bvolume *volume, double balance);
int pa_ext_volume_api_bvolume_balance_to_string(const pa_ext_volume_api_bvolume *volume, char **_r);

#define PA_EXT_VOLUME_API_BVOLUME_SNPRINT_BALANCE_MAX 500
char *pa_ext_volume_api_bvolume_snprint_balance(char *buf, size_t buf_size,
                                                const pa_ext_volume_api_bvolume *volume);

typedef enum pa_ext_volume_api_state {
    PA_EXT_VOLUME_API_STATE_UNCONNECTED,
    PA_EXT_VOLUME_API_STATE_CONNECTING,
    PA_EXT_VOLUME_API_STATE_READY,
    PA_EXT_VOLUME_API_STATE_FAILED,
    PA_EXT_VOLUME_API_STATE_TERMINATED
} pa_ext_volume_api_state_t;

int pa_ext_volume_api_connect(pa_context *context);
void pa_ext_volume_api_disconnect(pa_context *context);

typedef void (*pa_ext_volume_api_state_cb_t)(pa_context *context, void *userdata);
void pa_ext_volume_api_set_state_callback(pa_context *context, pa_ext_volume_api_state_cb_t cb, void *userdata);
pa_ext_volume_api_state_t pa_ext_volume_api_get_state(pa_context *context);

typedef enum pa_ext_volume_api_subscription_mask {
    PA_EXT_VOLUME_API_SUBSCRIPTION_MASK_NULL = 0x0U,
    PA_EXT_VOLUME_API_SUBSCRIPTION_MASK_SERVER = 0x1U,
    PA_EXT_VOLUME_API_SUBSCRIPTION_MASK_VOLUME_CONTROL = 0x2U,
    PA_EXT_VOLUME_API_SUBSCRIPTION_MASK_MUTE_CONTROL = 0x4U,
    PA_EXT_VOLUME_API_SUBSCRIPTION_MASK_DEVICE = 0x8U,
    PA_EXT_VOLUME_API_SUBSCRIPTION_MASK_STREAM = 0x10U,
    PA_EXT_VOLUME_API_SUBSCRIPTION_MASK_AUDIO_GROUP = 0x20U,
    PA_EXT_VOLUME_API_SUBSCRIPTION_MASK_ALL = 0x3FU,
} pa_ext_volume_api_subscription_mask_t;

pa_operation *pa_ext_volume_api_subscribe(pa_context *context, pa_ext_volume_api_subscription_mask_t mask,
                                          pa_context_success_cb_t cb, void *userdata);

typedef enum pa_ext_volume_api_subscription_event_type {
    PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_SERVER = 0x0U,
    PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_VOLUME_CONTROL = 0x1U,
    PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_MUTE_CONTROL = 0x2U,
    PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_DEVICE = 0x3U,
    PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_STREAM = 0x4U,
    PA_EXT_VOLUME_API_SUBSCRIPTION_EVENT_AUDIO_GROUP = 0x5U,
} pa_ext_volume_api_subscription_event_type_t;

typedef void (*pa_ext_volume_api_subscribe_cb_t)(pa_context *context,
                                                 pa_ext_volume_api_subscription_event_type_t event_type,
                                                 uint32_t idx, void *userdata);
void pa_ext_volume_api_set_subscribe_callback(pa_context *context, pa_ext_volume_api_subscribe_cb_t cb,
                                              void *userdata);

struct pa_ext_volume_api_server_info {
    uint32_t main_output_volume_control;
    uint32_t main_input_volume_control;
    uint32_t main_output_mute_control;
    uint32_t main_input_mute_control;
};

typedef void (*pa_ext_volume_api_server_info_cb_t)(pa_context *context, const pa_ext_volume_api_server_info *info,
                                                   void *userdata);
pa_operation *pa_ext_volume_api_get_server_info(pa_context *context, pa_ext_volume_api_server_info_cb_t cb,
                                                void *userdata);

struct pa_ext_volume_api_volume_control_info {
    uint32_t index;
    const char *name;
    const char *description;
    pa_proplist *proplist;
    pa_ext_volume_api_bvolume volume;
    int convertible_to_dB;
};

typedef void (*pa_ext_volume_api_volume_control_info_cb_t)(pa_context *context,
                                                           const pa_ext_volume_api_volume_control_info *info,
                                                           int eol, void *userdata);
pa_operation *pa_ext_volume_api_get_volume_control_info_by_index(pa_context *context, uint32_t idx,
                                                                 pa_ext_volume_api_volume_control_info_cb_t cb,
                                                                 void *userdata);
pa_operation *pa_ext_volume_api_get_volume_control_info_by_name(pa_context *context, const char *name,
                                                                pa_ext_volume_api_volume_control_info_cb_t cb,
                                                                void *userdata);
pa_operation *pa_ext_volume_api_get_volume_control_info_list(pa_context *context,
                                                             pa_ext_volume_api_volume_control_info_cb_t cb,
                                                             void *userdata);

pa_operation *pa_ext_volume_api_set_volume_control_volume_by_index(pa_context *context, uint32_t idx,
                                                                   pa_ext_volume_api_bvolume *volume,
                                                                   int set_volume, int set_balance,
                                                                   pa_context_success_cb_t cb, void *userdata);
pa_operation *pa_ext_volume_api_set_volume_control_volume_by_name(pa_context *context, const char *name,
                                                                  pa_ext_volume_api_bvolume *volume,
                                                                  int set_volume, int set_balance,
                                                                  pa_context_success_cb_t cb, void *userdata);

struct pa_ext_volume_api_mute_control_info {
    uint32_t index;
    const char *name;
    const char *description;
    pa_proplist *proplist;
    int mute;
};

typedef void (*pa_ext_volume_api_mute_control_info_cb_t)(pa_context *context,
                                                         const pa_ext_volume_api_mute_control_info *info, int eol,
                                                         void *userdata);
pa_operation *pa_ext_volume_api_get_mute_control_info_by_index(pa_context *context, uint32_t idx,
                                                               pa_ext_volume_api_mute_control_info_cb_t cb,
                                                               void *userdata);
pa_operation *pa_ext_volume_api_get_mute_control_info_by_name(pa_context *context, const char *name,
                                                              pa_ext_volume_api_mute_control_info_cb_t cb,
                                                              void *userdata);
pa_operation *pa_ext_volume_api_get_mute_control_info_list(pa_context *context,
                                                           pa_ext_volume_api_mute_control_info_cb_t cb,
                                                           void *userdata);

pa_operation *pa_ext_volume_api_set_mute_control_mute_by_index(pa_context *context, uint32_t idx, int mute,
                                                               pa_context_success_cb_t cb, void *userdata);
pa_operation *pa_ext_volume_api_set_mute_control_mute_by_name(pa_context *context, const char *name, int mute,
                                                              pa_context_success_cb_t cb, void *userdata);

struct pa_ext_volume_api_device_info {
    uint32_t index;
    const char *name;
    const char *description;
    pa_direction_t direction;
    const char **device_types;
    uint32_t n_device_types;
    pa_proplist *proplist;
    uint32_t volume_control;
    uint32_t mute_control;
};

typedef void (*pa_ext_volume_api_device_info_cb_t)(pa_context *context, const pa_ext_volume_api_device_info *info,
                                                   int eol, void *userdata);
pa_operation *pa_ext_volume_api_get_device_info_by_index(pa_context *context, uint32_t idx,
                                                         pa_ext_volume_api_device_info_cb_t cb, void *userdata);
pa_operation *pa_ext_volume_api_get_device_info_by_name(pa_context *context, const char *name,
                                                        pa_ext_volume_api_device_info_cb_t cb, void *userdata);
pa_operation *pa_ext_volume_api_get_device_info_list(pa_context *context, pa_ext_volume_api_device_info_cb_t cb,
                                                     void *userdata);

struct pa_ext_volume_api_stream_info {
    uint32_t index;
    const char *name;
    const char *description;
    pa_direction_t direction;
    pa_proplist *proplist;
    uint32_t volume_control;
    uint32_t mute_control;
};

typedef void (*pa_ext_volume_api_stream_info_cb_t)(pa_context *context, const pa_ext_volume_api_stream_info *info,
                                                   int eol, void *userdata);
pa_operation *pa_ext_volume_api_get_stream_info_by_index(pa_context *context, uint32_t idx,
                                                         pa_ext_volume_api_stream_info_cb_t cb, void *userdata);
pa_operation *pa_ext_volume_api_get_stream_info_by_name(pa_context *context, const char *name,
                                                        pa_ext_volume_api_stream_info_cb_t cb, void *userdata);
pa_operation *pa_ext_volume_api_get_stream_info_list(pa_context *context, pa_ext_volume_api_stream_info_cb_t cb,
                                                     void *userdata);

struct pa_ext_volume_api_audio_group_info {
    uint32_t index;
    const char *name;
    const char *description;
    pa_proplist *proplist;
    uint32_t volume_control;
    uint32_t mute_control;
};

typedef void (*pa_ext_volume_api_audio_group_info_cb_t)(pa_context *context,
                                                        const pa_ext_volume_api_audio_group_info *info, int eol,
                                                        void *userdata);
pa_operation *pa_ext_volume_api_get_audio_group_info_by_index(pa_context *context, uint32_t idx,
                                                              pa_ext_volume_api_audio_group_info_cb_t cb,
                                                              void *userdata);
pa_operation *pa_ext_volume_api_get_audio_group_info_by_name(pa_context *context, const char *name,
                                                             pa_ext_volume_api_audio_group_info_cb_t cb,
                                                             void *userdata);
pa_operation *pa_ext_volume_api_get_audio_group_info_list(pa_context *context,
                                                          pa_ext_volume_api_audio_group_info_cb_t cb,
                                                          void *userdata);

PA_C_DECL_END

#endif
