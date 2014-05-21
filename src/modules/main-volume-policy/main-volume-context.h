#ifndef foomainvolumecontexthfoo
#define foomainvolumecontexthfoo

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

#include <modules/main-volume-policy/main-volume-policy.h>

#include <modules/volume-api/binding.h>

typedef struct pa_main_volume_context pa_main_volume_context;

#define PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_TYPE "MainVolumeContext"
#define PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_FIELD_MAIN_OUTPUT_VOLUME_CONTROL "main_output_volume_control"
#define PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_FIELD_MAIN_INPUT_VOLUME_CONTROL "main_input_volume_control"
#define PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_FIELD_MAIN_OUTPUT_MUTE_CONTROL "main_output_mute_control"
#define PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_FIELD_MAIN_INPUT_MUTE_CONTROL "main_input_mute_control"

struct pa_main_volume_context {
    pa_main_volume_policy *main_volume_policy;
    uint32_t index;
    const char *name;
    char *description;
    pa_volume_control *main_output_volume_control;
    pa_volume_control *main_input_volume_control;
    pa_mute_control *main_output_mute_control;
    pa_mute_control *main_input_mute_control;

    pa_binding *main_output_volume_control_binding;
    pa_binding *main_input_volume_control_binding;
    pa_binding *main_output_mute_control_binding;
    pa_binding *main_input_mute_control_binding;

    bool linked;
    bool unlinked;
};

int pa_main_volume_context_new(pa_main_volume_policy *policy, const char *name, const char *description,
                               pa_main_volume_context **context);
void pa_main_volume_context_put(pa_main_volume_context *context);
void pa_main_volume_context_unlink(pa_main_volume_context *context);
void pa_main_volume_context_free(pa_main_volume_context *context);

const char *pa_main_volume_context_get_name(pa_main_volume_context *context);

void pa_main_volume_context_bind_main_output_volume_control(pa_main_volume_context *context,
                                                            pa_binding_target_info *target_info);
void pa_main_volume_context_bind_main_input_volume_control(pa_main_volume_context *context,
                                                           pa_binding_target_info *target_info);
void pa_main_volume_context_bind_main_output_mute_control(pa_main_volume_context *context,
                                                          pa_binding_target_info *target_info);
void pa_main_volume_context_bind_main_input_mute_control(pa_main_volume_context *context, pa_binding_target_info *target_info);

/* Called from main-volume-policy.c only. */
pa_binding_target_type *pa_main_volume_context_create_binding_target_type(pa_main_volume_policy *policy);

#endif
