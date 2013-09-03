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

#include "pulse-domain.h"

pa_domain *pa_pulse_domain_new(pa_core *core) {
    pa_domain_new_data data;
    pa_domain *dom;

    pa_assert(core);

    pa_domain_new_data_init(&data);
    data.name = PA_PULSE_DOMAIN_NAME;

    dom = pa_domain_new(core, &data);

    return dom;
}

void pa_pulse_domain_free(pa_domain *dom) {
    pa_assert(dom);
}
