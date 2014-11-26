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
#ifndef foomirconstrainfoo
#define foomirconstrainfoo

#include <sys/types.h>

#include "userdata.h"
#include "list.h"

typedef bool (*mir_constrain_func_t)(struct userdata *, mir_constr_def *,
                                          mir_node *, mir_node *);

struct pa_constrain {
    pa_hashmap *defs;
};


struct mir_constr_link {
    mir_dlist       link;
    mir_dlist       nodchain;
    mir_constr_def *def;
    mir_node       *node;
};


struct mir_constr_def {
    char                 *key;
    char                 *name;  /**< constrain name */
    mir_constrain_func_t  func;  /**< constrain enforcement function */
    mir_dlist             nodes; /**< listhead of mir_cstrlink's  */
};



pa_constrain *pa_constrain_init(struct userdata *);
void pa_constrain_done(struct userdata *);

mir_constr_def *mir_constrain_create(struct userdata *, const char *,
                                     mir_constrain_func_t, const char *);
void mir_constrain_destroy(struct userdata *, const char *);
mir_constr_def *mir_constrain_find(struct userdata *, const char *);

void mir_constrain_add_node(struct userdata *, mir_constr_def *, mir_node *);
void mir_constrain_remove_node(struct userdata *, mir_node *);

void mir_constrain_apply(struct userdata *, mir_node *, uint32_t);

int mir_constrain_print(mir_node *, char *, int);


bool mir_constrain_port(struct userdata *, mir_constr_def *,
                             mir_node *, mir_node *);
bool mir_constrain_profile(struct userdata *, mir_constr_def *,
                                mir_node *, mir_node *);



#endif  /* foomirconstrainfoo */


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
