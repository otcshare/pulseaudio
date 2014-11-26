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
#ifndef foomirrouterfoo
#define foomirrouterfoo

#include <sys/types.h>

#include <murphy/resource/data-types.h>

#include "userdata.h"
#include "list.h"

typedef bool (*mir_rtgroup_accept_t)(struct userdata *, mir_rtgroup *,
                                          mir_node *);
typedef int       (*mir_rtgroup_compare_t)(struct userdata *, mir_rtgroup *,
                                           mir_node *, mir_node *);

typedef struct {
    pa_hashmap *input;
    pa_hashmap *output;
} pa_rtgroup_hash;

typedef struct {
    mir_rtgroup **input[MRP_ZONE_MAX];
    mir_rtgroup **output[MRP_ZONE_MAX];
} pa_rtgroup_classmap;

struct pa_router {
    pa_rtgroup_hash      rtgroups;
    int                  maplen;   /**< length of the class- and priormap */
    pa_rtgroup_classmap  classmap; /**< to map device node types to rtgroups */
    int                 *priormap; /**< stream node priorities */
    mir_dlist            nodlist;  /**< priorized list of the stream nodes
                                        (entry in node: rtprilist) */
    mir_dlist            connlist; /**< listhead of the connections */
};


struct mir_rtentry {
    mir_dlist    link;        /**< rtgroup chain */
    mir_dlist    nodchain;    /**< node chain */
    mir_rtgroup *group;       /**< back pointer to the group  */
    mir_node    *node;        /**< pointer to the owning node */
    bool         blocked;     /**< weather this routing entry is active */
    uint32_t     stamp;
};

struct mir_rtgroup {
    char                  *name;      /**< name of the rtgroup */
    mir_dlist              entries;   /**< listhead of ordered rtentries */
    mir_rtgroup_accept_t   accept;    /**< wheter to accept a node or not */
    mir_rtgroup_compare_t  compare;   /**< comparision function for ordering */
    scripting_rtgroup     *scripting; /**< data for scripting, if any */
};

struct mir_connection {
    mir_dlist     link;     /**< list of connections */
    bool     blocked;  /**< true if this conflicts with another route */
    uint16_t      amid;     /**< audio manager connection id */
    uint32_t      from;     /**< source node index */
    uint32_t      to;       /**< destination node index */
    uint32_t      stream;   /**< index of the sink-input to be routed */
};


pa_router *pa_router_init(struct userdata *);
void pa_router_done(struct userdata *);

void mir_router_assign_class_priority(struct userdata *, mir_node_type, int);

mir_rtgroup *mir_router_create_rtgroup(struct userdata *,
                                       mir_direction, const char *,
                                       mir_rtgroup_accept_t,
                                       mir_rtgroup_compare_t);
void mir_router_destroy_rtgroup(struct userdata *, mir_direction,
                                const char *);
bool mir_router_assign_class_to_rtgroup(struct userdata *, mir_node_type,
                                             uint32_t, mir_direction,
                                             const char *);

void mir_router_register_node(struct userdata *, mir_node *);
void mir_router_unregister_node(struct userdata *, mir_node *);

mir_node *mir_router_make_prerouting(struct userdata *, mir_node *);
void mir_router_make_routing(struct userdata *);

mir_connection *mir_router_add_explicit_route(struct userdata *, uint16_t,
                                              mir_node *, mir_node *);
void mir_router_remove_explicit_route(struct userdata *, mir_connection *);


int mir_router_print_rtgroups(struct userdata *, char *, int);

bool mir_router_default_accept(struct userdata *, mir_rtgroup *,
                                    mir_node *);
bool mir_router_phone_accept(struct userdata *, mir_rtgroup *,
                                  mir_node *);

int mir_router_default_compare(struct userdata *, mir_rtgroup *,
                               mir_node *, mir_node *);
int mir_router_phone_compare(struct userdata *, mir_rtgroup *,
                             mir_node *, mir_node *);


#endif  /* foomirrouterfoo */


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
