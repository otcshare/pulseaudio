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
#ifndef foomultiplexfoo
#define foomultiplexfoo

#include <pulsecore/core.h>
#include <pulsecore/sink-input.h>

#include "list.h"

typedef struct pa_muxnode pa_muxnode;

typedef struct pa_multiplex {
    PA_LLIST_HEAD(pa_muxnode, muxnodes);
} pa_multiplex;


struct pa_muxnode {
    PA_LLIST_FIELDS(pa_muxnode);
    uint32_t   module_index;
    uint32_t   sink_index;
    uint32_t   defstream_index;
};

pa_multiplex *pa_multiplex_init(void);

void pa_multiplex_done(pa_multiplex *, pa_core *);

pa_muxnode *pa_multiplex_create(pa_multiplex *, pa_core *, uint32_t,
                                pa_channel_map *, const char *, const char *,
                                int);
void pa_multiplex_destroy(pa_multiplex *, pa_core *, pa_muxnode *);

pa_muxnode *pa_multiplex_find_by_sink(pa_multiplex *, uint32_t);
pa_muxnode *pa_multiplex_find_by_module(pa_multiplex *, pa_module *);

bool pa_multiplex_sink_input_remove(pa_multiplex *, pa_sink_input *);

bool pa_multiplex_add_default_route(pa_core *, pa_muxnode *,pa_sink *,int);
bool pa_multiplex_remove_default_route(pa_core *,pa_muxnode *,bool);
bool pa_multiplex_change_default_route(pa_core *,pa_muxnode *,pa_sink *);

bool pa_multiplex_add_explicit_route(pa_core*, pa_muxnode*, pa_sink*,int);
bool pa_multiplex_remove_explicit_route(pa_core *, pa_muxnode *, pa_sink *);

bool pa_multiplex_duplicate_route(pa_core *, pa_muxnode *,
                                       pa_sink_input *, pa_sink *);

int pa_multiplex_no_of_routes(pa_core *, pa_muxnode *);

int pa_multiplex_print(pa_muxnode *, char *, int);


#endif /* foomultiplexfoo */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
