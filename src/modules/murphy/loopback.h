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
#ifndef fooloopbackfoo
#define fooloopbackfoo

#include <pulsecore/core.h>
#include <pulsecore/sink-input.h>

#include "list.h"

typedef struct pa_loopnode pa_loopnode;

typedef enum {
    PA_LOOPBACK_TYPE_UNKNOWN = 0,
    PA_LOOPBACK_SOURCE,
    PA_LOOPBACK_SINK,
} pa_loopback_type;

typedef struct pa_loopback {
    PA_LLIST_HEAD(pa_loopnode, loopnodes);
} pa_loopback;


struct pa_loopnode {
    PA_LLIST_FIELDS(pa_loopnode);
    uint32_t   module_index;
    uint32_t   node_index;
    uint32_t   sink_input_index;
    uint32_t   source_output_index;
};

pa_loopback *pa_loopback_init(void);

void pa_loopback_done(pa_loopback *, pa_core *);

pa_loopnode *pa_loopback_create(pa_loopback *, pa_core *, pa_loopback_type,
                                uint32_t, uint32_t, uint32_t, const char *,
                                uint32_t, uint32_t, uint32_t);
void pa_loopback_destroy(pa_loopback *, pa_core *, pa_loopnode *);

uint32_t pa_loopback_get_sink_index(pa_core *, pa_loopnode *);

int pa_loopback_print(pa_loopnode *, char *, int);


#endif /* fooloopbackfoo */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
