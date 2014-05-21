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

#include "module-main-volume-policy-symdef.h"

#include <modules/main-volume-policy/main-volume-context.h>

#include <modules/volume-api/audio-group.h>
#include <modules/volume-api/volume-api.h>

#include <pulse/direction.h>

#include <pulsecore/conf-parser.h>
#include <pulsecore/core-util.h>
#include <pulsecore/i18n.h>

#define BIND_PREFIX "bind:"
#define BIND_AUDIO_GROUP_PREFIX BIND_PREFIX "AudioGroup:"

PA_MODULE_AUTHOR("Tanu Kaskinen");
PA_MODULE_DESCRIPTION(_("Main volume and mute policy"));
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);

enum control_type {
    CONTROL_TYPE_VOLUME,
    CONTROL_TYPE_MUTE,
};

enum model {
    MODEL_NONE,
    MODEL_BY_ACTIVE_MAIN_VOLUME_CONTEXT,
};

struct userdata {
    pa_volume_api *volume_api;
    pa_main_volume_policy *main_volume_policy;
    enum model output_volume_model;
    enum model input_volume_model;
    enum model output_mute_model;
    enum model input_mute_model;
    pa_hashmap *contexts; /* name -> struct context */

    pa_hook_slot *active_main_volume_context_changed_slot;
    pa_hook_slot *main_volume_context_main_output_volume_control_changed_slot;
    pa_hook_slot *main_volume_context_main_input_volume_control_changed_slot;
    pa_hook_slot *main_volume_context_main_output_mute_control_changed_slot;
    pa_hook_slot *main_volume_context_main_input_mute_control_changed_slot;
    pa_hook_slot *audio_group_put_slot;
    pa_hook_slot *audio_group_unlink_slot;
    pa_hook_slot *audio_group_volume_control_changed_slot;
    pa_hook_slot *audio_group_mute_control_changed_slot;
};

struct control_info {
    /* As appropriate for this control, points to one of
     *  - pa_main_volume_context.main_output_volume_control
     *  - pa_main_volume_context.main_input_volume_control
     *  - pa_main_volume_context.main_output_mute_control
     *  - pa_main_volume_context.main_input_mute_control */
    void **control;

    /* As appropriate for this control, points to one of
     *  - userdata.output_volume_model
     *  - userdata.input_volume_model
     *  - userdata.output_mute_model
     *  - userdata.input_mute_model */
    enum model *model;

    /* Name of the audio group to which the context volume or mute control is
     * bound. If the context control is not bound to anything, this is NULL. */
    char *binding_target_name;

    /* Points to the audio group to which the context volume or mute control is
     * bound. If the context control is not bound to anything, or it's bound
     * but the target doesn't currently exist, this is NULL. */
    pa_audio_group *binding_target;

    /* As appropriate for this control, points to one of
     *  - pa_main_volume_context_set_main_output_volume_control()
     *  - pa_main_volume_context_set_main_input_volume_control()
     *  - pa_main_volume_context_set_main_output_mute_control()
     *  - pa_main_volume_context_set_main_input_mute_control() */
    void (*set_control)(pa_main_volume_context *context, void *control);

    /* As appropriate for this control, points to one of
     *  - pa_volume_api_set_main_output_volume_control()
     *  - pa_volume_api_set_main_input_volume_control()
     *  - pa_volume_api_set_main_output_mute_control()
     *  - pa_volume_api_set_main_input_mute_control() */
    void (*set_volume_api_control)(pa_volume_api *api, void *control);
};

struct context {
    struct userdata *userdata;
    pa_main_volume_context *main_volume_context;
    struct control_info output_volume_info;
    struct control_info input_volume_info;
    struct control_info output_mute_info;
    struct control_info input_mute_info;

    bool unlinked;
};

static void context_free(struct context *context);

static const char *model_to_string(enum model model) {
    switch (model) {
        case MODEL_NONE:
            return "none";

        case MODEL_BY_ACTIVE_MAIN_VOLUME_CONTEXT:
            return "by-active-main-volume-context";
    }

    pa_assert_not_reached();
}

static int model_from_string(const char *str, enum model *model) {
    pa_assert(str);
    pa_assert(model);

    if (pa_streq(str, "none"))
        *model = MODEL_NONE;
    else if (pa_streq(str, "by-active-main-volume-context"))
        *model = MODEL_BY_ACTIVE_MAIN_VOLUME_CONTEXT;
    else
        return -PA_ERR_INVALID;

    return 0;
}

static int context_new(struct userdata *u, const char *name, struct context **_r) {
    struct context *context = NULL;
    int r;

    pa_assert(u);
    pa_assert(name);
    pa_assert(_r);

    context = pa_xnew0(struct context, 1);
    context->userdata = u;

    r = pa_main_volume_context_new(u->main_volume_policy, name, u, &context->main_volume_context);
    if (r < 0)
        goto fail;

    context->output_volume_info.control = (void **) &context->main_volume_context->main_output_volume_control;
    context->input_volume_info.control = (void **) &context->main_volume_context->main_input_volume_control;
    context->output_mute_info.control = (void **) &context->main_volume_context->main_output_mute_control;
    context->input_mute_info.control = (void **) &context->main_volume_context->main_input_mute_control;

    context->output_volume_info.model = &u->output_volume_model;
    context->input_volume_info.model = &u->input_volume_model;
    context->output_mute_info.model = &u->output_mute_model;
    context->input_mute_info.model = &u->input_mute_model;

    context->output_volume_info.set_control = (void *) pa_main_volume_context_set_main_output_volume_control;
    context->input_volume_info.set_control = (void *) pa_main_volume_context_set_main_input_volume_control;
    context->output_mute_info.set_control = (void *) pa_main_volume_context_set_main_output_mute_control;
    context->input_mute_info.set_control = (void *) pa_main_volume_context_set_main_input_mute_control;

    context->output_volume_info.set_volume_api_control = (void *) pa_volume_api_set_main_output_volume_control;
    context->input_volume_info.set_volume_api_control = (void *) pa_volume_api_set_main_input_volume_control;
    context->output_mute_info.set_volume_api_control = (void *) pa_volume_api_set_main_output_mute_control;
    context->input_mute_info.set_volume_api_control = (void *) pa_volume_api_set_main_input_mute_control;

    *_r = context;
    return 0;

fail:
    if (context)
        context_free(context);

    return r;
}

static void context_put(struct context *context) {
    pa_assert(context);

    pa_main_volume_context_put(context->main_volume_context);
}

static void context_unlink(struct context *context) {
    pa_assert(context);

    if (context->unlinked)
        return;

    context->unlinked = true;

    if (context->main_volume_context)
        pa_main_volume_context_unlink(context->main_volume_context);
}

static void context_free(struct context *context) {
    pa_assert(context);

    if (!context->unlinked)
        context_unlink(context);

    if (context->main_volume_context)
        pa_main_volume_context_free(context->main_volume_context);

    pa_xfree(context);
}

static struct control_info *context_get_control_info(struct context *context, enum control_type type,
                                                     pa_direction_t direction) {
    pa_assert(context);

    switch (type) {
        case CONTROL_TYPE_VOLUME:
            switch (direction) {
                case PA_DIRECTION_OUTPUT:
                    return &context->output_volume_info;

                case PA_DIRECTION_INPUT:
                    return &context->input_volume_info;
            }
            break;

        case CONTROL_TYPE_MUTE:
            switch (direction) {
                case PA_DIRECTION_OUTPUT:
                    return &context->output_mute_info;

                case PA_DIRECTION_INPUT:
                    return &context->input_mute_info;
            }
            break;
    }

    pa_assert_not_reached();
}

static void context_set_binding_target(struct context *context, enum control_type type, pa_direction_t direction,
                                       pa_audio_group *group) {
    struct control_info *info;
    void *control = NULL;

    pa_assert(context);

    info = context_get_control_info(context, type, direction);
    info->binding_target = group;

    if (group) {
        switch (type) {
            case CONTROL_TYPE_VOLUME:
                control = group->volume_control;
                break;

            case CONTROL_TYPE_MUTE:
                control = group->mute_control;
                break;
        }
    }

    info->set_control(context->main_volume_context, control);
}

static void context_set_binding_target_name(struct context *context, enum control_type type, pa_direction_t direction,
                                            const char *name) {
    struct control_info *info;
    pa_audio_group *group = NULL;

    pa_assert(context);

    info = context_get_control_info(context, type, direction);

    if (pa_safe_streq(name, info->binding_target_name))
        return;

    pa_xfree(info->binding_target_name);
    info->binding_target_name = pa_xstrdup(name);

    if (name)
        group = pa_hashmap_get(context->userdata->volume_api->audio_groups, name);

    context_set_binding_target(context, type, direction, group);
}

static pa_hook_result_t active_main_volume_context_changed_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_main_volume_context *context;

    pa_assert(u);

    context = u->main_volume_policy->active_main_volume_context;

    if (u->output_volume_model == MODEL_BY_ACTIVE_MAIN_VOLUME_CONTEXT) {
        if (context)
            pa_volume_api_set_main_output_volume_control(u->volume_api, context->main_output_volume_control);
        else
            pa_volume_api_set_main_output_volume_control(u->volume_api, NULL);
    }

    if (u->input_volume_model == MODEL_BY_ACTIVE_MAIN_VOLUME_CONTEXT) {
        if (context)
            pa_volume_api_set_main_input_volume_control(u->volume_api, context->main_input_volume_control);
        else
            pa_volume_api_set_main_input_volume_control(u->volume_api, NULL);
    }

    if (u->output_mute_model == MODEL_BY_ACTIVE_MAIN_VOLUME_CONTEXT) {
        if (context)
            pa_volume_api_set_main_output_mute_control(u->volume_api, context->main_output_mute_control);
        else
            pa_volume_api_set_main_output_mute_control(u->volume_api, NULL);
    }

    if (u->input_mute_model == MODEL_BY_ACTIVE_MAIN_VOLUME_CONTEXT) {
        if (context)
            pa_volume_api_set_main_input_mute_control(u->volume_api, context->main_input_mute_control);
        else
            pa_volume_api_set_main_input_mute_control(u->volume_api, NULL);
    }

    return PA_HOOK_OK;
}

static void handle_context_control_change(struct context *context, enum control_type type, pa_direction_t direction) {
    struct control_info *info;

    pa_assert(context);

    info = context_get_control_info(context, type, direction);

    if (*info->model == MODEL_BY_ACTIVE_MAIN_VOLUME_CONTEXT
            && context->userdata->main_volume_policy->active_main_volume_context == context->main_volume_context)
        info->set_volume_api_control(context->userdata->volume_api, *info->control);
}

static pa_hook_result_t main_volume_context_main_output_volume_control_changed_cb(void *hook_data, void *call_data,
                                                                                  void *userdata) {
    pa_main_volume_context *context = call_data;

    pa_assert(context);

    handle_context_control_change(context->userdata, CONTROL_TYPE_VOLUME, PA_DIRECTION_OUTPUT);

    return PA_HOOK_OK;
}

static pa_hook_result_t main_volume_context_main_input_volume_control_changed_cb(void *hook_data, void *call_data,
                                                                                 void *userdata) {
    pa_main_volume_context *context = call_data;

    pa_assert(context);

    handle_context_control_change(context->userdata, CONTROL_TYPE_VOLUME, PA_DIRECTION_INPUT);

    return PA_HOOK_OK;
}

static pa_hook_result_t main_volume_context_main_output_mute_control_changed_cb(void *hook_data, void *call_data,
                                                                                void *userdata) {
    pa_main_volume_context *context = call_data;

    pa_assert(context);

    handle_context_control_change(context->userdata, CONTROL_TYPE_MUTE, PA_DIRECTION_OUTPUT);

    return PA_HOOK_OK;
}

static pa_hook_result_t main_volume_context_main_input_mute_control_changed_cb(void *hook_data, void *call_data,
                                                                               void *userdata) {
    pa_main_volume_context *context = call_data;

    pa_assert(context);

    handle_context_control_change(context->userdata, CONTROL_TYPE_MUTE, PA_DIRECTION_INPUT);

    return PA_HOOK_OK;
}

static pa_hook_result_t audio_group_put_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_audio_group *group = call_data;
    struct context *context;
    void *state;

    pa_assert(u);
    pa_assert(group);

    PA_HASHMAP_FOREACH(context, u->contexts, state) {
        if (context->output_volume_info.binding_target_name
                && pa_streq(context->output_volume_info.binding_target_name, group->name))
            context_set_binding_target(context, CONTROL_TYPE_VOLUME, PA_DIRECTION_OUTPUT, group);

        if (context->input_volume_info.binding_target_name
                && pa_streq(context->input_volume_info.binding_target_name, group->name))
            context_set_binding_target(context, CONTROL_TYPE_VOLUME, PA_DIRECTION_INPUT, group);

        if (context->output_mute_info.binding_target_name
                && pa_streq(context->output_mute_info.binding_target_name, group->name))
            context_set_binding_target(context, CONTROL_TYPE_MUTE, PA_DIRECTION_OUTPUT, group);

        if (context->input_mute_info.binding_target_name
                && pa_streq(context->input_mute_info.binding_target_name, group->name))
            context_set_binding_target(context, CONTROL_TYPE_MUTE, PA_DIRECTION_INPUT, group);
    }

    return PA_HOOK_OK;
}

static pa_hook_result_t audio_group_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_audio_group *group = call_data;
    struct context *context;
    void *state;

    pa_assert(u);
    pa_assert(group);

    PA_HASHMAP_FOREACH(context, u->contexts, state) {
        if (context->output_volume_info.binding_target == group)
            context_set_binding_target(context, CONTROL_TYPE_VOLUME, PA_DIRECTION_OUTPUT, NULL);

        if (context->input_volume_info.binding_target == group)
            context_set_binding_target(context, CONTROL_TYPE_VOLUME, PA_DIRECTION_INPUT, NULL);

        if (context->output_mute_info.binding_target == group)
            context_set_binding_target(context, CONTROL_TYPE_MUTE, PA_DIRECTION_OUTPUT, NULL);

        if (context->input_mute_info.binding_target == group)
            context_set_binding_target(context, CONTROL_TYPE_MUTE, PA_DIRECTION_INPUT, NULL);
    }

    return PA_HOOK_OK;
}

static void handle_audio_group_control_change(struct userdata *u, pa_audio_group *group, enum control_type type) {
    struct context *context;
    void *state;

    pa_assert(u);
    pa_assert(group);

    PA_HASHMAP_FOREACH(context, u->contexts, state) {
        switch (type) {
            case CONTROL_TYPE_VOLUME:
                if (context->output_volume_info.binding_target == group)
                    pa_main_volume_context_set_main_output_volume_control(context->main_volume_context, group->volume_control);

                if (context->input_volume_info.binding_target == group)
                    pa_main_volume_context_set_main_input_volume_control(context->main_volume_context, group->volume_control);
                break;

            case CONTROL_TYPE_MUTE:
                if (context->output_mute_info.binding_target == group)
                    pa_main_volume_context_set_main_output_mute_control(context->main_volume_context, group->mute_control);

                if (context->input_mute_info.binding_target == group)
                    pa_main_volume_context_set_main_input_mute_control(context->main_volume_context, group->mute_control);
                break;
        }
    }
}

static pa_hook_result_t audio_group_volume_control_changed_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_audio_group *group = call_data;

    pa_assert(u);
    pa_assert(group);

    handle_audio_group_control_change(u, group, CONTROL_TYPE_VOLUME);

    return PA_HOOK_OK;
}

static pa_hook_result_t audio_group_mute_control_changed_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_audio_group *group = call_data;

    pa_assert(u);
    pa_assert(group);

    handle_audio_group_control_change(u, group, CONTROL_TYPE_MUTE);

    return PA_HOOK_OK;
}

static int parse_model(pa_config_parser_state *state) {
    int r;

    pa_assert(state);

    r = model_from_string(state->rvalue, state->data);
    if (r < 0)
        pa_log("[%s:%u] Failed to parse model: %s", state->filename, state->lineno, state->rvalue);

    return r;
}

static int get_context(struct userdata *u, const char *section, struct context **_r) {
    const char *name;
    struct context *context;

    pa_assert(u);

    if (!section)
        return -PA_ERR_INVALID;

    if (!pa_startswith(section, "MainVolumeContext "))
        return -PA_ERR_INVALID;

    name = section + 18;

    context = pa_hashmap_get(u->contexts, name);
    if (!context) {
        int r;

        r = context_new(u, name, &context);
        if (r < 0)
            return r;

        pa_hashmap_put(u->contexts, (void *) context->main_volume_context->name, context);
    }

    *_r = context;
    return 0;
}

static int parse_description(pa_config_parser_state *state) {
    struct userdata *u;
    int r;
    struct context *context;

    pa_assert(state);

    u = state->userdata;

    r = get_context(u, state->section, &context);
    if (r < 0) {
        pa_log("[%s:%u] Couldn't get main volume context for section \"%s\".", state->filename, state->lineno,
               pa_strnull(state->section));
        return -PA_ERR_INVALID;
    }

    pa_main_volume_context_set_description(context->main_volume_context, state->rvalue);

    return 0;
}

static int parse_control(pa_config_parser_state *state, enum control_type type, pa_direction_t direction) {
    struct userdata *u;
    int r;
    struct context *context;

    pa_assert(state);

    u = state->userdata;

    r = get_context(u, state->section, &context);
    if (r < 0) {
        pa_log("[%s:%u] Couldn't get main volume context for section \"%s\".", state->filename, state->lineno,
               pa_strnull(state->section));
        return -PA_ERR_INVALID;
    }

    if (pa_streq(state->rvalue, "none"))
        context_set_binding_target_name(context, type, direction, NULL);
    else if (pa_startswith(state->rvalue, BIND_PREFIX)) {
        if (pa_startswith(state->rvalue, BIND_AUDIO_GROUP_PREFIX))
            context_set_binding_target_name(context, type, direction, state->rvalue + strlen(BIND_AUDIO_GROUP_PREFIX));
        else {
            pa_log("[%s:%u] Failed to parse binding target \"%s\".", state->filename, state->lineno, state->rvalue + strlen(BIND_PREFIX));
            return -PA_ERR_INVALID;
        }
    } else {
        pa_log("[%s:%u] Failed to parse value \"%s\".", state->filename, state->lineno, state->rvalue);
        return -PA_ERR_INVALID;
    }

    return 0;
}

static int parse_main_output_volume_control(pa_config_parser_state *state) {
    pa_assert(state);

    return parse_control(state, CONTROL_TYPE_VOLUME, PA_DIRECTION_OUTPUT);
}

static int parse_main_input_volume_control(pa_config_parser_state *state) {
    pa_assert(state);

    return parse_control(state, CONTROL_TYPE_VOLUME, PA_DIRECTION_INPUT);
}

static int parse_main_output_mute_control(pa_config_parser_state *state) {
    pa_assert(state);

    return parse_control(state, CONTROL_TYPE_MUTE, PA_DIRECTION_OUTPUT);
}

static int parse_main_input_mute_control(pa_config_parser_state *state) {
    pa_assert(state);

    return parse_control(state, CONTROL_TYPE_MUTE, PA_DIRECTION_INPUT);
}

int pa__init(pa_module *module) {
    struct userdata *u;
    FILE *f;
    char *fn = NULL;
    struct context *context;
    void *state;

    pa_assert(module);

    u = module->userdata = pa_xnew0(struct userdata, 1);
    u->volume_api = pa_volume_api_get(module->core);
    u->main_volume_policy = pa_main_volume_policy_get(module->core);
    u->output_volume_model = MODEL_NONE;
    u->input_volume_model = MODEL_NONE;
    u->output_mute_model = MODEL_NONE;
    u->input_mute_model = MODEL_NONE;
    u->contexts = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL,
                                      (pa_free_cb_t) context_free);
    u->active_main_volume_context_changed_slot =
            pa_hook_connect(&u->main_volume_policy->hooks[PA_MAIN_VOLUME_POLICY_HOOK_ACTIVE_MAIN_VOLUME_CONTEXT_CHANGED],
                            PA_HOOK_NORMAL, active_main_volume_context_changed_cb, u);
    u->main_volume_context_main_output_volume_control_changed_slot =
            pa_hook_connect(&u->main_volume_policy->hooks
                                [PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_MAIN_OUTPUT_VOLUME_CONTROL_CHANGED],
                            PA_HOOK_NORMAL, main_volume_context_main_output_volume_control_changed_cb, u);
    u->main_volume_context_main_input_volume_control_changed_slot =
            pa_hook_connect(&u->main_volume_policy->hooks
                                [PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_MAIN_INPUT_VOLUME_CONTROL_CHANGED],
                            PA_HOOK_NORMAL, main_volume_context_main_input_volume_control_changed_cb, u);
    u->main_volume_context_main_output_mute_control_changed_slot =
            pa_hook_connect(&u->main_volume_policy->hooks
                                [PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_MAIN_OUTPUT_MUTE_CONTROL_CHANGED],
                            PA_HOOK_NORMAL, main_volume_context_main_output_mute_control_changed_cb, u);
    u->main_volume_context_main_input_mute_control_changed_slot =
            pa_hook_connect(&u->main_volume_policy->hooks
                                [PA_MAIN_VOLUME_POLICY_HOOK_MAIN_VOLUME_CONTEXT_MAIN_INPUT_MUTE_CONTROL_CHANGED],
                            PA_HOOK_NORMAL, main_volume_context_main_input_mute_control_changed_cb, u);
    u->audio_group_put_slot = pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_PUT], PA_HOOK_NORMAL,
                                              audio_group_put_cb, u);
    u->audio_group_unlink_slot = pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_UNLINK], PA_HOOK_NORMAL,
                                                 audio_group_unlink_cb, u);
    u->audio_group_volume_control_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_VOLUME_CONTROL_CHANGED], PA_HOOK_NORMAL,
                            audio_group_volume_control_changed_cb, u);
    u->audio_group_mute_control_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_AUDIO_GROUP_MUTE_CONTROL_CHANGED], PA_HOOK_NORMAL,
                            audio_group_mute_control_changed_cb, u);

    f = pa_open_config_file(PA_DEFAULT_CONFIG_DIR PA_PATH_SEP "main-volume-policy.conf", "main-volume-policy.conf", NULL, &fn);
    if (f) {
        pa_config_item config_items[] = {
            { "output-volume-model", parse_model, &u->output_volume_model, "General" },
            { "input-volume-model", parse_model, &u->input_volume_model, "General" },
            { "output-mute-model", parse_model, &u->output_mute_model, "General" },
            { "input-mute-model", parse_model, &u->input_mute_model, "General" },
            { "description", parse_description, NULL, NULL },
            { "main-output-volume-control", parse_main_output_volume_control, NULL, NULL },
            { "main-input-volume-control", parse_main_input_volume_control, NULL, NULL },
            { "main-output-mute-control", parse_main_output_mute_control, NULL, NULL },
            { "main-input-mute-control", parse_main_input_mute_control, NULL, NULL },
            { NULL },
        };

        pa_config_parse(fn, f, config_items, NULL, u);
        pa_xfree(fn);
        fn = NULL;
        fclose(f);
        f = NULL;
    }

    PA_HASHMAP_FOREACH(context, u->contexts, state)
        context_put(context);

    pa_log_debug("Output volume model: %s", model_to_string(u->output_volume_model));
    pa_log_debug("Input volume model: %s", model_to_string(u->input_volume_model));
    pa_log_debug("Output mute model: %s", model_to_string(u->output_mute_model));
    pa_log_debug("Input mute model: %s", model_to_string(u->input_mute_model));

    return 0;
}

void pa__done(pa_module *module) {
    struct userdata *u;

    pa_assert(module);

    u = module->userdata;
    if (!u)
        return;

    if (u->audio_group_mute_control_changed_slot)
        pa_hook_slot_free(u->audio_group_mute_control_changed_slot);

    if (u->audio_group_volume_control_changed_slot)
        pa_hook_slot_free(u->audio_group_volume_control_changed_slot);

    if (u->audio_group_unlink_slot)
        pa_hook_slot_free(u->audio_group_unlink_slot);

    if (u->audio_group_put_slot)
        pa_hook_slot_free(u->audio_group_put_slot);

    if (u->main_volume_context_main_input_mute_control_changed_slot)
        pa_hook_slot_free(u->main_volume_context_main_input_mute_control_changed_slot);

    if (u->main_volume_context_main_output_mute_control_changed_slot)
        pa_hook_slot_free(u->main_volume_context_main_output_mute_control_changed_slot);

    if (u->main_volume_context_main_input_volume_control_changed_slot)
        pa_hook_slot_free(u->main_volume_context_main_input_volume_control_changed_slot);

    if (u->main_volume_context_main_output_volume_control_changed_slot)
        pa_hook_slot_free(u->main_volume_context_main_output_volume_control_changed_slot);

    if (u->active_main_volume_context_changed_slot)
        pa_hook_slot_free(u->active_main_volume_context_changed_slot);

    if (u->contexts)
        pa_hashmap_free(u->contexts);

    if (u->main_volume_policy)
        pa_main_volume_policy_unref(u->main_volume_policy);

    if (u->volume_api)
        pa_volume_api_unref(u->volume_api);

    pa_xfree(u);
}
