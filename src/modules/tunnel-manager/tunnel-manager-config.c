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

#include "tunnel-manager-config.h"

#include <pulse/xmalloc.h>

#include <pulsecore/conf-parser.h>
#include <pulsecore/core-util.h>
#include <pulsecore/namereg.h>

#define GENERAL_SECTION_NAME "General"
#define REMOTE_SERVER_SECTION_NAME "RemoteServer"
#define REMOTE_SERVER_SECTION_PREFIX REMOTE_SERVER_SECTION_NAME " "

static int remote_server_config_new(pa_tunnel_manager_config *manager_config, const char *name,
                                    pa_tunnel_manager_remote_server_config **_r);
static void remote_server_config_free(pa_tunnel_manager_remote_server_config *config);

static pa_tunnel_manager_config_value *config_value_new(const char *value, const char *filename, unsigned lineno) {
    pa_tunnel_manager_config_value *config_value;

    pa_assert(value);
    pa_assert(filename);

    config_value = pa_xnew0(pa_tunnel_manager_config_value, 1);
    config_value->value = pa_xstrdup(value);
    config_value->filename = pa_xstrdup(filename);
    config_value->lineno = lineno;

    return config_value;
}

static void config_value_free(pa_tunnel_manager_config_value *value) {
    pa_assert(value);

    pa_xfree(value->filename);
    pa_xfree(value->value);
    pa_xfree(value);
}

static int get_remote_server_config(pa_tunnel_manager_config *manager_config, const char *section,
                                    pa_tunnel_manager_remote_server_config **_r) {
    char *name = NULL;
    pa_tunnel_manager_remote_server_config *server_config;
    int r;

    pa_assert(manager_config);
    pa_assert(section);
    pa_assert(_r);

    name = pa_xstrdup(section + strlen(REMOTE_SERVER_SECTION_PREFIX));
    name = pa_strip(name);

    server_config = pa_hashmap_get(manager_config->remote_servers, name);
    if (server_config)
        goto success;

    r = remote_server_config_new(manager_config, name, &server_config);
    if (r < 0)
        goto fail;

success:
    pa_xfree(name);

    *_r = server_config;
    return 0;

fail:
    pa_xfree(name);

    return r;
}

static int parse_config_value(pa_config_parser_state *state) {
    pa_tunnel_manager_config *manager_config;

    pa_assert(state);

    manager_config = state->userdata;

    if (!state->section || pa_streq(state->section, GENERAL_SECTION_NAME)) {
        if (pa_streq(state->lvalue, "remote_device_tunnel_enabled_condition")) {
            if (manager_config->remote_device_tunnel_enabled_condition)
                config_value_free(manager_config->remote_device_tunnel_enabled_condition);

            manager_config->remote_device_tunnel_enabled_condition = config_value_new(state->rvalue, state->filename,
                                                                                      state->lineno);
        } else
            pa_log("[%s:%u] \"%s\" is not valid in the " GENERAL_SECTION_NAME " section.", state->filename,
                   state->lineno, state->lvalue);
    } else if (pa_startswith(state->section, REMOTE_SERVER_SECTION_PREFIX)) {
        int r;
        pa_tunnel_manager_remote_server_config *server_config;

        r = get_remote_server_config(manager_config, state->section, &server_config);
        if (r < 0) {
            pa_log("[%s:%u] Invalid section: \"%s\"", state->filename, state->lineno, state->section);
            return 0;
        }

        if (pa_streq(state->lvalue, "address")) {
            if (server_config->address)
                config_value_free(server_config->address);

            server_config->address = config_value_new(state->rvalue, state->filename, state->lineno);
        } else
            pa_log("[%s:%u] \"%s\" is not valid in the " REMOTE_SERVER_SECTION_NAME " section.", state->filename,
                   state->lineno, state->lvalue);
    } else
        pa_log("[%s:%u] Invalid section: \"%s\"", state->filename, state->lineno, state->section);

    return 0;
}

pa_tunnel_manager_config *pa_tunnel_manager_config_new(void) {
    pa_tunnel_manager_config *config;
    FILE *f;
    char *fn = NULL;

    config = pa_xnew0(pa_tunnel_manager_config, 1);
    config->remote_servers = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    f = pa_open_config_file(PA_DEFAULT_CONFIG_DIR PA_PATH_SEP "tunnel-manager.conf", "tunnel-manager.conf", NULL, &fn);
    if (f) {
        pa_config_item config_items[] = {
            { NULL, parse_config_value, NULL, NULL },
            { NULL, NULL, NULL, NULL },
        };

        pa_config_parse(fn, f, config_items, NULL, config);
        pa_xfree(fn);
        fn = NULL;
        fclose(f);
        f = NULL;
    }

    return config;
}

void pa_tunnel_manager_config_free(pa_tunnel_manager_config *manager_config) {
    pa_assert(manager_config);

    if (manager_config->remote_servers) {
        pa_tunnel_manager_remote_server_config *server_config;

        while ((server_config = pa_hashmap_first(manager_config->remote_servers)))
            remote_server_config_free(server_config);

        pa_hashmap_free(manager_config->remote_servers);
    }

    if (manager_config->remote_device_tunnel_enabled_condition)
        config_value_free(manager_config->remote_device_tunnel_enabled_condition);

    pa_xfree(manager_config);
}

static int remote_server_config_new(pa_tunnel_manager_config *manager_config, const char *name,
                                    pa_tunnel_manager_remote_server_config **_r) {
    pa_tunnel_manager_remote_server_config *server_config = NULL;

    pa_assert(manager_config);
    pa_assert(name);
    pa_assert(_r);

    if (!pa_namereg_is_valid_name(name))
        return -PA_ERR_INVALID;

    server_config = pa_xnew0(pa_tunnel_manager_remote_server_config, 1);
    server_config->manager_config = manager_config;
    server_config->name = pa_xstrdup(name);

    pa_assert_se(pa_hashmap_put(manager_config->remote_servers, server_config->name, server_config) >= 0);

    *_r = server_config;
    return 0;
}

static void remote_server_config_free(pa_tunnel_manager_remote_server_config *config) {
    pa_assert(config);

    pa_hashmap_remove(config->manager_config->remote_servers, config->name);

    if (config->address)
        config_value_free(config->address);

    pa_xfree(config->name);
    pa_xfree(config);
}
