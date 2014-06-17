#ifndef foomainvolumepolicyhfoo
#define foomainvolumepolicyhfoo

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

#include <modules/volume-api/volume-api.h>

#include <pulsecore/core.h>

typedef struct pa_main_volume_policy pa_main_volume_policy;

/* Avoid circular dependencies... */
typedef struct pa_main_volume_context pa_main_volume_context;

enum {
    PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_PUT,
    PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_UNLINK,
    PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_DESCRIPTION_CHANGED,
    PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_MAIN_OUTPUT_VOLUME_CONTROL_CHANGED,
    PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_MAIN_INPUT_VOLUME_CONTROL_CHANGED,
    PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_MAIN_OUTPUT_MUTE_CONTROL_CHANGED,
    PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_MAIN_INPUT_MUTE_CONTROL_CHANGED,
    PA_MAIN_VOLUME_POLICY_HOOK_ACTIVE_MAIN_VOLUME_CONTEXT_CHANGED,
    PA_MAIN_VOLUME_POLICY_HOOK_MAX,
};

struct pa_main_volume_policy {
    pa_core *core;
    unsigned refcnt;
    pa_volume_api *volume_api;
    pa_hashmap *names; /* object name -> object name (hashmap-as-a-set) */
    pa_hashmap *main_volume_contexts; /* name -> pa_main_volume_context */
    pa_main_volume_context *active_main_volume_context;

    uint32_t next_main_volume_context_index;
    pa_hook hooks[PA_MAIN_VOLUME_POLICY_HOOK_MAX];

    pa_hook_slot *volume_control_unlink_slot;
    pa_hook_slot *mute_control_unlink_slot;
};

pa_main_volume_policy *pa_main_volume_policy_get(pa_core *core);
pa_main_volume_policy *pa_main_volume_policy_ref(pa_main_volume_policy *policy);
void pa_main_volume_policy_unref(pa_main_volume_policy *policy);

int pa_main_volume_policy_register_name(pa_main_volume_policy *policy, const char *requested_name,
                                        bool fail_if_already_registered, const char **registered_name);
void pa_main_volume_policy_unregister_name(pa_main_volume_policy *policy, const char *name);

uint32_t pa_main_volume_policy_allocate_main_volume_context_index(pa_main_volume_policy *policy);
void pa_main_volume_policy_add_main_volume_context(pa_main_volume_policy *policy, pa_main_volume_context *context);
int pa_main_volume_policy_remove_main_volume_context(pa_main_volume_policy *policy, pa_main_volume_context *context);
void pa_main_volume_policy_set_active_main_volume_context(pa_main_volume_policy *policy, pa_main_volume_context *context);

#endif
