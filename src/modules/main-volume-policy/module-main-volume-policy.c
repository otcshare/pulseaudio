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

#include <modules/volume-api/binding.h>
#include <modules/volume-api/volume-api.h>

#include <pulse/direction.h>

#include <pulsecore/conf-parser.h>
#include <pulsecore/core-util.h>
#include <pulsecore/i18n.h>

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
    pa_main_volume_policy *main_volume_policy;
    enum model output_volume_model;
    enum model input_volume_model;
    enum model output_mute_model;
    enum model input_mute_model;
    pa_hashmap *contexts; /* name -> struct context */

    pa_hook_slot *active_main_volume_context_changed_slot;

    /* The following fields are only used during initialization. */
    pa_hashmap *context_names; /* name -> name (hashmap-as-a-set) */
    pa_hashmap *unused_contexts; /* name -> struct context */
};

struct context {
    struct userdata *userdata;
    char *name;
    char *description;
    pa_binding_target_info *main_output_volume_control_target_info;
    pa_binding_target_info *main_input_volume_control_target_info;
    pa_binding_target_info *main_output_mute_control_target_info;
    pa_binding_target_info *main_input_mute_control_target_info;
    pa_main_volume_context *main_volume_context;

    bool unlinked;
};

static void context_unlink(struct context *context);

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

static struct context *context_new(struct userdata *u, const char *name) {
    struct context *context;

    pa_assert(u);
    pa_assert(name);

    context = pa_xnew0(struct context, 1);
    context->userdata = u;
    context->name = pa_xstrdup(name);
    context->description = pa_xstrdup(name);

    return context;
}

static int context_put(struct context *context) {
    int r;

    pa_assert(context);

    r = pa_main_volume_context_new(context->userdata->main_volume_policy, context->name, context->description,
                                   &context->main_volume_context);
    if (r < 0)
        goto fail;

    if (context->main_output_volume_control_target_info)
        pa_main_volume_context_bind_main_output_volume_control(context->main_volume_context,
                                                               context->main_output_volume_control_target_info);

    if (context->main_input_volume_control_target_info)
        pa_main_volume_context_bind_main_input_volume_control(context->main_volume_context,
                                                              context->main_input_volume_control_target_info);

    if (context->main_output_mute_control_target_info)
        pa_main_volume_context_bind_main_output_mute_control(context->main_volume_context,
                                                             context->main_output_mute_control_target_info);

    if (context->main_input_mute_control_target_info)
        pa_main_volume_context_bind_main_input_mute_control(context->main_volume_context,
                                                            context->main_input_mute_control_target_info);

    pa_main_volume_context_put(context->main_volume_context);

    return 0;

fail:
    context_unlink(context);

    return r;
}

static void context_unlink(struct context *context) {
    pa_assert(context);

    if (context->unlinked)
        return;

    context->unlinked = true;

    if (context->main_volume_context) {
        pa_main_volume_context_free(context->main_volume_context);
        context->main_volume_context = NULL;
    }
}

static void context_free(struct context *context) {
    pa_assert(context);

    if (!context->unlinked)
        context_unlink(context);

    if (context->main_input_mute_control_target_info)
        pa_binding_target_info_free(context->main_input_mute_control_target_info);

    if (context->main_output_mute_control_target_info)
        pa_binding_target_info_free(context->main_output_mute_control_target_info);

    if (context->main_input_volume_control_target_info)
        pa_binding_target_info_free(context->main_input_volume_control_target_info);

    if (context->main_output_volume_control_target_info)
        pa_binding_target_info_free(context->main_output_volume_control_target_info);

    pa_xfree(context->description);
    pa_xfree(context->name);
    pa_xfree(context);
}

static void context_set_description(struct context *context, const char *description) {
    pa_assert(context);
    pa_assert(description);

    pa_xfree(context->description);
    context->description = pa_xstrdup(description);
}

static void context_set_main_control_target_info(struct context *context, enum control_type type, pa_direction_t direction,
                                                 pa_binding_target_info *info) {
    pa_assert(context);

    switch (type) {
        case CONTROL_TYPE_VOLUME:
            if (direction == PA_DIRECTION_OUTPUT) {
                if (context->main_output_volume_control_target_info)
                    pa_binding_target_info_free(context->main_output_volume_control_target_info);

                if (info)
                    context->main_output_volume_control_target_info = pa_binding_target_info_copy(info);
                else
                    context->main_output_volume_control_target_info = NULL;
            } else {
                if (context->main_input_volume_control_target_info)
                    pa_binding_target_info_free(context->main_input_volume_control_target_info);

                if (info)
                    context->main_input_volume_control_target_info = pa_binding_target_info_copy(info);
                else
                    context->main_input_volume_control_target_info = NULL;
            }
            break;

        case CONTROL_TYPE_MUTE:
            if (direction == PA_DIRECTION_OUTPUT) {
                if (context->main_output_mute_control_target_info)
                    pa_binding_target_info_free(context->main_output_mute_control_target_info);

                if (info)
                    context->main_output_mute_control_target_info = pa_binding_target_info_copy(info);
                else
                    context->main_output_mute_control_target_info = NULL;
            } else {
                if (context->main_input_mute_control_target_info)
                    pa_binding_target_info_free(context->main_input_mute_control_target_info);

                if (info)
                    context->main_input_mute_control_target_info = pa_binding_target_info_copy(info);
                else
                    context->main_input_mute_control_target_info = NULL;
            }
            break;
    }
}

static pa_hook_result_t active_main_volume_context_changed_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_main_volume_context *context;
    pa_volume_api *api;
    pa_binding_target_info *info;

    pa_assert(u);

    context = u->main_volume_policy->active_main_volume_context;
    api = u->main_volume_policy->volume_api;

    if (u->output_volume_model == MODEL_BY_ACTIVE_MAIN_VOLUME_CONTEXT) {
        if (context) {
            info = pa_binding_target_info_new(PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_TYPE, context->name,
                                              PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_FIELD_MAIN_OUTPUT_VOLUME_CONTROL);
            pa_volume_api_bind_main_output_volume_control(api, info);
            pa_binding_target_info_free(info);
        } else
            pa_volume_api_set_main_output_volume_control(api, NULL);
    }

    if (u->input_volume_model == MODEL_BY_ACTIVE_MAIN_VOLUME_CONTEXT) {
        if (context) {
            info = pa_binding_target_info_new(PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_TYPE, context->name,
                                              PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_FIELD_MAIN_INPUT_VOLUME_CONTROL);
            pa_volume_api_bind_main_input_volume_control(api, info);
            pa_binding_target_info_free(info);
        } else
            pa_volume_api_set_main_input_volume_control(api, NULL);
    }

    if (u->output_mute_model == MODEL_BY_ACTIVE_MAIN_VOLUME_CONTEXT) {
        if (context) {
            info = pa_binding_target_info_new(PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_TYPE, context->name,
                                              PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_FIELD_MAIN_OUTPUT_MUTE_CONTROL);
            pa_volume_api_bind_main_output_mute_control(api, info);
            pa_binding_target_info_free(info);
        } else
            pa_volume_api_set_main_output_mute_control(api, NULL);
    }

    if (u->input_mute_model == MODEL_BY_ACTIVE_MAIN_VOLUME_CONTEXT) {
        if (context) {
            info = pa_binding_target_info_new(PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_TYPE, context->name,
                                              PA_MAIN_VOLUME_CONTEXT_BINDING_TARGET_FIELD_MAIN_INPUT_MUTE_CONTROL);
            pa_volume_api_bind_main_input_mute_control(api, info);
            pa_binding_target_info_free(info);
        } else
            pa_volume_api_set_main_input_mute_control(api, NULL);
    }

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

static int parse_main_volume_contexts(pa_config_parser_state *state) {
    struct userdata *u;
    char *name;
    const char *split_state = NULL;

    pa_assert(state);

    u = state->userdata;

    while ((name = pa_split_spaces(state->rvalue, &split_state)))
        pa_hashmap_put(u->context_names, name, name);

    return 0;
}

static struct context *get_context(struct userdata *u, const char *section) {
    const char *name;
    struct context *context;

    pa_assert(u);

    if (!section)
        return NULL;

    if (!pa_startswith(section, "MainVolumeContext "))
        return NULL;

    name = section + 18;

    context = pa_hashmap_get(u->unused_contexts, name);
    if (!context) {
        context = context_new(u, name);
        pa_hashmap_put(u->unused_contexts, context->name, context);
    }

    return context;
}

static int parse_description(pa_config_parser_state *state) {
    struct userdata *u;
    struct context *context;

    pa_assert(state);

    u = state->userdata;

    context = get_context(u, state->section);
    if (!context) {
        pa_log("[%s:%u] Key \"%s\" not expected in section %s.", state->filename, state->lineno, state->lvalue,
               pa_strnull(state->section));
        return -PA_ERR_INVALID;
    }

    context_set_description(context, state->rvalue);

    return 0;
}

static const char *get_target_field_name(enum control_type type) {
    switch (type) {
        case CONTROL_TYPE_VOLUME:
            return "volume_control";

        case CONTROL_TYPE_MUTE:
            return "mute_control";
    }

    pa_assert_not_reached();
}

static int parse_main_control(pa_config_parser_state *state, enum control_type type, pa_direction_t direction) {
    struct userdata *u;
    struct context *context;

    pa_assert(state);

    u = state->userdata;

    context = get_context(u, state->section);
    if (!context) {
        pa_log("[%s:%u] Key \"%s\" not expected in section %s.", state->filename, state->lineno, state->lvalue,
               pa_strnull(state->section));
        return -PA_ERR_INVALID;
    }

    if (pa_streq(state->rvalue, "none"))
        context_set_main_control_target_info(context, type, direction, NULL);
    else if (pa_startswith(state->rvalue, "bind:")) {
        int r;
        pa_binding_target_info *info;

        r = pa_binding_target_info_new_from_string(state->rvalue, get_target_field_name(type), &info);
        if (r < 0) {
            pa_log("[%s:%u] Failed to parse binding target \"%s\".", state->filename, state->lineno, state->rvalue);
            return r;
        }

        context_set_main_control_target_info(context, type, direction, info);
        pa_binding_target_info_free(info);
    } else {
        pa_log("[%s:%u] Failed to parse value \"%s\".", state->filename, state->lineno, state->rvalue);
        return -PA_ERR_INVALID;
    }

    return 0;
}

static int parse_main_output_volume_control(pa_config_parser_state *state) {
    pa_assert(state);

    return parse_main_control(state, CONTROL_TYPE_VOLUME, PA_DIRECTION_OUTPUT);
}

static int parse_main_input_volume_control(pa_config_parser_state *state) {
    pa_assert(state);

    return parse_main_control(state, CONTROL_TYPE_VOLUME, PA_DIRECTION_INPUT);
}

static int parse_main_output_mute_control(pa_config_parser_state *state) {
    pa_assert(state);

    return parse_main_control(state, CONTROL_TYPE_MUTE, PA_DIRECTION_OUTPUT);
}

static int parse_main_input_mute_control(pa_config_parser_state *state) {
    pa_assert(state);

    return parse_main_control(state, CONTROL_TYPE_MUTE, PA_DIRECTION_INPUT);
}

static void finalize_config(struct userdata *u) {
    const char *context_name;
    void *state;
    struct context *context;

    pa_assert(u);

    PA_HASHMAP_FOREACH(context_name, u->context_names, state) {
        int r;

        context = pa_hashmap_remove(u->unused_contexts, context_name);
        if (!context)
            context = context_new(u, context_name);

        r = context_put(context);
        if (r < 0) {
            pa_log_warn("Failed to create main volume context %s.", context_name);
            context_free(context);
            continue;
        }

        pa_assert_se(pa_hashmap_put(u->contexts, context->name, context) >= 0);
    }

    PA_HASHMAP_FOREACH(context, u->unused_contexts, state)
        pa_log_debug("Main volume context %s is not used.", context->name);

    pa_hashmap_free(u->unused_contexts);
    u->unused_contexts = NULL;

    pa_hashmap_free(u->context_names);
    u->context_names = NULL;
}

int pa__init(pa_module *module) {
    struct userdata *u;
    FILE *f;
    char *fn = NULL;

    pa_assert(module);

    u = module->userdata = pa_xnew0(struct userdata, 1);
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
    u->context_names = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL, pa_xfree);
    u->unused_contexts = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL,
                                             (pa_free_cb_t) context_free);

    f = pa_open_config_file(PA_DEFAULT_CONFIG_DIR PA_PATH_SEP "main-volume-policy.conf", "main-volume-policy.conf", NULL, &fn);
    if (f) {
        pa_config_item config_items[] = {
            { "output-volume-model", parse_model, &u->output_volume_model, "General" },
            { "input-volume-model", parse_model, &u->input_volume_model, "General" },
            { "output-mute-model", parse_model, &u->output_mute_model, "General" },
            { "input-mute-model", parse_model, &u->input_mute_model, "General" },
            { "main-volume-contexts", parse_main_volume_contexts, NULL, "General" },
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

    finalize_config(u);

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

    if (u->active_main_volume_context_changed_slot)
        pa_hook_slot_free(u->active_main_volume_context_changed_slot);

    if (u->contexts)
        pa_hashmap_free(u->contexts);

    if (u->main_volume_policy)
        pa_main_volume_policy_unref(u->main_volume_policy);

    pa_xfree(u);
}
