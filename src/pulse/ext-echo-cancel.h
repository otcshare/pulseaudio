#ifndef foopulseechocancelfoo
#define foopulseechocancelfoo

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

#include <pulse/context.h>
#include <pulse/version.h>

/** \file
 *
 * Routines for controlling module-echo-cancel
 */

PA_C_DECL_BEGIN

/** Set volume to AEC module */
pa_operation *pa_ext_echo_cancel_set_volume (
        pa_context *c,
        int volume,
        pa_context_success_cb_t cb,
        void *userdata);

pa_operation *pa_ext_echo_cancel_set_device (
        pa_context *c,
        int device,
        pa_context_success_cb_t cb,
        void *userdata);


PA_C_DECL_END

#endif
