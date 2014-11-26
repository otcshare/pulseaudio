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
 * Free Software Foundation, Inc.,  51 Franklin St - Fifth Floor, Boston,
 * MA 02110-1301 USA.
 *
 */
#ifndef foomirvolumefoo
#define foomirvolumefoo

#include <sys/types.h>

#include "userdata.h"
#include "list.h"


typedef double (*mir_volume_func_t)(struct userdata *, int, mir_node *, void*);
typedef void (*mir_change_value_t)(struct userdata *, const char *);

struct mir_vlim {
    size_t         maxentry;    /**< length of the class table  */
    size_t         nclass;      /**< number of classes (0 - maxentry) */
    int           *classes;     /**< class table  */
    uint32_t       clmask;      /**< bits of the classes */
    uint32_t       stamp;
};

struct mir_volume_suppress_arg {
    double *attenuation;
    struct {
        size_t nclass;
        int *classes;
        uint32_t clmask;
    } trigger;
};


pa_mir_volume *pa_mir_volume_init(struct userdata *);
void pa_mir_volume_done(struct userdata *);

void mir_volume_add_class_limit(struct userdata *,int,mir_volume_func_t,void*);
void mir_volume_add_generic_limit(struct userdata *, mir_volume_func_t,void *);
void mir_volume_add_maximum_limit(struct userdata *, double, size_t, int *);

void mir_volume_make_limiting(struct userdata *);

void mir_volume_add_limiting_class(struct userdata *,mir_node *,int,uint32_t);
double mir_volume_apply_limits(struct userdata *, mir_node *, int, uint32_t);

double mir_volume_suppress(struct userdata *, int, mir_node *, void *);
double mir_volume_correction(struct userdata *, int, mir_node *, void *);

void mir_volume_change_context(struct userdata *u, const char *volume_class);

#endif  /* foomirvolumefoo */


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
