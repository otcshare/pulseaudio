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
#include <pulsecore/log.h>
#include "internal.h"
#include "operation.h"
#include "fork-detect.h"

#include "ext-echo-cancel.h"

enum {
	AEC_SET_VOLUME,
	AEC_SET_DEVICE,
};

pa_operation *pa_ext_echo_cancel_set_device (
				pa_context *c,
				int device,
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
	pa_tagstruct_puts(t, "module-echo-cancel");
	pa_tagstruct_putu32(t, AEC_SET_DEVICE);
	pa_tagstruct_putu32(t, device);

	pa_pstream_send_tagstruct(c->pstream, t);
	pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);
	return o;
}


pa_operation *pa_ext_echo_cancel_set_volume (
				pa_context *c,
				int volume,
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
	pa_tagstruct_puts(t, "module-echo-cancel");
	pa_tagstruct_putu32(t, AEC_SET_VOLUME);
	pa_tagstruct_putu32(t, volume);

	pa_pstream_send_tagstruct(c->pstream, t);
	pa_pdispatch_register_reply(c->pdispatch, tag, DEFAULT_TIMEOUT, pa_context_simple_ack_callback, pa_operation_ref(o), (pa_free_cb_t) pa_operation_unref);

	return o;
}
