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
#ifndef foomirfaderfoo
#define foomirfaderfoo

#include <sys/types.h>

#include "userdata.h"
#include "list.h"


pa_fader *pa_fader_init(const char *, const char *);
void pa_fader_done(struct userdata *);

void pa_fader_apply_volume_limits(struct userdata *, uint32_t);

void pa_fader_ramp_volume(struct userdata *, pa_sink_input *, pa_volume_t);
void pa_fader_set_volume(struct userdata *, pa_sink_input *, pa_volume_t);
pa_volume_t pa_fader_get_volume(struct userdata *, pa_sink_input *);

#endif  /* foomirfaderfoo */


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
