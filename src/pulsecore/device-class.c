/***
  This file is part of PulseAudio.

  Copyright (c) 2013 Intel Corporation
  Author: Tanu Kaskinen <tanu.kaskinen@intel.com>

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

#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>

#include "device-class.h"

static const char * const string_table[PA_DEVICE_CLASS_MAX] = {
    [PA_DEVICE_CLASS_UNKNOWN] = "unknown",
    [PA_DEVICE_CLASS_COMPUTER] = "computer",
    [PA_DEVICE_CLASS_PHONE] = "phone",
    [PA_DEVICE_CLASS_HEADSET] = "headset",
    [PA_DEVICE_CLASS_HANDSFREE] = "handsfree",
    [PA_DEVICE_CLASS_MICROPHONE] = "microphone",
    [PA_DEVICE_CLASS_SPEAKERS] = "speakers",
    [PA_DEVICE_CLASS_HEADPHONES] = "headphones",
    [PA_DEVICE_CLASS_PORTABLE] = "portable",
    [PA_DEVICE_CLASS_CAR] = "car",
    [PA_DEVICE_CLASS_SETTOP_BOX] = "set-top-box",
    [PA_DEVICE_CLASS_HIFI] = "hifi",
    [PA_DEVICE_CLASS_VCR] = "vcr",
    [PA_DEVICE_CLASS_VIDEO_CAMERA] = "video-camera",
    [PA_DEVICE_CLASS_CAMCORDER] = "camcorder",
    [PA_DEVICE_CLASS_VIDEO_DISPLAY_AND_SPEAKERS] = "video-display-and-speakers",
    [PA_DEVICE_CLASS_VIDEO_CONFERENCING] = "video-conferencing",
    [PA_DEVICE_CLASS_GAMING_OR_TOY] = "gaming-or-toy",
    [PA_DEVICE_CLASS_RADIO_TUNER] = "radio-tuner",
    [PA_DEVICE_CLASS_TV_TUNER] = "tv-tuner"
};

static const char * const form_factor_table[PA_DEVICE_CLASS_MAX] = {
    [PA_DEVICE_CLASS_COMPUTER] = "computer",
    [PA_DEVICE_CLASS_PHONE] = "handset",
    [PA_DEVICE_CLASS_HEADSET] = "headset",
    [PA_DEVICE_CLASS_HANDSFREE] = "hands-free",
    [PA_DEVICE_CLASS_MICROPHONE] = "microphone",
    [PA_DEVICE_CLASS_SPEAKERS] = "speaker",
    [PA_DEVICE_CLASS_HEADPHONES] = "headphone",
    [PA_DEVICE_CLASS_PORTABLE] = "portable",
    [PA_DEVICE_CLASS_CAR] = "car",
    [PA_DEVICE_CLASS_HIFI] = "hifi",
    [PA_DEVICE_CLASS_VIDEO_CAMERA] = "webcam",
    [PA_DEVICE_CLASS_VIDEO_DISPLAY_AND_SPEAKERS] = "tv"
};

pa_device_class_t pa_device_class_from_string(const char *str) {
    unsigned i;

    pa_assert(str);

    for (i = 0; i < PA_DEVICE_CLASS_MAX; i++) {
        if (pa_streq(str, string_table[i]))
            return (pa_device_class_t) i;
    }

    return PA_DEVICE_CLASS_UNKNOWN;
}

const char *pa_device_class_to_string(pa_device_class_t class) {
    return string_table[class];
}

const char *pa_device_class_to_form_factor_string(pa_device_class_t class) {
    return form_factor_table[class];
}
