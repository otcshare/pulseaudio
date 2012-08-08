#ifndef foomixhfoo
#define foomixhfoo

/***
  This file is part of PulseAudio.

  Copyright 2004-2006 Lennart Poettering
  Copyright 2006 Pierre Ossman <ossman@cendio.se> for Cendio AB
  Copyright 2013 Peter Meerwald <pmeerw@pmeerw.net>

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

#include <pulse/sample.h>
#include <pulse/volume.h>
#include <pulsecore/memchunk.h>

typedef struct pa_mix_info {
    pa_memchunk chunk;
    pa_cvolume volume;
    void *userdata;

    /* The following fields are used internally by pa_mix(), should
     * not be initialised by the caller of pa_mix(). */
    void *ptr;
    union {
        int32_t i;
        float f;
    } linear[PA_CHANNELS_MAX];
} pa_mix_info;

size_t pa_mix(
    pa_mix_info channels[],
    unsigned nchannels,
    void *data,
    size_t length,
    const pa_sample_spec *spec,
    const pa_cvolume *volume,
    bool mute);

typedef void (*pa_do_mix_func_t) (pa_mix_info streams[], unsigned nstreams, unsigned channels, void *data, unsigned length);

pa_do_mix_func_t pa_get_mix_func(pa_sample_format_t f);
void pa_set_mix_func(pa_sample_format_t f, pa_do_mix_func_t func);

void pa_volume_memchunk(
    pa_memchunk*c,
    const pa_sample_spec *spec,
    const pa_cvolume *volume);

typedef struct pa_volume_ramp_int_t {
    pa_volume_ramp_type_t type;
    long length;
    long left;
    float start;
    float end;
    float curr;
    pa_volume_t target;
} pa_volume_ramp_int_t;

typedef struct pa_cvolume_ramp_int {
    uint8_t channels;
    pa_volume_ramp_int_t ramps[PA_CHANNELS_MAX];
} pa_cvolume_ramp_int;

pa_cvolume_ramp_int* pa_cvolume_ramp_convert(const pa_cvolume_ramp *src, pa_cvolume_ramp_int *dst, int sample_rate);
bool pa_cvolume_ramp_active(pa_cvolume_ramp_int *ramp);
bool pa_cvolume_ramp_target_active(pa_cvolume_ramp_int *ramp);
pa_cvolume_ramp_int* pa_cvolume_ramp_start_from(pa_cvolume_ramp_int *src, pa_cvolume_ramp_int *dst);
pa_cvolume_ramp_int* pa_cvolume_ramp_int_init(pa_cvolume_ramp_int *src, pa_volume_t vol, int channels);
pa_cvolume * pa_cvolume_ramp_get_targets(pa_cvolume_ramp_int *ramp, pa_cvolume *volume);

void pa_volume_ramp_memchunk(
        pa_memchunk *c,
        const pa_sample_spec *spec,
        pa_cvolume_ramp_int *ramp);

#endif
