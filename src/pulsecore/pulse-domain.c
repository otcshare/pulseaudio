/***
  This file is part of PulseAudio.

  Copyright (c) 2012 Intel Corporation
  Janos Kovacs <jankovac503@gmail.com>

  PulseAudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as published
  by the Free Software Foundation; either version 2.1 of the License,
  or (at your option) any later version.

  PulseAudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU Lesser General Public License
  along with PulseAudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/core.h>
#include <pulsecore/sink-input.h>

#include "pulse-domain.h"

const char *pa_pulse_domain_node_type_to_string(pa_pulse_domain_node_type_t type) {
    switch (type) {
        case PA_PULSE_DOMAIN_NODE_TYPE_PORT: return "port";
        case PA_PULSE_DOMAIN_NODE_TYPE_SINK: return "sink";
        case PA_PULSE_DOMAIN_NODE_TYPE_SOURCE: return "source";
        case PA_PULSE_DOMAIN_NODE_TYPE_SINK_INPUT: return "sink-input";
        case PA_PULSE_DOMAIN_NODE_TYPE_SOURCE_OUTPUT: return "source-output";
    }

    pa_assert_not_reached();
}

static void clear_temporary_constraints_cb(pa_domain *domain) {
    pa_assert(domain);
}

static int allocate_sink_input_to_port(pa_sink_input *input, pa_device_port *port) {
    pa_assert(input);
    pa_assert(port);

    pa_log("Not implemented: allocate_sink_input_to_port()");
    return -1;
}

static int allocate_connection_cb(pa_domain *domain, pa_node *input, pa_node *output) {
    pa_pulse_domain_node_data *input_data;
    pa_pulse_domain_node_data *output_data;
    pa_pulse_domain_node_type_t input_type;
    pa_pulse_domain_node_type_t output_type;

    pa_assert(domain);
    pa_assert(input);
    pa_assert(output);

    pa_assert_se(input_data = pa_node_get_domain_data(input, domain));
    pa_assert_se(output_data = pa_node_get_domain_data(output, domain));
    input_type = input_data->type;
    output_type = output_data->type;

    if (input_type == PA_PULSE_DOMAIN_NODE_TYPE_SINK_INPUT && output_type == PA_PULSE_DOMAIN_NODE_TYPE_PORT)
        return allocate_sink_input_to_port((pa_sink_input *) input_data->owner, (pa_device_port *) output_data->owner);

    pa_log("Unsupported node types for connection %s (%s) -> %s (%s).",
           input->name, pa_pulse_domain_node_type_to_string(input_type),
           output->name, pa_pulse_domain_node_type_to_string(output_type));
    pa_assert_not_reached();
}

static int implement_sink_input_to_port(pa_sink_input *input, pa_device_port *port) {
    pa_assert(input);
    pa_assert(port);

    pa_assert(port->sink);
    pa_assert(port == port->sink->active_port);

    if (pa_sink_input_move_to(input, port->sink, false) < 0)
        return -1;

    return 0;
}

static int implement_connection_cb(pa_domain *domain, pa_node *input, pa_node *output) {
    pa_pulse_domain_node_data *input_data;
    pa_pulse_domain_node_data *output_data;
    pa_pulse_domain_node_type_t input_type;
    pa_pulse_domain_node_type_t output_type;

    pa_assert(domain);
    pa_assert(input);
    pa_assert(output);

    pa_assert_se(input_data = pa_node_get_domain_data(input, domain));
    pa_assert_se(output_data = pa_node_get_domain_data(output, domain));
    input_type = input_data->type;
    output_type = output_data->type;

    if (input_type == PA_PULSE_DOMAIN_NODE_TYPE_SINK_INPUT && output_type == PA_PULSE_DOMAIN_NODE_TYPE_PORT)
        return implement_sink_input_to_port((pa_sink_input *) input_data->owner, (pa_device_port *) output_data->owner);

    pa_log("Unsupported node types for connection %s (%s) -> %s (%s).",
           input->name, pa_pulse_domain_node_type_to_string(input_type),
           output->name, pa_pulse_domain_node_type_to_string(output_type));
    pa_assert_not_reached();
}

pa_pulse_domain *pa_pulse_domain_new(pa_core *core) {
    pa_pulse_domain *pulse_domain;
    pa_domain_new_data data;

    pa_assert(core);

    pulse_domain = pa_xnew0(pa_pulse_domain, 1);

    pa_domain_new_data_init(&data);
    pa_domain_new_data_set_name(&data, PA_PULSE_DOMAIN_NAME);
    pulse_domain->domain = pa_domain_new(core, &data);
    pa_domain_new_data_done(&data);

    if (!pulse_domain->domain) {
        pa_log("Failed to create the pulse domain.");
        goto fail;
    }

    pulse_domain->domain->clear_temporary_constraints = clear_temporary_constraints_cb;
    pulse_domain->domain->allocate_connection = allocate_connection_cb;
    pulse_domain->domain->implement_connection = implement_connection_cb;

    return pulse_domain;

fail:
    pa_pulse_domain_free(pulse_domain);

    return NULL;
}

void pa_pulse_domain_free(pa_pulse_domain *pulse_domain) {
    pa_assert(pulse_domain);

    if (pulse_domain->domain)
        pa_domain_free(pulse_domain->domain);

    pa_xfree(pulse_domain);
}

pa_pulse_domain_node_data *pa_pulse_domain_node_data_new(pa_pulse_domain_node_type_t type, void *owner) {
    pa_pulse_domain_node_data *data;

    pa_assert(owner);

    data = pa_xnew0(pa_pulse_domain_node_data, 1);
    data->type = type;
    data->owner = owner;

    return data;
}

void pa_pulse_domain_node_data_free(pa_pulse_domain_node_data *data) {
    pa_assert(data);

    pa_xfree(data);
}
