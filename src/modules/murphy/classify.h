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
#ifndef foomirclassifyfoo
#define foomirclassifyfoo

#include <sys/types.h>

#include "userdata.h"

void pa_classify_node_by_card(mir_node *, pa_card *, pa_card_profile *,
                              pa_device_port *);
bool pa_classify_node_by_property(mir_node *, pa_proplist *);
void pa_classify_guess_device_node_type_and_name(mir_node*, const char *,
                                                 const char *);
mir_node_type pa_classify_guess_stream_node_type(struct userdata *,
                                                 pa_proplist *,
                                                 pa_nodeset_resdef **);
mir_node_type pa_classify_guess_application_class(mir_node *);

bool pa_classify_multiplex_stream(mir_node *);
bool pa_classify_ramping_stream(mir_node *);

const char *pa_classify_loopback_stream(mir_node *);

#endif  /* foomirclassifyfoo */


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
