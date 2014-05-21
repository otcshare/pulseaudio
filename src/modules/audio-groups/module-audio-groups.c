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

#include <string.h>

#include <pulse/xmalloc.h>

#include <pulsecore/core-util.h>
#include <pulsecore/dynarray.h>
#include <pulsecore/module.h>
#include <pulsecore/modargs.h>
#include <pulsecore/conf-parser.h>
#include <pulsecore/hashmap.h>

#include <modules/volume-api/sstream.h>
#include <modules/volume-api/volume-control.h>
#include <modules/volume-api/audio-group.h>

#include "module-audio-groups-symdef.h"

#define AUDIOGROUP_START "AudioGroup "
#define STREAM_RULE_START "StreamRule "
#define NONE_KEYWORD "none"
#define CREATE_PREFIX "create:"
#define BIND_PREFIX "bind:"
#define BIND_AUDIO_GROUP_PREFIX BIND_PREFIX "AudioGroup:"

PA_MODULE_AUTHOR("Ismo Puustinen");
PA_MODULE_DESCRIPTION("Create audio groups and classify streams to them");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);

enum match_direction {
    match_direction_unknown = 0,
    match_direction_input,
    match_direction_output,
};

/* logical expressions */

struct literal {

    /* TODO: this might be parsed to some faster-to-check format? */

    char *property_name;
    char *property_value;
    enum match_direction stream_direction;

    bool negation;
    PA_LLIST_FIELDS(struct literal);
};

struct conjunction {
    /* a conjunction of literals */
    PA_LLIST_HEAD(struct literal, literals);
    PA_LLIST_FIELDS(struct conjunction);
};

struct expression {
    /* this is in disjunctive normal form, so a disjunction of conjunctions */
    PA_LLIST_HEAD(struct conjunction, conjunctions);
};

struct group {
    struct userdata *userdata;
    pa_audio_group *audio_group;
    struct control *volume_control;
    struct control *mute_control;
    char *own_volume_control_name;
    char *own_mute_control_name;
    struct group *volume_master;
    struct group *mute_master;
    char *volume_master_name;
    char *mute_master_name;

    pa_hashmap *volume_slaves; /* struct group -> struct group (hashmap-as-a-set) */
    pa_hashmap *mute_slaves; /* struct group -> struct group (hashmap-as-a-set) */
    pa_hashmap *volume_stream_rules; /* struct stream_rule -> struct stream_rule (hashmap-as-a-set) */
    pa_hashmap *mute_stream_rules; /* struct stream_rule -> struct stream_rule (hashmap-as-a-set) */

    bool unlinked;
};

enum control_type {
    CONTROL_TYPE_VOLUME,
    CONTROL_TYPE_MUTE,
};

struct control {
    struct userdata *userdata;
    enum control_type type;

    union {
        pa_volume_control *volume_control;
        pa_mute_control *mute_control;
    };

    /* Controls that are created for streams don't own their pa_volume_control
     * and pa_mute_control objects, because they're owned by the streams. */
    bool own_control;

    /* If non-NULL, then this control mirrors the state of the master
     * control. If someone changes the master state, the state of this control
     * is also updated, and also if someone changes this control's state, the
     * change is applied also to the master. */
    struct control *master;

    /* struct control -> struct control (hashmap-as-a-set)
     * Contains the controls that have this control as their master. */
    pa_hashmap *slaves;

    /* Set to true when the master control's state has been copied to this
     * control. */
    bool synced_with_master;

    bool acquired;
    bool unlinked;
};

struct stream_rule {
    struct userdata *userdata;
    char *name;
    enum match_direction direction;
    char *audio_group_name_for_volume;
    char *audio_group_name_for_mute;
    struct group *group_for_volume;
    struct group *group_for_mute;
    struct expression *match_expression;
};

struct userdata {
    pa_volume_api *volume_api;
    pa_hashmap *groups; /* name -> struct group */
    pa_hashmap *stream_rules; /* name -> struct stream_rule */
    pa_dynarray *stream_rules_list; /* struct stream_rule */

    /* pas_stream -> struct stream_rule
     * When a stream matches with a rule, it's added here. */
    pa_hashmap *rules_by_stream;

    /* pas_stream -> struct control
     * Contains proxy controls for all relative volume controls of streams. */
    pa_hashmap *stream_volume_controls;

    /* pas_stream -> struct control
     * Contains proxy controls for all mute controls of streams. */
    pa_hashmap *stream_mute_controls;

    pa_hook_slot *stream_put_slot;
    pa_hook_slot *stream_unlink_slot;
    pa_hook_slot *volume_control_implementation_initialized_slot;
    pa_hook_slot *mute_control_implementation_initialized_slot;
    pa_hook_slot *volume_control_set_initial_volume_slot;
    pa_hook_slot *mute_control_set_initial_mute_slot;
    pa_hook_slot *volume_control_volume_changed_slot;
    pa_hook_slot *mute_control_mute_changed_slot;
    pa_hook_slot *volume_control_unlink_slot;
    pa_hook_slot *mute_control_unlink_slot;

    pa_dynarray *stream_rule_names; /* Only used during initialization. */
};

static const char* const valid_modargs[] = {
    "filename",
    NULL
};

static void control_free(struct control *control);
static void control_set_master(struct control *control, struct control *master);
static void control_add_slave(struct control *control, struct control *slave);
static void control_remove_slave(struct control *control, struct control *slave);

static void group_free(struct group *group);
static void group_set_master(struct group *group, enum control_type type, struct group *master);
static int group_set_master_name(struct group *group, enum control_type type, const char *name);
static void group_disable_control(struct group *group, enum control_type type);
static void group_add_slave(struct group *group, enum control_type type, struct group *slave);
static void group_remove_slave(struct group *group, enum control_type type, struct group *slave);

static void stream_rule_set_group(struct stream_rule *rule, enum control_type type, struct group *group);
static void stream_rule_set_group_name(struct stream_rule *rule, enum control_type type, const char *name);

static bool literal_match(struct literal *literal, pas_stream *stream);

static struct expression *expression_new(void);
static void expression_free(struct expression *expression);

static int volume_control_set_volume_cb(pa_volume_control *volume_control, const pa_bvolume *original_volume,
                                        const pa_bvolume *remapped_volume, bool set_volume, bool set_balance) {
    struct control *control;
    struct control *slave;
    void *state;

    pa_assert(volume_control);
    pa_assert(original_volume);
    pa_assert(remapped_volume);

    control = volume_control->userdata;

    /* There are four cases that need to be considered:
     *
     * 1) The master control is propagating the volume to this control. We need
     * to propagate the volume downstream.
     *
     * 2) This control was just assigned a master control and the volume hasn't
     * yet been synchronized. In this case the volume that is now being set for
     * this control is the master control's volume. We need to propagate the
     * volume downstream.
     *
     * 3) Someone set the volume directly for this control, and this control
     * has a master control. We need to propagate the volume upstream, and wait
     * for another call that will fall under the case 1.
     *
     * 4) Someone set the volume directly for this control, and this control
     * doesn't have a master control. We need to propagate the volume
     * downstream.
     *
     * As we can see, the action is the same in cases 1, 2 and 4. */

    /* Case 3. */
    if (control->synced_with_master && !control->master->volume_control->set_volume_in_progress) {
        pa_volume_control_set_volume(control->master->volume_control, original_volume, set_volume, set_balance);
        return 0;
    }

    /* Cases 1, 2 and 4. */
    PA_HASHMAP_FOREACH(slave, control->slaves, state)
        pa_volume_control_set_volume(slave->volume_control, original_volume, set_volume, set_balance);

    return 0;
}

static int mute_control_set_mute_cb(pa_mute_control *mute_control, bool mute) {
    struct control *control;
    struct control *slave;
    void *state;

    pa_assert(mute_control);

    control = mute_control->userdata;

    /* There are four cases that need to be considered:
     *
     * 1) The master control is propagating the mute to this control. We need
     * to propagate the mute downstream.
     *
     * 2) This control was just assigned a master control and the mute hasn't
     * yet been synchronized. In this case the mute that is now being set for
     * this control is the master control's mute. We need to propagate the mute
     * downstream.
     *
     * 3) Someone set the mute directly for this control, and this control has
     * a master control. We need to propagate the mute upstream, and wait for
     * another call that will fall under the case 1.
     *
     * 4) Someone set the mute directly for this control, and this control
     * doesn't have a master control. We need to propagate the mute downstream.
     *
     * As we can see, the action is the same in cases 1, 2 and 4. */

    /* Case 3. */
    if (control->synced_with_master && !control->master->mute_control->set_mute_in_progress) {
        pa_mute_control_set_mute(control->master->mute_control, mute);
        return 0;
    }

    /* Cases 1, 2 and 4. */
    PA_HASHMAP_FOREACH(slave, control->slaves, state)
        pa_mute_control_set_mute(slave->mute_control, mute);

    return 0;
}

static int control_new_for_group(struct group *group, enum control_type type, const char *name, bool persistent, struct control **_r) {
    struct control *control = NULL;
    int r = 0;

    pa_assert(group);
    pa_assert(name);
    pa_assert(_r);

    control = pa_xnew0(struct control, 1);
    control->userdata = group->userdata;
    control->type = type;
    control->slaves = pa_hashmap_new(NULL, NULL);

    switch (type) {
        case CONTROL_TYPE_VOLUME:
            if (persistent)
                control->volume_control = pa_hashmap_get(control->userdata->volume_api->volume_controls, name);

            if (!control->volume_control) {
                r = pa_volume_control_new(control->userdata->volume_api, name, persistent, &control->volume_control);
                if (r < 0)
                    goto fail;
            }

            pa_volume_control_set_convertible_to_dB(control->volume_control, true);

            if (persistent) {
                r = pa_volume_control_acquire_for_audio_group(control->volume_control, group->audio_group,
                                                              volume_control_set_volume_cb, control);
                if (r < 0)
                    goto fail;

                control->acquired = true;
            } else {
                control->volume_control->set_volume = volume_control_set_volume_cb;
                control->volume_control->userdata = control;
            }
            break;

        case CONTROL_TYPE_MUTE:
            if (persistent)
                control->mute_control = pa_hashmap_get(control->userdata->volume_api->mute_controls, name);

            if (!control->mute_control) {
                r = pa_mute_control_new(control->userdata->volume_api, name, persistent, &control->mute_control);
                if (r < 0)
                    goto fail;
            }

            if (persistent) {
                r = pa_mute_control_acquire_for_audio_group(control->mute_control, group->audio_group,
                                                            mute_control_set_mute_cb, control);
                if (r < 0)
                    goto fail;

                control->acquired = true;
            } else {
                control->mute_control->set_mute = mute_control_set_mute_cb;
                control->mute_control->userdata = control;
            }
            break;
    }

    control->own_control = true;

    *_r = control;
    return 0;

fail:
    if (control)
        control_free(control);

    return r;
}

static struct control *control_new_for_stream(struct userdata *u, enum control_type type, pas_stream *stream) {
    struct control *control;

    pa_assert(u);
    pa_assert(stream);

    control = pa_xnew0(struct control, 1);
    control->userdata = u;
    control->type = type;

    switch (type) {
        case CONTROL_TYPE_VOLUME:
            control->volume_control = stream->relative_volume_control;
            pa_assert(control->volume_control);
            break;

        case CONTROL_TYPE_MUTE:
            control->mute_control = stream->mute_control;
            pa_assert(control->mute_control);
            break;
    }

    return control;
}

static void control_put(struct control *control) {
    pa_assert(control);

    switch (control->type) {
        case CONTROL_TYPE_VOLUME:
            if (control->own_control && !control->volume_control->linked)
                pa_volume_control_put(control->volume_control);
            break;

        case CONTROL_TYPE_MUTE:
            if (control->own_control && !control->mute_control->linked)
                pa_mute_control_put(control->mute_control);
            break;
    }
}

static void control_unlink(struct control *control) {
    pa_assert(control);

    if (control->unlinked)
        return;

    control->unlinked = true;

    if (control->slaves) {
        struct control *slave;

        while ((slave = pa_hashmap_first(control->slaves)))
            control_set_master(slave, NULL);
    }

    control_set_master(control, NULL);

    switch (control->type) {
        case CONTROL_TYPE_VOLUME:
            if (control->own_control && control->volume_control && !control->volume_control->persistent)
                pa_volume_control_unlink(control->volume_control);
            break;

        case CONTROL_TYPE_MUTE:
            if (control->own_control && control->mute_control && !control->mute_control->persistent)
                pa_mute_control_unlink(control->mute_control);
            break;
    }
}

static void control_free(struct control *control) {
    pa_assert(control);

    if (!control->unlinked)
        control_unlink(control);

    if (control->slaves) {
        pa_assert(pa_hashmap_isempty(control->slaves));
        pa_hashmap_free(control->slaves);
    }

    switch (control->type) {
        case CONTROL_TYPE_VOLUME:
            if (control->acquired)
                pa_volume_control_release(control->volume_control);

            if (control->own_control && control->volume_control && !control->volume_control->persistent)
                pa_volume_control_free(control->volume_control);
            break;

        case CONTROL_TYPE_MUTE:
            if (control->acquired)
                pa_mute_control_release(control->mute_control);

            if (control->own_control && control->mute_control && !control->mute_control->persistent)
                pa_mute_control_free(control->mute_control);
            break;
    }

    pa_xfree(control);
}

static void control_set_master(struct control *control, struct control *master) {
    struct control *old_master;

    pa_assert(control);
    pa_assert(!master || master->type == control->type);

    old_master = control->master;

    if (master == old_master)
        return;

    if (old_master) {
        control_remove_slave(old_master, control);
        control->synced_with_master = false;
    }

    control->master = master;

    if (master) {
        control_add_slave(master, control);

        switch (control->type) {
            case CONTROL_TYPE_VOLUME:
                pa_volume_control_set_volume(control->volume_control, &master->volume_control->volume, true, true);
                break;

            case CONTROL_TYPE_MUTE:
                pa_mute_control_set_mute(control->mute_control, master->mute_control->mute);
                break;
        }

        control->synced_with_master = true;
    }
}

static void control_add_slave(struct control *control, struct control *slave) {
    pa_assert(control);
    pa_assert(slave);

    pa_assert_se(pa_hashmap_put(control->slaves, slave, slave) >= 0);
}

static void control_remove_slave(struct control *control, struct control *slave) {
    pa_assert(control);
    pa_assert(slave);

    pa_assert_se(pa_hashmap_remove(control->slaves, slave));
}

static int group_new(struct userdata *u, const char *name, struct group **_r) {
    struct group *group = NULL;
    int r;
    struct group *slave;
    struct stream_rule *rule;
    void *state;

    pa_assert(u);
    pa_assert(name);
    pa_assert(_r);

    group = pa_xnew0(struct group, 1);
    group->userdata = u;

    r = pa_audio_group_new(u->volume_api, name, &group->audio_group);
    if (r < 0)
        goto fail;

    group->volume_slaves = pa_hashmap_new(NULL, NULL);
    group->mute_slaves = pa_hashmap_new(NULL, NULL);
    group->volume_stream_rules = pa_hashmap_new(NULL, NULL);
    group->mute_stream_rules = pa_hashmap_new(NULL, NULL);

    PA_HASHMAP_FOREACH(slave, u->groups, state) {
        if (slave == group)
            continue;

        if (pa_safe_streq(slave->volume_master_name, group->audio_group->name))
            group_set_master(slave, CONTROL_TYPE_VOLUME, group);

        if (pa_safe_streq(slave->mute_master_name, group->audio_group->name))
            group_set_master(slave, CONTROL_TYPE_MUTE, group);
    }

    PA_HASHMAP_FOREACH(rule, u->stream_rules, state) {
        if (pa_safe_streq(rule->audio_group_name_for_volume, group->audio_group->name))
            stream_rule_set_group(rule, CONTROL_TYPE_VOLUME, group);

        if (pa_safe_streq(rule->audio_group_name_for_mute, group->audio_group->name))
            stream_rule_set_group(rule, CONTROL_TYPE_MUTE, group);
    }

    *_r = group;
    return 0;

fail:
    if (group)
        group_free(group);

    return r;
}

static void group_put(struct group *group) {
    pa_assert(group);

    pa_audio_group_put(group->audio_group);

    if (group->volume_control)
        control_put(group->volume_control);

    if (group->mute_control)
        control_put(group->mute_control);
}

static void group_unlink(struct group *group) {
    struct stream_rule *rule;
    struct group *slave;
    void *state;

    pa_assert(group);

    if (group->unlinked)
        return;

    group->unlinked = true;

    PA_HASHMAP_FOREACH(rule, group->volume_stream_rules, state)
        stream_rule_set_group(rule, CONTROL_TYPE_VOLUME, NULL);

    PA_HASHMAP_FOREACH(rule, group->mute_stream_rules, state)
        stream_rule_set_group(rule, CONTROL_TYPE_MUTE, NULL);

    PA_HASHMAP_FOREACH(slave, group->volume_slaves, state)
        group_set_master(slave, CONTROL_TYPE_VOLUME, NULL);

    PA_HASHMAP_FOREACH(slave, group->mute_slaves, state)
        group_set_master(slave, CONTROL_TYPE_MUTE, NULL);

    group_disable_control(group, CONTROL_TYPE_MUTE);
    group_disable_control(group, CONTROL_TYPE_VOLUME);

    if (group->audio_group)
        pa_audio_group_unlink(group->audio_group);
}

static void group_free(struct group *group) {
    pa_assert(group);

    group_unlink(group);

    if (group->mute_stream_rules) {
        pa_assert(pa_hashmap_isempty(group->mute_stream_rules));
        pa_hashmap_free(group->mute_stream_rules);
    }

    if (group->volume_stream_rules) {
        pa_assert(pa_hashmap_isempty(group->volume_stream_rules));
        pa_hashmap_free(group->volume_stream_rules);
    }

    if (group->mute_slaves) {
        pa_assert(pa_hashmap_isempty(group->mute_slaves));
        pa_hashmap_free(group->mute_slaves);
    }

    if (group->volume_slaves) {
        pa_assert(pa_hashmap_isempty(group->volume_slaves));
        pa_hashmap_free(group->volume_slaves);
    }

    pa_assert(!group->mute_master_name);
    pa_assert(!group->volume_master_name);
    pa_assert(!group->mute_master);
    pa_assert(!group->volume_master);
    pa_assert(!group->mute_control);
    pa_assert(!group->volume_control);

    if (group->audio_group)
        pa_audio_group_free(group->audio_group);

    pa_xfree(group);
}

static void group_set_own_control_name(struct group *group, enum control_type type, const char *name) {
    struct group *slave;
    void *state;

    pa_assert(group);

    if (name)
        group_set_master_name(group, type, NULL);

    switch (type) {
        case CONTROL_TYPE_VOLUME:
            if (pa_safe_streq(name, group->own_volume_control_name))
                return;

            if (group->volume_control) {
                control_free(group->volume_control);
                group->volume_control = NULL;
            }

            pa_xfree(group->own_volume_control_name);
            group->own_volume_control_name = pa_xstrdup(name);

            if (name) {
                control_new_for_group(group, CONTROL_TYPE_VOLUME, name, true, &group->volume_control);

                PA_HASHMAP_FOREACH(slave, group->volume_slaves, state) {
                    if (slave->volume_control)
                        control_set_master(slave->volume_control, group->volume_control);
                }
            }
            break;

        case CONTROL_TYPE_MUTE:
            if (pa_safe_streq(name, group->own_mute_control_name))
                return;

            if (group->mute_control) {
                control_free(group->mute_control);
                group->mute_control = NULL;
            }

            pa_xfree(group->own_mute_control_name);
            group->own_mute_control_name = pa_xstrdup(name);

            if (name) {
                control_new_for_group(group, CONTROL_TYPE_MUTE, name, true, &group->mute_control);

                PA_HASHMAP_FOREACH(slave, group->mute_slaves, state) {
                    if (slave->mute_control)
                        control_set_master(slave->mute_control, group->mute_control);
                }
            }
            break;
    }
}

static void group_set_master(struct group *group, enum control_type type, struct group *master) {
    struct group *old_master;
    struct control *master_control = NULL;

    pa_assert(group);
    pa_assert(master != group);

    switch (type) {
        case CONTROL_TYPE_VOLUME:
            old_master = group->volume_master;

            if (master == old_master)
                return;

            if (old_master)
                group_remove_slave(old_master, CONTROL_TYPE_VOLUME, group);

            group->volume_master = master;

            if (master)
                group_add_slave(master, CONTROL_TYPE_VOLUME, group);

            if (group->volume_control) {
                if (master)
                    master_control = master->volume_control;

                control_set_master(group->volume_control, master_control);
            }
            break;

        case CONTROL_TYPE_MUTE:
            old_master = group->mute_master;

            if (master == old_master)
                return;

            if (old_master)
                group_remove_slave(old_master, CONTROL_TYPE_MUTE, group);

            group->mute_master = master;

            if (master)
                group_add_slave(master, CONTROL_TYPE_MUTE, group);

            if (group->mute_control) {
                if (master)
                    master_control = master->volume_control;

                control_set_master(group->volume_control, master_control);
            }
            break;
    }
}

static int group_set_master_name(struct group *group, enum control_type type, const char *name) {
    struct group *slave;
    void *state;
    struct group *master = NULL;

    pa_assert(group);

    if (pa_safe_streq(name, group->audio_group->name)) {
        pa_log("Can't bind audio group control to itself.");
        return -PA_ERR_INVALID;
    }

    if (name)
        group_set_own_control_name(group, type, NULL);

    switch (type) {
        case CONTROL_TYPE_VOLUME:
            if (pa_safe_streq(name, group->volume_master_name))
                return 0;

            pa_xfree(group->volume_master_name);
            group->volume_master_name = pa_xstrdup(name);

            if (name && !group->volume_control) {
                control_new_for_group(group, CONTROL_TYPE_VOLUME, "audio-group-volume-control", false, &group->volume_control);

                PA_HASHMAP_FOREACH(slave, group->volume_slaves, state) {
                    if (slave->volume_control)
                        control_set_master(slave->volume_control, group->volume_control);
                }

            } else if (!name && group->volume_control) {
                control_free(group->volume_control);
                group->volume_control = NULL;
            }
            break;

        case CONTROL_TYPE_MUTE:
            if (pa_safe_streq(name, group->mute_master_name))
                return 0;

            pa_xfree(group->mute_master_name);
            group->mute_master_name = pa_xstrdup(name);

            if (name && !group->mute_control) {
                control_new_for_group(group, CONTROL_TYPE_MUTE, "audio-group-mute-control", false, &group->mute_control);

                PA_HASHMAP_FOREACH(slave, group->mute_slaves, state) {
                    if (slave->mute_control)
                        control_set_master(slave->mute_control, group->mute_control);
                }

            } else if (!name && group->mute_control) {
                control_free(group->mute_control);
                group->mute_control = NULL;
            }
            break;
    }

    if (name)
        master = pa_hashmap_get(group->userdata->groups, name);

    group_set_master(group, type, master);

    return 0;
}

static void group_disable_control(struct group *group, enum control_type type) {
    pa_assert(group);

    group_set_own_control_name(group, type, NULL);
    group_set_master_name(group, type, NULL);
}

static void group_add_slave(struct group *group, enum control_type type, struct group *slave) {
    pa_assert(group);
    pa_assert(slave);

    switch (type) {
        case CONTROL_TYPE_VOLUME:
            pa_assert_se(pa_hashmap_put(group->volume_slaves, slave, slave) >= 0);
            break;

        case CONTROL_TYPE_MUTE:
            pa_assert_se(pa_hashmap_put(group->mute_slaves, slave, slave) >= 0);
            break;
    }
}

static void group_remove_slave(struct group *group, enum control_type type, struct group *slave) {
    pa_assert(group);
    pa_assert(slave);

    switch (type) {
        case CONTROL_TYPE_VOLUME:
            pa_assert_se(pa_hashmap_remove(group->volume_slaves, slave));
            break;

        case CONTROL_TYPE_MUTE:
            pa_assert_se(pa_hashmap_remove(group->mute_slaves, slave));
    }
}

static void group_add_stream_rule(struct group *group, enum control_type type, struct stream_rule *rule) {
    pa_assert(group);
    pa_assert(rule);

    switch (type) {
        case CONTROL_TYPE_VOLUME:
            pa_assert_se(pa_hashmap_put(group->volume_stream_rules, rule, rule) >= 0);
            break;

        case CONTROL_TYPE_MUTE:
            pa_assert_se(pa_hashmap_put(group->mute_stream_rules, rule, rule) >= 0);
            break;
    }
}

static void group_remove_stream_rule(struct group *group, enum control_type type, struct stream_rule *rule) {
    pa_assert(group);
    pa_assert(rule);

    switch (type) {
        case CONTROL_TYPE_VOLUME:
            pa_assert_se(pa_hashmap_remove(group->volume_stream_rules, rule));
            break;

        case CONTROL_TYPE_MUTE:
            pa_assert_se(pa_hashmap_remove(group->mute_stream_rules, rule));
            break;
    }
}

static struct stream_rule *stream_rule_new(struct userdata *u, const char *name) {
    struct stream_rule *rule;

    pa_assert(u);
    pa_assert(name);

    rule = pa_xnew0(struct stream_rule, 1);
    rule->userdata = u;
    rule->name = pa_xstrdup(name);
    rule->direction = match_direction_unknown;
    rule->match_expression = expression_new();

    return rule;
}

static void stream_rule_free(struct stream_rule *rule) {
    pa_assert(rule);

    if (rule->match_expression)
        expression_free(rule->match_expression);

    stream_rule_set_group_name(rule, CONTROL_TYPE_MUTE, NULL);
    stream_rule_set_group_name(rule, CONTROL_TYPE_VOLUME, NULL);
    pa_xfree(rule->name);
    pa_xfree(rule);
}

static void stream_rule_set_match_expression(struct stream_rule *rule, struct expression *expression) {
    pa_assert(rule);
    pa_assert(expression);

    if (rule->match_expression)
        expression_free(rule->match_expression);

    rule->match_expression = expression;
}

static void stream_rule_set_group(struct stream_rule *rule, enum control_type type, struct group *group) {
    pa_assert(rule);

    switch (type) {
        case CONTROL_TYPE_VOLUME:
            if (group == rule->group_for_volume)
                return;

            if (rule->group_for_volume)
                group_remove_stream_rule(rule->group_for_volume, CONTROL_TYPE_VOLUME, rule);

            rule->group_for_volume = group;

            if (group)
                group_add_stream_rule(group, CONTROL_TYPE_VOLUME, rule);
            break;

        case CONTROL_TYPE_MUTE:
            if (group == rule->group_for_mute)
                return;

            if (rule->group_for_mute)
                group_remove_stream_rule(rule->group_for_mute, CONTROL_TYPE_MUTE, rule);

            rule->group_for_mute = group;

            if (group)
                group_add_stream_rule(group, CONTROL_TYPE_MUTE, rule);
            break;
    }
}

static void stream_rule_set_group_name(struct stream_rule *rule, enum control_type type, const char *name) {
    struct group *group = NULL;

    pa_assert(rule);

    switch (type) {
        case CONTROL_TYPE_VOLUME:
            pa_xfree(rule->audio_group_name_for_volume);
            rule->audio_group_name_for_volume = pa_xstrdup(name);
            break;

        case CONTROL_TYPE_MUTE:
            pa_xfree(rule->audio_group_name_for_mute);
            rule->audio_group_name_for_mute = pa_xstrdup(name);
            break;
    }

    if (name)
        group = pa_hashmap_get(rule->userdata->groups, name);

    stream_rule_set_group(rule, type, group);
}

static bool stream_rule_match(struct stream_rule *rule, pas_stream *stream) {
    struct conjunction *c;

    PA_LLIST_FOREACH(c, rule->match_expression->conjunctions) {
        struct literal *l;
        bool and_success = true;
        PA_LLIST_FOREACH(l, c->literals) {
            if (!literal_match(l, stream)) {
                /* at least one fail for conjunction */
                and_success = false;
                break;
            }
        }

        if (and_success) {
            /* at least one match for disjunction */
            return true;
        }
    }

    /* no matches */
    return false;
}

/* stream classification */

static bool literal_match(struct literal *literal, pas_stream *stream) {

    if (literal->stream_direction != match_direction_unknown) {
        /* check the stream direction; _sink inputs_ are always _outputs_ */

        if ((stream->direction == PA_DIRECTION_OUTPUT && literal->stream_direction == match_direction_output) ||
            (stream->direction == PA_DIRECTION_INPUT && literal->stream_direction == match_direction_input)) {
            return literal->negation ? false : true;
        }
    }
    else if (literal->property_name && literal->property_value) {
        /* check the property from the property list */

        if (pa_proplist_contains(stream->proplist, literal->property_name)) {
            const char *prop = pa_proplist_gets(stream->proplist, literal->property_name);

            if (prop && strcmp(prop, literal->property_value) == 0)
                return literal->negation ? false : true;
        }
    }

    /* no match */
    return literal->negation ? true : false;
}

static pa_hook_result_t stream_put_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pas_stream *stream = call_data;
    struct stream_rule *rule;
    unsigned idx;

    pa_assert(u);
    pa_assert(stream);

    PA_DYNARRAY_FOREACH(rule, u->stream_rules_list, idx) {
        if (stream_rule_match(rule, stream)) {
            pa_hashmap_put(u->rules_by_stream, stream, rule);
            break;
        }
    }

    return PA_HOOK_OK;
}

static pa_hook_result_t stream_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pas_stream *stream = call_data;

    pa_assert(u);
    pa_assert(stream);

    pa_hashmap_remove(u->rules_by_stream, stream);

    return PA_HOOK_OK;
}

static pa_hook_result_t volume_control_implementation_initialized_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_volume_control *volume_control = call_data;
    struct control *control;

    pa_assert(u);
    pa_assert(volume_control);

    if (volume_control->purpose != PA_VOLUME_CONTROL_PURPOSE_STREAM_RELATIVE_VOLUME)
        return PA_HOOK_OK;

    control = control_new_for_stream(u, CONTROL_TYPE_VOLUME, volume_control->owner_stream);
    control_put(control);
    pa_assert_se(pa_hashmap_put(u->stream_volume_controls, volume_control->owner_stream, control) >= 0);

    return PA_HOOK_OK;
}

static pa_hook_result_t mute_control_implementation_initialized_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_mute_control *mute_control = call_data;
    struct control *control;

    pa_assert(u);
    pa_assert(mute_control);

    if (mute_control->purpose != PA_MUTE_CONTROL_PURPOSE_STREAM_MUTE)
        return PA_HOOK_OK;

    control = control_new_for_stream(u, CONTROL_TYPE_MUTE, mute_control->owner_stream);
    control_put(control);
    pa_assert_se(pa_hashmap_put(u->stream_mute_controls, mute_control->owner_stream, control) >= 0);

    return PA_HOOK_OK;
}

static pa_hook_result_t volume_control_set_initial_volume_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_volume_control *volume_control = call_data;
    struct stream_rule *rule;
    struct control *control;

    pa_assert(u);
    pa_assert(volume_control);

    if (volume_control->purpose != PA_VOLUME_CONTROL_PURPOSE_STREAM_RELATIVE_VOLUME)
        return PA_HOOK_OK;

    rule = pa_hashmap_get(u->rules_by_stream, volume_control->owner_stream);
    if (!rule)
        return PA_HOOK_OK;

    if (!rule->group_for_volume)
        return PA_HOOK_OK;

    if (!rule->group_for_volume->volume_control)
        return PA_HOOK_OK;

    control = pa_hashmap_get(u->stream_volume_controls, volume_control->owner_stream);
    pa_assert(control);
    pa_assert(control->volume_control == volume_control);

    /* This will set the volume for volume_control. */
    control_set_master(control, rule->group_for_volume->volume_control);

    return PA_HOOK_STOP;
}

static pa_hook_result_t mute_control_set_initial_mute_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_mute_control *mute_control = call_data;
    struct stream_rule *rule;
    struct control *control;

    pa_assert(u);
    pa_assert(mute_control);

    if (mute_control->purpose != PA_MUTE_CONTROL_PURPOSE_STREAM_MUTE)
        return PA_HOOK_OK;

    rule = pa_hashmap_get(u->rules_by_stream, mute_control->owner_stream);
    if (!rule)
        return PA_HOOK_OK;

    if (!rule->group_for_mute)
        return PA_HOOK_OK;

    if (!rule->group_for_mute->mute_control)
        return PA_HOOK_OK;

    control = pa_hashmap_get(u->stream_mute_controls, mute_control->owner_stream);
    pa_assert(control);
    pa_assert(control->mute_control == mute_control);

    /* This will set the mute for mute_control. */
    control_set_master(control, rule->group_for_mute->mute_control);

    return PA_HOOK_STOP;
}

static pa_hook_result_t volume_control_volume_changed_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_volume_control *volume_control = call_data;
    struct control *control;

    pa_assert(u);
    pa_assert(volume_control);

    if (volume_control->purpose != PA_VOLUME_CONTROL_PURPOSE_STREAM_RELATIVE_VOLUME)
        return PA_HOOK_OK;

    control = pa_hashmap_get(u->stream_volume_controls, volume_control->owner_stream);
    if (!control)
        return PA_HOOK_OK;

    if (!control->master)
        return PA_HOOK_OK;

    pa_volume_control_set_volume(control->master->volume_control, &volume_control->volume, true, true);

    return PA_HOOK_OK;
}

static pa_hook_result_t mute_control_mute_changed_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_mute_control *mute_control = call_data;
    struct control *control;

    pa_assert(u);
    pa_assert(mute_control);

    if (mute_control->purpose != PA_MUTE_CONTROL_PURPOSE_STREAM_MUTE)
        return PA_HOOK_OK;

    control = pa_hashmap_get(u->stream_mute_controls, mute_control->owner_stream);
    if (!control)
        return PA_HOOK_OK;

    if (!control->master)
        return PA_HOOK_OK;

    pa_mute_control_set_mute(control->master->mute_control, mute_control->mute);

    return PA_HOOK_OK;
}

static pa_hook_result_t volume_control_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_volume_control *volume_control = call_data;
    struct control *control;

    pa_assert(u);
    pa_assert(volume_control);

    if (volume_control->purpose != PA_VOLUME_CONTROL_PURPOSE_STREAM_RELATIVE_VOLUME)
        return PA_HOOK_OK;

    control = pa_hashmap_remove(u->stream_volume_controls, volume_control->owner_stream);
    if (!control)
        return PA_HOOK_OK;

    control_free(control);

    return PA_HOOK_OK;
}

static pa_hook_result_t mute_control_unlink_cb(void *hook_data, void *call_data, void *userdata) {
    struct userdata *u = userdata;
    pa_mute_control *mute_control = call_data;
    struct control *control;

    pa_assert(u);
    pa_assert(mute_control);

    if (mute_control->purpose != PA_MUTE_CONTROL_PURPOSE_STREAM_MUTE)
        return PA_HOOK_OK;

    control = pa_hashmap_remove(u->stream_mute_controls, mute_control->owner_stream);
    if (!control)
        return PA_HOOK_OK;

    control_free(control);

    return PA_HOOK_OK;
}

/* parser for configuration file */

/*
    Parse the match expression. The syntax is this:

    OPER           := "AND" | "OR"
    OPEN_BRACE     := "("
    CLOSE_BRACE    := ")"
    EXPR           := OPEN_BRACE EXPR OPER EXPR CLOSE_BRACE | VAR
    VAR            := LIT | "NEG" LIT
    LIT            := PREDICATE (defined by rule semantics)

    In addition there is a requirement that the expressions need to be in
    disjunctive normal form. It means that if there is an expression that
    has AND operator, there may not be any OR operators in its subexpressions.

    Example expressions:

    (foo)
    (foo AND bar)
    (foo OR (bar AND xxx))
    (NEG foo OR (bar AND NEG xxx))

    The predicate here is the single rule that is matched against the new sink
    input. The syntax is this:

    PREDICATE      := "direction" DIRECTION  | "property" PROPERTY
    DIRECTION      := "input" | "output"
    PROPERTY       := PROPERTY_NAME "=" PROPERTY_VALUE
    PROPERTY_NAME  := STRING
    PROPERTY_VALUE := STRING

    The allowed characters for STRING are standard ascii characters. Not
    allowed substrings are the reserved words "AND", "OR", "(", ")", "NEG" and
    "=".

    Complete examples:

    (property application.process.binary=paplay)

    (property media.role=music AND direction input)

    (property application.process.binary=paplay OR (direction input OR direction output))
*/

#if 0
static void print_literal(struct literal *l) {
    if (l->stream_direction != match_direction_unknown) {
        pa_log_info("       %sstream direction %s",
                l->negation ? "NEG " : "",
                l->stream_direction == match_direction_input ? "input" : "output");
    }
    else {
        pa_log_info("       %sproperty %s == %s",
                l->negation ? "NEG " : "",
                l->property_name ? l->property_name : "NULL",
                l->property_value ? l->property_value : "NULL");
    }
}

static void print_conjunction(struct conjunction *c) {
    struct literal *l;
    pa_log_info("   conjunction for literals:");
    PA_LLIST_FOREACH(l, c->literals) {
        print_literal(l);
    }
}

static void print_expression(struct expression *e) {
    struct conjunction *c;
    pa_log_info("disjunction for conjunctions:");
    PA_LLIST_FOREACH(c, e->conjunctions) {
        print_conjunction(c);
    }
}
#endif

static void delete_literal(struct literal *l) {

    if (!l)
        return;

    pa_xfree(l->property_name);
    pa_xfree(l->property_value);
    pa_xfree(l);
}

static void delete_conjunction(struct conjunction *c) {
    struct literal *l;

    if (!c)
        return;

    PA_LLIST_FOREACH(l, c->literals) {
        delete_literal(l);
    }

    pa_xfree(c);
}

static struct expression *expression_new(void) {
    struct expression *expression;

    expression = pa_xnew0(struct expression, 1);

    return expression;
}

static void expression_free(struct expression *expression) {
    struct conjunction *c;

    pa_assert(expression);

    PA_LLIST_FOREACH(c, expression->conjunctions)
        delete_conjunction(c);

    pa_xfree(expression);
}

enum logic_operator {
    operator_not_set = 0,
    operator_and,
    operator_or,
};

struct expression_token {
    struct expression_token *left;
    struct expression_token *right;

    enum logic_operator oper;

    struct literal_token *lit;
};

struct literal_token {
    bool negation;
    char *var;
};

static void delete_literal_token(struct literal_token *l) {

    if (!l)
        return;

    pa_xfree(l->var);
    pa_xfree(l);
}

static void delete_expression_token(struct expression_token *e) {

    if (!e)
        return;

    delete_expression_token(e->left);
    delete_expression_token(e->right);
    delete_literal_token(e->lit);

    e->left = NULL;
    e->right = NULL;
    e->lit = NULL;

    pa_xfree(e);
}

static struct expression_token *parse_rule_internal(const char *rule, bool disjunction_allowed) {

    int len = strlen(rule);
    struct expression_token *et;
    char *p;
    int brace_count = 0;
    bool braces_present = false;
    char left_buf[len+1];
    char right_buf[len+1];

    et = pa_xnew0(struct expression_token, 1);

    if (!et)
        return NULL;

    /* count the braces -- we want to find the case when there is only one brace open */

    p = (char *) rule;

    while (*p) {
        if (*p == '(') {
            braces_present = true;
            brace_count++;
        }
        else if (*p == ')') {
            brace_count--;
        }

        if (brace_count == 1) {

            /* the parser is recursive and just goes down the tree on the
             * topmost level (where the brace count is 1). If there are no
             * braces this is a literal */

            /* find the operator AND or OR */

            if (strncmp(p, "AND", 3) == 0) {

                /* copy parts */
                char *begin_left = (char *) rule+1;
                char *begin_right = p+3;

                int left_len = p - rule - 1; /* minus '(' */
                int right_len = len - 3 - left_len - 2; /* minus AND and '(' and ')'*/

                memcpy(left_buf, begin_left, left_len);
                left_buf[left_len] = '\0';
                memcpy(right_buf, begin_right, right_len);
                right_buf[right_len] = '\0';

                et->left = parse_rule_internal(left_buf, false);
                et->right = parse_rule_internal(right_buf, false);
                et->oper = operator_and;

                if (!et->left || !et->right) {
                    delete_expression_token(et);
                    return NULL;
                }

                return et;
            }
            else if (strncmp(p, "OR", 2) == 0) {

                char *begin_left = (char *) rule+1;
                char *begin_right = p+2;

                int left_len = p - rule - 1; /* minus '(' */
                int right_len = len - 2 - left_len - 2; /* minus OR and '(' and ')'*/

                if (!disjunction_allowed) {
                    pa_log_error("logic expression not in dnf");
                    delete_expression_token(et);
                    return NULL;
                }

                memcpy(left_buf, begin_left, left_len);
                left_buf[left_len] = '\0';
                memcpy(right_buf, begin_right, right_len);
                right_buf[right_len] = '\0';

                et->left = parse_rule_internal(left_buf, true);
                et->right = parse_rule_internal(right_buf, true);
                et->oper = operator_or;

                if (!et->left || !et->right) {
                    delete_expression_token(et);
                    return NULL;
                }

                return et;
            }
            /* else a literal which is inside braces */
        }

        p++;
    }

    if (brace_count != 0) {
        /* the input is not valid */
        pa_log_error("mismatched braces in logic expression");
        delete_expression_token(et);
        return NULL;
    }
    else {
        /* this is a literal */
        char *begin_lit;
        char buf[len+1];
        struct literal_token *lit = pa_xnew0(struct literal_token, 1);

        if (!lit) {
            delete_expression_token(et);
            return NULL;
        }

        if (braces_present) {
            /* remove all braces */
            char *k;
            char *l;

            k = (char *) rule;
            l = buf;

            while (*k) {
                if (*k == '(' || *k == ')') {
                    k++;
                    continue;
                }

                *l = *k;
                l++;
                k++;
            }
            *l = '\0';
        }
        else {
            strncpy(buf, rule, len);
            buf[len] = '\0';
        }

        if (strncmp(buf, "NEG", 3) == 0) {
            begin_lit = (char *) buf + 3;
            lit->negation = true;
        }
        else {
            begin_lit = (char *) buf;
            lit->negation = false;
        }

        lit->var = pa_xstrdup(begin_lit);

        et->lit = lit;
    }

    return et;
}

static bool gather_literal(struct expression_token *et, struct literal *l) {
#define PROPERTY_KEYWORD "property"
#define DIRECTION_KEYWORD "direction"
#define DIRECTION_VALUE_INPUT "input"
#define DIRECTION_VALUE_OUTPUT "output"

    char *p = et->lit->var;
    int len = strlen(et->lit->var);

    l->negation = et->lit->negation;

    if (strncmp(p, PROPERTY_KEYWORD, strlen(PROPERTY_KEYWORD)) == 0) {
        char name[len];
        char value[len];
        int i = 0;

        p += strlen(PROPERTY_KEYWORD);

        /* parse the property pair: name=value */

        while (*p && *p != '=') {
            name[i++] = *p;
            p++;
        }

        /* check if we really found '=' */

        if (*p != '=') {
            pa_log_error("property syntax broken for '%s'", et->lit->var);
            goto error;
        }

        name[i] = '\0';

        p++;
        i = 0;

        while (*p) {
            value[i++] = *p;
            p++;
        }

        value[i] = '\0';

        l->property_name = pa_xstrdup(name);
        l->property_value = pa_xstrdup(value);
    }
    else if (strncmp(p, DIRECTION_KEYWORD, strlen(DIRECTION_KEYWORD)) == 0) {
        p += strlen(DIRECTION_KEYWORD);

        if (strncmp(p, DIRECTION_VALUE_INPUT, strlen(DIRECTION_VALUE_INPUT)) == 0) {
            l->stream_direction = match_direction_input;
        }
        else if (strncmp(p, DIRECTION_VALUE_OUTPUT, strlen(DIRECTION_VALUE_OUTPUT)) == 0) {
            l->stream_direction = match_direction_output;
        }
        else {
            pa_log_error("unknown direction(%s): %s", et->lit->var, p);
            goto error;
        }
    }
    else {
        pa_log_error("not able to parse the value: '%s'", et->lit->var);
        goto error;
    }

    return true;

error:
    return false;

#undef DIRECTION_VALUE_OUTPUT
#undef DIRECTION_VALUE_INPUT
#undef DIRECTION_KEYWORD
#undef PROPERTY_KEYWORD
}

static bool gather_conjunction(struct expression_token *et, struct conjunction *c) {

    if (et->oper == operator_and) {
        if (!gather_conjunction(et->left, c) ||
            !gather_conjunction(et->right, c)) {
            return false;
        }
    }
    else {
        /* literal */
        struct literal *l = pa_xnew0(struct literal, 1);

        if (!l)
            return false;

        if (!gather_literal(et, l)) {
            pa_log_error("audio groups config: literal parsing failed");
            delete_literal(l);
            return false;
        }

        PA_LLIST_INIT(struct literal, l);
        PA_LLIST_PREPEND(struct literal, c->literals, l);
    }

    return true;
}

static bool gather_expression(struct expression *e, struct expression_token *et) {

    if (et->oper == operator_or) {
        if (!gather_expression(e, et->right) ||
            !gather_expression(e, et->left))
            return false;
    }
    else {
        /* conjunction or literal */
        struct conjunction *c = pa_xnew0(struct conjunction, 1);

        if (!c)
            return false;

        PA_LLIST_HEAD_INIT(struct literal, c->literals);

        if (!gather_conjunction(et, c)) {
            delete_conjunction(c);
            return false;
        }

        PA_LLIST_INIT(struct conjunction, c);
        PA_LLIST_PREPEND(struct conjunction, e->conjunctions, c);
    }

    return true;
}

static int expression_from_string(const char *str, struct expression **_r) {
    const char *k;
    char *l;
    struct expression *e = NULL;
    char *buf = NULL;
    struct expression_token *et = NULL;

    pa_assert(str);
    pa_assert(_r);

    buf = pa_xmalloc0(strlen(str) + 1);

    /* remove whitespace */

    k = str;
    l = buf;

    while (*k) {
        if (*k == ' ') {
            k++;
            continue;
        }

        *l = *k;
        l++;
        k++;
    }

    /* et is the root of an expression tree */
    et = parse_rule_internal(buf, true);

    if (!et)
        goto error;

    e = pa_xnew0(struct expression, 1);

    PA_LLIST_HEAD_INIT(struct conjunction, e->conjunctions);

    /* gather expressions to actual match format */
    if (!gather_expression(e, et)) {
        /* gathering the expression from tokens went wrong */
        pa_log_error("failed to parse audio group stream classification data");
        goto error;
    }

#if 0
    print_expression(e);
#endif

    /* free memory */
    delete_expression_token(et);
    pa_xfree(buf);

    *_r = e;
    return 0;

error:
    delete_expression_token(et);
    pa_xfree(buf);
    expression_free(e);

    return -PA_ERR_INVALID;
}

static int parse_streams(pa_config_parser_state *state) {
    struct userdata *u;
    char *name;
    const char *split_state = NULL;

    pa_assert(state);

    u = state->userdata;

    while ((name = pa_split_spaces(state->rvalue, &split_state))) {
        const char *name2;
        unsigned idx;
        bool duplicate = false;

        /* Avoid adding duplicates in u->stream_rule_names. */
        PA_DYNARRAY_FOREACH(name2, u->stream_rule_names, idx) {
            if (pa_streq(name, name2)) {
                duplicate = true;
                break;
            }
        }

        if (duplicate) {
            pa_xfree(name);
            continue;
        }

        pa_dynarray_append(u->stream_rule_names, name);
    }

    return 0;
}

static int parse_group_control(pa_config_parser_state *state, struct group *group, enum control_type type) {
    pa_assert(state);
    pa_assert(group);

    if (pa_streq(state->rvalue, NONE_KEYWORD))
        group_disable_control(group, type);

    else if (pa_startswith(state->rvalue, CREATE_PREFIX))
        group_set_own_control_name(group, type, state->rvalue + strlen(CREATE_PREFIX));

    else if (pa_startswith(state->rvalue, BIND_PREFIX)) {
        if (pa_startswith(state->rvalue, BIND_AUDIO_GROUP_PREFIX)) {
            int r;

            r = group_set_master_name(group, type, state->rvalue + strlen(BIND_AUDIO_GROUP_PREFIX));
            if (r < 0) {
                pa_log("[%s:%u] Failed to set binding target \"%s\".", state->filename, state->lineno, state->rvalue + strlen(BIND_PREFIX));
                return r;
            }
        } else {
            pa_log("[%s:%u] Failed to parse binding target \"%s\".", state->filename, state->lineno, state->rvalue + strlen(BIND_PREFIX));
            return -PA_ERR_INVALID;
        }
    } else {
        pa_log("[%s:%u] Failed to parse value \"%s\".", state->filename, state->lineno, state->rvalue);
        return -PA_ERR_INVALID;
    }

    return 0;
}

static int parse_common(pa_config_parser_state *state) {
    char *section;
    struct userdata *u = state->userdata;
    const char *name;
    int r;

    pa_assert(state);

    section = state->section;
    if (!section) {
        pa_log("[%s:%u] Lvalue \"%s\" not expected in the General section.", state->filename, state->lineno, state->lvalue);
        return -PA_ERR_INVALID;
    }

    if (pa_startswith(section, AUDIOGROUP_START)) {
        struct group *group;

        name = section + strlen(AUDIOGROUP_START);

        group = pa_hashmap_get(u->groups, name);
        if (!group) {
            r = group_new(u, name, &group);
            if (r < 0) {
                pa_log("[%s:%u] Failed to create an audio group with name \"%s\".", state->filename, state->lineno, name);
                return r;
            }

            pa_hashmap_put(u->groups, (void *) group->audio_group->name, group);
        }

        if (pa_streq(state->lvalue, "description"))
            pa_audio_group_set_description(group->audio_group, state->rvalue);

        else if (pa_streq(state->lvalue, "volume-control"))
            return parse_group_control(state, group, CONTROL_TYPE_VOLUME);

        else if (pa_streq(state->lvalue, "mute-control"))
            return parse_group_control(state, group, CONTROL_TYPE_MUTE);

        else {
            pa_log("[%s:%u] Lvalue \"%s\" not expected in the AudioGroup section.", state->filename, state->lineno, state->lvalue);
            return -PA_ERR_INVALID;
        }
    }
    else if (pa_startswith(section, STREAM_RULE_START)) {
        struct stream_rule *rule;

        name = section + strlen(STREAM_RULE_START);

        rule = pa_hashmap_get(u->stream_rules, name);
        if (!rule) {
            rule = stream_rule_new(u, name);
            pa_hashmap_put(u->stream_rules, rule->name, rule);
        }

        if (pa_streq(state->lvalue, "audio-group-for-volume"))
            stream_rule_set_group_name(rule, CONTROL_TYPE_VOLUME, state->rvalue);

        else if (pa_streq(state->lvalue, "audio-group-for-mute"))
            stream_rule_set_group_name(rule, CONTROL_TYPE_MUTE, state->rvalue);

        else if (pa_streq(state->lvalue, "match")) {
            struct expression *expression;

            r = expression_from_string(state->rvalue, &expression);
            if (r < 0) {
                pa_log("[%s:%u] Failed to parse value \"%s\".", state->filename, state->lineno, state->rvalue);
                return r;
            }

            stream_rule_set_match_expression(rule, expression);
        }
    }

    return 0;
}

int pa__init(pa_module *module) {
    pa_modargs *ma = NULL;
    struct userdata *u;
    FILE *f;
    char *fn = NULL;
    struct group *group;
    void *state;
    const char *name;
    unsigned idx;

    pa_assert(module);

    if (!(ma = pa_modargs_new(module->argument, valid_modargs))) {
        pa_log("Failed to parse module arguments");
        goto fail;
    }

    u = module->userdata = pa_xnew0(struct userdata, 1);
    u->volume_api = pa_volume_api_get(module->core);
    u->groups = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL,
                                    (pa_free_cb_t) group_free);
    u->stream_rules = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL,
                                          (pa_free_cb_t) stream_rule_free);
    u->stream_rules_list = pa_dynarray_new(NULL);
    u->rules_by_stream = pa_hashmap_new(NULL, NULL);
    u->stream_volume_controls = pa_hashmap_new_full(NULL, NULL, NULL, (pa_free_cb_t) control_free);
    u->stream_mute_controls = pa_hashmap_new_full(NULL, NULL, NULL, (pa_free_cb_t) control_free);
    u->stream_put_slot = pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_PUT], PA_HOOK_NORMAL, stream_put_cb,
                                         u);
    u->stream_unlink_slot = pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_STREAM_UNLINK], PA_HOOK_NORMAL,
                                            stream_unlink_cb, u);
    u->volume_control_implementation_initialized_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_IMPLEMENTATION_INITIALIZED],
                            PA_HOOK_NORMAL, volume_control_implementation_initialized_cb, u);
    u->mute_control_implementation_initialized_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_IMPLEMENTATION_INITIALIZED],
                            PA_HOOK_NORMAL, mute_control_implementation_initialized_cb, u);
    u->volume_control_set_initial_volume_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_SET_INITIAL_VOLUME], PA_HOOK_NORMAL,
                            volume_control_set_initial_volume_cb, u);
    u->mute_control_set_initial_mute_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_SET_INITIAL_MUTE], PA_HOOK_NORMAL,
                            mute_control_set_initial_mute_cb, u);
    u->volume_control_volume_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_VOLUME_CHANGED], PA_HOOK_NORMAL,
                            volume_control_volume_changed_cb, u);
    u->mute_control_mute_changed_slot =
            pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_MUTE_CHANGED], PA_HOOK_NORMAL,
                            mute_control_mute_changed_cb, u);
    u->volume_control_unlink_slot = pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_VOLUME_CONTROL_UNLINK],
                                                    PA_HOOK_NORMAL, volume_control_unlink_cb, u);
    u->mute_control_unlink_slot = pa_hook_connect(&u->volume_api->hooks[PA_VOLUME_API_HOOK_MUTE_CONTROL_UNLINK],
                                                  PA_HOOK_NORMAL, mute_control_unlink_cb, u);
    u->stream_rule_names = pa_dynarray_new(pa_xfree);

    f = pa_open_config_file(PA_DEFAULT_CONFIG_DIR PA_PATH_SEP "audio-groups.conf", "audio-groups.conf", NULL, &fn);
    if (f) {
        pa_config_item config_items[] = {
            { "stream-rules", parse_streams, NULL, "General" },
            { NULL, parse_common, NULL, NULL },
            { NULL, NULL, NULL, NULL },
        };

        pa_config_parse(fn, f, config_items, NULL, u);
        pa_xfree(fn);
        fn = NULL;
        fclose(f);
        f = NULL;
    }

    PA_HASHMAP_FOREACH(group, u->groups, state)
        group_put(group);

    PA_DYNARRAY_FOREACH(name, u->stream_rule_names, idx) {
        struct stream_rule *rule;

        rule = pa_hashmap_get(u->stream_rules, name);
        if (rule)
            pa_dynarray_append(u->stream_rules_list, rule);
        else
            pa_log("Non-existent stream rule \"%s\" referenced, ignoring.", name);
    }

    pa_dynarray_free(u->stream_rule_names);
    u->stream_rule_names = NULL;

    pa_modargs_free(ma);

    return 0;

fail:
    pa__done(module);

    if (ma)
        pa_modargs_free(ma);

    return -1;
}

void pa__done(pa_module *m) {
    struct userdata *u;

    pa_assert(m);

    u = (struct userdata *) m->userdata;

    if (!u)
        return;

    if (u->mute_control_unlink_slot)
        pa_hook_slot_free(u->mute_control_unlink_slot);

    if (u->volume_control_unlink_slot)
        pa_hook_slot_free(u->volume_control_unlink_slot);

    if (u->mute_control_mute_changed_slot)
        pa_hook_slot_free(u->mute_control_mute_changed_slot);

    if (u->volume_control_volume_changed_slot)
        pa_hook_slot_free(u->volume_control_volume_changed_slot);

    if (u->mute_control_set_initial_mute_slot)
        pa_hook_slot_free(u->mute_control_set_initial_mute_slot);

    if (u->volume_control_set_initial_volume_slot)
        pa_hook_slot_free(u->volume_control_set_initial_volume_slot);

    if (u->mute_control_implementation_initialized_slot)
        pa_hook_slot_free(u->mute_control_implementation_initialized_slot);

    if (u->volume_control_implementation_initialized_slot)
        pa_hook_slot_free(u->volume_control_implementation_initialized_slot);

    if (u->stream_unlink_slot)
        pa_hook_slot_free(u->stream_unlink_slot);

    if (u->stream_put_slot)
        pa_hook_slot_free(u->stream_put_slot);

    if (u->stream_mute_controls)
        pa_hashmap_free(u->stream_mute_controls);

    if (u->stream_volume_controls)
        pa_hashmap_free(u->stream_volume_controls);

    if (u->rules_by_stream)
        pa_hashmap_free(u->rules_by_stream);

    if (u->stream_rules_list)
        pa_dynarray_free(u->stream_rules_list);

    if (u->stream_rules)
        pa_hashmap_free(u->stream_rules);

    if (u->groups)
        pa_hashmap_free(u->groups);

    if (u->volume_api)
        pa_volume_api_unref(u->volume_api);

    pa_xfree(u);
}
