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
#ifndef foomirnodefoo
#define foomirnodefoo

#include <sys/types.h>

#include "userdata.h"
#include "list.h"
#include "multiplex.h"
#include "loopback.h"
#include "volume.h"

#define AM_ID_INVALID   65535

struct pa_nodeset_resdef {
    uint32_t           priority;
    struct {
        uint32_t rset;
        uint32_t audio;
    }                  flags;
};

struct pa_nodeset_map {
    const char        *name;
    mir_node_type      type;
    const char        *role;
    pa_nodeset_resdef *resdef;
};

struct pa_node_card {
    uint32_t  index;
    char     *profile;
};

struct pa_node_rset {
    char     *id;               /**< resource set id, if any */
    bool      grant;            /**< permission to play/render etc */
};

/**
 * @brief routing endpoint
 *
 * @details node is a routing endpoint in the GenIVI audio model.
 *          In pulseaudio terminology a routing endpoint is one of
 *          the following
 * @li      node is a pulseaudio sink or source. Such node is a
 *          combination of pulseudio card/profile + sink/port
 * @li      node is a pulseaudio stream. Such node in pulseaudio
 *          is either a sink_input or a source_output
 */
struct mir_node {
    uint32_t       index;     /**< index into nodeset->idxset */
    char          *key;       /**< hash key for discover lookups */
    mir_direction  direction; /**< mir_input | mir_output */
    mir_implement  implement; /**< mir_device | mir_stream */
    uint32_t       channels;  /**< number of channels (eg. 1=mono, 2=stereo) */
    mir_location   location;  /**< mir_internal | mir_external */
    mir_privacy    privacy;   /**< mir_public | mir_private */
    mir_node_type  type;      /**< mir_speakers | mir_headset | ...  */
    char          *zone;      /**< zone where the node belong */
    bool           visible;   /**< internal or can appear on UI  */
    bool           available; /**< eg. is the headset connected?  */
    bool           ignore;    /**< do not consider it while routing  */
    bool           localrset; /**< locally generated resource set */
    const char    *amname;    /**< audiomanager name */
    const char    *amdescr;   /**< UI description */
    uint16_t       amid;      /**< handle to audiomanager, if any */
    const char    *paname;    /**< sink|source|sink_input|source_output name */
    uint32_t       paidx;     /**< sink|source|sink_input|source_output index*/
    pa_node_card   pacard;    /**< pulse card related data, if any  */
    const char    *paport;    /**< sink or source port if applies */
    pa_muxnode    *mux;       /**< for multiplexable input streams only */
    pa_loopnode   *loop;      /**< for looped back sources only */
    mir_dlist      rtentries; /**< in device nodes: listhead of nodchain */
    mir_dlist      rtprilist; /**< in stream nodes: priority link (head is in
                                                                   pa_router)*/
    mir_dlist      constrains;/**< listhead of constrains */
    mir_vlim       vlim;      /**< volume limit */
    pa_node_rset   rset;      /**< resource set info if applies */
    uint32_t       stamp;
    scripting_node *scripting;/** scripting data, if any */
};


pa_nodeset *pa_nodeset_init(struct userdata *);
void pa_nodeset_done(struct userdata *);

int pa_nodeset_add_class(struct userdata *u, mir_node_type , const char *);
void pa_nodeset_delete_class(struct userdata *, mir_node_type);
const char *pa_nodeset_get_class(struct userdata *, mir_node_type);

int pa_nodeset_add_role(struct userdata *, const char *, mir_node_type,
                        pa_nodeset_resdef *);
void pa_nodeset_delete_role(struct userdata *, const char *);
pa_nodeset_map *pa_nodeset_get_map_by_role(struct userdata *, const char *);

int pa_nodeset_add_binary(struct userdata *, const char *, mir_node_type,
                          const char *, pa_nodeset_resdef *);
void pa_nodeset_delete_binary(struct userdata *, const char *);
pa_nodeset_map *pa_nodeset_get_map_by_binary(struct userdata *, const char *);

int pa_nodeset_print_maps(struct userdata *, char *, int);

mir_node *pa_nodeset_iterate_nodes(struct userdata *, uint32_t *);


mir_node *mir_node_create(struct userdata *, mir_node *);
void mir_node_destroy(struct userdata *, mir_node *);

mir_node *mir_node_find_by_index(struct userdata *, uint32_t);


int mir_node_print(mir_node *, char *, int);

const char *mir_direction_str(mir_direction);
const char *mir_implement_str(mir_implement);
const char *mir_location_str(mir_location);
const char *mir_node_type_str(mir_node_type);
const char *mir_privacy_str(mir_privacy);

#endif


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
