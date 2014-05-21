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

#include "main-volume-context.h"

#include <modules/volume-api/mute-control.h>
#include <modules/volume-api/volume-control.h>

int pa_main_volume_context_new(pa_main_volume_policy *policy, const char *name, const char *description,
                               pa_main_volume_context **context) {
    pa_main_volume_context *context_local;
    int r;

    pa_assert(policy);
    pa_assert(name);
    pa_assert(description);
    pa_assert(context);

    context_local = pa_xnew0(struct pa_main_volume_context, 1);
    context_local->main_volume_policy = policy;
    context_local->index = pa_main_volume_policy_allocate_main_volume_context_index(policy);

    r = pa_main_volume_policy_register_name(policy, name, true, &context_local->name);
    if (r < 0)
        goto fail;

    context_local->description = pa_xstrdup(description);

    *context = context_local;

    return 0;

fail:
    pa_main_volume_context_free(context_local);

    return r;
}

void pa_main_volume_context_put(pa_main_volume_context *context) {
    pa_assert(context);

    pa_main_volume_policy_add_main_volume_context(context->main_volume_policy, context);

    context->linked = true;

    pa_log_debug("Created main volume context #%u.", context->index);
    pa_log_debug("    Name: %s", context->name);
    pa_log_debug("    Description: %s", context->description);
    pa_log_debug("    Main output volume control: %s",
                 context->main_output_volume_control ? context->main_output_volume_control->name : "(unset)");
    pa_log_debug("    Main input volume control: %s",
                 context->main_input_volume_control ? context->main_input_volume_control->name : "(unset)");
    pa_log_debug("    Main output mute control: %s",
                 context->main_output_mute_control ? context->main_output_mute_control->name : "(unset)");
    pa_log_debug("    Main input mute control: %s",
                 context->main_input_mute_control ? context->main_input_mute_control->name : "(unset)");

    pa_hook_fire(&context->main_volume_policy->hooks[PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_PUT], context);
}

void pa_main_volume_context_unlink(pa_main_volume_context *context) {
    pa_assert(context);

    if (context->unlinked) {
        pa_log_debug("Unlinking main volume context %s (already unlinked, this is a no-op).", context->name);
        return;
    }

    context->unlinked = true;

    pa_log_debug("Unlinking main volume context %s.", context->name);

    if (context->linked)
        pa_hook_fire(&context->main_volume_policy->hooks[PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_UNLINK], context);

    if (context->main_input_mute_control_binding) {
        pa_binding_free(context->main_input_mute_control_binding);
        context->main_input_mute_control_binding = NULL;
    }

    if (context->main_output_mute_control_binding) {
        pa_binding_free(context->main_output_mute_control_binding);
        context->main_output_mute_control_binding = NULL;
    }

    if (context->main_input_volume_control_binding) {
        pa_binding_free(context->main_input_volume_control_binding);
        context->main_input_volume_control_binding = NULL;
    }

    if (context->main_output_volume_control_binding) {
        pa_binding_free(context->main_output_volume_control_binding);
        context->main_output_volume_control_binding = NULL;
    }

    context->main_input_mute_control = NULL;
    context->main_output_mute_control = NULL;
    context->main_input_volume_control = NULL;
    context->main_output_volume_control = NULL;

    pa_main_volume_policy_remove_main_volume_context(context->main_volume_policy, context);
}

void pa_main_volume_context_free(pa_main_volume_context *context) {
    pa_assert(context);

    if (!context->unlinked)
        pa_main_volume_context_unlink(context);

    pa_xfree(context->description);

    if (context->name)
        pa_main_volume_policy_unregister_name(context->main_volume_policy, context->name);

    pa_xfree(context);
}

const char *pa_main_volume_context_get_name(pa_main_volume_context *context) {
    pa_assert(context);

    return context->name;
}

static void set_main_output_volume_control_internal(pa_main_volume_context *context, pa_volume_control *control) {
    pa_volume_control *old_control;

    pa_assert(context);

    old_control = context->main_output_volume_control;

    if (control == old_control)
        return;

    context->main_output_volume_control = control;

    if (!context->linked || context->unlinked)
        return;

    pa_log_debug("The main output volume control of main volume context %s changed from %s to %s.", context->name,
                 old_control ? old_control->name : "(unset)", control ? control->name : "(unset)");

    pa_hook_fire(&context->main_volume_policy->hooks
                     [PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_MAIN_OUTPUT_VOLUME_CONTROL_CHANGED],
                 context);
}

void pa_main_volume_context_bind_main_output_volume_control(pa_main_volume_context *context,
                                                            pa_binding_target_info *target_info) {
    pa_binding_owner_info owner_info = {
        .userdata = context,
        .set_value = (pa_binding_set_value_cb_t) set_main_output_volume_control_internal,
    };

    pa_assert(context);
    pa_assert(target_info);

    if (context->main_output_volume_control_binding)
        pa_binding_free(context->main_output_volume_control_binding);

    context->main_output_volume_control_binding = pa_binding_new(context->main_volume_policy->volume_api, &owner_info,
                                                                 target_info);
}

static void set_main_input_volume_control_internal(pa_main_volume_context *context, pa_volume_control *control) {
    pa_volume_control *old_control;

    pa_assert(context);

    old_control = context->main_input_volume_control;

    if (control == old_control)
        return;

    context->main_input_volume_control = control;

    if (!context->linked || context->unlinked)
        return;

    pa_log_debug("The main input volume control of main volume context %s changed from %s to %s.", context->name,
                 old_control ? old_control->name : "(unset)", control ? control->name : "(unset)");

    pa_hook_fire(&context->main_volume_policy->hooks
                     [PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_MAIN_INPUT_VOLUME_CONTROL_CHANGED],
                 context);
}

void pa_main_volume_context_bind_main_input_volume_control(pa_main_volume_context *context,
                                                           pa_binding_target_info *target_info) {
    pa_binding_owner_info owner_info = {
        .userdata = context,
        .set_value = (pa_binding_set_value_cb_t) set_main_input_volume_control_internal,
    };

    pa_assert(context);
    pa_assert(target_info);

    if (context->main_input_volume_control_binding)
        pa_binding_free(context->main_input_volume_control_binding);

    context->main_input_volume_control_binding = pa_binding_new(context->main_volume_policy->volume_api, &owner_info,
                                                                target_info);
}

static void set_main_output_mute_control_internal(pa_main_volume_context *context, pa_mute_control *control) {
    pa_mute_control *old_control;

    pa_assert(context);

    old_control = context->main_output_mute_control;

    if (control == old_control)
        return;

    context->main_output_mute_control = control;

    if (!context->linked || context->unlinked)
        return;

    pa_log_debug("The main output mute control of main volume context %s changed from %s to %s.", context->name,
                 old_control ? old_control->name : "(unset)", control ? control->name : "(unset)");

    pa_hook_fire(&context->main_volume_policy->hooks
                     [PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_MAIN_OUTPUT_MUTE_CONTROL_CHANGED],
                 context);
}

void pa_main_volume_context_bind_main_output_mute_control(pa_main_volume_context *context,
                                                          pa_binding_target_info *target_info) {
    pa_binding_owner_info owner_info = {
        .userdata = context,
        .set_value = (pa_binding_set_value_cb_t) set_main_output_mute_control_internal,
    };

    pa_assert(context);
    pa_assert(target_info);

    if (context->main_output_mute_control_binding)
        pa_binding_free(context->main_output_mute_control_binding);

    context->main_output_mute_control_binding = pa_binding_new(context->main_volume_policy->volume_api, &owner_info,
                                                               target_info);
}

static void set_main_input_mute_control_internal(pa_main_volume_context *context, pa_mute_control *control) {
    pa_mute_control *old_control;

    pa_assert(context);

    old_control = context->main_input_mute_control;

    if (control == old_control)
        return;

    context->main_input_mute_control = control;

    if (!context->linked || context->unlinked)
        return;

    pa_log_debug("The main input mute control of main volume context %s changed from %s to %s.", context->name,
                 old_control ? old_control->name : "(unset)", control ? control->name : "(unset)");

    pa_hook_fire(&context->main_volume_policy->hooks
                     [PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_MAIN_INPUT_MUTE_CONTROL_CHANGED],
                 context);
}

void pa_main_volume_context_bind_main_input_mute_control(pa_main_volume_context *context,
                                                         pa_binding_target_info *target_info) {
    pa_binding_owner_info owner_info = {
        .userdata = context,
        .set_value = (pa_binding_set_value_cb_t) set_main_input_mute_control_internal,
    };

    pa_assert(context);
    pa_assert(target_info);

    if (context->main_input_mute_control_binding)
        pa_binding_free(context->main_input_mute_control_binding);

    context->main_input_mute_control_binding = pa_binding_new(context->main_volume_policy->volume_api, &owner_info,
                                                              target_info);
}

pa_binding_target_type *pa_main_volume_context_create_binding_target_type(pa_main_volume_policy *policy) {
    pa_binding_target_type *type;

    pa_assert(policy);

    type = pa_binding_target_type_new(PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_TYPE, policy->main_volume_contexts,
                                      &policy->hooks[PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_PUT],
                                      &policy->hooks[PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_UNLINK],
                                      (pa_binding_target_type_get_name_cb_t) pa_main_volume_context_get_name);
    pa_binding_target_type_add_field(type, PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_FIELD_MAIN_OUTPUT_VOLUME_CONTROL,
                                     PA_BINDING_CALCULATE_FIELD_OFFSET(pa_main_volume_context, main_output_volume_control));
    pa_binding_target_type_add_field(type, PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_FIELD_MAIN_INPUT_VOLUME_CONTROL,
                                     PA_BINDING_CALCULATE_FIELD_OFFSET(pa_main_volume_context, main_input_volume_control));
    pa_binding_target_type_add_field(type, PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_FIELD_MAIN_OUTPUT_MUTE_CONTROL,
                                     PA_BINDING_CALCULATE_FIELD_OFFSET(pa_main_volume_context, main_output_mute_control));
    pa_binding_target_type_add_field(type, PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_FIELD_MAIN_INPUT_MUTE_CONTROL,
                                     PA_BINDING_CALCULATE_FIELD_OFFSET(pa_main_volume_context, main_input_mute_control));

    return type;
}
