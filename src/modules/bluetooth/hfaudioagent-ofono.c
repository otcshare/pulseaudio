/***
  This file is part of PulseAudio.

  Copyright 2013 João Paulo Rechi Vita

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation; either version 2.1 of the
  License, or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>

#include <pulsecore/core-util.h>
#include <pulsecore/dbus-shared.h>
#include <pulsecore/shared.h>

#include "bluez5-util.h"

#include "hfaudioagent.h"

#define HFP_AUDIO_CODEC_CVSD    0x01
#define HFP_AUDIO_CODEC_MSBC    0x02

#define OFONO_SERVICE "org.ofono"
#define HF_AUDIO_AGENT_INTERFACE OFONO_SERVICE ".HandsfreeAudioAgent"
#define HF_AUDIO_MANAGER_INTERFACE OFONO_SERVICE ".HandsfreeAudioManager"

#define HF_AUDIO_AGENT_PATH "/HandsfreeAudioAgent"

#define HF_AUDIO_AGENT_XML                                          \
    DBUS_INTROSPECT_1_0_XML_DOCTYPE_DECL_NODE                       \
    "<node>"                                                        \
    "  <interface name=\"org.freedesktop.DBus.Introspectable\">"    \
    "    <method name=\"Introspect\">"                              \
    "      <arg direction=\"out\" type=\"s\" />"                    \
    "    </method>"                                                 \
    "  </interface>"                                                \
    "  <interface name=\"org.ofono.HandsfreeAudioAgent\">"          \
    "    <method name=\"Release\">"                                 \
    "    </method>"                                                 \
    "    <method name=\"NewConnection\">"                           \
    "      <arg direction=\"in\"  type=\"o\" name=\"card_path\" />" \
    "      <arg direction=\"in\"  type=\"h\" name=\"sco_fd\" />"    \
    "      <arg direction=\"in\"  type=\"y\" name=\"codec\" />"     \
    "    </method>"                                                 \
    "  </interface>"                                                \
    "</node>"

typedef struct hf_audio_card {
    char *path;
    char *remote;
    char *local;

    int fd;
    uint8_t codec;

    pa_bluetooth_transport *transport;
} hf_audio_card;

struct hf_audio_agent_data {
    pa_core *core;
    pa_dbus_connection *connection;
    pa_bluetooth_discovery *discovery;

    bool filter_added;
    char *ofono_bus_id;
    pa_hashmap *hf_audio_cards;

    PA_LLIST_HEAD(pa_dbus_pending, pending);
};

static pa_dbus_pending* pa_bluetooth_dbus_send_and_add_to_pending(hf_audio_agent_data *hfdata, DBusMessage *m,
                                                                  DBusPendingCallNotifyFunction func, void *call_data) {
    pa_dbus_pending *p;
    DBusPendingCall *call;

    pa_assert(hfdata);
    pa_assert(m);

    pa_assert_se(dbus_connection_send_with_reply(pa_dbus_connection_get(hfdata->connection), m, &call, -1));

    p = pa_dbus_pending_new(pa_dbus_connection_get(hfdata->connection), m, call, hfdata, call_data);
    PA_LLIST_PREPEND(pa_dbus_pending, hfdata->pending, p);
    dbus_pending_call_set_notify(call, func, p, NULL);

    return p;
}

static hf_audio_card *hf_audio_card_new(hf_audio_agent_data *hfdata, const char *path) {
    hf_audio_card *hfac = pa_xnew0(hf_audio_card, 1);

    hfac->path = pa_xstrdup(path);
    hfac->fd = -1;

    return hfac;
}

static void hf_audio_card_free(void *data) {
    hf_audio_card *hfac = data;

    pa_assert(hfac);

    pa_bluetooth_transport_free(hfac->transport);
    pa_xfree(hfac->path);
    pa_xfree(hfac->remote);
    pa_xfree(hfac->local);
    pa_xfree(hfac);
}

static int hf_audio_agent_transport_acquire(pa_bluetooth_transport *t, bool optional, size_t *imtu, size_t *omtu) {
    hf_audio_agent_data *hfdata = t->userdata;
    hf_audio_card *hfac = pa_hashmap_get(hfdata->hf_audio_cards, t->path);

    if (hfac->fd < 0) {
        DBusMessage *m;

        pa_assert_se(m = dbus_message_new_method_call(t->owner, t->path, "org.ofono.HandsfreeAudioCard", "Connect"));
        pa_assert_se(dbus_connection_send(pa_dbus_connection_get(hfdata->connection), m, NULL));

        return -1;
    }

    /* The correct block size should take into account the SCO MTU from
     * the Bluetooth adapter and (for adapters in the USB bus) the MxPS
     * value from the Isoc USB endpoint in use by btusb and should be
     * made available to userspace by the Bluetooth kernel subsystem.
     * Meanwhile the empiric value 48 will be used. */
    if (imtu)
        *imtu = 48;
    if (omtu)
        *omtu = 48;

    if (hfac) {
        t->codec = hfac->codec;
        return hfac->fd;
    } else
        return -1;
}

static void hf_audio_agent_transport_release(pa_bluetooth_transport *t) {
    hf_audio_agent_data *hfdata = t->userdata;
    hf_audio_card *hfac = pa_hashmap_get(hfdata->hf_audio_cards, t->path);

    if (hfac) {
        shutdown(hfac->fd, SHUT_RDWR);
        hfac->fd = -1;
    }
}

static void hf_audio_agent_card_found(hf_audio_agent_data *hfdata, const char *path, DBusMessageIter *props_i) {
    DBusMessageIter i, value_i;
    const char *key, *value;
    hf_audio_card *hfac;
    pa_bluetooth_device *d;

    pa_assert(hfdata);
    pa_assert(path);
    pa_assert(props_i);

    pa_log_debug("New HF card found: %s", path);

    hfac = hf_audio_card_new(hfdata, path);

    while (dbus_message_iter_get_arg_type(props_i) != DBUS_TYPE_INVALID) {
        char c;

        if ((c = dbus_message_iter_get_arg_type(props_i)) != DBUS_TYPE_DICT_ENTRY) {
            pa_log_error("Invalid properties for %s: expected \'e\', received \'%c\'", path, c);
            goto fail;
        }

        dbus_message_iter_recurse(props_i, &i);

        if ((c = dbus_message_iter_get_arg_type(&i)) != DBUS_TYPE_STRING) {
            pa_log_error("Invalid properties for %s: expected \'s\', received \'%c\'", path, c);
            goto fail;
        }

        dbus_message_iter_get_basic(&i, &key);
        dbus_message_iter_next(&i);

        if ((c = dbus_message_iter_get_arg_type(&i)) != DBUS_TYPE_VARIANT) {
            pa_log_error("Invalid properties for %s: expected \'v\', received \'%c\'", path, c);
            goto fail;
        }

        dbus_message_iter_recurse(&i, &value_i);

        if ((c = dbus_message_iter_get_arg_type(&value_i)) != DBUS_TYPE_STRING) {
            pa_log_error("Invalid properties for %s: expected \'s\', received \'%c\'", path, c);
            goto fail;
        }

        dbus_message_iter_get_basic(&value_i, &value);

        if (pa_streq(key, "RemoteAddress"))
            hfac->remote = pa_xstrdup(value);
        else if (pa_streq(key, "LocalAddress"))
            hfac->local = pa_xstrdup(value);

        pa_log_debug("%s: %s", key, value);

        dbus_message_iter_next(props_i);
    }

    pa_hashmap_put(hfdata->hf_audio_cards, hfac->path, hfac);

    d = pa_bluetooth_discovery_get_device_by_address(hfdata->discovery, hfac->remote, hfac->local);
    if (d) {
        hfac->transport = pa_bluetooth_transport_new(d, hfdata->ofono_bus_id, path, PA_BLUETOOTH_PROFILE_HEADSET_AUDIO_GATEWAY, NULL, 0);
        hfac->transport->acquire = hf_audio_agent_transport_acquire;
        hfac->transport->release = hf_audio_agent_transport_release;
        hfac->transport->userdata = hfdata;

        d->transports[PA_BLUETOOTH_PROFILE_HEADSET_AUDIO_GATEWAY] = hfac->transport;

        pa_bluetooth_transport_put(hfac->transport);
    } else
        pa_log_error("Device doesnt exist for %s", path);

    return;

fail:
    pa_xfree(hfac);
}

static void hf_audio_agent_get_cards_reply(DBusPendingCall *pending, void *userdata) {
    DBusMessage *r;
    pa_dbus_pending *p;
    hf_audio_agent_data *hfdata;
    DBusMessageIter i, array_i, struct_i, props_i;
    char c;

    pa_assert_se(p = userdata);
    pa_assert_se(hfdata = p->context_data);
    pa_assert_se(r = dbus_pending_call_steal_reply(pending));

    if (dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_ERROR) {
        pa_log_error("Failed to get a list of handsfree audio cards from ofono: %s: %s",
                     dbus_message_get_error_name(r), pa_dbus_get_error_message(r));
        goto finish;
    }

    dbus_message_iter_init(r, &i);
    if ((c = dbus_message_iter_get_arg_type(&i)) != DBUS_TYPE_ARRAY) {
        pa_log_error("Invalid arguments in GetCards() reply: expected \'a\', received \'%c\'", c);
        goto finish;
    }

    dbus_message_iter_recurse(&i, &array_i);
    while (dbus_message_iter_get_arg_type(&array_i) != DBUS_TYPE_INVALID) {
        const char *path;

        if ((c = dbus_message_iter_get_arg_type(&array_i)) != DBUS_TYPE_STRUCT) {
            pa_log_error("Invalid arguments in GetCards() reply: expected \'r\', received \'%c\'", c);
            goto finish;
        }

        dbus_message_iter_recurse(&array_i, &struct_i);
        if ((c = dbus_message_iter_get_arg_type(&struct_i)) != DBUS_TYPE_OBJECT_PATH) {
            pa_log_error("Invalid arguments in GetCards() reply: expected \'o\', received \'%c\'", c);
            goto finish;
        }

        dbus_message_iter_get_basic(&struct_i, &path);

        dbus_message_iter_next(&struct_i);
        if ((c = dbus_message_iter_get_arg_type(&struct_i)) != DBUS_TYPE_ARRAY) {
            pa_log_error("Invalid arguments in GetCards() reply: expected \'a\', received \'%c\'", c);
            goto finish;
        }

        dbus_message_iter_recurse(&struct_i, &props_i);

        hf_audio_agent_card_found(hfdata, path, &props_i);

        dbus_message_iter_next(&array_i);
    }

finish:
    dbus_message_unref(r);

    PA_LLIST_REMOVE(pa_dbus_pending, hfdata->pending, p);
    pa_dbus_pending_free(p);
}

static void hf_audio_agent_get_cards(hf_audio_agent_data *hfdata) {
    DBusMessage *m;

    pa_assert(hfdata);

    pa_assert_se(m = dbus_message_new_method_call(OFONO_SERVICE, "/", HF_AUDIO_MANAGER_INTERFACE, "GetCards"));
    pa_bluetooth_dbus_send_and_add_to_pending(hfdata, m, hf_audio_agent_get_cards_reply, NULL);
}

static void hf_audio_agent_register_reply(DBusPendingCall *pending, void *userdata) {
    DBusMessage *r;
    pa_dbus_pending *p;
    hf_audio_agent_data *hfdata;

    pa_assert_se(p = userdata);
    pa_assert_se(hfdata = p->context_data);
    pa_assert_se(r = dbus_pending_call_steal_reply(pending));

    if (dbus_message_get_type(r) == DBUS_MESSAGE_TYPE_ERROR) {
        pa_log_error("Failed to register as a handsfree audio agent with ofono: %s: %s",
                     dbus_message_get_error_name(r), pa_dbus_get_error_message(r));
        goto finish;
    }

    hfdata->ofono_bus_id = pa_xstrdup(dbus_message_get_sender(r));

    hf_audio_agent_get_cards(hfdata);

finish:
    dbus_message_unref(r);

    PA_LLIST_REMOVE(pa_dbus_pending, hfdata->pending, p);
    pa_dbus_pending_free(p);
}

static void hf_audio_agent_register(hf_audio_agent_data *hfdata) {
    DBusMessage *m;
    unsigned char codecs[2];
    const unsigned char *pcodecs = codecs;
    int ncodecs = 0;
    const char *path = HF_AUDIO_AGENT_PATH;

    pa_assert(hfdata);

    pa_assert_se(m = dbus_message_new_method_call(OFONO_SERVICE, "/", HF_AUDIO_MANAGER_INTERFACE, "Register"));

    codecs[ncodecs++] = HFP_AUDIO_CODEC_CVSD;
    /* codecs[ncodecs++] = HFP_AUDIO_CODEC_MSBC; */

    pa_assert_se(dbus_message_append_args(m, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_ARRAY, DBUS_TYPE_BYTE, &pcodecs, ncodecs,
                                          DBUS_TYPE_INVALID));

    pa_bluetooth_dbus_send_and_add_to_pending(hfdata, m, hf_audio_agent_register_reply, NULL);
}

static void hf_audio_agent_unregister(hf_audio_agent_data *hfdata) {
    DBusMessage *m;
    const char *path = HF_AUDIO_AGENT_PATH;

    pa_assert(hfdata);
    pa_assert(hfdata->connection);

    if (hfdata->ofono_bus_id) {
        pa_assert_se(m = dbus_message_new_method_call(hfdata->ofono_bus_id, "/", HF_AUDIO_MANAGER_INTERFACE, "Unregister"));
        pa_assert_se(dbus_message_append_args(m, DBUS_TYPE_OBJECT_PATH, &path, DBUS_TYPE_INVALID));
        pa_assert_se(dbus_connection_send(pa_dbus_connection_get(hfdata->connection), m, NULL));

        pa_xfree(hfdata->ofono_bus_id);
        hfdata->ofono_bus_id = NULL;
    }
}

static DBusHandlerResult filter_cb(DBusConnection *bus, DBusMessage *m, void *data) {
    const char *sender;
    DBusError err;
    hf_audio_agent_data *hfdata = data;

    pa_assert(bus);
    pa_assert(m);
    pa_assert(hfdata);

    sender = dbus_message_get_sender(m);
    if (!pa_safe_streq(hfdata->ofono_bus_id, sender) && !pa_safe_streq("org.freedesktop.DBus", sender))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    dbus_error_init(&err);

    if (dbus_message_is_signal(m, "org.freedesktop.DBus", "NameOwnerChanged")) {
        const char *name, *old_owner, *new_owner;

        if (!dbus_message_get_args(m, &err,
                                   DBUS_TYPE_STRING, &name,
                                   DBUS_TYPE_STRING, &old_owner,
                                   DBUS_TYPE_STRING, &new_owner,
                                   DBUS_TYPE_INVALID)) {
            pa_log_error("Failed to parse org.freedesktop.DBus.NameOwnerChanged: %s", err.message);
            goto fail;
        }

        if (pa_streq(name, OFONO_SERVICE)) {

            if (old_owner && *old_owner) {
                pa_log_debug("oFono disappeared");

                if (hfdata->hf_audio_cards)
                    pa_hashmap_remove_all(hfdata->hf_audio_cards);

                if(hfdata->ofono_bus_id) {
                    pa_xfree(hfdata->ofono_bus_id);
                    hfdata->ofono_bus_id = NULL;
                }
            }

            if (new_owner && *new_owner) {
                pa_log_debug("oFono appeared");
                hf_audio_agent_register(hfdata);
            }
        }

    } else if (dbus_message_is_signal(m, "org.ofono.HandsfreeAudioManager", "CardAdded")) {
        const char *p;
        DBusMessageIter arg_i, props_i;

        if (!dbus_message_iter_init(m, &arg_i) || !pa_streq(dbus_message_get_signature(m), "oa{sv}")) {
            pa_log_error("Failed to parse org.ofono.HandsfreeAudioManager.CardAdded");
            goto fail;
        }

        dbus_message_iter_get_basic(&arg_i, &p);

        pa_assert_se(dbus_message_iter_next(&arg_i));
        pa_assert(dbus_message_iter_get_arg_type(&arg_i) == DBUS_TYPE_ARRAY);

        dbus_message_iter_recurse(&arg_i, &props_i);

        hf_audio_agent_card_found(hfdata, p, &props_i);

    } else if (dbus_message_is_signal(m, "org.ofono.HandsfreeAudioManager", "CardRemoved")) {
        const char *p;
        hf_audio_card *hfac;
        bool old_any_connected;

        if (!dbus_message_get_args(m, &err, DBUS_TYPE_OBJECT_PATH, &p, DBUS_TYPE_INVALID)) {
            pa_log_error("Failed to parse org.ofono.HandsfreeAudioManager.CardRemoved: %s", err.message);
            goto fail;
        }

        if ((hfac = pa_hashmap_remove(hfdata->hf_audio_cards, p)) != NULL) {
            old_any_connected = pa_bluetooth_device_any_transport_connected(hfac->transport->device);

            hfac->transport->state = PA_BLUETOOTH_TRANSPORT_STATE_DISCONNECTED;
            hfac->transport->device->transports[hfac->transport->profile] = NULL;
            pa_hook_fire(pa_bluetooth_discovery_hook(hfdata->discovery, PA_BLUETOOTH_HOOK_TRANSPORT_STATE_CHANGED), hfac->transport);

            if (old_any_connected != pa_bluetooth_device_any_transport_connected(hfac->transport->device)) {
                pa_hook_fire(pa_bluetooth_discovery_hook(hfdata->discovery, PA_BLUETOOTH_HOOK_DEVICE_CONNECTION_CHANGED), hfac->transport->device);
            }

            hf_audio_card_free(hfac);
        }
    }

fail:
    dbus_error_free(&err);
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static DBusMessage *hf_audio_agent_release(DBusConnection *c, DBusMessage *m, void *data) {
    DBusMessage *r;
    const char *sender;
    hf_audio_agent_data *hfdata = data;

    pa_assert(hfdata);

    sender = dbus_message_get_sender(m);
    if (!pa_streq(hfdata->ofono_bus_id, sender)) {
        pa_assert_se(r = dbus_message_new_error(m, "org.ofono.Error.NotAllowed", "Operation is not allowed by this sender"));
        return r;
    }

    pa_log_debug("HF audio agent has been unregistered by oFono (%s)", hfdata->ofono_bus_id);

    if (hfdata->hf_audio_cards) {
        pa_hashmap_free(hfdata->hf_audio_cards);
        hfdata->hf_audio_cards = NULL;
    }

    if(hfdata->ofono_bus_id) {
        pa_xfree(hfdata->ofono_bus_id);
        hfdata->ofono_bus_id = NULL;
    }

    pa_assert_se(r = dbus_message_new_method_return(m));

    return r;
}

static DBusMessage *hf_audio_agent_new_connection(DBusConnection *c, DBusMessage *m, void *data) {
    DBusMessage *r;
    const char *sender, *card;
    int fd;
    uint8_t codec;
    hf_audio_card *hfac;
    hf_audio_agent_data *hfdata = data;

    pa_assert(hfdata);

    sender = dbus_message_get_sender(m);
    if (!pa_streq(hfdata->ofono_bus_id, sender)) {
        pa_assert_se(r = dbus_message_new_error(m, "org.ofono.Error.NotAllowed", "Operation is not allowed by this sender"));
        return r;
    }

    if (dbus_message_get_args(m, NULL,
                              DBUS_TYPE_OBJECT_PATH, &card,
                              DBUS_TYPE_UNIX_FD, &fd,
                              DBUS_TYPE_BYTE, &codec,
                              DBUS_TYPE_INVALID) == FALSE) {
        pa_assert_se(r = dbus_message_new_error(m, "org.ofono.Error.InvalidArguments", "Invalid arguments in method call"));
        return r;
    }

    if ( !(hfac = pa_hashmap_get(hfdata->hf_audio_cards, card)) ) {
        pa_log_warn("New audio connection on unknown card %s (fd=%d, codec=%d)", card, fd, codec);
        pa_assert_se(r = dbus_message_new_error(m, "org.ofono.Error.InvalidArguments", "Unknown card"));
        return r;
    }

    pa_log_debug("New audio connection on card %s (fd=%d, codec=%d)", card, fd, codec);

    /* Do the socket defered setup */
    if (recv(fd, NULL, 0, 0) < 0) {
        const char *strerr = strerror(errno);
        pa_log_warn("Defered setup failed: %d (%s)", errno, strerr);
        pa_assert_se(r = dbus_message_new_error(m, "org.ofono.Error.InvalidArguments", strerr));
    }

    hfac->fd = fd;
    hfac->codec = codec;
    hfac->transport->state = PA_BLUETOOTH_TRANSPORT_STATE_PLAYING;
    pa_hook_fire(pa_bluetooth_discovery_hook(hfdata->discovery, PA_BLUETOOTH_HOOK_TRANSPORT_STATE_CHANGED), hfac->transport);

    pa_assert_se(r = dbus_message_new_method_return(m));

    return r;
}

static DBusHandlerResult hf_audio_agent_handler(DBusConnection *c, DBusMessage *m, void *data) {
    hf_audio_agent_data *hfdata = data;
    DBusMessage *r = NULL;
    const char *path, *interface, *member;

    pa_assert(hfdata);

    path = dbus_message_get_path(m);
    interface = dbus_message_get_interface(m);
    member = dbus_message_get_member(m);

    if (!pa_streq(path, HF_AUDIO_AGENT_PATH))
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    pa_log_debug("dbus: path=%s, interface=%s, member=%s", path, interface, member);

    if (dbus_message_is_method_call(m, "org.freedesktop.DBus.Introspectable", "Introspect")) {
        const char *xml = HF_AUDIO_AGENT_XML;

        pa_assert_se(r = dbus_message_new_method_return(m));
        pa_assert_se(dbus_message_append_args(r, DBUS_TYPE_STRING, &xml, DBUS_TYPE_INVALID));

    } else if (dbus_message_is_method_call(m, HF_AUDIO_AGENT_INTERFACE, "NewConnection"))
        r = hf_audio_agent_new_connection(c, m, data);
    else if (dbus_message_is_method_call(m, HF_AUDIO_AGENT_INTERFACE, "Release"))
        r = hf_audio_agent_release(c, m, data);
    else
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;

    if (r) {
        pa_assert_se(dbus_connection_send(pa_dbus_connection_get(hfdata->connection), r, NULL));
        dbus_message_unref(r);
    }

    return DBUS_HANDLER_RESULT_HANDLED;
}

hf_audio_agent_data *hf_audio_agent_init(pa_core *c) {
    hf_audio_agent_data *hfdata;
    DBusError err;
    static const DBusObjectPathVTable vtable_hf_audio_agent = {
        .message_function = hf_audio_agent_handler,
    };

    pa_assert(c);

    hfdata = pa_xnew0(hf_audio_agent_data, 1);
    hfdata->core = c;
    hfdata->hf_audio_cards = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func,
                                                 NULL, hf_audio_card_free);
    hfdata->discovery = pa_shared_get(c, "bluetooth-discovery");

    dbus_error_init(&err);

    if (!(hfdata->connection = pa_dbus_bus_get(c, DBUS_BUS_SYSTEM, &err))) {
        pa_log("Failed to get D-Bus connection: %s", err.message);
        dbus_error_free(&err);
        return NULL;
    }

    /* dynamic detection of handsfree audio cards */
    if (!dbus_connection_add_filter(pa_dbus_connection_get(hfdata->connection), filter_cb, hfdata, NULL)) {
        pa_log_error("Failed to add filter function");
        hf_audio_agent_done(hfdata);
        return NULL;
    }
    hfdata->filter_added = true;

    if (pa_dbus_add_matches(pa_dbus_connection_get(hfdata->connection), &err,
            "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged',"
            "arg0='" OFONO_SERVICE "'",
            "type='signal',sender='" OFONO_SERVICE "',interface='" HF_AUDIO_MANAGER_INTERFACE "',member='CardAdded'",
            "type='signal',sender='" OFONO_SERVICE "',interface='" HF_AUDIO_MANAGER_INTERFACE "',member='CardRemoved'",
            NULL) < 0) {
        pa_log("Failed to add oFono D-Bus matches: %s", err.message);
        hf_audio_agent_done(hfdata);
        return NULL;
    }

    pa_assert_se(dbus_connection_register_object_path(pa_dbus_connection_get(hfdata->connection), HF_AUDIO_AGENT_PATH,
                                                      &vtable_hf_audio_agent, hfdata));

    hf_audio_agent_register(hfdata);

    return hfdata;
}

void hf_audio_agent_done(hf_audio_agent_data *data) {
    hf_audio_agent_data *hfdata = data;

    pa_assert(hfdata);

    pa_dbus_free_pending_list(&hfdata->pending);

    if (hfdata->hf_audio_cards) {
        pa_hashmap_free(hfdata->hf_audio_cards);
        hfdata->hf_audio_cards = NULL;
    }

    if (hfdata->connection) {

        pa_dbus_remove_matches(
            pa_dbus_connection_get(hfdata->connection),
            "type='signal',sender='org.freedesktop.DBus',interface='org.freedesktop.DBus',member='NameOwnerChanged',"
            "arg0='" OFONO_SERVICE "'",
            "type='signal',sender='" OFONO_SERVICE "',interface='" HF_AUDIO_MANAGER_INTERFACE "',member='CardAdded'",
            "type='signal',sender='" OFONO_SERVICE "',interface='" HF_AUDIO_MANAGER_INTERFACE "',member='CardRemoved'",
            NULL);

        if (hfdata->filter_added)
            dbus_connection_remove_filter(pa_dbus_connection_get(hfdata->connection), filter_cb, hfdata);

        hf_audio_agent_unregister(hfdata);

        dbus_connection_unregister_object_path(pa_dbus_connection_get(hfdata->connection), HF_AUDIO_AGENT_PATH);

        pa_dbus_connection_unref(hfdata->connection);
    }

    if (hfdata->discovery)
        hfdata->discovery = NULL;

    pa_xfree(hfdata);
}
