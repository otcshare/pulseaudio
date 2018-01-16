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
#ifndef foomurphyiffoo
#define foomurphyiffoo

#include <stdarg.h>

#ifdef WITH_MURPHYIF
#include <murphy/domain-control/client.h>
#else
typedef void mrp_domctl_value_t;
#endif

#include "userdata.h"


typedef void (*pa_murphyif_watch_cb)(struct userdata *u, const char *,
                                         int, mrp_domctl_value_t **);

pa_murphyif *pa_murphyif_init(struct userdata *, const char *, const char *);
void pa_murphyif_done(struct userdata *);

void pa_murphyif_add_table(struct userdata *, const char *, const char *,
                           const char *);
int  pa_murphyif_add_watch(struct userdata *, const char *, const char *,
                           const char *, int);
void pa_murphyif_setup_domainctl(struct userdata *, pa_murphyif_watch_cb);

void pa_murphyif_add_audio_resource(struct userdata *, mir_direction,
                                    const char *);
void pa_murphyif_add_audio_attribute(struct userdata *, const char *,
                                     const char *, mqi_data_type_t, ... );
void pa_murphyif_create_resource_set(struct userdata *, mir_node *,
                                     pa_nodeset_resdef *);
void pa_murphyif_destroy_resource_set(struct userdata *, mir_node *);

int  pa_murphyif_add_node(struct userdata *, mir_node *);
void pa_murphyif_delete_node(struct userdata *, mir_node *);


#endif /* foomurphyiffoo */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
