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

#include "main-volume-policy.h"

#include <modules/main-volume-policy/main-volume-context.h>

#include <pulsecore/core-util.h>
#include <pulsecore/namereg.h>
#include <pulsecore/shared.h>

static pa_main_volume_policy *main_volume_policy_new(pa_core *core);
static void main_volume_policy_free(pa_main_volume_policy *policy);

pa_main_volume_policy *pa_main_volume_policy_get(pa_core *core) {
    pa_main_volume_policy *policy;

    pa_assert(core);

    policy = pa_shared_get(core, "main-volume-policy");

    if (policy)
        pa_main_volume_policy_ref(policy);
    else {
        policy = main_volume_policy_new(core);
        pa_assert_se(pa_shared_set(core, "main-volume-policy", policy) >= 0);
    }

    return policy;
}

pa_main_volume_policy *pa_main_volume_policy_ref(pa_main_volume_policy *policy) {
    pa_assert(policy);

    policy->refcnt++;

    return policy;
}

void pa_main_volume_policy_unref(pa_main_volume_policy *policy) {
    pa_assert(policy);
    pa_assert(policy->refcnt > 0);

    policy->refcnt--;

    if (policy->refcnt == 0) {
        pa_assert_se(pa_shared_remove(policy->core, "main-volume-policy") >= 0);
        main_volume_policy_free(policy);
    }
}

static pa_hook_result_t volume_control_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    pa_main_volume_policy *policy = userdata;
    pa_volume_control *control = call_data;
    pa_main_volume_context *context;
    void *state;

    pa_assert(policy);
    pa_assert(control);

    PA_HASHMAP_FOREACH(context, policy->main_volume_contexts, state) {
        if (context->main_output_volume_control == control)
            pa_main_volume_context_set_main_output_volume_control(context, NULL);

        if (context->main_input_volume_control == control)
            pa_main_volume_context_set_main_input_volume_control(context, NULL);
    }

    return PA_HOOK_OK;
}

static pa_hook_result_t mute_control_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    pa_main_volume_policy *policy = userdata;
    pa_mute_control *control = call_data;
    pa_main_volume_context *context;
    void *state;

    pa_assert(policy);
    pa_assert(control);

    PA_HASHMAP_FOREACH(context, policy->main_volume_contexts, state) {
        if (context->main_output_mute_control == control)
            pa_main_volume_context_set_main_output_mute_control(context, NULL);

        if (context->main_input_mute_control == control)
            pa_main_volume_context_set_main_input_mute_control(context, NULL);
    }

    return PA_HOOK_OK;
}

static pa_main_volume_policy *main_volume_policy_new(pa_core *core) {
    pa_main_volume_policy *policy;
    unsigned i;

    pa_assert(core);

    policy = pa_xnew0(pa_main_volume_policy, 1);
    policy->core = core;
    policy->refcnt = 1;
    policy->volume_api = pa_volume_api_get(core);
    policy->names = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL, pa_xfree);
    policy->main_volume_contexts = pa_hashmap_new(pa_idxset_string_hash_func, pa_idxset_string_compare_func);

    for (i = 0; i < PA_MAIN_VOLUME_POLICY_HOOK_MAX; i++)
        pa_hook_init(&policy->hooks[i], policy);

    policy->volume_control_unlink_slot = pa_hook_connect(&policy->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_UNLINK],
                                                         PA_HOOK_NORMAL, volume_control_unlink_cb, policy);
    policy->mute_control_unlink_slot = pa_hook_connect(&policy->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_UNLINK],
                                                       PA_HOOK_NORMAL, mute_control_unlink_cb, policy);

    pa_log_debug("Created a pa_main_volume_policy object.");

    return policy;
}

static void main_volume_policy_free(pa_main_volume_policy *policy) {
    unsigned i;

    pa_assert(policy);
    pa_assert(policy->refcnt == 0);

    pa_log_debug("Freeing the pa_main_volume_policy object.");

    if (policy->mute_control_unlink_slot)
        pa_hook_slot_free(policy->mute_control_unlink_slot);

    if (policy->volume_control_unlink_slot)
        pa_hook_slot_free(policy->volume_control_unlink_slot);

    for (i = 0; i < PA_MAIN_VOLUME_POLICY_HOOK_MAX; i++)
        pa_hook_done(&policy->hooks[i]);

    if (policy->main_volume_contexts) {
        pa_assert(pa_hashmap_isempty(policy->main_volume_contexts));
        pa_hashmap_free(policy->main_volume_contexts);
    }

    if (policy->names) {
        pa_assert(pa_hashmap_isempty(policy->names));
        pa_hashmap_free(policy->names);
    }

    if (policy->volume_api)
        pa_volume_api_unref(policy->volume_api);

    pa_xfree(policy);
}

int pa_main_volume_policy_register_name(pa_main_volume_policy *policy, const char *requested_name,
                                        bool fail_if_already_registered, const char **registered_name) {
    char *n;

    pa_assert(policy);
    pa_assert(requested_name);
    pa_assert(registered_name);

    if (!pa_namereg_is_valid_name(requested_name)) {
        pa_log("Invalid name: \"%s\"", requested_name);
        return -PA_ERR_INVALID;
    }

    n = pa_xstrdup(requested_name);

    if (pa_hashmap_put(policy->names, n, n) < 0) {
        unsigned i = 1;

        if (fail_if_already_registered) {
            pa_xfree(n);
            pa_log("Name %s already registered.", requested_name);
            return -PA_ERR_EXIST;
        }

        do {
            pa_xfree(n);
            i++;
            n = pa_sprintf_malloc("%s.%u", requested_name, i);
        } while (pa_hashmap_put(policy->names, n, n) < 0);
    }

    *registered_name = n;

    return 0;
}

void pa_main_volume_policy_unregister_name(pa_main_volume_policy *policy, const char *name) {
    pa_assert(policy);
    pa_assert(name);

    pa_assert_se(pa_hashmap_remove_and_free(policy->names, name) >= 0);
}

uint32_t pa_main_volume_policy_allocate_main_volume_context_index(pa_main_volume_policy *policy) {
    uint32_t idx;

    pa_assert(policy);

    idx = policy->next_main_volume_context_index++;

    return idx;
}

void pa_main_volume_policy_add_main_volume_context(pa_main_volume_policy *policy, pa_main_volume_context *context) {
    pa_assert(policy);
    pa_assert(context);

    pa_assert_se(pa_hashmap_put(policy->main_volume_contexts, (void *) context->name, context) >= 0);
}

int pa_main_volume_policy_remove_main_volume_context(pa_main_volume_policy *policy, pa_main_volume_context *context) {
    pa_assert(policy);
    pa_assert(context);

    if (!pa_hashmap_remove(policy->main_volume_contexts, context->name))
        return -1;

    if (context == policy->active_main_volume_context)
        pa_main_volume_policy_set_active_main_volume_context(policy, NULL);

    return 0;
}

void pa_main_volume_policy_set_active_main_volume_context(pa_main_volume_policy *policy, pa_main_volume_context *context) {
    pa_main_volume_context *old_context;

    pa_assert(policy);

    old_context = policy->active_main_volume_context;

    if (context == old_context)
        return;

    policy->active_main_volume_context = context;

    pa_log_debug("The active main volume context changed from %s to %s.", old_context ? old_context->name : "(unset)",
                 context ? context->name : "(unset)");

    pa_hook_fire(&policy->hooks[PA_MAIN_VOLUME_POLICY_HOOK_ACTIVE_MAIN_VOLUME_CONTEXT_CHANGED], NULL);
}
