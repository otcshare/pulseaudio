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
#ifndef foodiscoverfoo
#define foodiscoverfoo

#include <sys/types.h>
#include <regex.h>

#include "userdata.h"


#define PA_BIT(a)      (1UL << (a))

#if 0
enum pa_bus_type {
    pa_bus_unknown = 0,
    pa_bus_pci,
    pa_bus_usb,
    pa_bus_bluetooth,
};

enum pa_form_factor {
    pa_form_factor_unknown,
    pa_internal,
    pa_speaker,
    pa_handset,
    pa_tv,
    pa_webcam,
    pa_microphone,
    pa_headset,
    pa_headphone,
    pa_hands_free,
    pa_car,
    pa_hifi,
    pa_computer,
    pa_portable
};
#endif

struct pa_discover {
    /*
     * cirteria for filtering sinks and sources
     */
    unsigned        chmin;    /**< minimum of max channels */
    unsigned        chmax;    /**< maximum of max channels */
    bool       selected; /**< for alsa cards: whether to consider the
                                   selected profile alone.
                                   for bluetooth cards: no effect */
    struct {
        pa_hashmap *byname;
        pa_hashmap *byptr;
    }               nodes;
};


struct pa_discover *pa_discover_init(struct userdata *);
void  pa_discover_done(struct userdata *);

void pa_discover_domain_up(struct userdata *);
void pa_discover_domain_down(struct userdata *);

void pa_discover_add_card(struct userdata *, pa_card *);
void pa_discover_remove_card(struct userdata *, pa_card *);
void pa_discover_profile_changed(struct userdata *, pa_card *);

void pa_discover_port_available_changed(struct userdata *, pa_device_port *);

void pa_discover_add_sink(struct userdata *, pa_sink *, bool);
void pa_discover_remove_sink(struct userdata *, pa_sink *);

void pa_discover_add_source(struct userdata *, pa_source *);
void pa_discover_remove_source(struct userdata *, pa_source *);

void pa_discover_register_sink_input(struct userdata *, pa_sink_input *);
bool pa_discover_preroute_sink_input(struct userdata *,
                                          pa_sink_input_new_data *);
void pa_discover_add_sink_input(struct userdata *, pa_sink_input *);
void pa_discover_remove_sink_input(struct userdata *, pa_sink_input *);

void pa_discover_register_source_output(struct userdata *, pa_source_output *);
bool pa_discover_preroute_source_output(struct userdata *,
                                             pa_source_output_new_data *);
void pa_discover_add_source_output(struct userdata *, pa_source_output *);
void pa_discover_remove_source_output(struct userdata *, pa_source_output *);


mir_node *pa_discover_find_node_by_key(struct userdata *, const char *);
mir_node *pa_discover_find_node_by_ptr(struct userdata *, void *);

void pa_discover_add_node_to_ptr_hash(struct userdata *, void *, mir_node *);
mir_node *pa_discover_remove_node_from_ptr_hash(struct userdata *, void *);

#endif


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
