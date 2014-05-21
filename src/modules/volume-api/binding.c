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

#include "binding.h"

#include <pulse/def.h>
#include <pulse/xmalloc.h>

#include <pulsecore/core-util.h>
#include <pulsecore/macro.h>

struct field_entry {
    char *name;
    size_t offset;
};

static void set_target_type(pa_binding *binding, pa_binding_target_type *type);
static void set_target_object(pa_binding *binding, void *object);

pa_binding_owner_info *pa_binding_owner_info_new(pa_binding_set_value_cb_t set_value, void *userdata) {
    pa_binding_owner_info *info;

    pa_assert(set_value);

    info = pa_xnew0(pa_binding_owner_info, 1);
    info->set_value = set_value;
    info->userdata = userdata;

    return info;
}

pa_binding_owner_info *pa_binding_owner_info_copy(const pa_binding_owner_info *info) {
    pa_assert(info);

    return pa_binding_owner_info_new(info->set_value, info->userdata);
}

void pa_binding_owner_info_free(pa_binding_owner_info *info) {
    pa_assert(info);

    pa_xfree(info);
}

pa_binding_target_info *pa_binding_target_info_new(const char *type, const char *name, const char *field) {
    pa_binding_target_info *info;

    pa_assert(type);
    pa_assert(name);
    pa_assert(field);

    info = pa_xnew0(pa_binding_target_info, 1);
    info->type = pa_xstrdup(type);
    info->name = pa_xstrdup(name);
    info->field = pa_xstrdup(field);

    return info;
}

int pa_binding_target_info_new_from_string(const char *str, const char *field, pa_binding_target_info **info) {
    const char *colon;
    char *type = NULL;
    char *name = NULL;

    pa_assert(str);
    pa_assert(field);
    pa_assert(info);

    if (!pa_startswith(str, "bind:"))
        goto fail;

    colon = strchr(str + 5, ':');
    if (!colon)
        goto fail;

    type = pa_xstrndup(str + 5, colon - (str + 5));

    if (!*type)
        goto fail;

    name = pa_xstrdup(colon + 1);

    if (!*name)
        goto fail;

    *info = pa_binding_target_info_new(type, name, field);
    pa_xfree(name);
    pa_xfree(type);

    return 0;

fail:
    pa_log("Invalid binding target: %s", str);
    pa_xfree(name);
    pa_xfree(type);

    return -PA_ERR_INVALID;
}

pa_binding_target_info *pa_binding_target_info_copy(const pa_binding_target_info *info) {
    pa_assert(info);

    return pa_binding_target_info_new(info->type, info->name, info->field);
}

void pa_binding_target_info_free(pa_binding_target_info *info) {
    pa_assert(info);

    pa_xfree(info->field);
    pa_xfree(info->name);
    pa_xfree(info->type);
    pa_xfree(info);
}

static void field_entry_free(struct field_entry *entry) {
    pa_assert(entry);

    pa_xfree(entry->name);
    pa_xfree(entry);
}

pa_binding_target_type *pa_binding_target_type_new(const char *name, pa_hashmap *objects, pa_hook *put_hook,
                                                   pa_hook *unlink_hook, pa_binding_target_type_get_name_cb_t get_name) {
    pa_binding_target_type *type;

    pa_assert(name);
    pa_assert(objects);
    pa_assert(put_hook);
    pa_assert(unlink_hook);
    pa_assert(get_name);

    type = pa_xnew0(pa_binding_target_type, 1);
    type->name = pa_xstrdup(name);
    type->objects = objects;
    type->put_hook = put_hook;
    type->unlink_hook = unlink_hook;
    type->get_name = get_name;
    type->fields = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL, (pa_free_cb_t) field_entry_free);

    return type;
}

void pa_binding_target_type_free(pa_binding_target_type *type) {
    pa_assert(type);

    if (type->fields)
        pa_hashmap_free(type->fields);

    pa_xfree(type->name);
    pa_xfree(type);
}

void pa_binding_target_type_add_field(pa_binding_target_type *type, const char *name, size_t offset) {
    struct field_entry *entry;

    pa_assert(type);
    pa_assert(name);

    entry = pa_xnew0(struct field_entry, 1);
    entry->name = pa_xstrdup(name);
    entry->offset = offset;

    pa_assert_se(pa_hashmap_put(type->fields, entry->name, entry) >= 0);
}

int pa_binding_target_type_get_field_offset(pa_binding_target_type *type, const char *field, size_t *offset) {
    struct field_entry *entry;

    pa_assert(type);
    pa_assert(field);
    pa_assert(offset);

    entry = pa_hashmap_get(type->fields, field);
    if (!entry)
        return -PA_ERR_NOENTITY;

    *offset = entry->offset;

    return 0;
}

static pa_hook_result_t target_type_added_cb(void *hook_data, void *call_data, void *userdata) {
    pa_binding_target_type *type = call_data;
    pa_binding *binding = userdata;

    pa_assert(type);
    pa_assert(binding);

    if (!pa_streq(type->name, binding->target_info->type))
        return PA_HOOK_OK;

    set_target_type(binding, type);

    return PA_HOOK_OK;
}

static pa_hook_result_t target_type_removed_cb(void *hook_data, void *call_data, void *userdata) {
    pa_binding_target_type *type = call_data;
    pa_binding *binding = userdata;

    pa_assert(type);
    pa_assert(binding);

    if (type != binding->target_type)
        return PA_HOOK_OK;

    set_target_type(binding, NULL);

    return PA_HOOK_OK;
}

static pa_hook_result_t target_put_cb(void *hook_data, void *call_data, void *userdata) {
    pa_binding *binding = userdata;

    pa_assert(call_data);
    pa_assert(binding);

    if (!pa_streq(binding->target_type->get_name(call_data), binding->target_info->name))
        return PA_HOOK_OK;

    set_target_object(binding, call_data);

    return PA_HOOK_OK;
}

static pa_hook_result_t target_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    pa_binding *binding = userdata;

    pa_assert(call_data);
    pa_assert(binding);

    if (call_data != binding->target_object)
        return PA_HOOK_OK;

    set_target_object(binding, NULL);

    return PA_HOOK_OK;
}

static void set_target_object(pa_binding *binding, void *object) {
    pa_assert(binding);

    binding->target_object = object;

    if (object) {
        if (binding->target_put_slot) {
            pa_hook_slot_free(binding->target_put_slot);
            binding->target_put_slot = NULL;
        }

        if (!binding->target_unlink_slot)
            binding->target_unlink_slot = pa_hook_connect(binding->target_type->unlink_hook, PA_HOOK_NORMAL, target_unlink_cb,
                                                          binding);

        if (binding->target_field_offset_valid)
            binding->owner_info->set_value(binding->owner_info->userdata,
                                           *((void **) (((uint8_t *) object) + binding->target_field_offset)));
        else
            binding->owner_info->set_value(binding->owner_info->userdata, NULL);
    } else {
        if (binding->target_unlink_slot) {
            pa_hook_slot_free(binding->target_unlink_slot);
            binding->target_unlink_slot = NULL;
        }

        if (binding->target_type) {
            if (!binding->target_put_slot)
                binding->target_put_slot = pa_hook_connect(binding->target_type->put_hook, PA_HOOK_NORMAL, target_put_cb, binding);
        } else {
            if (binding->target_put_slot) {
                pa_hook_slot_free(binding->target_put_slot);
                binding->target_put_slot = NULL;
            }
        }

        binding->owner_info->set_value(binding->owner_info->userdata, NULL);
    }
}

static void set_target_type(pa_binding *binding, pa_binding_target_type *type) {
    pa_assert(binding);

    binding->target_type = type;

    if (type) {
        int r;

        if (binding->target_type_added_slot) {
            pa_hook_slot_free(binding->target_type_added_slot);
            binding->target_type_added_slot = NULL;
        }

        if (!binding->target_type_removed_slot)
            binding->target_type_removed_slot =
                    pa_hook_connect(&binding->volume_api->hooks[PA_VOLUME_API_HOOK_BINDING_TARGET_TYPE_REMOVED],
                                    PA_HOOK_NORMAL, target_type_removed_cb, binding);

        r = pa_binding_target_type_get_field_offset(type, binding->target_info->field, &binding->target_field_offset);
        if (r >= 0)
            binding->target_field_offset_valid = true;
        else {
            pa_log_warn("Reference to non-existing field \"%s\" in binding target type \"%s\".", binding->target_info->field,
                        type->name);
            binding->target_field_offset_valid = false;
        }

        set_target_object(binding, pa_hashmap_get(type->objects, binding->target_info->name));
    } else {
        if (binding->target_type_removed_slot) {
            pa_hook_slot_free(binding->target_type_removed_slot);
            binding->target_type_removed_slot = NULL;
        }

        if (!binding->target_type_added_slot)
            binding->target_type_added_slot =
                    pa_hook_connect(&binding->volume_api->hooks[PA_VOLUME_API_HOOK_BINDING_TARGET_TYPE_ADDED],
                                    PA_HOOK_NORMAL, target_type_added_cb, binding);

        binding->target_field_offset_valid = false;

        set_target_object(binding, NULL);
    }
}

pa_binding *pa_binding_new(pa_volume_api *api, const pa_binding_owner_info *owner_info,
                           const pa_binding_target_info *target_info) {
    pa_binding *binding;

    pa_assert(api);
    pa_assert(owner_info);
    pa_assert(target_info);

    binding = pa_xnew0(pa_binding, 1);
    binding->volume_api = api;
    binding->owner_info = pa_binding_owner_info_copy(owner_info);
    binding->target_info = pa_binding_target_info_copy(target_info);

    set_target_type(binding, pa_hashmap_get(api->binding_target_types, target_info->type));

    return binding;
}

void pa_binding_free(pa_binding *binding) {
    pa_assert(binding);

    if (binding->target_unlink_slot)
        pa_hook_slot_free(binding->target_unlink_slot);

    if (binding->target_put_slot)
        pa_hook_slot_free(binding->target_put_slot);

    if (binding->target_type_removed_slot)
        pa_hook_slot_free(binding->target_type_removed_slot);

    if (binding->target_type_added_slot)
        pa_hook_slot_free(binding->target_type_added_slot);

    if (binding->target_info)
        pa_binding_target_info_free(binding->target_info);

    if (binding->owner_info)
        pa_binding_owner_info_free(binding->owner_info);

    pa_xfree(binding);
}
