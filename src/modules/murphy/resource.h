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
#ifndef fooresourcefoo
#define fooresourcefoo

#include <stdarg.h>

#include "userdata.h"

/* rset types */
#define PA_RESOURCE_RECORDING  0
#define PA_RESOURCE_PLAYBACK   1

#define PA_RESOURCE_RELEASE    1
#define PA_RESOURCE_ACQUIRE    2


struct pa_resource_rset_data {
    char *id;
    bool  autorel;
    int   state;
    bool  grant[2];
    char *policy[2];
    char *name;
    char *pid;
};


pa_resource *pa_resource_init(struct userdata *);
void pa_resource_done(struct userdata *);
unsigned pa_resource_get_number_of_resources(struct userdata *, int);
void pa_resource_purge(struct userdata *, uint32_t, int);
int pa_resource_enforce_policies(struct userdata *, int);

pa_resource_rset_data *pa_resource_rset_data_new(void);
void pa_resource_rset_data_free(pa_resource_rset_data *);

int pa_resource_rset_update(struct userdata *, const char *, const char *, int,
                            pa_resource_rset_data *, uint32_t);
int pa_resource_rset_remove(struct userdata *, const char *, const char *);


int pa_resource_stream_update(struct userdata *, const char *, const char *,
                              mir_node *);
int pa_resource_stream_remove(struct userdata *, mir_node *);



#endif /* fooresourcefoo */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
