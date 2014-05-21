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

typedef struct pa_main_volume_context pa_main_volume_context;

struct pa_main_volume_context {
    pa_main_volume_policy *main_volume_policy;
    uint32_t index;
    const char *name;
    char *description;
    pa_volume_control *main_output_volume_control;
    pa_volume_control *main_input_volume_control;
    pa_mute_control *main_output_mute_control;
    pa_mute_control *main_input_mute_control;

    bool linked;
    bool unlinked;

    void *userdata;
};

int pa_main_volume_context_new(pa_main_volume_policy *policy, const char *name, void *userdata, pa_main_volume_context **_r);
void pa_main_volume_context_put(pa_main_volume_context *context);
void pa_main_volume_context_unlink(pa_main_volume_context *context);
void pa_main_volume_context_free(pa_main_volume_context *context);

void pa_main_volume_context_set_description(pa_main_volume_context *context, const char *description);
void pa_main_volume_context_set_main_output_volume_control(pa_main_volume_context *context, pa_volume_control *control);
void pa_main_volume_context_set_main_input_volume_control(pa_main_volume_context *context, pa_volume_control *control);
void pa_main_volume_context_set_main_output_mute_control(pa_main_volume_context *context, pa_mute_control *control);
void pa_main_volume_context_set_main_input_mute_control(pa_main_volume_context *context, pa_mute_control *control);

#endif
