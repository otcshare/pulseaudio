/***
 This file is part of PulseAudio.

 Copyright (c) 2012 Intel Corporation
 Janos Kovacs <jankovac503@gmail.com>

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

#ifndef foonodehfoo
#define foonodehfoo

#include <pulsecore/card.h>
#include <pulsecore/device-port.h>

#define PA_NODE_TYPE_MASK(w,b)      (((((uint32_t)1) << (w)) - 1) << (b))
#define PA_NODE_TYPE_VALUE(n,b)     (((uint32_t)n) << (b))


/* device bitfield widths */
#define PA_DEVICE_POSITION_WIDTH    4
#define PA_DEVICE_LINK_WIDTH        4
#define PA_DEVICE_CLASS_WIDTH       4

#define PA_DEVICE_POSITION_BASE     0
#define PA_DEVICE_LINK_BASE         (PA_DEVICE_POSITION_BASE + PA_DEVICE_POSITION_WIDTH)
#define PA_DEVICE_CLASS_BASE        (PA_DEVICE_LINK_BASE + PA_DEVICE_LINK_WIDTH)

#define PA_DEVICE_POSITION_VALUE(x) (PA_NODE_TYPE_VALUE(x, PA_DEVICE_POSITION_BASE) & PA_DEVICE_POSITION_MASK)
#define PA_DEVICE_LINK_VALUE(x)     (PA_NODE_TYPE_VALUE(x, PA_DEVICE_LINK_BASE) & PA_DEVICE_LINK_MASK)
#define PA_DEVICE_CLASS_VALUE(x)    (PA_NODE_TYPE_VALUE(x, PA_DEVICE_CLASS_BASE) & PA_DEVICE_CLASS_MASK)

#define PA_DEVICE_POSITION_MASK     PA_NODE_TYPE_MASK(PA_DEVICE_POSITION_WIDTH, PA_DEVICE_POSITION_BASE)
#define PA_DEVICE_LINK_MASK         PA_NODE_TYPE_MASK(PA_DEVICE_LINK_WIDTH, PA_DEVICE_LINK_BASE)
#define PA_DEVICE_CLASS_MASK        PA_NODE_TYPE_MASK(PA_DEVICE_CLASS_WIDTH, PA_DEVICE_CLASS_BASE)

/* positions */
#define PA_DEVICE_POSITION_ANY      PA_DEVICE_POSITION_VALUE(0)
#define PA_DEVICE_POSITION_FRONT    PA_DEVICE_POSITION_VALUE(1)
#define PA_DEVICE_POSITION_CENTER   PA_DEVICE_POSITION_VALUE(2)
#define PA_DEVICE_POSITION_LFE      PA_DEVICE_POSITION_VALUE(3)
#define PA_DEVICE_POSITION_SIDE     PA_DEVICE_POSITION_VALUE(4)
#define PA_DEVICE_POSITION_REAR     PA_DEVICE_POSITION_VALUE(5)

/* link types */
#define PA_DEVICE_LINK_INTERNAL     PA_DEVICE_LINK_VALUE(0)
#define PA_DEVICE_LINK_JACK         PA_DEVICE_LINK_VALUE(1)
#define PA_DEVICE_LINK_HDMI         PA_DEVICE_LINK_VALUE(2)
#define PA_DEVICE_LINK_SPDIF        PA_DEVICE_LINK_VALUE(3)
#define PA_DEVICE_LINK_A2DP         PA_DEVICE_LINK_VALUE(4)
#define PA_DEVICE_LINK_SCO          PA_DEVICE_LINK_VALUE(5)
#define PA_DEVICE_LINK_USB          PA_DEVICE_LINK_VALUE(6)

/* device classes */
#define PA_DEVICE_CLASS_UNKNOWN     PA_DEVICE_CLASS_VALUE(0)
#define PA_DEVICE_CLASS_NULL        PA_DEVICE_CLASS_VALUE(1)
#define PA_DEVICE_CLASS_SPEAKERS    PA_DEVICE_CLASS_VALUE(2)
#define PA_DEVICE_CLASS_MICROPHONE  PA_DEVICE_CLASS_VALUE(3)
#define PA_DEVICE_CLASS_LINK        PA_DEVICE_CLASS_VALUE(4)
#define PA_DEVICE_CLASS_HEADPHONE   PA_DEVICE_CLASS_VALUE(5)
#define PA_DEVICE_CLASS_HEADSET     PA_DEVICE_CLASS_VALUE(6)
#define PA_DEVICE_CLASS_HANDSET     PA_DEVICE_CLASS_VALUE(7)
#define PA_DEVICE_CLASS_CAMERA      PA_DEVICE_CLASS_VALUE(8)
#define PA_DEVICE_CLASS_TUNER       PA_DEVICE_CLASS_VALUE(9)

/* predefined device types */
#define PA_NODE_TYPE_UNKNOWN        0

#define PA_DEVICE_NULL              (PA_DEVICE_CLASS_NULL | PA_DEVICE_LINK_INTERNAL | PA_DEVICE_POSITION_ANY)

#define PA_SPEAKERS                 (PA_DEVICE_CLASS_SPEAKERS | PA_DEVICE_LINK_INTERNAL | PA_DEVICE_POSITION_ANY)
#define PA_FRONT_SPEAKERS           (PA_DEVICE_CLASS_SPEAKERS | PA_DEVICE_LINK_INTERNAL | PA_DEVICE_POSITION_FRONT)
#define PA_REAR_SPEAKERS            (PA_DEVICE_CLASS_SPEAKERS | PA_DEVICE_LINK_INTERNAL | PA_DEVICE_POSITION_REAR)
#define PA_SIDE_SPEAKERS            (PA_DEVICE_CLASS_SPEAKERS | PA_DEVICE_LINK_INTERNAL | PA_DEVICE_POSITION_SIDE)
#define PA_CENTER_SPEAKER           (PA_DEVICE_CLASS_SPEAKERS | PA_DEVICE_LINK_INTERNAL | PA_DEVICE_POSITION_CENTER)
#define PA_LFE_SPEAKER              (PA_DEVICE_CLASS_SPEAKERS | PA_DEVICE_LINK_INTERNAL | PA_DEVICE_POSITION_LFE)

#define PA_MICROPHONE               (PA_DEVICE_CLASS_MICROPHONE | PA_DEVICE_LINK_INTERNAL | PA_DEVICE_POSITION_ANY)
#define PA_FRONT_MICROPHONE         (PA_DEVICE_CLASS_MICROPHONE | PA_DEVICE_LINK_INTERNAL | PA_DEVICE_POSITION_FRONT)
#define PA_REAR_MICROPHONE          (PA_DEVICE_CLASS_MICROPHONE | PA_DEVICE_LINK_INTERNAL | PA_DEVICE_POSITION_REAR)

#define PA_JACK_LINK                (PA_DEVICE_CLASS_LINK | PA_DEVICE_LINK_JACK  | PA_DEVICE_POSITION_ANY)
#define PA_SPDIF_LINK               (PA_DEVICE_CLASS_LINK | PA_DEVICE_LINK_SPDIF | PA_DEVICE_POSITION_ANY)
#define PA_HDMI_LINK                (PA_DEVICE_CLASS_LINK | PA_DEVICE_LINK_HDMI  | PA_DEVICE_POSITION_ANY)
#define PA_BLUETOOTH_LINK           (PA_DEVICE_CLASS_LINK | PA_DEVICE_LINK_A2DP  | PA_DEVICE_POSITION_ANY)

#define PA_WIRED_HEADSET            (PA_DEVICE_CLASS_HEADSET | PA_DEVICE_LINK_JACK | PA_DEVICE_POSITION_ANY)
#define PA_WIRED_HEADSET_FRONT      (PA_DEVICE_CLASS_HEADSET | PA_DEVICE_LINK_JACK | PA_DEVICE_POSITION_FRONT)
#define PA_WIRED_HEADSET_REAR       (PA_DEVICE_CLASS_HEADSET | PA_DEVICE_LINK_JACK | PA_DEVICE_POSITION_REAR)
#define PA_BLUETOOTH_STEREO_HEADSET (PA_DEVICE_CLASS_HEADSET | PA_DEVICE_LINK_A2DP | PA_DEVICE_POSITION_ANY)
#define PA_BLUETOOTH_MONO_HEADSET   (PA_DEVICE_CLASS_HEADSET | PA_DEVICE_LINK_SCO  | PA_DEVICE_POSITION_ANY)
#define PA_USB_HEADSET              (PA_DEVICE_CLASS_HEADSET | PA_DEVICE_LINK_USB  | PA_DEVICE_POSITION_ANY)

#define PA_WIRED_HEADPHONE          (PA_DEVICE_CLASS_HEADPHONE | PA_DEVICE_LINK_JACK | PA_DEVICE_POSITION_ANY)
#define PA_WIRED_HEADPHONE_FRONT    (PA_DEVICE_CLASS_HEADPHONE | PA_DEVICE_LINK_JACK | PA_DEVICE_POSITION_FRONT)
#define PA_WIRED_HEADPHONE_REAR     (PA_DEVICE_CLASS_HEADPHONE | PA_DEVICE_LINK_JACK | PA_DEVICE_POSITION_REAR)
#define PA_USB_HEADPHONE            (PA_DEVICE_CLASS_HEADPHONE | PA_DEVICE_LINK_USB  | PA_DEVICE_POSITION_ANY)

#define PA_EARPIECE                 (PA_DEVICE_CLASS_HEADPHONE | PA_DEVICE_LINK_INTERNAL | PA_DEVICE_POSITION_ANY)

#define PA_CAMERA                   (PA_DEVICE_CLASS_CAMERA | PA_DEVICE_LINK_INTERNAL | PA_DEVICE_POSITION_ANY)
#define PA_CAMERA_FRONT             (PA_DEVICE_CLASS_CAMERA | PA_DEVICE_LINK_INTERNAL | PA_DEVICE_POSITION_FRONT)
#define PA_CAMERA_REAR              (PA_DEVICE_CLASS_CAMERA | PA_DEVICE_LINK_INTERNAL | PA_DEVICE_POSITION_REAR)
#define PA_USB_CAMERA               (PA_DEVICE_CLASS_CAMERA | PA_DEVICE_LINK_USB | PA_DEVICE_POSITION_ANY)

#define PA_TUNER                    (PA_DEVICE_CLASS_TUNER | PA_DEVICE_LINK_INTERNAL | PA_DEVICE_POSITION_ANY)
#define PA_USB_TUNER                (PA_DEVICE_CLASS_TUNER | PA_DEVICE_LINK_USB | PA_DEVICE_POSITION_ANY)

#define PA_HANDSET_HANDSFREE        (PA_DEVICE_CLASS_HANDSET | PA_DEVICE_LINK_SCO | PA_DEVICE_POSITION_ANY)
#define PA_HANDSET_MEDIA            (PA_DEVICE_CLASS_HANDSET | PA_DEVICE_LINK_A2DP | PA_DEVICE_POSITION_ANY)

/* stream bitfield widths */
#define PA_STREAM_SUBTYPE_WIDTH     8
#define PA_STREAM_TYPE_WIDTH        4

#define PA_STREAM_SUBTYPE_BASE      0
#define PA_STREAM_TYPE_BASE         (PA_STREAM_SUBTYPE_BASE + PA_STREAM_SUBTYPE_WIDTH)

#define PA_STREAM_SUBTYPE_VALUE(x)  (PA_NODE_TYPE_VALUE(x, PA_STREAM_SUBTYPE_BASE) & PA_STREAM_SUBTYPE_MASK)
#define PA_STREAM_TYPE_VALUE(x)     (PA_NODE_TYPE_VALUE(x, PA_STREAM_TYPE_BASE) & PA_STREAM_TYPE_MASK)

#define PA_STREAM_SUBTYPE_MASK      PA_NODE_TYPE_MASK(PA_STREAM_SUBTYPE_WIDTH, PA_STREAM_SUBTYPE_BASE)
#define PA_STREAM_TYPE_MASK         PA_NODE_TYPE_MASK(PA_STREAM_TYPE_WIDTH, PA_STREAM_TYPE_BASE)

/* stream types */
#define PA_STREAM_PLAYER            PA_STREAM_TYPE_VALUE(0)
#define PA_STREAM_RECORDER          PA_STREAM_TYPE_VALUE(1)
#define PA_STREAM_NAVIGATOR         PA_STREAM_TYPE_VALUE(2)
#define PA_STREAM_GAME              PA_STREAM_TYPE_VALUE(3)
#define PA_STREAM_CAMERA            PA_STREAM_TYPE_VALUE(4)
#define PA_STREAM_PHONE             PA_STREAM_TYPE_VALUE(5)
#define PA_STREAM_SPEECH            PA_STREAM_TYPE_VALUE(6)
#define PA_STREAM_ALERT             PA_STREAM_TYPE_VALUE(7)
#define PA_STREAM_EVENT             PA_STREAM_TYPE_VALUE(8)
#define PA_STREAM_SYSTEM            PA_STREAM_TYPE_VALUE(9)

/* predefined stream types */
#define PA_RADIO                    (PA_PLAYER | PA_STREAM_SUBTYPE_VALUE(0))
#define PA_MEDIA_PLAYER             (PA_PLAYER | PA_STREAM_SUBTYPE_VALUE(1))
#define PA_BROWSER                  (PA_PLAYER | PA_STREAM_SUBTYPE_VALUE(2))

typedef enum {
    pa_node_direction_any = 0,
    pa_node_input,
    pa_node_output
} pa_node_direction;

typedef enum {
    pa_node_implementat_unknown = 0,
    pa_node_device,
    pa_node_stream
} pa_node_implement;

typedef enum {
    pa_node_location_unknown = 0,
    pa_node_internal,
    pa_node_external
} pa_node_location;


typedef enum {
    pa_node_privacy_unknown = 0,
    pa_node_public,
    pa_node_private
} pa_node_privacy;



typedef enum {
    pa_device_position_any = 0,
    pa_device_front,
    pa_device_rear,
    pa_device_side,
    pa_device_center,
} pa_device_position;



typedef struct pa_node_new_data {
    char *name;
    char *description;

    pa_proplist *proplist;

    pa_node_direction direction;
    pa_node_implement implement;
    pa_node_location location;
    pa_node_privacy privacy;
    uint32_t type;
    uint8_t channels;
    unsigned priority;

    pa_bool_t visible:1;
    pa_bool_t ignore:1;

    pa_card_profile *profile;
    pa_device_port *port;
} pa_node_new_data;

typedef struct pa_node {
    uint32_t index;
    pa_core *core;

    char *name;
    char *description;

    pa_proplist *proplist;

    pa_node_direction direction; /**< pa_node_input | pa_node_output */
    pa_node_implement implement; /**< pa_node_device | pa_node_stream */
    pa_node_location location;   /**< pa_node_internal | pa_node_external */
    pa_node_privacy privacy;     /**< pa_node_public | pa_node_private */
    uint32_t type;
    uint8_t channels;            /**< number of channels */
    unsigned priority;

    pa_bool_t visible:1;         /**< internal(eg. null-sink) or can appear on UI */
    pa_bool_t available:1;       /**< eg. is the port available (ALSA) or the profile active (BT) */
    pa_bool_t ignore:1;          /**< do not consider it while routing */

    pa_card_profile *profile;
    pa_device_port *port;
    union {
        struct pa_sink *sink;
        struct pa_source *source;
        struct pa_sink_input *sink_input;
        struct pa_source_output *source_output;
        void *ptr;
    } pulse_object;

    uint32_t stamp;             /**< marker to see when this node was last processed */

} pa_node;


pa_node_new_data *pa_node_new_data_init(pa_node_new_data *data);
void pa_node_new_data_set_name(pa_node_new_data *data, const char *name);
void pa_node_new_data_done(pa_node_new_data *data);

pa_node *pa_node_new(pa_core *core, pa_node_new_data *data);
void pa_node_unlink(pa_node *node);

void pa_node_availability_changed(pa_node *node, pa_bool_t available);

void pa_node_set_location(pa_node_new_data *data);
void pa_node_set_privacy(pa_node_new_data *data);

void pa_node_dump(pa_node *n, const char *event);

pa_bool_t pa_node_issource(pa_node *n);
pa_bool_t pa_node_issink(pa_node *n);

#endif
