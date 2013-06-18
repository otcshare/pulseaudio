/***
  This file is part of PulseAudio.

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
#include <pulse/gccmacro.h>
#include <pulse/xmalloc.h>

#include <pulsecore/macro.h>
#include <pulsecore/pstream-util.h>

#include "internal.h"
#include "operation.h"
#include "fork-detect.h"

#include "ext-policy.h"

enum {
    SUBCOMMAND_TEST,
    SUBCOMMAND_MONO,
    SUBCOMMAND_BALANCE,
};

static void ext_policy_test_cb(pa_pdispatch *pd, uint32_t command, uint32_t tag, pa_tagstruct *t, void *userdata) {
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
        pa_ext_policy_test_cb_t cb = (pa_ext_policy_test_cb_t) o->callback;
        cb(o->context, version, o->userdata);
    }

finish:
    pa_operation_done(o);
    pa_operation_unref(o);
}

pa_operation *pa_ext_policy_test(
        pa_context *c,
        pa_ext_device_manager_test_cb_t cb,
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
    pa_tagstruct_puts(t, "module-policy");
    pa_tagstruct_putu32(t, SUBCOMMAND_TEST);
    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, ext_policy_test_cb, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation *pa_ext_policy_set_mono (
        pa_context *c,
        int enable,
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
    pa_tagstruct_puts(t, "module-policy");
    pa_tagstruct_putu32(t, SUBCOMMAND_MONO);
    pa_tagstruct_put_boolean(t, !!enable);

    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

pa_operation *pa_ext_policy_set_balance (
        pa_context *c,
        double *balance,
        pa_context_success_cb_t cb,
        void *userdata) {

    uint32_t tag;
    pa_operation *o = NULL;
    pa_tagstruct *t = NULL;
    pa_cvolume cvol;
    pa_channel_map map;

    pa_assert(c);
    pa_assert(PA_REFCNT_VALUE(c) >= 1);

    PA_CHECK_VALIDITY_RETURN_NULL(c, !pa_detect_fork(), PA_ERR_FORKED);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->state == PA_CONTEXT_READY, PA_ERR_BADSTATE);
    PA_CHECK_VALIDITY_RETURN_NULL(c, c->version >= 14, PA_ERR_NOTSUPPORTED);

    o = pa_operation_new(c, NULL, (pa_operation_cb_t) cb, userdata);

    t = pa_tagstruct_command(c, PA_COMMAND_EXTENSION, &tag);
    pa_tagstruct_putu32(t, PA_INVALID_INDEX);
    pa_tagstruct_puts(t, "module-policy");
    pa_tagstruct_putu32(t, SUBCOMMAND_BALANCE);

    /* Prepare cvolume for transfer */
    pa_channel_map_init_stereo(&map);
    pa_cvolume_set(&cvol, map.channels, 65535);

    pa_log_error ("balance = %f", *balance);

    pa_cvolume_set_balance(&cvol, &map, *balance);

    pa_log_error ("balance get = %f", pa_cvolume_get_balance(&cvol, &map));

    pa_tagstruct_put_cvolume(t, &cvol);

    pa_pstream_send_tagstruct(c->pstream, t);
    pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

    return o;
}

