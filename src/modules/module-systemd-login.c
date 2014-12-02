/***
    This file is part of PulseAudio.

    Copyright 2012 Lennart Poettering

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

#include "module-systemd-login-symdef.h"

#include <modules/logind/logind.h>

#include <pulse/xmalloc.h>

#include <pulsecore/modargs.h>

PA_MODULE_AUTHOR("Lennart Poettering");
PA_MODULE_DESCRIPTION("Create a client for each login session of this user");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);

static const char* const valid_modargs[] = {
    NULL
};

struct session_client {
    struct userdata *userdata;
    pa_logind_session *session;
    pa_client *client;
};

static void session_client_free(struct session_client *client);

struct userdata {
    pa_module *module;
    pa_core *core;
    pa_logind *logind;
    pa_hashmap *session_clients; /* pa_logind_session -> struct session_client */
    pa_hook_slot *session_added_slot;
    pa_hook_slot *session_removed_slot;
};

static void session_client_new(struct userdata *u, pa_logind_session *session) {
    struct session_client *client;
    pa_client_new_data data;

    pa_assert(u);
    pa_assert(session);

    client = pa_xnew0(struct session_client, 1);
    client->userdata = u;
    client->session = session;

    pa_client_new_data_init(&data);
    data.module = u->module;
    data.driver = __FILE__;
    pa_proplist_setf(data.proplist, PA_PROP_APPLICATION_NAME, "Login Session %s", session->id);
    pa_proplist_sets(data.proplist, "systemd-login.session", session->id);
    client->client = pa_client_new(u->core, &data);
    pa_client_new_data_done(&data);

    if (!client->client)
        goto fail;

    pa_assert_se(pa_hashmap_put(u->session_clients, client->session, client) >= 0);

    return;

fail:
    if (client)
        session_client_free(client);
}

static void session_client_free(struct session_client *client) {
    pa_assert(client);

    pa_hashmap_remove(client->userdata->session_clients, client->session);

    if (client->client)
        pa_client_free(client->client);

    pa_xfree(client);
}

static pa_hook_result_t session_added_cb(void *hook_data, void *call_data, void *userdata) {
    pa_logind_session *session = call_data;
    struct userdata *u = userdata;

    pa_assert(session);
    pa_assert(u);

    session_client_new(u, session);

    return PA_HOOK_OK;
}

static pa_hook_result_t session_removed_cb(void *hook_data, void *call_data, void *userdata) {
    pa_logind_session *session = call_data;
    struct userdata *u = userdata;
    struct session_client *client;

    pa_assert(session);
    pa_assert(u);

    client = pa_hashmap_get(u->session_clients, session);
    if (client)
        session_client_free(client);

    return PA_HOOK_OK;
}

int pa__init(pa_module *m) {
    struct userdata *u = NULL;
    pa_modargs *ma;
    pa_logind_session *session;
    void *state;

    pa_assert(m);

    ma = pa_modargs_new(m->argument, valid_modargs);
    if (!ma) {
        pa_log("Failed to parse module arguments");
        goto fail;
    }

    m->userdata = u = pa_xnew0(struct userdata, 1);
    u->module = m;
    u->core = m->core;
    u->logind = pa_logind_get(m->core);
    u->session_clients = pa_hashmap_new(NULL, NULL);
    u->session_added_slot = pa_hook_connect(&u->logind->hooks[PA_LOGIND_HOOK_SESSION_ADDED], PA_HOOK_NORMAL,
                                            session_added_cb, u);
    u->session_removed_slot = pa_hook_connect(&u->logind->hooks[PA_LOGIND_HOOK_SESSION_REMOVED], PA_HOOK_NORMAL,
                                              session_removed_cb, u);

    PA_HASHMAP_FOREACH(session, u->logind->sessions, state)
        session_client_new(u, session);

    pa_modargs_free(ma);

    return 0;

fail:
    if (ma)
        pa_modargs_free(ma);

    pa__done(m);

    return -1;
}

void pa__done(pa_module *m) {
    struct userdata *u;

    pa_assert(m);

    u = m->userdata;
    if (!u)
        return;

    if (u->session_clients) {
        struct session_client *client;

        while ((client = pa_hashmap_first(u->session_clients)))
            session_client_free(client);
    }

    if (u->session_removed_slot)
        pa_hook_slot_free(u->session_removed_slot);

    if (u->session_added_slot)
        pa_hook_slot_free(u->session_added_slot);

    if (u->session_clients) {
        pa_assert(pa_hashmap_isempty(u->session_clients));
        pa_hashmap_free(u->session_clients);
    }

    if (u->logind)
        pa_logind_unref(u->logind);

    pa_xfree(u);
}
