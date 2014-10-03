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

#include "remote-server.h"

#include <modules/tunnel-manager/remote-device.h>

#include <pulse/error.h>
#include <pulse/introspect.h>
#include <pulse/subscribe.h>

#include <pulsecore/core-util.h>
#include <pulsecore/device-type.h>
#include <pulsecore/parseaddr.h>

static void set_up_connection(pa_tunnel_manager_remote_server *server);

struct device_stub {
    pa_tunnel_manager_remote_server *server;
    pa_device_type_t type;
    uint32_t index;

    pa_operation *get_info_operation;

    /* These are a workaround for the problem that the introspection API's info
     * callbacks are called multiple times, which means that if the userdata
     * needs to be freed during the callbacks, the freeing needs to be
     * postponed until the last call. */
    bool can_free;
    bool dead;
};

static void device_stub_new(pa_tunnel_manager_remote_server *server, pa_device_type_t type, uint32_t idx);
static void device_stub_free(struct device_stub *stub);

void pa_tunnel_manager_remote_server_new(pa_tunnel_manager *manager, pa_tunnel_manager_remote_server_config *config) {
    int r;
    pa_parsed_address parsed_address;
    pa_tunnel_manager_remote_server *server = NULL;

    pa_assert(manager);
    pa_assert(config);

    if (!config->address) {
        pa_log("No address configured for remote server %s.", config->name);
        return;
    }

    r = pa_parse_address(config->address->value, &parsed_address);
    if (r < 0) {
        pa_log("[%s:%u] Invalid address: \"%s\"", config->address->filename, config->address->lineno, config->address->value);
        return;
    }

    pa_xfree(parsed_address.path_or_host);

    server = pa_xnew0(pa_tunnel_manager_remote_server, 1);
    server->manager = manager;
    server->name = pa_xstrdup(config->name);
    server->address = pa_xstrdup(config->address->value);
    server->devices = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);
    server->device_stubs = pa_hashmap_new(NULL, NULL);

    pa_assert_se(pa_hashmap_put(manager->remote_servers, server->name, server) >= 0);

    pa_log_debug("Created remote server %s.", server->name);
    pa_log_debug("    Address: %s", server->address);
    pa_log_debug("    Failed: %s", pa_boolean_to_string(server->failed));

    set_up_connection(server);
}

static void subscribe_cb(pa_context *context, pa_subscription_event_type_t event_type, uint32_t idx, void *userdata) {
    pa_tunnel_manager_remote_server *server = userdata;
    pa_device_type_t device_type;
    pa_tunnel_manager_remote_device *device;
    void *state;

    pa_assert(context);
    pa_assert(server);

    if ((event_type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SINK)
        device_type = PA_DEVICE_TYPE_SINK;
    else if ((event_type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) == PA_SUBSCRIPTION_EVENT_SOURCE)
        device_type = PA_DEVICE_TYPE_SOURCE;
    else {
        pa_log("[%s] Unexpected event facility: %u", server->name,
               (unsigned) (event_type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK));
        pa_tunnel_manager_remote_server_set_failed(server, true);
        return;
    }

    if (idx == PA_INVALID_INDEX) {
        pa_log("[%s] Invalid %s index.", server->name, pa_device_type_to_string(device_type));
        pa_tunnel_manager_remote_server_set_failed(server, true);
        return;
    }

    if ((event_type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_NEW) {
        if ((device_type == PA_DEVICE_TYPE_SINK && server->list_sinks_operation)
                || (device_type == PA_DEVICE_TYPE_SOURCE && server->list_sources_operation))
            return;

        device_stub_new(server, device_type, idx);

        return;
    }

    if ((event_type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_REMOVE) {
        struct device_stub *stub;

        PA_HASHMAP_FOREACH(device, server->devices, state) {
            if (device->type == device_type && device->index == idx) {
                pa_tunnel_manager_remote_device_free(device);
                return;
            }
        }

        PA_HASHMAP_FOREACH(stub, server->device_stubs, state) {
            if (stub->type == device_type && stub->index == idx) {
                device_stub_free(stub);
                return;
            }
        }

        return;
    }

    if ((event_type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) == PA_SUBSCRIPTION_EVENT_CHANGE) {
        PA_HASHMAP_FOREACH(device, server->devices, state) {
            if (device->type == device_type && device->index == idx) {
                pa_tunnel_manager_remote_device_update(device);
                return;
            }
        }

        return;
    }
}

static void subscribe_success_cb(pa_context *context, int success, void *userdata) {
    pa_tunnel_manager_remote_server *server = userdata;

    pa_assert(context);
    pa_assert(server);

    if (!success) {
        pa_log("[%s] Subscribing to device events failed: %s", server->name, pa_strerror(pa_context_errno(context)));
        pa_tunnel_manager_remote_server_set_failed(server, true);
    }
}

static void get_sink_info_list_cb(pa_context *context, const pa_sink_info *info, int is_last, void *userdata) {
    pa_tunnel_manager_remote_server *server = userdata;

    pa_assert(context);
    pa_assert(server);

    if (server->list_sinks_operation) {
        pa_operation_unref(server->list_sinks_operation);
        server->list_sinks_operation = NULL;
    }

    if (is_last < 0) {
        pa_log("[%s] Listing sinks failed: %s", server->name, pa_strerror(pa_context_errno(context)));
        pa_tunnel_manager_remote_server_set_failed(server, true);
        return;
    }

    if (is_last)
        return;

    pa_tunnel_manager_remote_device_new(server, PA_DEVICE_TYPE_SINK, info);
}

static void get_source_info_list_cb(pa_context *context, const pa_source_info *info, int is_last, void *userdata) {
    pa_tunnel_manager_remote_server *server = userdata;

    pa_assert(context);
    pa_assert(server);

    if (server->list_sources_operation) {
        pa_operation_unref(server->list_sources_operation);
        server->list_sources_operation = NULL;
    }

    if (is_last < 0) {
        pa_log("[%s] Listing sources failed: %s", server->name, pa_strerror(pa_context_errno(context)));
        pa_tunnel_manager_remote_server_set_failed(server, true);
        return;
    }

    if (is_last)
        return;

    pa_tunnel_manager_remote_device_new(server, PA_DEVICE_TYPE_SOURCE, info);
}

static void context_state_cb(pa_context *context, void *userdata) {
    pa_tunnel_manager_remote_server *server = userdata;
    pa_context_state_t state;

    pa_assert(context);
    pa_assert(server);
    pa_assert(context == server->context);

    state = pa_context_get_state(context);

    switch (state) {
        case PA_CONTEXT_READY: {
            pa_operation *operation;

            pa_context_set_subscribe_callback(context, subscribe_cb, server);
            operation = pa_context_subscribe(context, PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SOURCE,
                                             subscribe_success_cb, server);
            if (operation)
                pa_operation_unref(operation);
            else {
                pa_log("[%s] pa_context_subscribe() failed: %s", server->name, pa_strerror(pa_context_errno(context)));
                pa_tunnel_manager_remote_server_set_failed(server, true);
                return;
            }

            pa_assert(!server->list_sinks_operation);
            pa_assert(!server->list_sources_operation);

            server->list_sinks_operation = pa_context_get_sink_info_list(server->context, get_sink_info_list_cb, server);
            if (!server->list_sinks_operation) {
                pa_log("[%s] pa_context_get_sink_info_list() failed: %s", server->name,
                       pa_strerror(pa_context_errno(context)));
                pa_tunnel_manager_remote_server_set_failed(server, true);
                return;
            }

            server->list_sources_operation = pa_context_get_source_info_list(server->context, get_source_info_list_cb, server);
            if (!server->list_sources_operation) {
                pa_log("[%s] pa_context_get_source_info_list() failed: %s", server->name,
                       pa_strerror(pa_context_errno(context)));
                pa_tunnel_manager_remote_server_set_failed(server, true);
                return;
            }

            return;
        }

        case PA_CONTEXT_FAILED:
            pa_log("[%s] Context failed: %s", server->name, pa_strerror(pa_context_errno(context)));
            pa_tunnel_manager_remote_server_set_failed(server, true);
            return;

        default:
            return;
    }
}

static void set_up_connection(pa_tunnel_manager_remote_server *server) {
    pa_assert(server);
    pa_assert(!server->context);

    server->context = pa_context_new(server->manager->core->mainloop, "PulseAudio");
    if (server->context) {
        int r;

        r = pa_context_connect(server->context, server->address, PA_CONTEXT_NOFLAGS, NULL);
        if (r >= 0)
            pa_context_set_state_callback(server->context, context_state_cb, server);
        else {
            pa_log("[%s] pa_context_connect() failed: %s", server->name, pa_strerror(pa_context_errno(server->context)));
            pa_tunnel_manager_remote_server_set_failed(server, true);
        }
    } else {
        pa_log("[%s] pa_context_new() failed.", server->name);
        pa_tunnel_manager_remote_server_set_failed(server, true);
    }
}

static void tear_down_connection(pa_tunnel_manager_remote_server *server) {
    pa_assert(server);

    if (server->device_stubs) {
        struct device_stub *stub;

        while ((stub = pa_hashmap_first(server->device_stubs)))
            device_stub_free(stub);
    }

    if (server->devices) {
        pa_tunnel_manager_remote_device *device;

        while ((device = pa_hashmap_first(server->devices)))
            pa_tunnel_manager_remote_device_free(device);
    }

    if (server->list_sources_operation) {
        pa_operation_cancel(server->list_sources_operation);
        pa_operation_unref(server->list_sources_operation);
        server->list_sources_operation = NULL;
    }

    if (server->list_sinks_operation) {
        pa_operation_cancel(server->list_sinks_operation);
        pa_operation_unref(server->list_sinks_operation);
        server->list_sinks_operation = NULL;
    }

    if (server->context) {
        pa_context_disconnect(server->context);
        pa_context_unref(server->context);
        server->context = NULL;
    }
}

void pa_tunnel_manager_remote_server_free(pa_tunnel_manager_remote_server *server) {
    pa_assert(server);

    pa_log_debug("Freeing remote server %s.", server->name);

    pa_hashmap_remove(server->manager->remote_servers, server->name);

    tear_down_connection(server);

    if (server->device_stubs) {
        pa_assert(pa_hashmap_isempty(server->device_stubs));
        pa_hashmap_free(server->device_stubs);
    }

    if (server->devices) {
        pa_assert(pa_hashmap_isempty(server->devices));
        pa_hashmap_free(server->devices);
    }

    pa_xfree(server->address);
    pa_xfree(server->name);
    pa_xfree(server);
}

void pa_tunnel_manager_remote_server_set_failed(pa_tunnel_manager_remote_server *server, bool failed) {
    pa_assert(server);

    if (failed == server->failed)
        return;

    server->failed = failed;

    pa_log_debug("[%s] Failed changed from %s to %s.", server->name, pa_boolean_to_string(!failed),
                 pa_boolean_to_string(failed));

    if (failed)
        tear_down_connection(server);
}

static void device_stub_get_info_cb(pa_context *context, const void *info, int is_last, void *userdata) {
    struct device_stub *stub = userdata;
    pa_tunnel_manager_remote_server *server;
    pa_device_type_t type;
    uint32_t idx = PA_INVALID_INDEX;

    pa_assert(context);
    pa_assert(stub);

    server = stub->server;
    type = stub->type;

    if (stub->get_info_operation) {
        pa_operation_unref(stub->get_info_operation);
        stub->get_info_operation = NULL;
    }

    if (is_last < 0) {
        pa_log_debug("[%s] Getting info for %s %u failed: %s", server->name, pa_device_type_to_string(type), stub->index,
                     pa_strerror(pa_context_errno(context)));
        device_stub_free(stub);
        return;
    }

    if (is_last) {
        stub->can_free = true;

        /* TODO: libpulse should ensure that the get info operation doesn't
         * return an empty result. Then this check wouldn't be needed. */
        if (!stub->dead) {
            pa_log("[%s] No info received for %s %u.", server->name, pa_device_type_to_string(type), stub->index);
            pa_tunnel_manager_remote_server_set_failed(server, true);
            return;
        }

        device_stub_free(stub);
        return;
    }

    /* This callback will still be called at least once, so we need to keep the
     * stub alive. */
    stub->can_free = false;

    /* TODO: libpulse should ensure that the get info operation doesn't return
     * more than one result. Then this check wouldn't be needed. */
    if (stub->dead) {
        pa_log("[%s] Multiple info structs received for %s %u.", server->name, pa_device_type_to_string(type), stub->index);
        pa_tunnel_manager_remote_server_set_failed(server, true);
        return;
    }

    switch (type) {
        case PA_DEVICE_TYPE_SINK:
            idx = ((const pa_sink_info *) info)->index;
            break;

        case PA_DEVICE_TYPE_SOURCE:
            idx = ((const pa_source_info *) info)->index;
            break;
    }

    if (idx != stub->index) {
        pa_log("[%s] Index mismatch for %s %u.", server->name, pa_device_type_to_string(type), stub->index);
        pa_tunnel_manager_remote_server_set_failed(server, true);
        return;
    }

    /* pa_tunnel_manager_remote_device_new() checks whether the maximum device
     * limit has been reached, and device stubs count towards that limit. This
     * stub shouldn't any more count towards the limit, so let's free the stub
     * before calling pa_tunnel_manager_remote_device_new(). */
    device_stub_free(stub);

    pa_tunnel_manager_remote_device_new(server, type, info);
}

static void device_stub_new(pa_tunnel_manager_remote_server *server, pa_device_type_t type, uint32_t idx) {
    pa_tunnel_manager_remote_device *device;
    void *state;
    struct device_stub *stub;

    pa_assert(server);

    PA_HASHMAP_FOREACH(device, server->devices, state) {
        if (device->type == type && device->index == idx) {
            pa_log("[%s] Duplicate %s index %u.", server->name, pa_device_type_to_string(type), idx);
            pa_tunnel_manager_remote_server_set_failed(server, true);
            return;
        }
    }

    PA_HASHMAP_FOREACH(stub, server->device_stubs, state) {
        if (stub->type == type && stub->index == idx) {
            pa_log("[%s] Duplicate %s index %u.", server->name, pa_device_type_to_string(type), idx);
            pa_tunnel_manager_remote_server_set_failed(server, true);
            return;
        }
    }

    if (pa_hashmap_size(server->devices) + pa_hashmap_size(server->device_stubs) >= PA_TUNNEL_MANAGER_MAX_DEVICES_PER_SERVER) {
        pa_log("[%s] Maximum number of devices exceeded.", server->name);
        pa_tunnel_manager_remote_server_set_failed(server, true);
        return;
    }

    stub = pa_xnew0(struct device_stub, 1);
    stub->server = server;
    stub->type = type;
    stub->index = idx;
    stub->can_free = true;

    pa_hashmap_put(server->device_stubs, stub, stub);

    switch (type) {
        case PA_DEVICE_TYPE_SINK:
            stub->get_info_operation = pa_context_get_sink_info_by_index(server->context, idx,
                                                                         (pa_sink_info_cb_t) device_stub_get_info_cb,
                                                                         stub);
            break;

        case PA_DEVICE_TYPE_SOURCE:
            stub->get_info_operation = pa_context_get_source_info_by_index(server->context, idx,
                                                                           (pa_source_info_cb_t) device_stub_get_info_cb,
                                                                           stub);
            break;
    }

    if (!stub->get_info_operation) {
        pa_log("[%s] pa_context_get_%s_info_by_index() failed: %s", server->name, pa_device_type_to_string(type),
               pa_strerror(pa_context_errno(server->context)));
        pa_tunnel_manager_remote_server_set_failed(server, true);
        return;
    }
}

static void device_stub_free(struct device_stub *stub) {
    pa_assert(stub);

    if (stub->dead) {
        pa_assert(stub->can_free);
        pa_xfree(stub);
        return;
    }

    stub->dead = true;

    pa_hashmap_remove(stub->server->device_stubs, stub);

    if (stub->get_info_operation) {
        pa_operation_cancel(stub->get_info_operation);
        pa_operation_unref(stub->get_info_operation);

        /* This member will still be accessed in device_stub_get_info_cb if
         * stub->can_free is true, so we need to set it to NULL here. */
        stub->get_info_operation = NULL;
    }

    if (stub->can_free)
        pa_xfree(stub);
}
