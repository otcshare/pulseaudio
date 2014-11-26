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
#ifndef foomirzonefoo
#define foomirzonefoo

#include <sys/types.h>

#include "userdata.h"

struct mir_zone {
    const char *name;
    uint32_t    index;
};

pa_zoneset *pa_zoneset_init(struct userdata *);
void pa_zoneset_done(struct userdata *);

int pa_zoneset_add_zone(struct userdata *, const char *, uint32_t);
mir_zone *pa_zoneset_get_zone_by_name(struct userdata *, const char *);
mir_zone *pa_zoneset_get_zone_by_index(struct userdata *, uint32_t);

void pa_zoneset_update_module_property(struct userdata *);

#endif  /* foomirzonefoo */


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
