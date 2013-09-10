#ifndef foonullsinkhfoo
#define foonullsinkhfoo

/***
  This file is part of PulseAudio.

  Copyright 2013 Intel Corporation

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

typedef struct pa_null_sink pa_null_sink;
typedef struct pa_null_sink_new_data pa_null_sink_new_data;

#include <pulsecore/sink.h>

struct pa_null_sink_new_data {
    pa_module *module;
    char *name;
    pa_sample_spec sample_spec;
    bool sample_spec_is_set;
    pa_channel_map channel_map;
    bool channel_map_is_set;
    pa_proplist *proplist;
};

void pa_null_sink_new_data_init(pa_null_sink_new_data *data);
void pa_null_sink_new_data_set_name(pa_null_sink_new_data *data, const char *name);
void pa_null_sink_new_data_done(pa_null_sink_new_data *data);

pa_null_sink *pa_null_sink_new(pa_core *core, pa_null_sink_new_data *null_sink_data);
void pa_null_sink_free(pa_null_sink *null_sink);

pa_sink* pa_null_sink_get_sink(pa_null_sink *null_sink);

#endif
