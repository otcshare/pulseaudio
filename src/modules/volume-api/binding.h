#ifndef foobindinghfoo
#define foobindinghfoo

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

typedef struct pa_binding pa_binding;
typedef struct pa_binding_owner_info pa_binding_owner_info;
typedef struct pa_binding_target_info pa_binding_target_info;
typedef struct pa_binding_target_type pa_binding_target_type;

typedef void (*pa_binding_set_value_cb_t)(void *userdata, void *value);

struct pa_binding_owner_info {
    /* This is the object that has the variable that the binding is created
     * for. */
    void *userdata;

    /* Called when the owner object's value needs to be updated. The userdata
     * parameter of the callback is the same as the userdata field in this
     * struct, and the value parameter is the new value for whatever variable
     * the binding was created for. */
    pa_binding_set_value_cb_t set_value;
};

pa_binding_owner_info *pa_binding_owner_info_new(pa_binding_set_value_cb_t set_value, void *userdata);
pa_binding_owner_info *pa_binding_owner_info_copy(const pa_binding_owner_info *info);
void pa_binding_owner_info_free(pa_binding_owner_info *info);

struct pa_binding_target_info {
    /* The target type name as registered with
     * pa_binding_target_type_register(). */
    char *type;

    /* The target object name as returned by the get_name callback of
     * pa_binding_target_type. */
    char *name;

    /* The target field of the target object. */
    char *field;
};

pa_binding_target_info *pa_binding_target_info_new(const char *type, const char *name, const char *field);

/* The string format is "bind:TYPE:NAME". */
int pa_binding_target_info_new_from_string(const char *str, const char *field, pa_binding_target_info **info);

pa_binding_target_info *pa_binding_target_info_copy(const pa_binding_target_info *info);
void pa_binding_target_info_free(pa_binding_target_info *info);

typedef const char *(*pa_binding_target_type_get_name_cb_t)(void *object);

struct pa_binding_target_type {
    /* Identifier for this target type. */
    char *name;

    /* name -> object. Points directly to some "master" object hashmap, so the
     * hashmap is not owned by pa_binding_target_type. */
    pa_hashmap *objects;

    /* The hook that notifies of new objects if this target type. The call data
     * of the hook must be a pointer to the new object (this should be true for
     * all PUT hooks, so don't worry too much). */
    pa_hook *put_hook;

    /* The hook that notifies of unlinked objects of this target type. The call
     * data of the hook must be a pointer to the removed object (this should be
     * true for all UNLINK hooks, so don't worry too much). */
    pa_hook *unlink_hook;

    /* Function for getting the name of an object of this target type. */
    pa_binding_target_type_get_name_cb_t get_name;

    pa_hashmap *fields;
};

pa_binding_target_type *pa_binding_target_type_new(const char *name, pa_hashmap *objects, pa_hook *put_hook,
                                                   pa_hook *unlink_hook, pa_binding_target_type_get_name_cb_t get_name);
void pa_binding_target_type_free(pa_binding_target_type *type);

/* Useful when calling pa_binding_target_type_add_field(). */
#define PA_BINDING_CALCULATE_FIELD_OFFSET(type, field) ((size_t) &(((type *) 0)->field))

/* Called during the type initialization (right after
 * pa_binding_target_type_new()). */
void pa_binding_target_type_add_field(pa_binding_target_type *type, const char *name, size_t offset);

int pa_binding_target_type_get_field_offset(pa_binding_target_type *type, const char *field, size_t *offset);

struct pa_binding {
    pa_volume_api *volume_api;
    pa_binding_owner_info *owner_info;
    pa_binding_target_info *target_info;
    pa_binding_target_type *target_type;
    void *target_object;
    size_t target_field_offset;
    bool target_field_offset_valid;
    pa_hook_slot *target_type_added_slot;
    pa_hook_slot *target_type_removed_slot;
    pa_hook_slot *target_put_slot;
    pa_hook_slot *target_unlink_slot;
};

pa_binding *pa_binding_new(pa_volume_api *api, const pa_binding_owner_info *owner_info,
                           const pa_binding_target_info *target_info);
void pa_binding_free(pa_binding *binding);

#endif
