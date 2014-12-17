/***
  This file is part of PulseAudio.

  Copyright 2014 Krisztian Litkey

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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <time.h>

#include <pulse/xmalloc.h>

#include <pulsecore/module.h>
#include <pulsecore/modargs.h>
#include <pulsecore/log.h>
#include <pulsecore/client.h>
#include <pulsecore/core-util.h>

#include "module-expose-environment-symdef.h"

PA_MODULE_AUTHOR("Krisztian Litkey");
PA_MODULE_DESCRIPTION("Expose a selected set of client environment variables as properties of the client, prefixed with a common unique prefix.");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);
PA_MODULE_USAGE("variables=<var1,var2,...,varN>");

#define PROP_PID_KEY PA_PROP_APPLICATION_PROCESS_ID
#define PROP_NATIVE_PEER "native-protocol.peer"
#define UNIX_CLIENT_VALUE "UNIX socket client"


/* possible module arguments */
static const char *const valid_modargs[] = {
    "variables"
};

/* plugin userdata */
struct userdata {
    char **variables;
    pa_hook_slot *client_new;
    pa_hook_slot *proplist_changed;
};

/* structure for reading and looping through the environment */
struct proc_env {
    char buf[16 * 1024];
    int size;
    char *p;
};

/* parse the configure set of environment variable names */
static int parse_variables(struct userdata *u, const char *config)
{
    char **variables;
    const char *v, *next, *end;
    int n, l;

    variables = u->variables = NULL;

    if (!config || !*config)
        return 0;

    n = 0;
    v = config;

    while (v && *v) {
        while (*v == ',' || *v == ' ' || *v == '\t')
            v++;

        next = strchr(v, ',');

        if (next != NULL) {
            end = next - 1;
            while (end > v && (*end == ' ' || *end == '\t'))
                end--;

            l = end - v + 1;
        }
        else
            l = strlen(v);

        variables = pa_xrealloc(variables, sizeof(char *) * (n + 1));
        v = variables[n++] = pa_xstrndup(v, l);
        pa_log("Registered client environment variable '%s'.", v);

        v = next ? next + 1 : NULL;
    }

    variables = pa_xrealloc(variables, sizeof(char *) * (n + 1));
    variables[n] = NULL;

    u->variables = variables;

    return 0;
}

/* read the environment for the given process */
static int proc_env_read(struct proc_env *e, const char *pid)
{
    char path[PATH_MAX];
    int fd;

    e->buf[0] = '\0';
    e->size = 0;
    e->p = NULL;

    if (snprintf(path, sizeof(path),
                 "/proc/%s/environ", pid) >= (ssize_t)sizeof(path))
        return -1;

    if ((fd = open(path, O_RDONLY)) < 0)
        return -1;
    e->size = read(fd, e->buf, sizeof(e->buf) - 1);
    close(fd);

    if (e->size < 0)
        return -1;

    e->buf[e->size] = '\0';
    e->p = e->buf;

    return 0;
}

/* loop through the given environment */
static char *proc_env_foreach(struct proc_env *e, char *key, size_t size,
                              size_t *klenp)
{
    char *k, *v;
    size_t klen, vlen;

    k = e->p;
    v = strchr(k, '=');

    if (!v)
        return NULL;

    klen = v++ - k;
    vlen = strlen(v);

    if (klen > size - 1)
        return NULL;

    strncpy(key, k, klen);
    key[klen] = '\0';

    if ((e->p = v + vlen + 1) >= e->buf + e->size)
        e->p = e->buf;

    if (klenp != NULL)
        *klenp = klen;

    return v;
}

/* export the given environment variables for the given process */
static void export_client_variables(const char *pid, char **names,
                                    pa_proplist *proplist)
{
    char key[512] = PA_PROP_APPLICATION_PROCESS_ENVIRONMENT".";
    struct proc_env e;
    char *val, *guard, *k;
    size_t offs, max, len;
    int i;

    if (proc_env_read(&e, pid) < 0)
        return;

    offs = sizeof(PA_PROP_APPLICATION_PROCESS_ENVIRONMENT);
    k = key + offs;
    max = sizeof(key) - offs - 1;

    for (i = 0; names[i]; i++) {
        guard = NULL;

        while ((val = proc_env_foreach(&e, k, max, &len)) != NULL) {
            if (!strcmp(k, names[i])) {
                pa_proplist_sets(proplist, key, val);
                break;
            }

            if (!guard)
                guard = val;
            else if (guard == val)
                break;
        }
    }
}


static bool looks_local_client(pa_proplist *proplist)
{
    const char *peer;

    /*
     * Notes: XXX TODO
     *   Yes, this is an insufficient kludge, at least we should detect
     *   and handle local clients that come over TCP as local ones as
     *   well.
     *
     *   For detecting the client socket type I could not find any other
     *   way then this. There seems to be no API for querying it.
     */

    if (!proplist)
        return false;

    if (!(peer = pa_proplist_gets(proplist, PROP_NATIVE_PEER)))
        return false;

    return !strcmp(peer, UNIX_CLIENT_VALUE) ? true : false;
}


/* new client creation hook */
static pa_hook_result_t client_cb(pa_core *core, pa_client_new_data *data,
                                  struct userdata *u)
{
    const char *pid;

    pa_core_assert_ref(core);
    pa_assert(data);
    pa_assert(u);

    if (!looks_local_client(data->proplist))
        return PA_HOOK_OK;

    if (!(pid = pa_proplist_gets(data->proplist, PROP_PID_KEY)))
        return PA_HOOK_OK;

    export_client_variables(pid, u->variables, data->proplist);

    return PA_HOOK_OK;
}

/* property list change hook */
static pa_hook_result_t proplist_cb(pa_core *core, pa_client *client,
                                    struct userdata *u)
{
    const char *pid;

    /*
     * Note:
     *   For trustability, it would be better to collect all the key-value
     *   pairs to our userdata on a per client basis and then always clear
     *   and re-set all values whenever the proplist changes. Maybe in the
     *   next version...
     */

    pa_core_assert_ref(core);
    pa_assert(client);
    pa_assert(u);

    if (!looks_local_client(client->proplist))
        return PA_HOOK_OK;

    if (!(pid = pa_proplist_gets(client->proplist, PROP_PID_KEY)))
        return PA_HOOK_OK;

    export_client_variables(pid, u->variables, client->proplist);

    return PA_HOOK_OK;
}


int pa__init(pa_module *m)
{
    pa_modargs *ma = NULL;
    const char *config;
    struct userdata *u;
    pa_hook *hook;
    int type;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments.");
        goto fail;
    }

    config = pa_modargs_get_value(ma, "variables", NULL);

    if (!config || !*config)
        return 0;

    m->userdata = u = pa_xnew0(struct userdata, 1);

    if (parse_variables(u, config) < 0)
        goto fail;

    if (u->variables == NULL)
        return 0;

    hook = &m->core->hooks[PA_CORE_HOOK_CLIENT_NEW];
    type = PA_HOOK_EARLY;
    u->client_new = pa_hook_connect(hook, type, (pa_hook_cb_t)client_cb, u);

    hook = &m->core->hooks[PA_CORE_HOOK_CLIENT_PROPLIST_CHANGED];
    type = PA_HOOK_EARLY;
    u->proplist_changed = pa_hook_connect(hook, type,
                                          (pa_hook_cb_t)proplist_cb, u);

    return 0;

 fail:
    pa__done(m);

    if (ma)
        pa_modargs_free(ma);

    return -1;
}


void pa__done(pa_module *m)
{
    struct userdata *u;
    int i;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->client_new)
        pa_hook_slot_free(u->client_new);
    if (u->proplist_changed)
        pa_hook_slot_free(u->proplist_changed);

    if (u->variables) {
        for (i = 0; u->variables[i]; i++)
            pa_xfree(u->variables[i]);

        pa_xfree(u->variables);
    }

    pa_xfree(u);
}
