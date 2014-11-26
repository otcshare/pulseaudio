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
#ifndef foomurphyiviutilsfoo
#define foomurphyiviutilsfoo

#include <stdbool.h>
#include <pulsecore/core.h>

struct pa_null_sink;

struct pa_null_sink *pa_utils_create_null_sink(struct userdata *,const char *);
void pa_utils_destroy_null_sink(struct userdata *);
pa_sink   *pa_utils_get_null_sink(struct userdata *);
pa_source *pa_utils_get_null_source(struct userdata *);

const char *pa_utils_get_card_name(pa_card *);
const char *pa_utils_get_card_bus(pa_card *);
const char *pa_utils_get_sink_name(pa_sink *);
const char *pa_utils_get_source_name(pa_source *);
const char *pa_utils_get_sink_input_name(pa_sink_input *);
const char *pa_utils_get_sink_input_name_from_data(pa_sink_input_new_data *);
const char *pa_utils_get_source_output_name(pa_source_output *);
const char *pa_utils_get_source_output_name_from_data(pa_source_output_new_data *);

char *pa_utils_get_zone(pa_proplist *);
const char *pa_utils_get_appid(pa_proplist *);

bool pa_utils_set_stream_routing_properties(pa_proplist *, int, void *);
bool pa_utils_unset_stream_routing_properties(pa_proplist *);
void      pa_utils_set_stream_routing_method_property(pa_proplist *,bool);
bool pa_utils_stream_has_default_route(pa_proplist *);
int       pa_utils_get_stream_class(pa_proplist *);


#ifdef foomurphyuserdatafoo  /* argh ... */
bool pa_utils_set_resource_properties(pa_proplist *, pa_nodeset_resdef *);
bool pa_utils_unset_resource_properties(pa_proplist *);
pa_nodeset_resdef *pa_utils_get_resource_properties(pa_proplist *,
                                                    pa_nodeset_resdef *);

void      pa_utils_set_port_properties(pa_device_port *, mir_node *);
mir_node *pa_utils_get_node_from_port(struct userdata *, pa_device_port *, void **);
mir_node *pa_utils_get_node_from_stream(struct userdata *,mir_direction,void*);
mir_node *pa_utils_get_node_from_data(struct userdata *, mir_direction,void *);
#endif

const char *pa_utils_file_path(const char *, const char *, char *, size_t);

uint32_t pa_utils_new_stamp(void);
uint32_t pa_utils_get_stamp(void);

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
