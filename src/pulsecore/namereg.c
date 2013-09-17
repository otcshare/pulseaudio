/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering

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

#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <stdio.h>

#include <pulse/xmalloc.h>

#include <pulsecore/source.h>
#include <pulsecore/sink.h>
#include <pulsecore/core-subscribe.h>
#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>

#include "namereg.h"

struct namereg_entry {
    pa_namereg_type_t type;
    char *name;
    void *data;
};

static bool is_valid_char(char c) {
    return
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '.' ||
        c == '-' ||
        c == '_';
}

bool pa_namereg_is_valid_name(const char *name) {
    const char *c;

    pa_assert(name);

    if (*name == 0)
        return false;

    for (c = name; *c && (c-name < PA_NAME_MAX); c++)
        if (!is_valid_char(*c))
            return false;

    if (*c)
        return false;

    return true;
}

bool pa_namereg_is_valid_name_or_wildcard(const char *name, pa_namereg_type_t type) {

    pa_assert(name);

    if (pa_namereg_is_valid_name(name))
        return true;

    if (type == PA_NAMEREG_SINK &&
        pa_streq(name, "@DEFAULT_SINK@"))
        return true;

    if (type == PA_NAMEREG_SOURCE &&
        (pa_streq(name, "@DEFAULT_SOURCE@") ||
         pa_streq(name, "@DEFAULT_MONITOR@")))
        return true;

    return false;
}

char* pa_namereg_make_valid_name(const char *name) {
    const char *a;
    char *b, *n;

    if (*name == 0)
        return NULL;

    n = pa_xnew(char, strlen(name)+1);

    for (a = name, b = n; *a && (a-name < PA_NAME_MAX); a++, b++)
        *b = (char) (is_valid_char(*a) ? *a : '_');

    *b = 0;

    return n;
}

const char *pa_namereg_register(pa_core *c, const char *name, pa_namereg_type_t type, void *data, bool fail) {
    struct namereg_entry *e;
    char *n = NULL;

    pa_assert(c);
    pa_assert(name);
    pa_assert(data);

    if (!*name)
        return NULL;

    if ((type == PA_NAMEREG_SINK || type == PA_NAMEREG_SOURCE || type == PA_NAMEREG_CARD ||
         type == PA_NAMEREG_NODE || type == PA_NAMEREG_DOMAIN || type == PA_NAMEREG_ROUTING_GROUP) &&
        !pa_namereg_is_valid_name(name)) {

        if (fail)
            return NULL;

        if (!(name = n = pa_namereg_make_valid_name(name)))
            return NULL;
    }

    if ((e = pa_hashmap_get(c->namereg, name)) && fail) {
        pa_xfree(n);
        return NULL;
    }

    if (e) {
        unsigned i;
        size_t l = strlen(name);
        char *k;

        if (l+4 > PA_NAME_MAX) {
            pa_xfree(n);
            return NULL;
        }

        k = pa_xmalloc(l+4);

        for (i = 2; i <= 99; i++) {
            pa_snprintf(k, l+4, "%s.%u", name, i);

            if (!(e = pa_hashmap_get(c->namereg, k)))
                break;
        }

        if (e) {
            pa_xfree(n);
            pa_xfree(k);
            return NULL;
        }

        pa_xfree(n);
        n = k;
    }

    e = pa_xnew(struct namereg_entry, 1);
    e->type = type;
    e->name = n ? n : pa_xstrdup(name);
    e->data = data;

    pa_assert_se(pa_hashmap_put(c->namereg, e->name, e) >= 0);

    return e->name;
}

void pa_namereg_unregister(pa_core *c, const char *name) {
    struct namereg_entry *e;

    pa_assert(c);
    pa_assert(name);

    pa_assert_se(e = pa_hashmap_remove(c->namereg, name));

    /* pa_sink_unlink() calls pa_namereg_update_default_sink() before
     * unregistering the name. pa_namereg_update_default_sink() will then make
     * sure that the unlinked sink won't get assigned as the default sink, so
     * at the time pa_namereg_unregister() is called, the sink can't be the
     * default sink any more. The same is true for sources.*/
    pa_assert(e->data != c->default_sink);
    pa_assert(e->data != c->default_source);

    pa_xfree(e->name);
    pa_xfree(e);
}

void* pa_namereg_get(pa_core *c, const char *name, pa_namereg_type_t type) {
    struct namereg_entry *e;
    uint32_t idx;
    pa_assert(c);

    if (type == PA_NAMEREG_SOURCE && (!name || pa_streq(name, "@DEFAULT_SOURCE@"))) {
        pa_source *s;

        if ((s = pa_namereg_get_default_source(c)))
            return s;

    } else if (type == PA_NAMEREG_SINK && (!name || pa_streq(name, "@DEFAULT_SINK@"))) {
        pa_sink *s;

        if ((s = pa_namereg_get_default_sink(c)))
            return s;

    } else if (type == PA_NAMEREG_SOURCE && name && pa_streq(name, "@DEFAULT_MONITOR@")) {
        pa_sink *s;

        if ((s = pa_namereg_get(c, NULL, PA_NAMEREG_SINK)))
            return s->monitor_source;
    }

    if (!name)
        return NULL;

    if ((type == PA_NAMEREG_SINK || type == PA_NAMEREG_SOURCE || type == PA_NAMEREG_CARD) &&
        !pa_namereg_is_valid_name(name))
        return NULL;

    if ((e = pa_hashmap_get(c->namereg, name)))
        if (e->type == type)
            return e->data;

    if (pa_atou(name, &idx) < 0)
        return NULL;

    if (type == PA_NAMEREG_SINK)
        return pa_idxset_get_by_index(c->sinks, idx);
    else if (type == PA_NAMEREG_SOURCE)
        return pa_idxset_get_by_index(c->sources, idx);
    else if (type == PA_NAMEREG_SAMPLE && c->scache)
        return pa_idxset_get_by_index(c->scache, idx);
    else if (type == PA_NAMEREG_CARD)
        return pa_idxset_get_by_index(c->cards, idx);

    return NULL;
}

static bool sink_has_node(pa_sink *s) {
    pa_assert(s);

    return s->node || (s->active_port && s->active_port->node);
}

int pa_namereg_set_default_sink(pa_core*c, pa_sink *s, bool save) {
    pa_sink *old_default_sink;

    pa_assert(c);
    pa_assert(s || !save);

    if (s && !PA_SINK_IS_LINKED(pa_sink_get_state(s))) {
        pa_log("Tried to set the default sink to an unlinked sink: %s.", s->name);
        return -1;
    }

    if (s && !sink_has_node(s)) {
        pa_log("Tried to set the default sink to a sink without a node: %s.", s->name);
        return -1;
    }

    old_default_sink = c->default_sink;

    if (s == old_default_sink) {
        c->save_default_sink |= save;
        return 0;
    }

    c->default_sink = s;
    c->save_default_sink = save;

    pa_log_debug("Default sink changed from %s to %s.",
                 old_default_sink ? old_default_sink->name : "(none)", s ? s->name : "(none)");

    pa_hook_fire(&c->hooks[PA_CORE_HOOK_DEFAULT_SINK_CHANGED], NULL);
    pa_subscription_post(c, PA_SUBSCRIPTION_EVENT_SERVER|PA_SUBSCRIPTION_EVENT_CHANGE, PA_INVALID_INDEX);

    return 0;
}

static bool source_has_node(pa_source *s) {
    pa_assert(s);

    return s->node || (s->active_port && s->active_port->node);
}

int pa_namereg_set_default_source(pa_core*c, pa_source *s, bool save) {
    pa_source *old_default_source;

    pa_assert(c);
    pa_assert(s || !save);

    if (s && !PA_SOURCE_IS_LINKED(pa_source_get_state(s))) {
        pa_log_debug("Tried to set the default source to an unlinked source: %s.", s->name);
        return -1;
    }

    if (s && !source_has_node(s)) {
        pa_log_debug("Tried to set the default source to a source without a node: %s.", s->name);
        return -1;
    }

    old_default_source = c->default_source;

    if (s == old_default_source) {
        c->save_default_source |= save;
        return 0;
    }

    c->default_source = s;
    c->save_default_source = save;

    pa_log_debug("Default source changed from %s to %s.",
                 old_default_source ? old_default_source->name : "(none)", s ? s->name : "(none)");

    pa_hook_fire(&c->hooks[PA_CORE_HOOK_DEFAULT_SOURCE_CHANGED], NULL);
    pa_subscription_post(c, PA_SUBSCRIPTION_EVENT_SERVER|PA_SUBSCRIPTION_EVENT_CHANGE, PA_INVALID_INDEX);

    return 0;
}

pa_sink *pa_namereg_get_default_sink(pa_core *c) {
    pa_assert(c);

    return c->default_sink;
}

pa_source *pa_namereg_get_default_source(pa_core *c) {
    pa_assert(c);

    return c->default_source;
}

void pa_namereg_update_default_sink(pa_core *c) {
    pa_sink *s, *best = NULL;
    uint32_t idx;

    pa_assert(c);

    if (c->save_default_sink && PA_SINK_IS_LINKED(pa_sink_get_state(c->default_sink)))
        return;

    PA_IDXSET_FOREACH(s, c->sinks, idx)
        if (PA_SINK_IS_LINKED(pa_sink_get_state(s)) && sink_has_node(s))
            if (!best || s->priority > best->priority)
                best = s;

    pa_namereg_set_default_sink(c, best, false);
}

void pa_namereg_update_default_source(pa_core *c) {
    pa_source *s, *best = NULL;
    uint32_t idx;

    pa_assert(c);

    if (c->save_default_source && PA_SOURCE_IS_LINKED(pa_source_get_state(c->default_source)))
        return;

    /* First, try to find one that isn't a monitor. */
    PA_IDXSET_FOREACH(s, c->sources, idx)
        if (!s->monitor_of && PA_SOURCE_IS_LINKED(pa_source_get_state(s)) && source_has_node(s))
            if (!best || s->priority > best->priority)
                best = s;

    if (best) {
        pa_namereg_set_default_source(c, best, false);
        return;
    }

    /* Then, fall back to a monitor. */
    PA_IDXSET_FOREACH(s, c->sources, idx)
        if (PA_SOURCE_IS_LINKED(pa_source_get_state(s)) && source_has_node(s))
            if (!best
                    || s->priority > best->priority
                    || (s->priority == best->priority
                        && s->monitor_of && best->monitor_of
                        && s->monitor_of->priority > best->monitor_of->priority))
                best = s;

    pa_namereg_set_default_source(c, best, false);
}
