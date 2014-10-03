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

#include "remote-device.h"

#include <pulse/error.h>
#include <pulse/introspect.h>

#include <pulsecore/core-util.h>
#include <pulsecore/device-type.h>
#include <pulsecore/namereg.h>

void pa_tunnel_manager_remote_device_new(pa_tunnel_manager_remote_server *server, pa_device_type_t type, const void *info) {
    const char *name = NULL;
    uint32_t idx = PA_INVALID_INDEX;
    pa_proplist *proplist = NULL;
    const pa_sample_spec *sample_spec = NULL;
    const pa_channel_map *channel_map = NULL;
    bool is_monitor = false;
    pa_tunnel_manager_remote_device *device;
    unsigned i;
    char sample_spec_str[PA_SAMPLE_SPEC_SNPRINT_MAX];
    char channel_map_str[PA_CHANNEL_MAP_SNPRINT_MAX];

    pa_assert(server);
    pa_assert(info);

    switch (type) {
        case PA_DEVICE_TYPE_SINK: {
            const pa_sink_info *sink_info = info;

            name = sink_info->name;
            idx = sink_info->index;
            proplist = sink_info->proplist;
            sample_spec = &sink_info->sample_spec;
            channel_map = &sink_info->channel_map;
            break;
        }

        case PA_DEVICE_TYPE_SOURCE: {
            const pa_source_info *source_info = info;

            name = source_info->name;
            idx = source_info->index;
            proplist = source_info->proplist;
            sample_spec = &source_info->sample_spec;
            channel_map = &source_info->channel_map;
            is_monitor = !!source_info->monitor_of_sink_name;

            break;
        }
    }

    /* TODO: This check should be done in libpulse. */
    if (!name || !pa_namereg_is_valid_name(name)) {
        pa_log("[%s] Invalid remote device name: %s", server->name, pa_strnull(name));
        pa_tunnel_manager_remote_server_set_failed(server, true);
        return;
    }

    if (pa_hashmap_get(server->devices, name)) {
        pa_log("[%s] Duplicate remote device name: %s", server->name, name);
        pa_tunnel_manager_remote_server_set_failed(server, true);
        return;
    }

    if (pa_hashmap_size(server->devices) + pa_hashmap_size(server->device_stubs) >= PA_TUNNEL_MANAGER_MAX_DEVICES_PER_SERVER) {
        pa_log("[%s] Maximum number of devices exceeded.", server->name);
        pa_tunnel_manager_remote_server_set_failed(server, true);
        return;
    }

    /* TODO: This check should be done in libpulse. */
    if (!pa_sample_spec_valid(sample_spec)) {
        pa_log("[%s %s] Invalid sample spec.", server->name, name);
        pa_tunnel_manager_remote_server_set_failed(server, true);
        return;
    }

    /* TODO: This check should be done in libpulse. */
    if (!pa_channel_map_valid(channel_map)) {
        pa_log("[%s %s] Invalid channel map.", server->name, name);
        pa_tunnel_manager_remote_server_set_failed(server, true);
        return;
    }

    device = pa_xnew0(pa_tunnel_manager_remote_device, 1);
    device->server = server;
    device->type = type;
    device->name = pa_xstrdup(name);
    device->index = idx;
    device->proplist = pa_proplist_copy(proplist);
    device->sample_spec = *sample_spec;
    device->channel_map = *channel_map;
    device->is_monitor = is_monitor;

    for (i = 0; i < PA_TUNNEL_MANAGER_REMOTE_DEVICE_HOOK_MAX; i++)
        pa_hook_init(&device->hooks[i], device);

    device->can_free = true;

    pa_hashmap_put(server->devices, device->name, device);

    pa_log_debug("[%s] Created remote device %s.", server->name, device->name);
    pa_log_debug("        Type: %s", pa_device_type_to_string(type));
    pa_log_debug("        Index: %u", idx);
    pa_log_debug("        Sample spec: %s", pa_sample_spec_snprint(sample_spec_str, sizeof(sample_spec_str), sample_spec));
    pa_log_debug("        Channel map: %s", pa_channel_map_snprint(channel_map_str, sizeof(channel_map_str), channel_map));
    pa_log_debug("        Is monitor: %s", pa_boolean_to_string(device->is_monitor));
}

void pa_tunnel_manager_remote_device_free(pa_tunnel_manager_remote_device *device) {
    unsigned i;

    pa_assert(device);

    if (device->dead) {
        pa_assert(device->can_free);
        pa_xfree(device);
        return;
    }

    device->dead = true;

    pa_log_debug("[%s] Freeing remote device %s.", device->server->name, device->name);

    pa_hashmap_remove(device->server->devices, device->name);
    pa_hook_fire(&device->hooks[PA_TUNNEL_MANAGER_REMOTE_DEVICE_HOOK_UNLINKED], NULL);

    if (device->get_info_operation) {
        pa_operation_cancel(device->get_info_operation);
        pa_operation_unref(device->get_info_operation);

        /* This member will still be accessed in get_info_cb if
         * device->can_free is true, so we need to set it to NULL here. */
        device->get_info_operation = NULL;
    }

    for (i = 0; i < PA_TUNNEL_MANAGER_REMOTE_DEVICE_HOOK_MAX; i++)
        pa_hook_done(&device->hooks[i]);

    if (device->proplist)
        pa_proplist_free(device->proplist);

    pa_xfree(device->name);

    if (device->can_free)
        pa_xfree(device);
}

static void set_proplist(pa_tunnel_manager_remote_device *device, pa_proplist *proplist) {
    pa_assert(device);
    pa_assert(proplist);

    if (pa_proplist_equal(proplist, device->proplist))
        return;

    pa_proplist_update(device->proplist, PA_UPDATE_SET, proplist);

    pa_log_debug("[%s %s] Proplist changed.", device->server->name, device->name);

    pa_hook_fire(&device->hooks[PA_TUNNEL_MANAGER_REMOTE_DEVICE_HOOK_PROPLIST_CHANGED], NULL);
}

static void get_info_cb(pa_context *context, const void *info, int is_last, void *userdata) {
    pa_tunnel_manager_remote_device *device = userdata;
    pa_proplist *proplist = NULL;

    pa_assert(context);
    pa_assert(device);

    if (device->get_info_operation) {
        pa_operation_unref(device->get_info_operation);
        device->get_info_operation = NULL;
    }

    if (is_last < 0) {
        pa_log_debug("[%s %s] Getting info failed: %s", device->server->name, device->name,
                     pa_strerror(pa_context_errno(context)));
        return;
    }

    if (is_last) {
        device->can_free = true;

        if (device->dead)
            pa_tunnel_manager_remote_device_free(device);

        return;
    }

    device->can_free = false;

    if (device->dead)
        return;

    switch (device->type) {
        case PA_DEVICE_TYPE_SINK:
            proplist = ((const pa_sink_info *) info)->proplist;
            break;

        case PA_DEVICE_TYPE_SOURCE:
            proplist = ((const pa_source_info *) info)->proplist;
            break;
    }

    set_proplist(device, proplist);
}

void pa_tunnel_manager_remote_device_update(pa_tunnel_manager_remote_device *device) {
    pa_assert(device);

    if (device->get_info_operation)
        return;

    switch (device->type) {
        case PA_DEVICE_TYPE_SINK:
            device->get_info_operation = pa_context_get_sink_info_by_name(device->server->context, device->name,
                                                                          (pa_sink_info_cb_t) get_info_cb, device);
            break;

        case PA_DEVICE_TYPE_SOURCE:
            device->get_info_operation = pa_context_get_source_info_by_name(device->server->context, device->name,
                                                                            (pa_source_info_cb_t) get_info_cb, device);
            break;
    }

    if (!device->get_info_operation) {
        pa_log("[%s %s] pa_context_get_%s_info_by_name() failed: %s", device->server->name, device->name,
               pa_device_type_to_string(device->type), pa_strerror(pa_context_errno(device->server->context)));
        pa_tunnel_manager_remote_server_set_failed(device->server, true);
    }
}
