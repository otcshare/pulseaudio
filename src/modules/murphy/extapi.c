/*
 * module-murphy-ivi -- PulseAudio module for providing audio routing support
 * Copyright (c) 2012, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU Lesser General Public License,
 * version 2.1, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St - Fifth Floor, Boston,
 * MA 02110-1301 USA.
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <pulse/def.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/core-util.h>
#include <pulsecore/protocol-native.h>
#include <pulsecore/tagstruct.h>
#include <pulsecore/pstream-util.h>

#include "extapi.h"
#include "node.h"
#include "router.h"

enum {
    SUBCOMMAND_TEST,
    SUBCOMMAND_READ,
    SUBCOMMAND_CONNECT,
    SUBCOMMAND_DISCONNECT,
    SUBCOMMAND_SUBSCRIBE,
    SUBCOMMAND_EVENT
};

struct pa_nodeset {
    pa_idxset *nodes;
};

struct pa_extapi {
    uint32_t conn_id;
    pa_hashmap *conns;
    pa_idxset *subscribed;
};

static const char *mir_direction_names[] = {
    [mir_direction_unknown] = "unknown",
    [mir_input] = "input",
    [mir_output] = "output"
};

#if 0
static const char *mir_implement_names[] = {
    [mir_implementation_unknown] = "unknown",
    [mir_device] = "device",
    [mir_stream] = "stream"
};
#endif

static const char *mir_location_names[] = {
    [mir_location_unknown] = "unknown",
    [mir_internal] = "internal",
    [mir_external] = "external"
};

static const char *mir_node_type_names[512] = {
    [mir_node_type_unknown] = "unknown",
    [mir_radio] = "radio",
    [mir_player] = "player",
    [mir_navigator] = "navigator",
    [mir_game] = "game",
    [mir_browser] = "browser",
    [mir_phone] = "phone",
    [mir_event] = "event",
    [mir_null] = "null",
    [mir_speakers] = "speakers",
    [mir_front_speakers] = "front_speakers",
    [mir_rear_speakers] = "rear_speakers",
    [mir_microphone] = "microphone",
    [mir_jack] = "jack",
    [mir_spdif] = "spdif",
    [mir_hdmi] = "hdmi",
    [mir_wired_headset] = "wired_headset",
    [mir_wired_headphone] = "wired_headphone",
    [mir_usb_headset] ="usb_headset",
    [mir_usb_headphone] = "usb_headphone",
    [mir_bluetooth_sco] = "bluetooth_sco",
    [mir_bluetooth_a2dp] = "bluetooth_a2dp",
    [mir_bluetooth_carkit] = "bluetooth_carkit",
    [mir_bluetooth_source] = "bluetooth_source",
    [mir_bluetooth_sink] = "bluetoohth_sink"
};

static const char *mir_privacy_names[] = {
    [mir_privacy_unknown] ="unknown",
    [mir_public] = "public",
    [mir_private] = "private"
};

static void *conn_hash(uint32_t connid);

struct pa_extapi *pa_extapi_init(struct userdata *u) {
    pa_extapi *ap;

    pa_assert(u);

    ap = pa_xnew0(pa_extapi, 1);

    ap->conn_id = 0;

    ap->conns = pa_hashmap_new(pa_idxset_trivial_hash_func,
                               pa_idxset_trivial_compare_func);

    ap->subscribed = pa_idxset_new(pa_idxset_trivial_hash_func,
                                   pa_idxset_trivial_compare_func);

    return ap;
}

void pa_extapi_done(struct userdata *u) {
    pa_extapi *ap;

    if (u && (ap = u->extapi)) {
        if (ap->conns)
            pa_hashmap_free(ap->conns);
        if (ap->subscribed)
            pa_idxset_free(ap->subscribed, NULL);
        pa_xfree(ap);
    }
}

int extension_cb(pa_native_protocol *p, pa_module *m, pa_native_connection *c, uint32_t tag, pa_tagstruct *t) {
  struct userdata *u;
  uint32_t command;
  pa_tagstruct *reply = NULL;

  pa_assert(p);
  pa_assert(m);
  pa_assert(c);
  pa_assert(t);

  u = m->userdata;

  pa_log_debug("in module-murphy-ivi extension callback");

  if (pa_tagstruct_getu32(t, &command) < 0)
    goto fail;

  reply = pa_tagstruct_new(NULL, 0);
  pa_tagstruct_putu32(reply, PA_COMMAND_REPLY);
  pa_tagstruct_putu32(reply, tag);

  switch (command) {
    case SUBCOMMAND_TEST: {
      pa_log_debug("got test request to module-murphy-ivi");

      if (!pa_tagstruct_eof(t))
        goto fail;

      pa_tagstruct_putu32(reply, 1);
      break;
    }

    case SUBCOMMAND_READ: {
      mir_node *node;
      uint32_t index;
      pa_proplist *prop;
      char buf[256];

      if (!pa_tagstruct_eof(t))
        goto fail;

      pa_log_debug("got read request to module-murphy-ivi");

      PA_IDXSET_FOREACH(node, u->nodeset->nodes, index) {

          if (!node->visible || !node->available)
              continue;

          pa_tagstruct_puts(reply, node->amname);

          prop = pa_proplist_new();

          sprintf(buf, "%d", node->index);
          pa_proplist_sets(prop, "index", buf);
          pa_proplist_sets(prop, "direction", mir_direction_names[node->direction]);
          sprintf(buf, "%d", node->channels);
          pa_proplist_sets(prop, "channels", buf);
          pa_proplist_sets(prop, "location", mir_location_names[node->location]);
          pa_proplist_sets(prop, "privacy", mir_privacy_names[node->privacy]);
          pa_proplist_sets(prop, "type", mir_node_type_names[node->type]);
          pa_proplist_sets(prop, "amname", node->amname);
          pa_proplist_sets(prop, "amdescr", node->amdescr);
          sprintf(buf, "%d", node->amid);
          pa_proplist_sets(prop, "amid", buf);
          pa_proplist_sets(prop, "paname", node->paname);
          sprintf(buf, "%d", node->paidx);
          pa_proplist_sets(prop, "paidx", buf);

          pa_tagstruct_put_proplist(reply, prop);
      }

      break;
    }

    case SUBCOMMAND_CONNECT: {
        uint32_t id, from_index, to_index;
        mir_node *from, *to;
        mir_connection *conn;

        pa_log_debug("connect called in module-murphy-ivi");

        if (pa_tagstruct_getu32(t, &from_index) < 0 ||
            pa_tagstruct_getu32(t, &to_index) < 0 ||
            !pa_tagstruct_eof(t))
            goto fail;

        id = u->extapi->conn_id++;
        if (!(from = pa_idxset_get_by_index(u->nodeset->nodes, from_index)) ||
            !(to = pa_idxset_get_by_index(u->nodeset->nodes, to_index))) {
            pa_log_debug("invalid index for connection in module-murphy-ivi");
            goto fail;
        }

        if(!(conn = mir_router_add_explicit_route(u, id, from, to))) {
            pa_log_debug("explicit connection failed in module-murphy-ivi");
            goto fail;
        }

        pa_hashmap_put(u->extapi->conns, conn_hash(id), conn);

        pa_tagstruct_putu32(reply, id);

        break;
    }

    case SUBCOMMAND_DISCONNECT: {
        mir_connection *conn;
        uint32_t id;

        pa_log_debug("disconnect called in module-murphy-ivi");

        if (pa_tagstruct_getu32(t, &id) < 0 ||
            !pa_tagstruct_eof(t))
            goto fail;

        pa_log_debug("got id in disconnect %d and hash %ld", id, (long)conn_hash(id));

        /* get conn from somewhere with id */

        if ((conn = pa_hashmap_remove(u->extapi->conns, conn_hash(id))))
            mir_router_remove_explicit_route(u, conn);
        else
            goto fail;

        pa_log_debug("sending reply from node disconnect");

        break;
    }

    case SUBCOMMAND_SUBSCRIBE: {

        bool enabled;

        pa_log_debug("subscribe called in module-murphy-ivi");

        if (pa_tagstruct_get_boolean(t, &enabled) < 0 ||
            !pa_tagstruct_eof(t))
            goto fail;

        if (enabled) {
            pa_idxset_put(u->extapi->subscribed, c, NULL);
            pa_log_debug("enabling subscribe in module-murphy-ivi");
        }
        else {
            pa_idxset_remove_by_data(u->extapi->subscribed, c, NULL);
            pa_log_debug("disabling subscribe in module-murphy-ivi");
        }
        break;
    }

    default:
      goto fail;
  }

  pa_pstream_send_tagstruct(pa_native_connection_get_pstream(c), reply);
  return 0;

  fail:

  pa_log_debug("ext command callback failed in module-murphy-ivi");

  if (reply)
    pa_tagstruct_free(reply);

  return -1;
}

void extapi_signal_node_change(struct userdata *u) {
    pa_native_connection *c;
    uint32_t idx;
    pa_extapi *ap;

    if ((ap = u->extapi)) {

        pa_log_debug("signalling node change to extapi subscribers");

        for (c = pa_idxset_first(ap->subscribed, &idx); c; c = pa_idxset_next(ap->subscribed, &idx)) {
            pa_tagstruct *t;

            t = pa_tagstruct_new(NULL, 0);
            pa_tagstruct_putu32(t, PA_COMMAND_EXTENSION);
            pa_tagstruct_putu32(t, 0);
            pa_tagstruct_putu32(t, u->module->index);
            pa_tagstruct_puts(t, "module-node-manager");
            pa_tagstruct_putu32(t, SUBCOMMAND_EVENT);

            pa_pstream_send_tagstruct(pa_native_connection_get_pstream(c), t);
        }
    }
}

static void *conn_hash(uint32_t connid)
{
    return (char *)NULL + connid;
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
