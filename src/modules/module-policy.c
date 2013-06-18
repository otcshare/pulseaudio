#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/core.h>
#include <pulsecore/module.h>
#include <pulsecore/modargs.h>
#include <pulsecore/core-rtclock.h>
#include <pulsecore/core-util.h>
#include <pulsecore/log.h>
#include <stdbool.h>
#include <strings.h>

#include <pulsecore/log.h>
#include <pulsecore/core-subscribe.h>
#include <pulsecore/sink-input.h>
#include <pulsecore/source-output.h>
#include <pulsecore/namereg.h>
#include <pulsecore/core-error.h>

#include <pulsecore/protocol-native.h>
#include <pulsecore/pstream-util.h>
#include <vconf.h> // for mono

#include "module-policy-symdef.h"

PA_MODULE_AUTHOR("Seungbae Shin");
PA_MODULE_DESCRIPTION("Media Policy module");
PA_MODULE_VERSION(PACKAGE_VERSION);
PA_MODULE_LOAD_ONCE(true);
PA_MODULE_USAGE(
        "on_hotplug=<When new device becomes available, recheck streams?> ");

static const char* const valid_modargs[] = {
    "on_hotplug",
    NULL
};

struct userdata {
    pa_core *core;
    pa_module *module;

    pa_hook_slot *sink_input_new_hook_slot,*sink_put_hook_slot;

    pa_hook_slot *sink_input_unlink_slot,*sink_unlink_slot;
    pa_hook_slot *sink_input_unlink_post_slot, *sink_unlink_post_slot;
    pa_hook_slot *sink_input_move_start_slot,*sink_input_move_finish_slot;
    pa_subscription *subscription;

    bool on_hotplug:1;
    int	bt_off_idx;

    int is_mono;
    float balance;
    pa_module* module_mono_bt;
    pa_module* module_combined;
    pa_module* module_mono_combined;
    pa_native_protocol *protocol;
    pa_hook_slot *source_output_new_hook_slot;
};

enum {
    SUBCOMMAND_TEST,
    SUBCOMMAND_MONO,
    SUBCOMMAND_BALANCE,
};

/* DEFINEs */
#define AEC_SINK			"alsa_output.0.analog-stereo.echo-cancel"
#define AEC_SOURCE			"alsa_input.0.analog-stereo.echo-cancel"
#define	SINK_ALSA			"alsa_output.0.analog-stereo"
#define SINK_MONO_ALSA		"mono_alsa"
#define SINK_MONO_BT		"mono_bt"
#define SINK_COMBINED		"combined"
#define SINK_MONO_COMBINED	"mono_combined"
#define POLICY_AUTO			"auto"
#define POLICY_PHONE		"phone"
#define POLICY_ALL			"all"
#define POLICY_VOIP			"voip"
#define BLUEZ_API			"bluez"
#define ALSA_API			"alsa"
#define MONO_KEY 			VCONFKEY_SETAPPL_ACCESSIBILITY_MONO_AUDIO

/* check if this sink is bluez */
static bool policy_is_bluez (pa_sink* sink)
{
	const char* api_name = NULL;

	if (sink == NULL) {
		pa_log_warn ("input param sink is null");
		return false;
	}

    api_name = pa_proplist_gets(sink->proplist, PA_PROP_DEVICE_API);
	if (api_name) {
		if (pa_streq (api_name, BLUEZ_API)) {
#ifdef DEBUG_DETAIL
			pa_log_debug("[POLICY][%s] [%s] exists and it is [%s]...true !!", __func__, PA_PROP_DEVICE_API, api_name);
#endif
			return true;
		} else {
#ifdef DEBUG_DETAIL
			pa_log_debug("[POLICY][%s] [%s] exists, but not bluez...false !!", __func__, PA_PROP_DEVICE_API);
#endif
		}
	} else {
#ifdef DEBUG_DETAIL
		pa_log_debug("[POLICY][%s] No [%s] exists...false!!", __func__, PA_PROP_DEVICE_API);
#endif
	}

	return false;
}

/* check if this sink is bluez */
static bool policy_is_usb_alsa (pa_sink* sink)
{
	const char* api_name = NULL;
	const char* device_bus_name = NULL;

	if (sink == NULL) {
		pa_log_warn ("input param sink is null");
		return false;
	}

    api_name = pa_proplist_gets(sink->proplist, PA_PROP_DEVICE_API);
	if (api_name) {
		if (pa_streq (api_name, ALSA_API)) {
#ifdef DEBUG_DETAIL
			pa_log_debug("[POLICY][%s] [%s] exists and it is [%s]...true !!", __func__, PA_PROP_DEVICE_API, api_name);
#endif
			device_bus_name = pa_proplist_gets(sink->proplist, PA_PROP_DEVICE_BUS);
			if (device_bus_name) {
				if (pa_streq (device_bus_name, "usb")) {
					return true;
				}
			}
		} else {
#ifdef DEBUG_DETAIL
			pa_log_debug("[POLICY][%s] [%s] exists, but not alsa...false !!", __func__, PA_PROP_DEVICE_API);
#endif
		}
	} else {
#ifdef DEBUG_DETAIL
		pa_log_debug("[POLICY][%s] No [%s] exists...false!!", __func__, PA_PROP_DEVICE_API);
#endif
	}

	return false;
}

/* Get sink by name */
static pa_sink* policy_get_sink_by_name (pa_core *c, const char* sink_name)
{
    pa_sink *s = NULL;
    uint32_t idx;

    if (c == NULL || sink_name == NULL) {
		pa_log_warn ("input param is null");
		return NULL;
    }

	PA_IDXSET_FOREACH(s, c->sinks, idx) {
		if (pa_streq (s->name, sink_name)) {
			pa_log_debug ("[POLICY][%s] return [%p] for [%s]\n",  __func__, s, sink_name);
			return s;
		}
	}
	return NULL;
}

/* Get bt sink if available */
static pa_sink* policy_get_bt_sink (pa_core *c)
{
    pa_sink *s = NULL;
    uint32_t idx;

    if (c == NULL) {
		pa_log_warn ("input param is null");
		return NULL;
    }

	PA_IDXSET_FOREACH(s, c->sinks, idx) {
		if (policy_is_bluez (s)) {
			pa_log_debug ("[POLICY][%s] return [%p] for [%s]\n", __func__, s, s->name);
			return s;
		}
	}
	return NULL;
}

/* Select sink for given condition */
static pa_sink* policy_select_proper_sink (pa_core *c, const char* policy, int is_mono)
{
	pa_sink* sink = NULL;
	pa_sink* bt_sink = NULL;
	pa_sink* def = NULL;

	if (c == NULL || policy == NULL) {
		pa_log_warn ("input param is null");
		return NULL;
	}

	pa_assert (c);

	bt_sink = policy_get_bt_sink(c);
	def = pa_namereg_get_default_sink(c);
	if (def == NULL) {
		pa_log_warn ("POLICY][%s] pa_namereg_get_default_sink() returns null", __func__);
		return NULL;
	}

	pa_log_debug ("[POLICY][%s] policy[%s], is_mono[%d], current default[%s], bt sink[%s]\n",
			__func__, policy, is_mono, def->name, (bt_sink)? bt_sink->name:"null");

	/* Select sink to */
	if (pa_streq(policy, POLICY_ALL)) {
		/* all */
		if (bt_sink) {
			sink = policy_get_sink_by_name(c, (is_mono)? SINK_MONO_COMBINED : SINK_COMBINED);
		} else {
			sink = policy_get_sink_by_name (c, (is_mono)? SINK_MONO_ALSA : SINK_ALSA);
		}

	} else if (pa_streq(policy, POLICY_PHONE)) {
		/* phone */
		sink = policy_get_sink_by_name (c, (is_mono)? SINK_MONO_ALSA : SINK_ALSA);
	} else if (pa_streq(policy, POLICY_VOIP)) {
		/* VOIP */
		sink = policy_get_sink_by_name (c,AEC_SINK);
	} else {
		/* auto */
		if (policy_is_bluez(def)) {
			sink = (is_mono)? policy_get_sink_by_name (c, SINK_MONO_BT) : def;
		} else if (policy_is_usb_alsa(def)) {
			sink = def;
		} else {
			sink = (is_mono)? policy_get_sink_by_name (c, SINK_MONO_ALSA) : def;
		}
	}

	pa_log_debug ("[POLICY][%s] selected sink : [%s]\n", __func__, (sink)? sink->name : "null");
	return sink;
}

static bool policy_is_filter (pa_sink_input* si)
{
	const char* role = NULL;

	if (si == NULL) {
		pa_log_warn ("input param sink-input is null");
		return false;
	}

	if ((role = pa_proplist_gets(si->proplist, PA_PROP_MEDIA_ROLE))) {
#ifdef DEBUG_DETAIL
		pa_log_debug("[POLICY][%s] Role of sink input [%d] = %s", __func__, si->index, role);
#endif
		if (pa_streq(role, "filter")) {
#ifdef DEBUG_DETAIL
			pa_log_debug("[POLICY] no need to change of sink for %s", role);
#endif
			return true;
		}
	}

	return false;
}



#define EXT_VERSION 1

static int extension_cb(pa_native_protocol *p, pa_module *m, pa_native_connection *c, uint32_t tag, pa_tagstruct *t) {
  struct userdata *u = NULL;
  uint32_t command;
  pa_tagstruct *reply = NULL;

  pa_sink_input *si = NULL;
  pa_sink *s = NULL;
  uint32_t idx;
  pa_sink* sink_to_move  = NULL;

  pa_assert(p);
  pa_assert(m);
  pa_assert(c);
  pa_assert(t);

  u = m->userdata;

  if (pa_tagstruct_getu32(t, &command) < 0)
    goto fail;

  reply = pa_tagstruct_new(NULL, 0);
  pa_tagstruct_putu32(reply, PA_COMMAND_REPLY);
  pa_tagstruct_putu32(reply, tag);

  switch (command) {
    case SUBCOMMAND_TEST: {
		if (!pa_tagstruct_eof(t))
			goto fail;

		pa_tagstruct_putu32(reply, EXT_VERSION);
		break;
    }

    case SUBCOMMAND_MONO: {

        bool enable;

        if (pa_tagstruct_get_boolean(t, &enable) < 0)
            goto fail;

        pa_log_debug ("[POLICY][%s] new mono value = %d\n", __func__, enable);
        if (enable == u->is_mono) {
			pa_log_debug ("[POLICY][%s] No changes in mono value = %d", __func__, u->is_mono);
			break;
        }

        u->is_mono = enable;

		/* Move current sink-input to proper mono sink */
		PA_IDXSET_FOREACH(si, u->core->sink_inputs, idx) {
			const char *policy = NULL;

			/* Skip this if it is already in the process of being moved
			 * anyway */
			if (!si->sink)
				continue;

			/* It might happen that a stream and a sink are set up at the
			   same time, in which case we want to make sure we don't
			   interfere with that */
			if (!PA_SINK_INPUT_IS_LINKED(pa_sink_input_get_state(si)))
				continue;

			/* Get role (if role is filter, skip it) */
			if (policy_is_filter(si))
				continue;

			/* Check policy, if no policy exists, treat as AUTO */
			if (!(policy = pa_proplist_gets(si->proplist, PA_PROP_MEDIA_POLICY))) {
				pa_log_debug("[POLICY] set policy of sink-input[%d] from [%s] to [auto]", si->index, "null");
				policy  = POLICY_AUTO;
			}
			pa_log_debug("[POLICY] Policy of sink input [%d] = %s", si->index, policy);

			/* Select sink to move and move to it */
			sink_to_move = policy_select_proper_sink (u->core, policy, u->is_mono);
			if (sink_to_move) {
				pa_log_debug("[POLICY][%s] Moving sink-input[%d] from [%s] to [%s]", __func__, si->index, si->sink->name, sink_to_move->name);
				pa_sink_input_move_to(si, sink_to_move, false);
			} else {
				pa_log_debug("[POLICY][%s] Can't move sink-input....", __func__);
			}
		}
        break;
    }

    case SUBCOMMAND_BALANCE: {
		float balance;
		pa_cvolume cvol;
		pa_channel_map map;

		if (pa_tagstruct_get_cvolume(t, &cvol) < 0)
			goto fail;

		pa_channel_map_init_stereo(&map);
		balance = pa_cvolume_get_balance(&cvol, &map);

		pa_log_debug ("[POLICY][%s] new balance value = [%f]\n", __func__, balance);

		if (balance == u->balance) {
			pa_log_debug ("[POLICY][%s] No changes in balance value = [%f]", __func__, u->balance);
			break;
		}

		u->balance = balance;

		/* Apply balance value to each Sinks */
		PA_IDXSET_FOREACH(s, u->core->sinks, idx) {
			pa_cvolume* cvol = pa_sink_get_volume (s, false);
			pa_cvolume_set_balance (cvol, &s->channel_map, u->balance);
			pa_sink_set_volume(s, cvol, true, true);
		}
		break;
	}

    default:
      goto fail;
  }

  pa_pstream_send_tagstruct(pa_native_connection_get_pstream(c), reply);
  return 0;

  fail:

  if (reply)
	  pa_tagstruct_free(reply);

  return -1;
}

/*  Called when new sink-input is creating  */
static pa_hook_result_t sink_input_new_hook_callback(pa_core *c, pa_sink_input_new_data *new_data, struct userdata *u)
{
    const char *policy = NULL;

    pa_assert(c);
    pa_assert(new_data);
    pa_assert(u);

    if (!new_data->proplist) {
        pa_log_debug("[POLICY] New stream lacks property data.");
        return PA_HOOK_OK;
    }

    /* If sink-input has already sink, skip */
    if (new_data->sink) {
    	/* sink-input with filter role will be also here because sink is already set */
#ifdef DEBUG_DETAIL
        pa_log_debug("[POLICY] Not setting device for stream [%s], because already set.",
        		pa_strnull(pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_NAME)));
#endif
        return PA_HOOK_OK;
    }

    /* If no policy exists, skip */
    if (!(policy = pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_POLICY))) {
        pa_log_debug("[POLICY][%s] Not setting device for stream [%s], because it lacks policy.",
        		__func__, pa_strnull(pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_NAME)));
        return PA_HOOK_OK;
    }
    pa_log_debug("[POLICY][%s] Policy for stream [%s] = [%s]",
    		__func__, pa_strnull(pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_NAME)), policy);

    /* Set proper sink to sink-input */
    pa_sink* new_sink = policy_select_proper_sink(c, policy, u->is_mono);
    if(new_sink != new_data->sink)
    {
        pa_sink_input_new_data_set_sink(new_data, new_sink, false);
    }
	/*new_data->save_sink = false;
	new_data->sink = policy_select_proper_sink (c, policy, u->is_mono);*/
	pa_log_debug("[POLICY][%s] set sink of sink-input to [%s]", __func__, (new_data->sink)? new_data->sink->name : "null");

    return PA_HOOK_OK;
}

/*  Called when new sink is added while sink-input is existing  */
static pa_hook_result_t sink_put_hook_callback(pa_core *c, pa_sink *sink, struct userdata *u)
{
    pa_sink_input *si;
    pa_sink *sink_to_move;
    uint32_t idx;
    char *args = NULL;

    bool is_bt;
    bool is_usb_alsa;

    pa_assert(c);
    pa_assert(sink);
    pa_assert(u);
    pa_assert(u->on_hotplug);

    /* If connected sink is BLUETOOTH, set as default */
    /* we are checking with device.api property */
    is_bt = policy_is_bluez(sink);
    is_usb_alsa = policy_is_usb_alsa(sink);

	if (is_bt || is_usb_alsa) {
		pa_log_debug("[POLICY][%s] set default sink to sink[%s][%d]", __func__, sink->name, sink->index);
		pa_namereg_set_default_sink (c,sink);
	} else {
		pa_log_debug("[POLICY][%s] this sink [%s][%d] is not a bluez....return", __func__, sink->name, sink->index);
		return PA_HOOK_OK;
	}

	if (is_bt) {
		/* Load mono_bt sink */
		args = pa_sprintf_malloc("sink_name=%s master=%s channels=1", SINK_MONO_BT, sink->name);
		u->module_mono_bt = pa_module_load(u->module->core, "module-remap-sink", args);
		pa_xfree(args);

		/* load combine sink */
		args = pa_sprintf_malloc("sink_name=%s slaves=\"%s,%s\"", SINK_COMBINED, sink->name, SINK_ALSA);
		u->module_combined = pa_module_load(u->module->core, "module-combine", args);
		pa_xfree(args);

		/* load mono_combine sink */
		args = pa_sprintf_malloc("sink_name=%s master=%s channels=1", SINK_MONO_COMBINED, SINK_COMBINED);
		u->module_mono_combined = pa_module_load(u->module->core, "module-remap-sink", args);
		pa_xfree(args);
	}

	/* Iterate each sink inputs to decide whether we should move to new sink */
    PA_IDXSET_FOREACH(si, c->sink_inputs, idx) {
        const char *policy = NULL;

        if (si->sink == sink)
        	continue;

        /* Skip this if it is already in the process of being moved
         * anyway */
        if (!si->sink)
            continue;

        /* It might happen that a stream and a sink are set up at the
           same time, in which case we want to make sure we don't
           interfere with that */
        if (!PA_SINK_INPUT_IS_LINKED(pa_sink_input_get_state(si)))
            continue;

		/* Get role (if role is filter, skip it) */
        if (policy_is_filter(si))
        	continue;

		/* Check policy */
		if (!(policy = pa_proplist_gets(si->proplist, PA_PROP_MEDIA_POLICY))) {
			/* No policy exists, this means auto */
			pa_log_debug("[POLICY][%s] set policy of sink-input[%d] from [%s] to [auto]", __func__, si->index, "null");
			policy = POLICY_AUTO;
		}

		sink_to_move = policy_select_proper_sink (c, policy, u->is_mono);
		if (sink_to_move) {
			pa_log_debug("[POLICY][%s] Moving sink-input[%d] from [%s] to [%s]", __func__, si->index, si->sink->name, sink_to_move->name);
			pa_sink_input_move_to(si, sink_to_move, false);
		} else {
			pa_log_debug("[POLICY][%s] Can't move sink-input....",__func__);
		}
    }

	/* Reset sink volume with balance from userdata */
	pa_cvolume* cvol = pa_sink_get_volume(sink, false);
	pa_cvolume_set_balance(cvol, &sink->channel_map, u->balance);
	pa_sink_set_volume(sink, cvol, true, true);

    return PA_HOOK_OK;
}

static void subscribe_cb(pa_core *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
    struct userdata *u = userdata;
    pa_sink *def;
    pa_sink_input *si;
    uint32_t idx2;
    pa_sink *sink_to_move = NULL;
    pa_assert(u);

    pa_log_debug("[POLICY][%s] subscribe_cb() t=[0x%x], idx=[%d]", __func__, t, idx);

    /* We only handle server changes */
    if (t == (PA_SUBSCRIPTION_EVENT_SERVER|PA_SUBSCRIPTION_EVENT_CHANGE)) {

    	def = pa_namereg_get_default_sink(c);
		if (def == NULL) {
			pa_log_warn("[POLICY][%s] pa_namereg_get_default_sink() returns null", __func__);
			return;
		}
    	pa_log_debug("[POLICY][%s] trying to move stream to current default sink = [%s]", __func__, def->name);

    	/* Iterate each sink inputs to decide whether we should move to new DEFAULT sink */
    	PA_IDXSET_FOREACH(si, c->sink_inputs, idx2) {
			const char *policy = NULL;

			if (!si->sink)
				continue;

			/* Get role (if role is filter, skip it) */
			if (policy_is_filter(si))
				continue;

			/* Get policy */
			if (!(policy = pa_proplist_gets(si->proplist, PA_PROP_MEDIA_POLICY))) {
				/* No policy exists, this means auto */
				pa_log_debug("[POLICY][%s] set policy of sink-input[%d] from [%s] to [auto]", __func__, si->index, "null");
				policy = POLICY_AUTO;
			}

			sink_to_move = policy_select_proper_sink (c, policy, u->is_mono);
			if (sink_to_move) {
				/* Move sink-input to new DEFAULT sink */
				pa_log_debug("[POLICY][%s] Moving sink-input[%d] from [%s] to [%s]", __func__, si->index, si->sink->name, sink_to_move->name);
				pa_sink_input_move_to(si, sink_to_move, false);
			}
    	}
    }
}

static pa_hook_result_t sink_unlink_hook_callback(pa_core *c, pa_sink *sink, void* userdata) {
    struct userdata *u = userdata;
    uint32_t idx;
    pa_sink *sink_to_move;
    pa_sink_input	*si;

    pa_assert(c);
    pa_assert(sink);
    pa_assert(u);

     /* There's no point in doing anything if the core is shut down anyway */
    if (c->state == PA_CORE_SHUTDOWN)
        return PA_HOOK_OK;

    /* if unloading sink is not bt, just return */
	if (!policy_is_bluez (sink)) {
		pa_log_debug("[POLICY][%s] sink[%s][%d] unlinked but not a bluez....return\n", __func__,  sink->name, sink->index);
		return PA_HOOK_OK;
	}

	pa_log_debug ("[POLICY][%s] SINK unlinked ================================ sink [%s][%d], bt_off_idx was [%d]",
	    		__func__, sink->name, sink->index,u->bt_off_idx);

	u->bt_off_idx = sink->index;
	pa_log_debug ("[POLICY][%s] bt_off_idx is set to [%d]", __func__, u->bt_off_idx);

	/* BT sink is unloading, move sink-input to proper sink */
	PA_IDXSET_FOREACH(si, c->sink_inputs, idx) {

		if (!si->sink)
			continue;

		/* Get role (if role is filter, skip it) */
		if (policy_is_filter(si))
			continue;

		/* Find who were using bt sink or bt related sink and move them to proper sink (alsa/mono_alsa) */
		if (pa_streq (si->sink->name, SINK_MONO_BT) ||
			pa_streq (si->sink->name, SINK_MONO_COMBINED) ||
			pa_streq (si->sink->name, SINK_COMBINED) ||
			policy_is_bluez (si->sink)) {

			/* Move sink-input to proper sink : only alsa related sink is available now */
			sink_to_move = policy_get_sink_by_name (c, (u->is_mono)? SINK_MONO_ALSA : SINK_ALSA);
			if (sink_to_move) {
				pa_log_debug("[POLICY][%s] Moving sink-input[%d] from [%s] to [%s]", __func__, si->index, si->sink->name, sink_to_move->name);
				pa_sink_input_move_to(si, sink_to_move, false);
			} else {
				pa_log_warn("[POLICY][%s] No sink to move", __func__);
			}
		}
	}

	pa_log_debug ("[POLICY][%s] unload sink in dependencies", __func__);

    /* Unload mono_combine sink */
    if (u->module_mono_combined) {
        pa_module_unload(u->module->core, u->module_mono_combined, true);
    	u->module_mono_combined = NULL;
    }

	/* Unload combine sink */
    if (u->module_combined) {
        pa_module_unload(u->module->core, u->module_combined, true);
        u->module_combined = NULL;
    }

    /* Unload mono_bt sink */
	if (u->module_mono_bt) {
		pa_module_unload(u->module->core, u->module_mono_bt, true);
		u->module_mono_bt = NULL;
	}

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_unlink_post_hook_callback(pa_core *c, pa_sink *sink, void* userdata) {
    struct userdata *u = userdata;

    pa_assert(c);
    pa_assert(sink);
    pa_assert(u);

    pa_log_debug("[POLICY][%s] SINK unlinked POST ================================ sink [%s][%d]", __func__, sink->name, sink->index);

     /* There's no point in doing anything if the core is shut down anyway */
    if (c->state == PA_CORE_SHUTDOWN)
        return PA_HOOK_OK;

    /* if unloading sink is not bt, just return */
	if (!policy_is_bluez (sink)) {
		pa_log_debug("[POLICY][%s] not a bluez....return\n", __func__);
		return PA_HOOK_OK;
	}

    u->bt_off_idx = -1;
    pa_log_debug ("[POLICY][%s] bt_off_idx is cleared to [%d]", __func__, u->bt_off_idx);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_move_start_cb(pa_core *core, pa_sink_input *i, struct userdata *u) {
    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    /* There's no point in doing anything if the core is shut down anyway */
   if (core->state == PA_CORE_SHUTDOWN)
       return PA_HOOK_OK;

    pa_log_debug ("[POLICY][%s]  sink_input_move_start_cb -------------------------------------- sink-input [%d] was sink [%s][%d] : Trying to mute!!!",
    		__func__, i->index, i->sink->name, i->sink->index);
    pa_sink_input_set_mute(i, true, false);

    return PA_HOOK_OK;
}

static pa_hook_result_t sink_input_move_finish_cb(pa_core *core, pa_sink_input *i, struct userdata *u) {
    pa_core_assert_ref(core);
    pa_sink_input_assert_ref(i);

    /* There's no point in doing anything if the core is shut down anyway */
   if (core->state == PA_CORE_SHUTDOWN)
       return PA_HOOK_OK;

    pa_log_debug("[POLICY][%s] sink_input_move_finish_cb -------------------------------------- sink-input [%d], sink [%s][%d], bt_off_idx [%d] : %s",
    		__func__, i->index, i->sink->name, i->sink->index, u->bt_off_idx,
    		(u->bt_off_idx == -1)? "Trying to un-mute!!!!" : "skip un-mute...");

    /* If sink input move is caused by bt sink unlink, then skip un-mute operation */
    if (u->bt_off_idx == -1) {
        pa_sink_input_set_mute(i, false, false);
    }

    return PA_HOOK_OK;
}

static pa_source* policy_get_source_by_name (pa_core *c, const char* source_name)
{
	pa_source *s = NULL;
	uint32_t idx;

	if (c == NULL || source_name == NULL) {
		pa_log_warn ("input param is null");
		return NULL;
	}

	PA_IDXSET_FOREACH(s, c->sources, idx) {
		if (pa_streq (s->name, source_name)) {
			pa_log_debug ("[POLICY][%s] return [%p] for [%s]\n",  __func__, s, source_name);
			return s;
		}
	}
	return NULL;
}

/* Select source for given condition */
static pa_source* policy_select_proper_source (pa_core *c, const char* policy)
{
	pa_source* source = NULL;
	pa_source* def = NULL;

	if (c == NULL || policy == NULL) {
		pa_log_warn ("input param is null");
		return NULL;
	}

	pa_assert (c);
	def = pa_namereg_get_default_source(c);
	if (def == NULL) {
		pa_log_warn ("POLICY][%s] pa_namereg_get_default_source() returns null", __func__);
		return NULL;
	}

	/* Select source  to */
	if (pa_streq(policy, POLICY_VOIP)) {
		source = policy_get_source_by_name (c, AEC_SOURCE);

	} else {
		source = def;
	}

	pa_log_debug ("[POLICY][%s] selected source : [%s]\n", __func__, (source)? source->name : "null");
	return source;
}


/*  Called when new source-output is creating  */
static pa_hook_result_t source_output_new_hook_callback(pa_core *c, pa_source_output_new_data *new_data, struct userdata *u) {
	const char *policy = NULL;
	pa_assert(c);
	pa_assert(new_data);
	pa_assert(u);

	if (!new_data->proplist) {
		pa_log_debug("New stream lacks property data.");
		return PA_HOOK_OK;
	}

	if (new_data->source) {
		pa_log_debug("Not setting device for stream %s, because already set.", pa_strnull(pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_NAME)));
		return PA_HOOK_OK;
	}

	/* If no policy exists, skip */
	if (!(policy = pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_POLICY))) {
		pa_log_debug("[POLICY][%s] Not setting device for stream [%s], because it lacks policy.",
				__func__, pa_strnull(pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_NAME)));
		return PA_HOOK_OK;
	}
	pa_log_debug("[POLICY][%s] Policy for stream [%s] = [%s]",
			__func__, pa_strnull(pa_proplist_gets(new_data->proplist, PA_PROP_MEDIA_NAME)), policy);

	/* Set proper source to source-output */
    pa_source* new_source = policy_select_proper_source(c, policy);
    if(new_source != new_data->source)
    {
        pa_source_output_new_data_set_source(new_data, new_source, false);
    }
	/*new_data->save_source= false;
	new_data->source= policy_select_proper_source (c, policy);*/
	pa_log_debug("[POLICY][%s] set source of source-input to [%s]", __func__, (new_data->source)? new_data->source->name : "null");

	return PA_HOOK_OK;
}

int pa__init(pa_module *m)
{
	pa_modargs *ma = NULL;
	struct userdata *u;
	bool on_hotplug = true, on_rescue = true;

	pa_assert(m);

	if (!(ma = pa_modargs_new(m->argument, valid_modargs))) {
		pa_log("Failed to parse module arguments");
		goto fail;
	}

	if (pa_modargs_get_value_boolean(ma, "on_hotplug", &on_hotplug) < 0 ||
		pa_modargs_get_value_boolean(ma, "on_rescue", &on_rescue) < 0) {
		pa_log("on_hotplug= and on_rescue= expect boolean arguments");
		goto fail;
	}

	m->userdata = u = pa_xnew0(struct userdata, 1);
	u->core = m->core;
	u->module = m;
	u->on_hotplug = on_hotplug;


	/* A little bit later than module-stream-restore */
	u->sink_input_new_hook_slot =
			pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_NEW], PA_HOOK_EARLY+10, (pa_hook_cb_t) sink_input_new_hook_callback, u);

	u->source_output_new_hook_slot =
			pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SOURCE_OUTPUT_NEW], PA_HOOK_EARLY+10, (pa_hook_cb_t) source_output_new_hook_callback, u);

	if (on_hotplug) {
		/* A little bit later than module-stream-restore */
		u->sink_put_hook_slot =
			pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_PUT], PA_HOOK_LATE+10, (pa_hook_cb_t) sink_put_hook_callback, u);
	}

	/* sink unlink comes before sink-input unlink */
	u->sink_unlink_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_UNLINK], PA_HOOK_EARLY, (pa_hook_cb_t) sink_unlink_hook_callback, u);
	u->sink_unlink_post_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_UNLINK_POST], PA_HOOK_EARLY, (pa_hook_cb_t) sink_unlink_post_hook_callback, u);

	u->sink_input_move_start_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_MOVE_START], PA_HOOK_LATE, (pa_hook_cb_t) sink_input_move_start_cb, u);
	u->sink_input_move_finish_slot = pa_hook_connect(&m->core->hooks[PA_CORE_HOOK_SINK_INPUT_MOVE_FINISH], PA_HOOK_LATE, (pa_hook_cb_t) sink_input_move_finish_cb, u);

	u->subscription = pa_subscription_new(u->core, PA_SUBSCRIPTION_MASK_SERVER, subscribe_cb, u);


	u->bt_off_idx = -1;	/* initial bt off sink index */

	u->module_mono_bt = NULL;
	u->module_combined = NULL;
	u->module_mono_combined = NULL;

    u->protocol = pa_native_protocol_get(m->core);
    pa_native_protocol_install_ext(u->protocol, m, extension_cb);

    /* Get mono key value for init */
	vconf_get_bool(MONO_KEY, &u->is_mono);

	pa_log_info("policy module is loaded\n");

	if (ma)
		pa_modargs_free(ma);

	return 0;

fail:
	if (ma)
		pa_modargs_free(ma);

	pa__done(m);

	return -1;
}

void pa__done(pa_module *m)
{
    struct userdata* u;

    pa_assert(m);

    if (!(u = m->userdata))
        return;

    if (u->sink_input_new_hook_slot)
        pa_hook_slot_free(u->sink_input_new_hook_slot);
    if (u->sink_put_hook_slot)
        pa_hook_slot_free(u->sink_put_hook_slot);
    if (u->sink_unlink_slot)
        pa_hook_slot_free(u->sink_unlink_slot);
    if (u->sink_unlink_post_slot)
        pa_hook_slot_free(u->sink_unlink_post_slot);
    if (u->sink_input_move_start_slot)
        pa_hook_slot_free(u->sink_input_move_start_slot);
    if (u->sink_input_move_finish_slot)
        pa_hook_slot_free(u->sink_input_move_finish_slot);
    if (u->subscription)
        pa_subscription_free(u->subscription);
    if (u->protocol) {
        pa_native_protocol_remove_ext(u->protocol, m);
        pa_native_protocol_unref(u->protocol);
    }
    if (u->source_output_new_hook_slot)
        pa_hook_slot_free(u->source_output_new_hook_slot);

    pa_xfree(u);


	pa_log_info("policy module is unloaded\n");
}
