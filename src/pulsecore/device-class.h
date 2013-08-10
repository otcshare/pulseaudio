#ifndef fooformfactorhfoo
#define fooformfactorhfoo

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

/* One source of device class definitions is the Bluetooth specification:
 * https://www.bluetooth.org/en-us/specification/assigned-numbers-overview/baseband
 *
 * The Bluetooth specification divides device classes to major and minor
 * classes. We don't list every possible Bluetooth minor device class here.
 * Instead, the "computer" and "phone" major classes in Bluetooth are mapped to
 * single "computer" and "phone" classes here. Almost all of the minor classes
 * in the "audio/video" major class in Bluetooth have their own device class
 * here. All other Bluetooth major device classes are categorized as "unknown"
 * (they are not likely to have audio capabilities).
 *
 * Even though this list is heavily based on the Bluetooth specification, this
 * is not intended to be Bluetooth specific in any way. New classes can be
 * freely added if something is missing. */
typedef enum {
    /* This can mean that we don't have enough information about the device
     * class, or we don't understand the information (e.g. udev can give
     * arbitrary strings as the form factor). */
    PA_DEVICE_CLASS_UNKNOWN,

    PA_DEVICE_CLASS_COMPUTER,
    PA_DEVICE_CLASS_PHONE,
    PA_DEVICE_CLASS_HEADSET,
    PA_DEVICE_CLASS_HANDSFREE,
    PA_DEVICE_CLASS_MICROPHONE,
    PA_DEVICE_CLASS_SPEAKERS,
    PA_DEVICE_CLASS_HEADPHONES,
    PA_DEVICE_CLASS_PORTABLE,
    PA_DEVICE_CLASS_CAR,
    PA_DEVICE_CLASS_SETTOP_BOX,
    PA_DEVICE_CLASS_HIFI,
    PA_DEVICE_CLASS_VCR,
    PA_DEVICE_CLASS_VIDEO_CAMERA,
    PA_DEVICE_CLASS_CAMCORDER,
    PA_DEVICE_CLASS_VIDEO_DISPLAY_AND_SPEAKERS,
    PA_DEVICE_CLASS_VIDEO_CONFERENCING,
    PA_DEVICE_CLASS_GAMING_OR_TOY,
    PA_DEVICE_CLASS_RADIO_TUNER,
    PA_DEVICE_CLASS_TV_TUNER,
    PA_DEVICE_CLASS_MAX
} pa_device_class_t;

pa_device_class_t pa_device_class_from_string(const char *str);
const char *pa_device_class_to_string(pa_device_class_t device_class);

/* This function produces a string that is suitable to be used as the
 * PA_PROP_DEVICE_FORM_FACTOR property. Not all device classes are suitable,
 * because the documentation for the property defines a fixed list of possible
 * values, and that list doesn't contain all the device classes that we have
 * available. If the device class can't be converted to one of the listed form
 * factors, this function returns NULL.
 *
 * We possibly could change the documentation of the DEVICE_FORM_FACTOR
 * property, but that would be strictly speaking an ABI break. Also, it's quite
 * nice to have a device class enumeration that isn't exposed to clients,
 * because it allows us to easily tune the enumeration contents without
 * worrying about client compatibility, so I'm not eager to force the device
 * class enumeration to be the same thing as the form factor property, even if
 * they are pretty similar (also note that they may be similar, but definitely
 * not the same thing, because e.g. "tuner" is a valid device class, but not a
 * form factor). */
const char *pa_device_class_to_form_factor_string(pa_device_class_t device_class);

#endif
