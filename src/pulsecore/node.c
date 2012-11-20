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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pulse/utf8.h>
#include <pulsecore/namereg.h>
#include <pulsecore/core-util.h>

#include "node.h"


pa_node_new_data *pa_node_new_data_init(pa_node_new_data *data) {

    pa_assert(data);

    pa_zero(*data);
    data->proplist = pa_proplist_new();

    return data;
}

void pa_node_new_data_set_name(pa_node_new_data *data, const char *name) {
    char *dup_name;

    pa_assert(data);

    dup_name = pa_xstrdup(name);
    pa_xfree(data->name);
    data->name = dup_name;
}

void pa_node_new_data_done(pa_node_new_data *data) {
    pa_assert(data);

    pa_proplist_free(data->proplist);

    pa_xfree(data->name);
}

static char *strnodetype(pa_node_implement implement, uint32_t type) {

    const char *class;
    const char *link;
    const char *position;
    const char *type_string;
    char buf[512];

    switch (implement) {
    default:                                 type_string = "<unknown>";                break;
    case pa_node_device:
        switch (type) {
        case PA_DEVICE_NULL:                 type_string = "<null-device>";            break;
        case PA_SPEAKERS:                    type_string = "speakers";                 break;
        case PA_FRONT_SPEAKERS:              type_string = "front-speakers";           break;
        case PA_REAR_SPEAKERS:               type_string = "rear-speakers";            break;
        case PA_SIDE_SPEAKERS:               type_string = "side-speakers";            break;
        case PA_CENTER_SPEAKER:              type_string = "center-speaker";           break;
        case PA_LFE_SPEAKER:                 type_string = "subwoofer";                break;
        case PA_MICROPHONE:                  type_string = "microphone";               break;
        case PA_FRONT_MICROPHONE:            type_string = "front-microphone";         break;
        case PA_REAR_MICROPHONE:             type_string = "rear-microphone";          break;
        case PA_JACK_LINK:                   type_string = "jack";                     break;
        case PA_SPDIF_LINK:                  type_string = "spdif";                    break;
        case PA_HDMI_LINK:                   type_string = "hdmi";                     break;
        case PA_BLUETOOTH_LINK:              type_string = "bluetooth-link";           break;
        case PA_WIRED_HEADSET:               type_string = "wired-headset";            break;
        case PA_WIRED_HEADSET_FRONT:         type_string = "wired-headset-front";      break;
        case PA_WIRED_HEADSET_REAR:          type_string = "wired-headset-rear";       break;
        case PA_BLUETOOTH_STEREO_HEADSET:    type_string = "bluetooth-stereo-headset"; break;
        case PA_BLUETOOTH_MONO_HEADSET:      type_string = "bluetooth-mono-headset";   break;
        case PA_USB_HEADSET:                 type_string = "usb-headset";              break;
        case PA_WIRED_HEADPHONE:             type_string = "wired-headphone";          break;
        case PA_WIRED_HEADPHONE_FRONT:       type_string = "wired-headphone-front";    break;
        case PA_WIRED_HEADPHONE_REAR:        type_string = "wired-headphone-rear";     break;
        case PA_USB_HEADPHONE:               type_string = "usb-headphone";            break;
        case PA_EARPIECE:                    type_string = "earpiece";                 break;
        case PA_CAMERA:                      type_string = "camera";                   break;
        case PA_CAMERA_FRONT:                type_string = "usb-camera-front";         break;
        case PA_CAMERA_REAR:                 type_string = "usb-camera-rear";          break;
        case PA_USB_CAMERA:                  type_string = "usb-camera";               break;
        case PA_TUNER:                       type_string = "tuner";                    break;
        case PA_USB_TUNER:                   type_string = "usb-tuner";                break;
        case PA_HANDSET_HANDSFREE:           type_string = "handset-handsfree";        break;
        case PA_HANDSET_MEDIA:               type_string = "handset-media";            break;
        default:                             type_string = buf;
            switch ((type & PA_DEVICE_POSITION_MASK)) {
            default:                         position = "";                            break;
            case PA_DEVICE_POSITION_FRONT:   position = "front-";                      break;
            case PA_DEVICE_POSITION_CENTER:  position = "center-";                     break;
            case PA_DEVICE_POSITION_LFE:     position = "lfe-";                        break;
            case PA_DEVICE_POSITION_SIDE:    position = "side-";                       break;
            case PA_DEVICE_POSITION_REAR:    position = "rear-";                       break;
            }
            switch ((type & PA_DEVICE_LINK_MASK)) {
            default:                         link = "";                                break;
            case PA_DEVICE_LINK_JACK:        link = "jack-";                           break;
            case PA_DEVICE_LINK_HDMI:        link = "hdmi-";                           break;
            case PA_DEVICE_LINK_SPDIF:       link = "spdif-";                          break;
            case PA_DEVICE_LINK_A2DP:        link = "bluetooth-stereo-";               break;
            case PA_DEVICE_LINK_SCO:         link = "bluetooth-mono-";                 break;
            case PA_DEVICE_LINK_USB:         link = "usb-";                            break;
            }
            switch ((type & PA_DEVICE_CLASS_MASK)) {
            default:                         class = "?";                              break;
            case PA_DEVICE_CLASS_NULL:       class = "null-device";                    break;
            case PA_DEVICE_CLASS_SPEAKERS:   class = "speaker";                        break;
            case PA_DEVICE_CLASS_MICROPHONE: class = "microphone";                     break;
            case PA_DEVICE_CLASS_LINK:       class = "link";                           break;
            case PA_DEVICE_CLASS_HEADPHONE:  class = "headphone";                      break;
            case PA_DEVICE_CLASS_HEADSET:    class = "headset";                        break;
            case PA_DEVICE_CLASS_HANDSET:    class = "handset";                        break;
            }
            snprintf(buf, sizeof(buf), "%s%s%s", position, link, class);
            break;
        }
        break;
    case pa_node_stream:
        switch (type & PA_STREAM_TYPE_MASK) {
        case PA_STREAM_PLAYER:               type_string = "player";                   break;
        case PA_STREAM_NAVIGATOR:            type_string = "navigator";                break;
        case PA_STREAM_GAME:                 type_string = "game";                     break;
        case PA_STREAM_CAMERA:               type_string = "camera";                   break;
        case PA_STREAM_PHONE:                type_string = "phone";                    break;
        case PA_STREAM_SPEECH:               type_string = "speech";                   break;
        case PA_STREAM_ALERT:                type_string = "alert";                    break;
        case PA_STREAM_EVENT:                type_string = "event";                    break;
        case PA_STREAM_SYSTEM:               type_string = "system";                   break;
        default:                             type_string = "<unknown-stream-type>";    break;
        }
        break;
    }

    return pa_xstrdup(type_string);
}

static char *compose_description(pa_node_new_data *data, char *original_description)
{
    char *description;

    pa_assert(data);
    pa_assert(original_description);

    if (data->description != original_description)
        description = pa_xstrdup(data->description);
    else
        description = pa_sprintf_malloc("%s (%s)", strnodetype(data->implement, data->type), data->description);

    return description;
}


pa_node *pa_node_new(pa_core *core, pa_node_new_data *data) {

    pa_node *n;
    const char *name;
    char *description;
    pa_node_direction direction;
    pa_node_implement implement;
    uint32_t channels;
    pa_card_profile *profile;
    pa_device_port *port;

    pa_assert(core);
    pa_assert(data);
    pa_assert(data->name);

    description = data->description;
    direction = data->direction;
    implement = data->implement;
    channels = data->channels;
    profile = data->profile;
    port = data->port;

    pa_assert(description);
    pa_assert(direction == pa_node_input || direction == pa_node_output);
    pa_assert(implement == pa_node_device || implement == pa_node_stream);
    pa_assert(channels > 0 && channels <= PA_CHANNELS_MAX);

    n = pa_xnew0(pa_node, 1);

    if (!(name = pa_namereg_register(core, data->name, PA_NAMEREG_NODE, n, FALSE))) {
        pa_log_debug("Failed to register name %s.", data->name);
        pa_xfree(n);
    }

    pa_node_new_data_set_name(data, name);

    if (pa_hook_fire(&core->hooks[PA_CORE_HOOK_NODE_NEW], data) < 0)
        goto failed;

    if (!data->name || !pa_utf8_valid(data->name) || !data->name[0])
        goto failed;

    /* these can't be changed by the hooks */
    pa_assert(direction == data->direction);
    pa_assert(implement == data->implement);
    pa_assert(channels == data->channels);
    pa_assert(profile == data->profile);
    pa_assert(port == data->port);

    /* sanity checks */
    pa_assert(data->location == pa_node_internal || data->location == pa_node_external);
    pa_assert(data->privacy == pa_node_public || data->privacy == pa_node_private);

    pa_assert_se(pa_idxset_put(core->nodes, n, &n->index) >= 0);

    n->core = core;
    n->name = pa_xstrdup(name);
    n->description = compose_description(data, description);
    n->proplist = pa_proplist_copy(data->proplist);
    n->direction = direction;
    n->implement = implement;
    n->location = data->location;
    n->privacy = data->privacy;
    n->type = data->type;
    n->channels = channels;
    n->priority = data->priority;
    n->visible = data->visible;
    n->ignore = data->ignore;
    n->profile = profile;
    n->port = port;

    if (port)
        pa_assert_se(pa_hashmap_put(port->nodes, n, n) == 0);

    return n;

 failed:
    pa_xfree(n);
    pa_namereg_unregister(core, name);
    return NULL;
}

static void unlink_pulse_object(pa_node *n) {

    pa_assert(n);
    pa_assert(n->implement == pa_node_device || n->implement == pa_node_stream);
    pa_assert(n->direction == pa_node_input || n->direction == pa_node_output);

    if (n->pulse_object.ptr != NULL) {

        if (n->implement == pa_node_device) {
            if (n->direction == pa_node_output) {
                pa_sink *s = n->pulse_object.sink;
            }
            else {
                pa_source *s = n->pulse_object.source;
            }
        }
        else {
            if (n->direction == pa_node_input) {
                pa_sink_input *s = n->pulse_object.sink_input;
            }
            else {
                pa_source_output *s = n->pulse_object.source_output;
            }
        }

        n->pulse_object.ptr = NULL;
    }
}

void pa_node_unlink(pa_node *n) {

    pa_device_port *p;

    pa_assert(n);
    pa_assert_ctl_context();

    pa_node_dump(n, "Delete");

    pa_hook_fire(&n->core->hooks[PA_CORE_HOOK_NODE_UNLINK], n);

    if ((p = n->port) != NULL)
        pa_assert_se(pa_hashmap_remove(p->nodes, n) == (void *)n);

    pa_namereg_unregister(n->core, n->name);
    pa_idxset_remove_by_data(n->core->nodes, n, NULL);

    unlink_pulse_object(n);
}

void pa_node_availability_changed(pa_node *node, pa_bool_t available) {
    pa_assert(node);

    if ((available && !node->available) || (!available && node->available)) {
        node->available = available;

        pa_node_dump(node, "Availability changed for");
    }
}

void pa_node_set_location(pa_node_new_data *data) {
    uint32_t link;

    pa_assert(data);

    if (!data->implement == pa_node_device)
        data->location = pa_node_internal;
    else {
        switch((data->type & PA_DEVICE_CLASS_MASK)) {
        case PA_DEVICE_CLASS_HEADPHONE:
        case PA_DEVICE_CLASS_HEADSET:
        case PA_DEVICE_CLASS_LINK:
            data->location = pa_node_external;
            break;
        default:
            switch ((data->type & PA_DEVICE_LINK_MASK)) {
            case PA_DEVICE_LINK_JACK:
            case PA_DEVICE_LINK_HDMI:
            case PA_DEVICE_LINK_SPDIF:
            case PA_DEVICE_LINK_A2DP:
            case PA_DEVICE_LINK_SCO:
                data->location = pa_node_external;
                break;
            default:
                data->location = pa_node_internal;
                break;
            }
            break;
        }
    }
}

void pa_node_set_privacy(pa_node_new_data *data) {
    pa_assert(data);

    if (data->implement == pa_node_device) {
        if (data->direction == pa_node_input)
            data->privacy = pa_node_public;
        else {
            switch((data->type & PA_DEVICE_CLASS_MASK)) {
            case PA_DEVICE_CLASS_SPEAKERS:
            case PA_DEVICE_CLASS_HANDSET:
            case PA_DEVICE_CLASS_LINK:
                data->privacy = pa_node_public;
                break;
            case PA_DEVICE_CLASS_HEADPHONE:
            case PA_DEVICE_CLASS_HEADSET:
                data->privacy = pa_node_private;
                break;
            default:
                data->privacy = pa_node_public;
                break;
            }
        }
    }
}


static const char *strimplement(pa_node_implement implement) {
    const char *implement_string = "?";

    switch (implement) {
    case pa_node_device:    implement_string = "device-node";    break;
    case pa_node_stream:    implement_string = "stream-node";    break;
    default:                pa_assert_not_reached();             break;
    }

    return implement_string;
}

static const char *strdirection(pa_node_direction direction) {
    const char *direction_string = "?";

    switch (direction) {
    case pa_node_input:     direction_string = "input";    break;
    case pa_node_output:    direction_string = "output";   break;
    default:                pa_assert_not_reached();       break;
    }

    return direction_string;
}

static const char *strlocation(pa_node_location location) {
    const char *location_string = "?";

    switch (location) {
    case pa_node_internal:  location_string = "internal";   break;
    case pa_node_external:  location_string = "external";   break;
    default:                pa_assert_not_reached();        break;
    }

    return location_string;
}


static char *stream_name(pa_proplist *pl)
{
    const char  *appnam;
    const char  *binnam;

    if (!pl)
        return NULL;

    if ((appnam = pa_proplist_gets(pl, PA_PROP_APPLICATION_NAME)))
        return (char *)appnam;

    if ((binnam = pa_proplist_gets(pl, PA_PROP_APPLICATION_PROCESS_BINARY)))
        return (char *)binnam;

    return NULL;
}

void pa_node_dump(pa_node *n, const char *event)
{
    const char *implement;
    const char *direction;
    const char *location;
    const char *objtyp;
    const char *objnam;
    char *type;

    pa_assert(n);
    pa_assert(event);

    implement = strimplement(n->implement);
    direction = strdirection(n->direction);
    location = strlocation(n->location);
    type = strnodetype(n->implement, n->type);

    if (n->implement == pa_node_device && n->direction == pa_node_input) {
        objtyp = "source";
        objnam = n->pulse_object.ptr ? n->pulse_object.source->name : NULL;
    } else if (n->implement == pa_node_device && n->direction == pa_node_output) {
        objtyp = "sink";
        objnam = n->pulse_object.ptr ? n->pulse_object.sink->name : NULL;
    } else if (n->implement == pa_node_stream && n->direction == pa_node_input) {
        objtyp = "sink-input";
        objnam = n->pulse_object.ptr ? stream_name(n->pulse_object.sink_input->proplist) : NULL;
    } else if (n ->implement == pa_node_stream && n->direction == pa_node_output) {
        objtyp = "source_output";
        objnam = n->pulse_object.ptr ? stream_name(n->pulse_object.source_output->proplist) : NULL;
    } else {
        objtyp = "<unknown>";
        objnam = NULL;
    }


    pa_log_debug("%s node %s:\n"
                 "   description  : '%s'\n"
                 "   implement    : %s\n"
                 "   direction    : %s\n"
                 "   location     : %s\n"
                 "   type         : %s\n"
                 "   channels     : %u\n"
                 "   priority     : %u\n"
                 "   visible      : %s\n"
                 "   available    : %s\n"
                 "   ignore       : %s\n"
                 "   profile      : '%s'\n"
                 "   port         : '%s'\n"
                 "   pulse_object : %s / '%s'",
                 event, n->name, pa_strnull(n->description), implement, direction, location, type,
                 n->channels, n->priority, pa_yes_no(n->visible), pa_yes_no(n->available),
                 pa_yes_no(n->ignore), n->profile ? n->profile->name : "<none>",
                 n->port ? n->port->name : "<none>", objtyp, pa_strnull(objnam));

    pa_xfree(type);
}


pa_bool_t pa_node_issource(pa_node *n) {
    return (n->implement == pa_node_device && n->direction == pa_node_input);
}

pa_bool_t pa_node_issink(pa_node *n) {
    return (n->implement == pa_node_device && n->direction == pa_node_output);
}
