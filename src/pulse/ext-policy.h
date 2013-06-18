#ifndef foopulseextpolicyhfoo
#define foopulseextpolicyhfoo

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
 * Routines for controlling module-policy
 */

PA_C_DECL_BEGIN

/** Callback prototype for pa_ext_policy_test(). \since 0.9.21 */
typedef void (*pa_ext_policy_test_cb_t)(
        pa_context *c,
        uint32_t version,
        void *userdata);

/** Test if this extension module is available in the server. \since 0.9.21 */
pa_operation *pa_ext_policy_test(
        pa_context *c,
        pa_ext_policy_test_cb_t cb,
        void *userdata);

/** Enable the mono mode. \since 0.9.21 */
pa_operation *pa_ext_policy_set_mono (
        pa_context *c,
        int enable,
        pa_context_success_cb_t cb,
        void *userdata);

/** Enable the balance mode. \since 0.9.21 */
pa_operation *pa_ext_policy_set_balance (
        pa_context *c,
        double *balance,
        pa_context_success_cb_t cb,
        void *userdata);

PA_C_DECL_END

#endif
