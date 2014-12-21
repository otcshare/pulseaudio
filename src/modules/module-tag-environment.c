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
#include <pulsecore/sink-input.h>
#include <pulsecore/core-util.h>

#include "module-tag-environment-symdef.h"

PA_MODULE_AUTHOR("Krisztian Litkey");
PA_MODULE_DESCRIPTION("Expose a selected set of client environment variables as properties of the client.");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(false);
PA_MODULE_USAGE("variables=<var1[:prop1],...,varN[:propN]> [prefix=<prefix>]");

#define PROP_PID PA_PROP_APPLICATION_PROCESS_ID
#define PROP_NATIVE_PEER "native-protocol.peer"
#define UNIX_CLIENT "UNIX socket client"
#define TCP_CLIENT "TCP/IP client from "
#define PROP_KEY_LEN 512

/*
 * module configuration:
 *
 * - prefix: (default) proplist prefix for exported environment variables
 * - variables: variables to export
 *
 * The variable configuration string has the following syntax:
 *
 *     name1[:[.]prop1],...,nameN[:[.]propN], where
 *
 * name1...nameN are the environment variables to export, and
 * prop1...propN are the property names to use for these.
 *
 * If a property name does not start with a '.' it will be prefixed
 * with the common default prefix. Otherwise the property name will
 * be used verbatim without the leading dot. If property is omitted
 * it defaults to the name of the environment variable (prefixed
 * with the default prefix). For instance,
 *
 *     variables=HOME:.user.home,SHELL:.user.shell,HOSTNAME
 *
 * will set the following properties on the client if the corresponding
 * environment variables are set:
 *
 *     user.home=$HOME,
 *     user.shell=$SHELL,
 *     application.process.environment.HOSTNAME=$HOSTNAME
 */

/* possible module arguments */
static const char *const valid_modargs[] = {
    "prefix",
    "properties",
};

/* environment variable-property mapping */
struct envvar {
    char *name;
    char *prop;
};

/* plugin userdata */
struct userdata {
    char *prefix;
    int prefix_len;
    struct envvar *variables;
    pa_hook_slot *new_stream;
    pa_hook_slot *proplist_changed;
};

/* structure for reading and looping through the environment */
struct proc_env {
    char buf[16 * 1024];
    int size;
    char *p;
};

/* parse a single variable configuration (var[:[.]name) */
static int parse_variable(size_t prefix_len, const char *var, size_t vlen, const char **namep, size_t *nlenp,
                          const char **propp, size_t *plenp, char *buf, size_t bufsize) {
    const char *p;
    int nlen, plen;

    if (!(p = strchr(var, ':')) || (p - var) >= (int)vlen) {
        nlen = vlen;
        plen = vlen;

        if (plen > (int)bufsize) {
        overflow:
            *namep = NULL;
            *propp = NULL;
            return -1;
        }

        strncpy(buf + prefix_len, var, plen);
        buf[prefix_len + plen] = '\0';

        *namep = var;
        *nlenp = nlen;
        *propp = buf;
        *plenp = prefix_len + plen;
    }
    else {
        nlen = p++ - var;
        plen = vlen - nlen - 1;

        if (plen > (int)bufsize)
            goto overflow;

        *namep = var;
        *nlenp = nlen;

        if (p[0] == '.') {
            *propp = p + 1;
            *plenp = plen - 1;
        }
        else {
            strncpy(buf + prefix_len, p, plen);
            buf[prefix_len + plen] = '\0';
            *propp = buf;
            *plenp = prefix_len + plen;
        }
    }

    return 0;
}

/* parse the configured set of environment variables */
static int parse_variables(struct userdata *u, const char *variables) {
    char propbuf[PROP_KEY_LEN];
    struct envvar *vars;
    const char *v, *next, *end, *prop, *name;
    size_t nlen, plen;
    int n, l, prefix_len;

    vars = u->variables = NULL;

    if (!variables || !*variables)
        return 0;

    /* prefill prop buffer with prefix if we have one */
    if (u->prefix_len > 0)
        prefix_len = snprintf(propbuf, sizeof(propbuf), "%s.", u->prefix);
    else
        prefix_len = 0;

    n = 0;
    v = variables;

    /* loop through configuration (var1[:[.]name1],...,varN[:[.]nameN]) */
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

        vars = pa_xrealloc(vars, sizeof(*vars) * (n + 1));

        if (parse_variable(prefix_len, v, l, &name, &nlen, &prop, &plen, propbuf, sizeof(propbuf)) < 0) {
            vars[n].name = vars[n].prop = NULL;
            return -1;
        }

        vars[n].name = pa_xstrndup(name, nlen);
        vars[n].prop = pa_xstrndup(prop, plen);

        pa_log_debug("tag stream with environment variable '%s' as '%s'", vars[n].name, vars[n].prop);

        n++;

        v = next ? next + 1 : NULL;
    }

    vars = pa_xrealloc(vars, sizeof(*vars) * (n + 1));
    vars[n].name = vars[n].prop = NULL;

    u->variables = vars;

    return 0;
}

/* tag the stream with a selection of variables from the environment of the owning client */
static void tag_client_stream(pa_sink_input *stream, struct userdata *u)
{
    pa_client *c = stream->client;
    struct envvar *var;
    const char *val;

    pa_log_debug("tagging stream #%u with environment from client #%u", stream->index, c->index);

    for (var = u->variables; var->name; var++) {
        if ((val = pa_client_getenv(c, var->name)))
            pa_proplist_sets(stream->proplist, var->prop, val);
    }
}

static int new_stream(pa_core *core, pa_sink_input *stream, struct userdata *u) {
    if (stream->client != NULL)
        tag_client_stream(stream, u);

    return PA_HOOK_OK;
}

int pa__init(pa_module *m)
{
    pa_modargs *ma = NULL;
    const char *variables, *prefix;
    struct userdata *u;

    pa_assert(m);

    if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
        pa_log("failed to parse module arguments");
        goto fail;
    }

    variables = pa_modargs_get_value(ma, "properties", NULL);
    prefix = pa_modargs_get_value(ma, "prefix", "application.process.environment");

    if (!variables || !*variables)
        goto done;

    m->userdata = u = pa_xnew0(struct userdata, 1);

    u->prefix = pa_xstrdup(prefix);
    u->prefix_len = strlen(u->prefix);

    if (u->prefix_len > PROP_KEY_LEN / 2)
        goto fail;

    if (parse_variables(u, variables) < 0)
        goto fail;

    if (u->variables == NULL)
        goto done;

    u->new_stream = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_PUT], PA_HOOK_EARLY,
                                    (pa_hook_cb_t)new_stream, u);

 done:
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
    struct envvar *v;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    pa_xfree(u->prefix);

    if (u->variables) {
        for (v = u->variables; v->name; v++) {
            pa_xfree(v->name);
            pa_xfree(v->prop);
        }

        pa_xfree(u->variables);
    }

    if (u->new_stream)
        pa_hook_slot_free(u->new_stream);

    pa_xfree(u);
}
