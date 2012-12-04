/***
  This file is part of PulseAudio.

  Copyright 2012 Jaska Uimonen
  Copyright 2012 Janos Kovacs

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

#include <pulse/context.h>
#include <pulse/xmalloc.h>
#include <pulse/fork-detect.h>
#include <pulse/operation.h>

#include <pulsecore/macro.h>
#include <pulsecore/pstream-util.h>

#include "internal.h"
#include "ext-node-manager.h"

enum {
    SUBCOMMAND_TEST,
    SUBCOMMAND_READ,
    SUBCOMMAND_CONNECT,
    SUBCOMMAND_DISCONNECT,
    SUBCOMMAND_SUBSCRIBE,
    SUBCOMMAND_EVENT
};

static void ext_node_manager_test_cb(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_operation *o = userdata;
    uint32_t version = PA_INVALID_INDEX;

    pa_assert(pd);
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    if (!o->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(o->context, command, t, FALSE) < 0)
            goto finish;

    } else if (pa_tagstruct_getu32(t, &version) < 0 ||
               !pa_tagstruct_eof(t)) {

        pa_context_fail(o->context, PA_ERR_PROTOCOL);
        goto finish;
    }

    if (o->callback) {
        pa_ext_node_manager_test_cb_t cb = (pa_ext_node_manager_test_cb_t) o->callback;
        cb(o->context, version, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation *pa_ext_node_manager_test(
        pa_context *c,
        pa_ext_node_manager_test_cb_t cb,
        void *userdata) {

    uint32_t tag;
    pa_operation *o;
    pa_tagstruct *t;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 14, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, "module-murphy-ivi");
    pa_tagstruct_putu32(t, SUBCOMMAND_TEST);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, ext_node_manager_test_cb, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

static void ext_node_manager_read_cb(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_operation *o = userdata;
    pa_ext_node_manager_info i;

    pa_assert(pd);
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    if (!o->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(o->context, command, t, FALSE) < 0)
            goto finish;
    } else {

        while (!pa_tagstruct_eof(t)) {

            memset(&i, 0, sizeof(i));

	    i.props = pa_proplist_new();

            if (pa_tagstruct_gets(t, &i.name) < 0 ||
                pa_tagstruct_get_proplist(t, i.props) < 0) {

                pa_context_fail(o->context, PA_ERR_PROTOCOL);
                goto finish;
            }

            if (o->callback) {
                pa_ext_node_manager_read_cb_t cb = (pa_ext_node_manager_read_cb_t) o->callback;
                cb(o->context, &i, 0, o->userdata);
            }

	    pa_proplist_free(i.props);
        }

	/* let's send end marker */
	if (o->callback) {
                pa_ext_node_manager_read_cb_t cb = (pa_ext_node_manager_read_cb_t) o->callback;
                cb(o->context, &i, 1, o->userdata);
            }
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation *pa_ext_node_manager_read_nodes(
        pa_context *c,
        pa_ext_node_manager_read_cb_t cb,
        void *userdata) {

    uint32_t tag;
    pa_operation *o;
    pa_tagstruct *t;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 14, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, "module-murphy-ivi");
    pa_tagstruct_putu32(t, SUBCOMMAND_READ);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, ext_node_manager_read_cb, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

static void ext_node_manager_connect_cb(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
    pa_operation *o = userdata;
    uint32_t connection = PA_INVALID_INDEX;

    pa_assert(pd);
    pa_assert(o);
    pa_assert(PA_REFCNT_VALUE(o) >= 1);

    if (!o->context)
        goto finish;

    if (command != PA_COMMAND_REPLY) {
        if (pa_context_handle_error(o->context, command, t, FALSE) < 0)
            goto finish;

    } else if (pa_tagstruct_getu32(t, &connection) < 0 ||
               !pa_tagstruct_eof(t)) {

        pa_context_fail(o->context, PA_ERR_PROTOCOL);
        goto finish;
    }

    if (o->callback) {
        pa_ext_node_manager_connect_cb_t cb = (pa_ext_node_manager_connect_cb_t) o->callback;
        cb(o->context, connection, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation *pa_ext_node_manager_connect_nodes(
        pa_context *c,
        uint32_t source_node_id,
        uint32_t sink_node_id,
        pa_ext_node_manager_connect_cb_t cb,
        void *userdata) {

    uint32_t tag;
    pa_operation *o = NULL;
    pa_tagstruct *t = NULL;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 14, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, "module-murphy-ivi");
    pa_tagstruct_putu32(t, SUBCOMMAND_CONNECT);

    pa_tagstruct_putu32(t, source_node_id);
    pa_tagstruct_putu32(t, sink_node_id);

    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, ext_node_manager_connect_cb, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation *pa_ext_node_manager_disconnect_nodes(
        pa_context *c,
	uint32_t conn_id,
        pa_context_success_cb_t cb,
        void *userdata) {

    uint32_t tag;
    pa_operation *o = NULL;
    pa_tagstruct *t = NULL;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 14, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, "module-murphy-ivi");
    pa_tagstruct_putu32(t, SUBCOMMAND_DISCONNECT);

    pa_tagstruct_putu32(t, conn_id);

    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation *pa_ext_node_manager_subscribe(
        pa_context *c,
        int enable,
        pa_context_success_cb_t cb,
        void *userdata) {

    uint32_t tag;
    pa_operation *o;
    pa_tagstruct *t;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 14, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, "module-murphy-ivi");
    pa_tagstruct_putu32(t, SUBCOMMAND_SUBSCRIBE);
    pa_tagstruct_put_boolean(t, enable);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

void pa_ext_node_manager_set_subscribe_cb(
        pa_context *c,
        pa_ext_node_manager_subscribe_cb_t cb,
        void *userdata) {

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    if (pa_detect_fork())
        return;

    c->ext_node_manager.callback = cb;
    c->ext_node_manager.userdata = userdata;
}

void pa_ext_node_manager_command(pa_context *c, uint32_t tag, pa_tagstruct *t) {
    uint32_t subcommand;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);
    pa_assert(t);

    if (pa_tagstruct_getu32(t, &subcommand) < 0 ||
        !pa_tagstruct_eof(t)) {

        pa_context_fail(c, PA_ERR_PROTOCOL);
        return;
    }

    if (subcommand != SUBCOMMAND_EVENT) {
        pa_context_fail(c, PA_ERR_PROTOCOL);
        return;
    }

    if (c->ext_node_manager.callback)
        c->ext_node_manager.callback(c, c->ext_node_manager.userdata);
}
