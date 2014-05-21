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

#include "device-creator.h"

#include <modules/volume-api/device.h>
#include <modules/volume-api/mute-control.h>
#include <modules/volume-api/volume-control.h>

#include <pulsecore/core-util.h>
#include <pulsecore/i18n.h>

struct pa_device_creator {
    pa_volume_api *volume_api;
    pa_hashmap *devices; /* pa_device_port/pa_sink/pa_source -> struct device */
    pa_hook_slot *card_put_slot;
    pa_hook_slot *card_unlink_slot;
    pa_hook_slot *sink_put_slot;
    pa_hook_slot *sink_unlink_slot;
    pa_hook_slot *source_put_slot;
    pa_hook_slot *source_unlink_slot;
};

enum device_type {
    DEVICE_TYPE_PORT,
    DEVICE_TYPE_PORT_MONITOR,
    DEVICE_TYPE_SINK,
    DEVICE_TYPE_SOURCE,
};

struct device_volume_control {
    struct device *device;
    pa_volume_control *volume_control;

    bool unlinked;

    pa_hook_slot *volume_changed_slot;
};

static void device_volume_control_free(struct device_volume_control *control);

struct device_mute_control {
    struct device *device;
    pa_mute_control *mute_control;

    bool unlinked;

    pa_hook_slot *mute_changed_slot;
};

static void device_mute_control_free(struct device_mute_control *control);

struct device {
    pa_device_creator *creator;
    enum device_type type;
    pa_device_port *port;
    pa_sink *sink;
    pa_source *source;
    pa_device *device;
    struct device_volume_control *volume_control;
    struct device_mute_control *mute_control;

    bool unlinked;

    pa_hook_slot *proplist_changed_slot;
    pa_hook_slot *port_active_changed_slot;
    struct device *monitor;
};

static void device_free(struct device *device);

static const char *device_type_from_icon_name(const char *icon_name) {
    if (!icon_name)
        return NULL;

    if (pa_streq(icon_name, "audio-input-microphone"))
        return "microphone";

    if (pa_streq(icon_name, "audio-speakers"))
        return "speakers";

    if (pa_streq(icon_name, "audio-headphones"))
        return "headphones";

    return NULL;
}

static const char *device_type_from_port_name(pa_device_port *port) {
    pa_assert(port);

    if (strstr(port->name, "analog")) {
        if (port->direction == PA_DIRECTION_INPUT)
            return "analog-input";
        else
            return "analog-output";
    }

    if (strstr(port->name, "hdmi")) {
        if (port->direction == PA_DIRECTION_INPUT)
            return "hdmi-input";
        else
            return "hdmi-output";
    }

    if (strstr(port->name, "iec958")) {
        if (port->direction == PA_DIRECTION_INPUT)
            return "spdif-input";
        else
            return "spdif-output";
    }

    return NULL;
}

static const char *device_type_from_port(pa_device_port *port) {
    const char *device_type;

    pa_assert(port);

    device_type = device_type_from_icon_name(pa_proplist_gets(port->proplist, PA_PROP_DEVICE_ICON_NAME));
    if (device_type)
        return device_type;

    device_type = device_type_from_port_name(port);
    if (device_type)
        return device_type;

    return NULL;
}

static const char *get_sink_description(pa_sink *sink) {
    const char *description;

    pa_assert(sink);

    description = pa_proplist_gets(sink->proplist, PA_PROP_DEVICE_DESCRIPTION);
    if (description)
        return description;

    return sink->name;
}

static const char *get_source_description(pa_source *source) {
    const char *description;

    pa_assert(source);

    description = pa_proplist_gets(source->proplist, PA_PROP_DEVICE_DESCRIPTION);
    if (description)
        return description;

    return source->name;
}

static pa_hook_result_t sink_or_source_volume_changed_cb(void *hook_data, void *call_data, void *userdata) {
    struct device_volume_control *control = userdata;
    struct device *device;
    pa_sink *sink = NULL;
    pa_source *source = NULL;
    pa_bvolume bvolume;

    pa_assert(control);
    pa_assert(call_data);

    device = control->device;

    switch (device->type) {
        case DEVICE_TYPE_PORT:
            if (device->port->direction == PA_DIRECTION_OUTPUT)
                sink = call_data;
            else
                source = call_data;
            break;

        case DEVICE_TYPE_PORT_MONITOR:
        case DEVICE_TYPE_SOURCE:
            source = call_data;
            break;

        case DEVICE_TYPE_SINK:
            sink = call_data;
            break;
    }

    if ((sink && sink != device->sink) || (source && source != device->source))
        return PA_HOOK_OK;

    if (sink)
        pa_bvolume_from_cvolume(&bvolume, &sink->reference_volume, &sink->channel_map);
    else if (source)
        pa_bvolume_from_cvolume(&bvolume, &source->reference_volume, &source->channel_map);
    else
        pa_assert_not_reached();

    pa_volume_control_set_volume(control->volume_control, &bvolume, true, true);

    return PA_HOOK_OK;
}

static int volume_control_set_volume_cb(pa_volume_control *c, const pa_bvolume *original_volume,
                                        const pa_bvolume *remapped_volume, bool set_volume, bool set_balance) {
    struct device_volume_control *control;
    struct device *device;
    pa_bvolume bvolume;
    pa_cvolume cvolume;

    pa_assert(c);
    pa_assert(original_volume);
    pa_assert(remapped_volume);

    control = c->userdata;
    device = control->device;

    switch (device->type) {
        case DEVICE_TYPE_PORT:
            if (device->port->direction == PA_DIRECTION_OUTPUT)
                pa_bvolume_from_cvolume(&bvolume, &device->sink->reference_volume, &device->sink->channel_map);
            else
                pa_bvolume_from_cvolume(&bvolume, &device->source->reference_volume, &device->source->channel_map);
            break;

        case DEVICE_TYPE_SINK:
            pa_bvolume_from_cvolume(&bvolume, &device->sink->reference_volume, &device->sink->channel_map);
            break;

        case DEVICE_TYPE_PORT_MONITOR:
        case DEVICE_TYPE_SOURCE:
            pa_bvolume_from_cvolume(&bvolume, &device->source->reference_volume, &device->source->channel_map);
            break;
    }

    if (set_volume)
        bvolume.volume = remapped_volume->volume;

    if (set_balance)
        pa_bvolume_copy_balance(&bvolume, remapped_volume);

    pa_bvolume_to_cvolume(&bvolume, &cvolume);

    switch (device->type) {
        case DEVICE_TYPE_PORT:
            if (device->port->direction == PA_DIRECTION_OUTPUT)
                pa_sink_set_volume(device->sink, &cvolume, true, true);
            else
                pa_source_set_volume(device->source, &cvolume, true, true);
            break;

        case DEVICE_TYPE_PORT_MONITOR:
        case DEVICE_TYPE_SOURCE:
            pa_source_set_volume(device->source, &cvolume, true, true);
            break;

        case DEVICE_TYPE_SINK:
            pa_sink_set_volume(device->sink, &cvolume, true, true);
            break;
    }

    return 0;
}

static int device_volume_control_new(struct device *device, struct device_volume_control **_r) {
    struct device_volume_control *control = NULL;
    const char *name = NULL;
    pa_bvolume volume;
    bool convertible_to_dB = false;
    int r;

    pa_assert(device);
    pa_assert(_r);

    control = pa_xnew0(struct device_volume_control, 1);
    control->device = device;

    switch (device->type) {
        case DEVICE_TYPE_PORT:
            name = "port-volume-control";

            if (device->port->direction == PA_DIRECTION_OUTPUT) {
                control->volume_changed_slot = pa_hook_connect(&device->port->core->hooks[PA_CORE_HOOK_SINK_VOLUME_CHANGED],
                                                               PA_HOOK_NORMAL, sink_or_source_volume_changed_cb, control);
                pa_bvolume_from_cvolume(&volume, &device->sink->reference_volume, &device->sink->channel_map);
                convertible_to_dB = device->sink->flags & PA_SINK_DECIBEL_VOLUME;
            } else {
                control->volume_changed_slot = pa_hook_connect(&device->port->core->hooks[PA_CORE_HOOK_SOURCE_VOLUME_CHANGED],
                                                               PA_HOOK_NORMAL, sink_or_source_volume_changed_cb, control);
                pa_bvolume_from_cvolume(&volume, &device->source->reference_volume, &device->source->channel_map);
                convertible_to_dB = device->source->flags & PA_SOURCE_DECIBEL_VOLUME;
            }

            break;

        case DEVICE_TYPE_PORT_MONITOR:
            name = "port-monitor-volume-control";
            control->volume_changed_slot = pa_hook_connect(&device->source->core->hooks[PA_CORE_HOOK_SOURCE_VOLUME_CHANGED],
                                                           PA_HOOK_NORMAL, sink_or_source_volume_changed_cb, control);
            pa_bvolume_from_cvolume(&volume, &device->source->reference_volume, &device->source->channel_map);
            convertible_to_dB = device->source->flags & PA_SOURCE_DECIBEL_VOLUME;
            break;

        case DEVICE_TYPE_SINK:
            name = "sink-volume-control";
            control->volume_changed_slot = pa_hook_connect(&device->sink->core->hooks[PA_CORE_HOOK_SINK_VOLUME_CHANGED],
                                                           PA_HOOK_NORMAL, sink_or_source_volume_changed_cb, control);
            pa_bvolume_from_cvolume(&volume, &device->sink->reference_volume, &device->sink->channel_map);
            convertible_to_dB = device->sink->flags & PA_SINK_DECIBEL_VOLUME;
            break;

        case DEVICE_TYPE_SOURCE:
            name = "source-volume-control";
            control->volume_changed_slot = pa_hook_connect(&device->source->core->hooks[PA_CORE_HOOK_SOURCE_VOLUME_CHANGED],
                                                           PA_HOOK_NORMAL, sink_or_source_volume_changed_cb, control);
            pa_bvolume_from_cvolume(&volume, &device->source->reference_volume, &device->source->channel_map);
            convertible_to_dB = device->source->flags & PA_SOURCE_DECIBEL_VOLUME;
            break;
    }

    r = pa_volume_control_new(device->creator->volume_api, name, false, &control->volume_control);
    if (r < 0)
        goto fail;

    pa_volume_control_set_description(control->volume_control, device->device->description);
    pa_volume_control_set_channel_map(control->volume_control, &volume.channel_map);
    pa_volume_control_set_volume(control->volume_control, &volume, true, true);
    pa_volume_control_set_convertible_to_dB(control->volume_control, convertible_to_dB);
    control->volume_control->set_volume = volume_control_set_volume_cb;
    control->volume_control->userdata = control;

    *_r = control;
    return 0;

fail:
    if (control)
        device_volume_control_free(control);

    return r;
}

static void device_volume_control_put(struct device_volume_control *control) {
    pa_assert(control);

    pa_volume_control_put(control->volume_control);
}

static void device_volume_control_unlink(struct device_volume_control *control) {
    pa_assert(control);

    if (control->unlinked)
        return;

    control->unlinked = true;

    if (control->volume_control)
        pa_volume_control_unlink(control->volume_control);
}

static void device_volume_control_free(struct device_volume_control *control) {
    pa_assert(control);

    if (!control->unlinked)
        device_volume_control_unlink(control);

    if (control->volume_control)
        pa_volume_control_free(control->volume_control);

    if (control->volume_changed_slot)
        pa_hook_slot_free(control->volume_changed_slot);

    pa_xfree(control);
}

static pa_hook_result_t sink_or_source_mute_changed_cb(void *hook_data, void *call_data, void *userdata) {
    struct device_mute_control *control = userdata;
    struct device *device;
    pa_sink *sink = NULL;
    pa_source *source = NULL;
    bool mute;

    pa_assert(control);
    pa_assert(call_data);

    device = control->device;

    switch (device->type) {
        case DEVICE_TYPE_PORT:
            if (device->port->direction == PA_DIRECTION_OUTPUT)
                sink = call_data;
            else
                source = call_data;
            break;

        case DEVICE_TYPE_PORT_MONITOR:
        case DEVICE_TYPE_SOURCE:
            source = call_data;
            break;

        case DEVICE_TYPE_SINK:
            sink = call_data;
            break;
    }

    if ((sink && sink != device->sink) || (source && source != device->source))
        return PA_HOOK_OK;

    if (sink)
        mute = sink->muted;
    else if (source)
        mute = source->muted;
    else
        pa_assert_not_reached();

    pa_mute_control_set_mute(control->mute_control, mute);

    return PA_HOOK_OK;
}

static int mute_control_set_mute_cb(pa_mute_control *c, bool mute) {
    struct device_mute_control *control;
    struct device *device;

    pa_assert(c);

    control = c->userdata;
    device = control->device;

    switch (device->type) {
        case DEVICE_TYPE_PORT:
            if (device->port->direction == PA_DIRECTION_OUTPUT)
                pa_sink_set_mute(device->sink, mute, true);
            else
                pa_source_set_mute(device->source, mute, true);
            break;

        case DEVICE_TYPE_PORT_MONITOR:
        case DEVICE_TYPE_SOURCE:
            pa_source_set_mute(device->source, mute, true);
            break;

        case DEVICE_TYPE_SINK:
            pa_sink_set_mute(device->sink, mute, true);
            break;
    }

    return 0;
}

static int device_mute_control_new(struct device *device, struct device_mute_control **_r) {
    struct device_mute_control *control = NULL;
    const char *name = NULL;
    bool mute = false;
    int r;

    pa_assert(device);
    pa_assert(_r);

    control = pa_xnew0(struct device_mute_control, 1);
    control->device = device;

    switch (device->type) {
        case DEVICE_TYPE_PORT:
            name = "port-mute-control";

            if (device->port->direction == PA_DIRECTION_OUTPUT) {
                control->mute_changed_slot = pa_hook_connect(&device->port->core->hooks[PA_CORE_HOOK_SINK_MUTE_CHANGED],
                                                             PA_HOOK_NORMAL, sink_or_source_mute_changed_cb, control);
                mute = device->sink->muted;
            } else {
                control->mute_changed_slot = pa_hook_connect(&device->port->core->hooks[PA_CORE_HOOK_SOURCE_MUTE_CHANGED],
                                                             PA_HOOK_NORMAL, sink_or_source_mute_changed_cb, control);
                mute = device->source->muted;
            }
            break;

        case DEVICE_TYPE_PORT_MONITOR:
            name = "port-monitor-mute-control";
            control->mute_changed_slot = pa_hook_connect(&device->port->core->hooks[PA_CORE_HOOK_SOURCE_MUTE_CHANGED],
                                                         PA_HOOK_NORMAL, sink_or_source_mute_changed_cb, control);
            mute = device->source->muted;
            break;

        case DEVICE_TYPE_SINK:
            name = "sink-mute-control";
            control->mute_changed_slot = pa_hook_connect(&device->sink->core->hooks[PA_CORE_HOOK_SINK_MUTE_CHANGED],
                                                         PA_HOOK_NORMAL, sink_or_source_mute_changed_cb, control);
            mute = device->sink->muted;
            break;

        case DEVICE_TYPE_SOURCE:
            name = "source-mute-control";
            control->mute_changed_slot = pa_hook_connect(&device->source->core->hooks[PA_CORE_HOOK_SOURCE_MUTE_CHANGED],
                                                         PA_HOOK_NORMAL, sink_or_source_mute_changed_cb, control);
            mute = device->source->muted;
            break;
    }

    r = pa_mute_control_new(device->creator->volume_api, name, false, &control->mute_control);
    if (r < 0)
        goto fail;

    pa_mute_control_set_description(control->mute_control, device->device->description);
    pa_mute_control_set_mute(control->mute_control, mute);
    control->mute_control->set_mute = mute_control_set_mute_cb;
    control->mute_control->userdata = control;

    *_r = control;
    return 0;

fail:
    if (control)
        device_mute_control_free(control);

    return r;
}

static void device_mute_control_put(struct device_mute_control *control) {
    pa_assert(control);

    pa_mute_control_put(control->mute_control);
}

static void device_mute_control_unlink(struct device_mute_control *control) {
    pa_assert(control);

    if (control->unlinked)
        return;

    control->unlinked = true;

    if (control->mute_control)
        pa_mute_control_unlink(control->mute_control);
}

static void device_mute_control_free(struct device_mute_control *control) {
    pa_assert(control);

    if (!control->unlinked)
        device_mute_control_unlink(control);

    if (control->mute_control)
        pa_mute_control_free(control->mute_control);

    if (control->mute_changed_slot)
        pa_hook_slot_free(control->mute_changed_slot);

    pa_xfree(control);
}

static void device_set_sink_and_source_from_port(struct device *device) {
    pa_sink *sink;
    pa_source *source;
    uint32_t idx;

    pa_assert(device);

    device->sink = NULL;
    device->source = NULL;

    if (!device->port->active)
        return;

    switch (device->type) {
        case DEVICE_TYPE_PORT:
            if (device->port->direction == PA_DIRECTION_OUTPUT) {
                PA_IDXSET_FOREACH(sink, device->port->card->sinks, idx) {
                    if (sink->active_port == device->port) {
                        device->sink = sink;
                        break;
                    }
                }

                pa_assert(device->sink);
            } else {
                PA_IDXSET_FOREACH(source, device->port->card->sources, idx) {
                    if (source->active_port == device->port) {
                        device->source = source;
                        break;
                    }
                }

                pa_assert(device->source);
            }
            break;

        case DEVICE_TYPE_PORT_MONITOR: {
            PA_IDXSET_FOREACH(sink, device->port->card->sinks, idx) {
                if (sink->active_port == device->port) {
                    device->sink = sink;
                    device->source = sink->monitor_source;
                    break;
                }
            }

            pa_assert(device->sink);
            break;
        }

        case DEVICE_TYPE_SINK:
        case DEVICE_TYPE_SOURCE:
            pa_assert_not_reached();
    }
}

static pa_hook_result_t sink_or_source_proplist_changed_cb(void *hook_data, void *call_data, void *userdata) {
    struct device *device = userdata;
    pa_sink *sink = NULL;
    pa_source *source = NULL;
    const char *description = NULL;

    pa_assert(device);
    pa_assert(call_data);

    switch (device->type) {
        case DEVICE_TYPE_PORT:
        case DEVICE_TYPE_PORT_MONITOR:
            pa_assert_not_reached();

        case DEVICE_TYPE_SINK:
            sink = call_data;

            if (sink != device->sink)
                return PA_HOOK_OK;

            description = get_sink_description(sink);
            break;

        case DEVICE_TYPE_SOURCE:
            source = call_data;

            if (source != device->source)
                return PA_HOOK_OK;

            description = get_source_description(source);
            break;
    }

    pa_device_description_changed(device->device, description);
    pa_volume_control_set_description(device->volume_control->volume_control, description);
    pa_mute_control_set_description(device->mute_control->mute_control, description);

    return PA_HOOK_OK;
}

static int device_new(pa_device_creator *creator, enum device_type type, void *core_device, struct device **_r) {
    struct device *device = NULL;
    const char *name = NULL;
    char *description = NULL;
    pa_direction_t direction = PA_DIRECTION_OUTPUT;
    const char *device_type = NULL;
    bool create_volume_and_mute_controls = true;
    int r;

    pa_assert(creator);
    pa_assert(core_device);
    pa_assert(_r);

    device = pa_xnew0(struct device, 1);
    device->creator = creator;
    device->type = type;

    switch (type) {
        case DEVICE_TYPE_PORT:
            device->port = core_device;
            device_set_sink_and_source_from_port(device);
            name = "port-device";
            description = pa_xstrdup(device->port->description);
            direction = device->port->direction;
            device_type = device_type_from_port(device->port);

            if (!device->sink && !device->source)
                create_volume_and_mute_controls = false;
            break;

        case DEVICE_TYPE_PORT_MONITOR:
            device->port = core_device;
            device_set_sink_and_source_from_port(device);
            name = "port-monitor-device";
            description = pa_sprintf_malloc(_("Monitor of %s"), device->port->description);
            direction = PA_DIRECTION_INPUT;

            if (!device->source)
                create_volume_and_mute_controls = false;
            break;

        case DEVICE_TYPE_SINK:
            device->sink = core_device;
            name = "sink-device";
            description = pa_xstrdup(get_sink_description(device->sink));
            direction = PA_DIRECTION_OUTPUT;
            break;

        case DEVICE_TYPE_SOURCE:
            device->source = core_device;
            name = "source-device";
            description = pa_xstrdup(get_source_description(device->source));
            direction = PA_DIRECTION_INPUT;
            break;
    }

    r = pa_device_new(creator->volume_api, name, description, direction, &device_type, device_type ? 1 : 0, &device->device);
    pa_xfree(description);
    if (r < 0)
        goto fail;

    if (create_volume_and_mute_controls) {
        device_volume_control_new(device, &device->volume_control);
        device_mute_control_new(device, &device->mute_control);
    }

    switch (type) {
        case DEVICE_TYPE_PORT:
            if (device->port->direction == PA_DIRECTION_OUTPUT)
                device_new(creator, DEVICE_TYPE_PORT_MONITOR, device->port, &device->monitor);
            break;

        case DEVICE_TYPE_PORT_MONITOR:
            break;

        case DEVICE_TYPE_SINK:
            device->proplist_changed_slot = pa_hook_connect(&device->sink->core->hooks[PA_CORE_HOOK_SINK_PROPLIST_CHANGED],
                                                            PA_HOOK_NORMAL, sink_or_source_proplist_changed_cb, device);
            break;

        case DEVICE_TYPE_SOURCE:
            device->proplist_changed_slot = pa_hook_connect(&device->source->core->hooks[PA_CORE_HOOK_SOURCE_PROPLIST_CHANGED],
                                                            PA_HOOK_NORMAL, sink_or_source_proplist_changed_cb, device);
            break;
    }

    *_r = device;
    return 0;

fail:
    if (device)
        device_free(device);

    return r;
}

static pa_hook_result_t port_active_changed_cb(void *hook_data, void *call_data, void *userdata) {
    struct device *device = userdata;
    pa_device_port *port = call_data;
    bool should_have_volume_and_mute_controls = false;

    pa_assert(device);
    pa_assert(port);

    if (port != device->port)
        return PA_HOOK_OK;

    device_set_sink_and_source_from_port(device);

    switch (device->type) {
        case DEVICE_TYPE_PORT:
            should_have_volume_and_mute_controls = device->sink || device->source;
            break;

        case DEVICE_TYPE_PORT_MONITOR:
            should_have_volume_and_mute_controls = !!device->source;
            break;

        case DEVICE_TYPE_SINK:
        case DEVICE_TYPE_SOURCE:
            pa_assert_not_reached();
    }

    if (should_have_volume_and_mute_controls) {
        int r;

        if (!device->volume_control) {
            r = device_volume_control_new(device, &device->volume_control);
            if (r >= 0) {
                device_volume_control_put(device->volume_control);
                pa_device_set_default_volume_control(device->device, device->volume_control->volume_control);
            }
        }

        if (!device->mute_control) {
            r = device_mute_control_new(device, &device->mute_control);
            if (r >= 0) {
                device_mute_control_put(device->mute_control);
                pa_device_set_default_mute_control(device->device, device->mute_control->mute_control);
            }
        }
    }

    if (!should_have_volume_and_mute_controls) {
        if (device->mute_control) {
            device_mute_control_free(device->mute_control);
            device->mute_control = NULL;
        }

        if (device->volume_control) {
            device_volume_control_free(device->volume_control);
            device->volume_control = NULL;
        }
    }

    return PA_HOOK_OK;
}

static void device_put(struct device *device) {
    pa_assert(device);

    switch (device->type) {
        case DEVICE_TYPE_PORT:
        case DEVICE_TYPE_PORT_MONITOR:
            device->port_active_changed_slot = pa_hook_connect(&device->port->core->hooks[PA_CORE_HOOK_PORT_ACTIVE_CHANGED],
                                                               PA_HOOK_NORMAL, port_active_changed_cb, device);

        case DEVICE_TYPE_SINK:
        case DEVICE_TYPE_SOURCE:
            break;
    }

    if (device->volume_control)
        device_volume_control_put(device->volume_control);

    if (device->mute_control)
        device_mute_control_put(device->mute_control);

    pa_device_put(device->device, device->volume_control ? device->volume_control->volume_control : NULL,
                  device->mute_control ? device->mute_control->mute_control : NULL);

    if (device->monitor)
        device_put(device->monitor);
}

static void device_unlink(struct device *device) {
    pa_assert(device);

    if (device->unlinked)
        return;

    device->unlinked = true;

    if (device->monitor)
        device_unlink(device->monitor);

    if (device->device)
        pa_device_unlink(device->device);

    if (device->mute_control)
        device_mute_control_unlink(device->mute_control);

    if (device->volume_control)
        device_volume_control_unlink(device->volume_control);

    if (device->port_active_changed_slot) {
        pa_hook_slot_free(device->port_active_changed_slot);
        device->port_active_changed_slot = NULL;
    }
}

static void device_free(struct device *device) {
    pa_assert(device);

    if (!device->unlinked)
        device_unlink(device);

    if (device->monitor)
        device_free(device->monitor);

    if (device->proplist_changed_slot)
        pa_hook_slot_free(device->proplist_changed_slot);

    if (device->mute_control)
        device_mute_control_free(device->mute_control);

    if (device->volume_control)
        device_volume_control_free(device->volume_control);

    if (device->device)
        pa_device_free(device->device);

    pa_xfree(device);
}

static void create_device(pa_device_creator *creator, enum device_type type, void *core_device) {
    struct device *device;
    int r;

    pa_assert(creator);
    pa_assert(core_device);

    switch (type) {
        case DEVICE_TYPE_PORT:
            break;

        case DEVICE_TYPE_PORT_MONITOR:
            pa_assert_not_reached();

        case DEVICE_TYPE_SINK:
            if (!pa_hashmap_isempty(((pa_sink *) core_device)->ports))
                return;
            break;

        case DEVICE_TYPE_SOURCE: {
            pa_source *source = core_device;

            if (source->monitor_of && !pa_hashmap_isempty(source->monitor_of->ports))
                return;

            if (!pa_hashmap_isempty(((pa_source *) core_device)->ports))
                return;
            break;
        }
    }

    r = device_new(creator, type, core_device, &device);
    if (r >= 0) {
        pa_hashmap_put(creator->devices, core_device, device);
        device_put(device);
    }
}

static pa_hook_result_t card_put_cb(void *hook_data, void *call_data, void *userdata) {
    pa_device_creator *creator = userdata;
    pa_card *card = call_data;
    pa_device_port *port;
    void *state;

    pa_assert(creator);
    pa_assert(card);

    PA_HASHMAP_FOREACH(port, card->ports, state)
        create_device(creator, DEVICE_TYPE_PORT, port);

    return PA_HOOK_OK;
}

static pa_hook_result_t card_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    pa_device_creator *creator = userdata;
    pa_card *card = call_data;
    pa_device_port *port;
    void *state;

    pa_assert(creator);
    pa_assert(card);

    PA_HASHMAP_FOREACH(port, card->ports, state)
        pa_hashmap_remove_and_free(creator->devices, port);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_put_cb(void *hook_data, void *call_data, void *userdata) {
    pa_device_creator *creator = userdata;
    pa_sink *sink = call_data;

    pa_assert(creator);
    pa_assert(sink);

    create_device(creator, DEVICE_TYPE_SINK, sink);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    pa_device_creator *creator = userdata;
    pa_sink *sink = call_data;

    pa_assert(creator);
    pa_assert(sink);

    pa_hashmap_remove_and_free(creator->devices, sink);

    return PA_HOOK_OK;
}

static pa_hook_result_t source_put_cb(void *hook_data, void *call_data, void *userdata) {
    pa_device_creator *creator = userdata;
    pa_source *source = call_data;

    pa_assert(creator);
    pa_assert(source);

    create_device(creator, DEVICE_TYPE_SOURCE, source);

    return PA_HOOK_OK;
}

static pa_hook_result_t source_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    pa_device_creator *creator = userdata;
    pa_source *source = call_data;

    pa_assert(creator);
    pa_assert(source);

    pa_hashmap_remove_and_free(creator->devices, source);

    return PA_HOOK_OK;
}

pa_device_creator *pa_device_creator_new(pa_volume_api *api) {
    pa_device_creator *creator;
    pa_card *card;
    uint32_t idx;
    pa_sink *sink;
    pa_source *source;

    pa_assert(api);

    creator = pa_xnew0(pa_device_creator, 1);
    creator->volume_api = api;
    creator->devices = pa_hashmap_new_full(NULL, NULL, NULL, (pa_free_cb_t) device_free);
    creator->card_put_slot = pa_hook_connect(&api->core->hooks[PA_CORE_HOOK_CARD_PUT], PA_HOOK_NORMAL, card_put_cb, creator);
    creator->card_unlink_slot = pa_hook_connect(&api->core->hooks[PA_CORE_HOOK_CARD_UNLINK], PA_HOOK_NORMAL, card_unlink_cb,
                                                creator);
    creator->sink_put_slot = pa_hook_connect(&api->core->hooks[PA_CORE_HOOK_SINK_PUT], PA_HOOK_NORMAL, sink_put_cb, creator);
    creator->sink_unlink_slot = pa_hook_connect(&api->core->hooks[PA_CORE_HOOK_SINK_UNLINK], PA_HOOK_NORMAL, sink_unlink_cb,
                                                creator);
    creator->source_put_slot = pa_hook_connect(&api->core->hooks[PA_CORE_HOOK_SOURCE_PUT], PA_HOOK_NORMAL, source_put_cb,
                                               creator);
    creator->source_unlink_slot = pa_hook_connect(&api->core->hooks[PA_CORE_HOOK_SOURCE_UNLINK], PA_HOOK_NORMAL,
                                                  source_unlink_cb, creator);

    PA_IDXSET_FOREACH(card, api->core->cards, idx) {
        pa_device_port *port;
        void *state;

        PA_HASHMAP_FOREACH(port, card->ports, state)
            create_device(creator, DEVICE_TYPE_PORT, port);
    }

    PA_IDXSET_FOREACH(sink, api->core->sinks, idx)
        create_device(creator, DEVICE_TYPE_SINK, sink);

    PA_IDXSET_FOREACH(source, api->core->sources, idx)
        create_device(creator, DEVICE_TYPE_SOURCE, source);

    return creator;
}

void pa_device_creator_free(pa_device_creator *creator) {
    pa_assert(creator);

    if (creator->devices)
        pa_hashmap_remove_all(creator->devices);

    if (creator->source_unlink_slot)
        pa_hook_slot_free(creator->source_unlink_slot);

    if (creator->source_put_slot)
        pa_hook_slot_free(creator->source_put_slot);

    if (creator->sink_unlink_slot)
        pa_hook_slot_free(creator->sink_unlink_slot);

    if (creator->sink_put_slot)
        pa_hook_slot_free(creator->sink_put_slot);

    if (creator->card_unlink_slot)
        pa_hook_slot_free(creator->card_unlink_slot);

    if (creator->card_put_slot)
        pa_hook_slot_free(creator->card_put_slot);

    if (creator->devices)
        pa_hashmap_free(creator->devices);

    pa_xfree(creator);
}
