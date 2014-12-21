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
#include <pulsecore/shared.h>
#include <pulsecore/strlist.h>
#include <pulsecore/idxset.h>
#include <pulsecore/hashmap.h>

#include "module-expose-environment-symdef.h"

PA_MODULE_AUTHOR("Krisztian Litkey");
PA_MODULE_DESCRIPTION("Expose the environment variables of local clients.");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);
PA_MODULE_USAGE("[variables={*|<var1 ... varN>}]");

#define PROP_KEY_LEN 256
#define VARIABLES_WILDCARD "*"
#define VARIABLES_ALL ((pa_strlist *)NULL)

/* possible module arguments */
static const char *const valid_modargs[] = {
    "variables",
};

/* plugin userdata */
struct userdata {
    uint32_t index;
    pa_strlist *variables;
    pa_hashmap *cache;
    pa_hook_slot *client_unlink;
};

/* structure for reading and looping through the environment */
struct proc_env {
    char buf[16 * 1024];
    int size;
    char *p;
};

static uint32_t module_index = PA_IDXSET_INVALID;

/* read the environment for the given process */
static int proc_env_read(struct proc_env *e, pid_t pid) {
    char path[PATH_MAX];
    int fd;

    e->buf[0] = '\0';
    e->size = 0;
    e->p = NULL;

    if (snprintf(path, sizeof(path), "/proc/%u/environ", pid) >= (ssize_t)sizeof(path))
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
static char *proc_env_foreach(struct proc_env *e, char *key, size_t size, size_t *klenp) {
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
static const char *expose_client_variables(pid_t pid, pa_strlist *variables, pa_hashmap *env, const char *find) {
    struct proc_env e;
    const char *var;
    char key[PROP_KEY_LEN], *val, *v, *guard, *found;

    pa_log_debug("exporting environment for client %u...", pid);

    if (proc_env_read(&e, pid) < 0)
        return NULL;

    /*
     * Notes:
     *   This search algorithm is designed to run O(n) if the order of
     *   variables in your configuration matches the order of variables
     *   of interest in the environment...
     */

    found = NULL;

    if (variables == VARIABLES_ALL) {
        guard = NULL;

        while ((val = proc_env_foreach(&e, key, sizeof(key), NULL)) != NULL) {
            pa_log_debug("exporting %s=%s", key, val);
            pa_hashmap_put(env, pa_xstrdup(key), v = pa_xstrdup(val ? val : ""));

            if (find && !found && pa_streq(find, key))
                found = v;

            if (!guard)
                guard = val;
            else
                if (guard == val)
                    break;
        }
    }
    else {
        while (variables) {
            guard = NULL;

            var = pa_strlist_data(variables);
            v = NULL;

            while ((val = proc_env_foreach(&e, key, sizeof(key), NULL)) != NULL) {
                if (pa_streq(key, var)) {
                    pa_log_debug("exporting %s=%s", key, val);
                    pa_hashmap_put(env, pa_xstrdup(key), v = pa_xstrdup(val));
                    break;
                }

                if (!guard)
                    guard = val;
                else if (guard == val)
                    break;
            }

            if (find && !found && pa_streq(find, key))
                found = v;

            variables = pa_strlist_next(variables);
        }
    }

    return found;
}

/* client unlink hook */
static pa_hook_result_t client_unlink_cb(pa_core *core, pa_client *c, struct userdata *u) {
    pa_core_assert_ref(core);
    pa_assert(c);
    pa_assert(u);

    pa_hashmap_remove_and_free(u->cache, (void *)(ptrdiff_t)c->index);

    return PA_HOOK_OK;
}

/* cache the environment for the given process and look up the value of name */
static const char *cache_env(struct userdata *u, pa_client *c, const char *name) {
    pid_t pid;
    pa_hashmap *h;

    if (!(pid = pa_client_pid(c)))
        return NULL;

    pa_log_debug("populating cache for client #%u", c->index);

    h = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, pa_xfree, pa_xfree);
    pa_hashmap_put(u->cache, (void *)(ptrdiff_t)c->index, h);

    return expose_client_variables(pid, u->variables, h, name);
}

/* callback from pa_client_getenv */
static const char *client_getenv(pa_client *c, const char *name) {
    pa_core *core;
    pa_module *m;
    pa_hashmap *env;
    struct userdata *u;

    pa_assert(c);
    pa_assert(module_index != PA_IDXSET_INVALID);

    pa_log_debug("looking for variable '%s' in environment of client #%u", name, c->index);

    core = c->core;
    m = pa_idxset_get_by_index(core->modules, module_index);

    pa_assert_se(m && (u = m->userdata));

    env = pa_hashmap_get(u->cache, (void *)(ptrdiff_t)c->index);

    if (env)
        return pa_hashmap_get(env, name);
    else
        return cache_env(u, c, name);
}

int pa__init(pa_module *m) {
    pa_modargs *ma;
    const char *variables;
    struct userdata *u;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("failed to parse module arguments");
        goto fail;
    }

    m->userdata = u = pa_xnew0(struct userdata, 1);

    variables = pa_modargs_get_value(ma, "variables", VARIABLES_WILDCARD);

    pa_log_debug("environment variables to export: '%s'", variables);

    if (pa_streq(variables, VARIABLES_WILDCARD))
        u->variables = VARIABLES_ALL;
    else
        u->variables = pa_strlist_parse(variables);

    u->cache = pa_hashmap_new_full(pa_idxset_trivial_hash_func, pa_idxset_trivial_compare_func,
                                   NULL, (void (*)(void *))pa_hashmap_free);

    u->client_unlink = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_CLIENT_UNLINK], PA_HOOK_EARLY,
                                       (pa_hook_cb_t)client_unlink_cb, u);

    module_index = u->index = m->index;
    m->core->client_getenv = client_getenv;

    pa_modargs_free(ma);

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

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->variables && u->variables != VARIABLES_ALL)
        pa_strlist_free(u->variables);

    if (u->cache)
        pa_hashmap_free(u->cache);

    if (u->client_unlink)
        pa_hook_slot_free(u->client_unlink);

    pa_xfree(u);

    m->core->client_getenv = NULL;
    module_index = PA_IDXSET_INVALID;
}
