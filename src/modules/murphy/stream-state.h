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
#ifndef foostreamstatefoo
#define foostreamstatefoo

#include <sys/types.h>

#include "userdata.h"

#define PA_STREAM_BLOCK   1
#define PA_STREAM_RUN     0
#define PA_STREAM_KILL   -1


bool pa_stream_state_start_corked(struct userdata *,
                                       pa_sink_input_new_data *,
                                       pa_nodeset_resdef *);
void pa_stream_state_change(struct userdata *u, mir_node *, int);


#endif  /* foostreamstatefoo */


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
