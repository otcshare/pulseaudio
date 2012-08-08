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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <math.h>

#include <pulsecore/sample-util.h>
#include <pulsecore/macro.h>
#include <pulsecore/g711.h>
#include <pulsecore/endianmacros.h>

#include "mix.h"

#define VOLUME_PADDING 32

static void calc_linear_integer_volume(int32_t linear[], const pa_cvolume *volume) {
    unsigned channel, nchannels, padding;

    pa_assert(linear);
    pa_assert(volume);

    nchannels = volume->channels;

    for (channel = 0; channel < nchannels; channel++)
        linear[channel] = (int32_t) lrint(pa_sw_volume_to_linear(volume->values[channel]) * 0x10000);

    for (padding = 0; padding < VOLUME_PADDING; padding++, channel++)
        linear[channel] = linear[padding];
}

static void calc_linear_float_volume(float linear[], const pa_cvolume *volume) {
    unsigned channel, nchannels, padding;

    pa_assert(linear);
    pa_assert(volume);

    nchannels = volume->channels;

    for (channel = 0; channel < nchannels; channel++)
        linear[channel] = (float) pa_sw_volume_to_linear(volume->values[channel]);

    for (padding = 0; padding < VOLUME_PADDING; padding++, channel++)
        linear[channel] = linear[padding];
}

static void calc_linear_integer_stream_volumes(pa_mix_info streams[], unsigned nstreams, const pa_cvolume *volume, const pa_sample_spec *spec) {
    unsigned k, channel;
    float linear[PA_CHANNELS_MAX + VOLUME_PADDING];

    pa_assert(streams);
    pa_assert(spec);
    pa_assert(volume);

    calc_linear_float_volume(linear, volume);

    for (k = 0; k < nstreams; k++) {

        for (channel = 0; channel < spec->channels; channel++) {
            pa_mix_info *m = streams + k;
            m->linear[channel].i = (int32_t) lrint(pa_sw_volume_to_linear(m->volume.values[channel]) * linear[channel] * 0x10000);
        }
    }
}

static void calc_linear_float_stream_volumes(pa_mix_info streams[], unsigned nstreams, const pa_cvolume *volume, const pa_sample_spec *spec) {
    unsigned k, channel;
    float linear[PA_CHANNELS_MAX + VOLUME_PADDING];

    pa_assert(streams);
    pa_assert(spec);
    pa_assert(volume);

    calc_linear_float_volume(linear, volume);

    for (k = 0; k < nstreams; k++) {

        for (channel = 0; channel < spec->channels; channel++) {
            pa_mix_info *m = streams + k;
            m->linear[channel].f = (float) (pa_sw_volume_to_linear(m->volume.values[channel]) * linear[channel]);
        }
    }
}

typedef void (*pa_calc_stream_volumes_func_t) (pa_mix_info streams[], unsigned nstreams, const pa_cvolume *volume, const pa_sample_spec *spec);

static const pa_calc_stream_volumes_func_t calc_stream_volumes_table[] = {
  [PA_SAMPLE_U8]        = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_ALAW]      = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_ULAW]      = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_S16LE]     = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_S16BE]     = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_FLOAT32LE] = (pa_calc_stream_volumes_func_t) calc_linear_float_stream_volumes,
  [PA_SAMPLE_FLOAT32BE] = (pa_calc_stream_volumes_func_t) calc_linear_float_stream_volumes,
  [PA_SAMPLE_S32LE]     = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_S32BE]     = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_S24LE]     = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_S24BE]     = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_S24_32LE]  = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes,
  [PA_SAMPLE_S24_32BE]  = (pa_calc_stream_volumes_func_t) calc_linear_integer_stream_volumes
};

/* special case: mix 2 s16ne streams, 1 channel each */
static void pa_mix2_ch1_s16ne(pa_mix_info streams[], int16_t *data, unsigned length) {
    const int16_t *ptr0 = streams[0].ptr;
    const int16_t *ptr1 = streams[1].ptr;

    const int32_t cv0 = streams[0].linear[0].i;
    const int32_t cv1 = streams[1].linear[0].i;

    length /= sizeof(int16_t);

    for (; length > 0; length--) {
        int32_t sum;

        sum = pa_mult_s16_volume(*ptr0++, cv0);
        sum += pa_mult_s16_volume(*ptr1++, cv1);

        sum = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
        *data++ = sum;
    }
}

/* special case: mix 2 s16ne streams, 2 channels each */
static void pa_mix2_ch2_s16ne(pa_mix_info streams[], int16_t *data, unsigned length) {
    const int16_t *ptr0 = streams[0].ptr;
    const int16_t *ptr1 = streams[1].ptr;

    length /= sizeof(int16_t) * 2;

    for (; length > 0; length--) {
        int32_t sum;

        sum = pa_mult_s16_volume(*ptr0++, streams[0].linear[0].i);
        sum += pa_mult_s16_volume(*ptr1++, streams[1].linear[0].i);

        sum = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
        *data++ = sum;

        sum = pa_mult_s16_volume(*ptr0++, streams[0].linear[1].i);
        sum += pa_mult_s16_volume(*ptr1++, streams[1].linear[1].i);

        sum = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
        *data++ = sum;
    }
}

/* special case: mix 2 s16ne streams */
static void pa_mix2_s16ne(pa_mix_info streams[], unsigned channels, int16_t *data, unsigned length) {
    const int16_t *ptr0 = streams[0].ptr;
    const int16_t *ptr1 = streams[1].ptr;
    unsigned channel = 0;

    length /= sizeof(int16_t);

    for (; length > 0; length--) {
        int32_t sum;

        sum = pa_mult_s16_volume(*ptr0++, streams[0].linear[channel].i);
        sum += pa_mult_s16_volume(*ptr1++, streams[1].linear[channel].i);

        sum = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
        *data++ = sum;

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

/* special case: mix s16ne streams, 2 channels each */
static void pa_mix_ch2_s16ne(pa_mix_info streams[], unsigned nstreams, int16_t *data, unsigned length) {

    length /= sizeof(int16_t) * 2;

    for (; length > 0; length--) {
        int32_t sum0 = 0, sum1 = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv0 = m->linear[0].i;
            int32_t cv1 = m->linear[1].i;

            sum0 += pa_mult_s16_volume(*((int16_t*) m->ptr), cv0);
            m->ptr = (uint8_t*) m->ptr + sizeof(int16_t);

            sum1 += pa_mult_s16_volume(*((int16_t*) m->ptr), cv1);
            m->ptr = (uint8_t*) m->ptr + sizeof(int16_t);
        }

        *data++ = PA_CLAMP_UNLIKELY(sum0, -0x8000, 0x7FFF);
        *data++ = PA_CLAMP_UNLIKELY(sum1, -0x8000, 0x7FFF);
    }
}

static void pa_mix_generic_s16ne(pa_mix_info streams[], unsigned nstreams, unsigned channels, int16_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(int16_t);

    for (; length > 0; length--) {
        int32_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;

            if (PA_LIKELY(cv > 0))
                sum += pa_mult_s16_volume(*((int16_t*) m->ptr), cv);
            m->ptr = (uint8_t*) m->ptr + sizeof(int16_t);
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
        *data++ = sum;

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_s16ne_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, int16_t *data, unsigned length) {
    if (nstreams == 2 && channels == 1)
        pa_mix2_ch1_s16ne(streams, data, length);
    else if (nstreams == 2 && channels == 2)
        pa_mix2_ch2_s16ne(streams, data, length);
    else if (nstreams == 2)
        pa_mix2_s16ne(streams, channels, data, length);
    else if (channels == 2)
        pa_mix_ch2_s16ne(streams, nstreams, data, length);
    else
        pa_mix_generic_s16ne(streams, nstreams, channels, data, length);
}

static void pa_mix_s16re_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, int16_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(int16_t);

    for (; length > 0; length--, data++) {
        int32_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;

            if (PA_LIKELY(cv > 0))
                sum += pa_mult_s16_volume(PA_INT16_SWAP(*((int16_t*) m->ptr)), cv);
            m->ptr = (uint8_t*) m->ptr + sizeof(int16_t);
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
        *data = PA_INT16_SWAP((int16_t) sum);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_s32ne_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, int32_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(int32_t);

    for (; length > 0; length--, data++) {
        int64_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;
            int64_t v;

            if (PA_LIKELY(cv > 0)) {
                v = *((int32_t*) m->ptr);
                v = (v * cv) >> 16;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + sizeof(int32_t);
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x80000000LL, 0x7FFFFFFFLL);
        *data = (int32_t) sum;

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_s32re_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, int32_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(int32_t);

    for (; length > 0; length--, data++) {
        int64_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;
            int64_t v;

            if (PA_LIKELY(cv > 0)) {
                v = PA_INT32_SWAP(*((int32_t*) m->ptr));
                v = (v * cv) >> 16;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + sizeof(int32_t);
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x80000000LL, 0x7FFFFFFFLL);
        *data = PA_INT32_SWAP((int32_t) sum);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_s24ne_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, uint8_t *data, unsigned length) {
    unsigned channel = 0;

    for (; length > 0; length -= 3, data += 3) {
        int64_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;
            int64_t v;

            if (PA_LIKELY(cv > 0)) {
                v = (int32_t) (PA_READ24NE(m->ptr) << 8);
                v = (v * cv) >> 16;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + 3;
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x80000000LL, 0x7FFFFFFFLL);
        PA_WRITE24NE(data, ((uint32_t) sum) >> 8);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_s24re_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, uint8_t *data, unsigned length) {
    unsigned channel = 0;

    for (; length > 0; length -= 3, data += 3) {
        int64_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;
            int64_t v;

            if (PA_LIKELY(cv > 0)) {
                v = (int32_t) (PA_READ24RE(m->ptr) << 8);
                v = (v * cv) >> 16;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + 3;
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x80000000LL, 0x7FFFFFFFLL);
        PA_WRITE24RE(data, ((uint32_t) sum) >> 8);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_s24_32ne_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, uint32_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(uint32_t);

    for (; length > 0; length--, data++) {
        int64_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;
            int64_t v;

            if (PA_LIKELY(cv > 0)) {
                v = (int32_t) (*((uint32_t*)m->ptr) << 8);
                v = (v * cv) >> 16;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + sizeof(int32_t);
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x80000000LL, 0x7FFFFFFFLL);
        *data = ((uint32_t) (int32_t) sum) >> 8;

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_s24_32re_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, uint32_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(uint32_t);

    for (; length > 0; length--, data++) {
        int64_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;
            int64_t v;

            if (PA_LIKELY(cv > 0)) {
                v = (int32_t) (PA_UINT32_SWAP(*((uint32_t*) m->ptr)) << 8);
                v = (v * cv) >> 16;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + 3;
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x80000000LL, 0x7FFFFFFFLL);
        *data = PA_INT32_SWAP(((uint32_t) (int32_t) sum) >> 8);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_u8_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, uint8_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(uint8_t);

    for (; length > 0; length--, data++) {
        int32_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t v, cv = m->linear[channel].i;

            if (PA_LIKELY(cv > 0)) {
                v = (int32_t) *((uint8_t*) m->ptr) - 0x80;
                v = (v * cv) >> 16;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + 1;
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x80, 0x7F);
        *data = (uint8_t) (sum + 0x80);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_ulaw_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, uint8_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(uint8_t);

    for (; length > 0; length--, data++) {
        int32_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;

            if (PA_LIKELY(cv > 0))
                sum += pa_mult_s16_volume(st_ulaw2linear16(*((uint8_t*) m->ptr)), cv);
            m->ptr = (uint8_t*) m->ptr + 1;
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
        *data = (uint8_t) st_14linear2ulaw((int16_t) sum >> 2);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_alaw_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, uint8_t *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(uint8_t);

    for (; length > 0; length--, data++) {
        int32_t sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            int32_t cv = m->linear[channel].i;

            if (PA_LIKELY(cv > 0))
                sum += pa_mult_s16_volume(st_alaw2linear16(*((uint8_t*) m->ptr)), cv);
            m->ptr = (uint8_t*) m->ptr + 1;
        }

        sum = PA_CLAMP_UNLIKELY(sum, -0x8000, 0x7FFF);
        *data = (uint8_t) st_13linear2alaw((int16_t) sum >> 3);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_float32ne_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, float *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(float);

    for (; length > 0; length--, data++) {
        float sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            float v, cv = m->linear[channel].f;

            if (PA_LIKELY(cv > 0)) {
                v = *((float*) m->ptr);
                v *= cv;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + sizeof(float);
        }

        *data = sum;

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static void pa_mix_float32re_c(pa_mix_info streams[], unsigned nstreams, unsigned channels, float *data, unsigned length) {
    unsigned channel = 0;

    length /= sizeof(float);

    for (; length > 0; length--, data++) {
        float sum = 0;
        unsigned i;

        for (i = 0; i < nstreams; i++) {
            pa_mix_info *m = streams + i;
            float v, cv = m->linear[channel].f;

            if (PA_LIKELY(cv > 0)) {
                v = PA_FLOAT32_SWAP(*(float*) m->ptr);
                v *= cv;
                sum += v;
            }
            m->ptr = (uint8_t*) m->ptr + sizeof(float);
        }

        *data = PA_FLOAT32_SWAP(sum);

        if (PA_UNLIKELY(++channel >= channels))
            channel = 0;
    }
}

static pa_do_mix_func_t do_mix_table[] = {
    [PA_SAMPLE_U8]        = (pa_do_mix_func_t) pa_mix_u8_c,
    [PA_SAMPLE_ALAW]      = (pa_do_mix_func_t) pa_mix_alaw_c,
    [PA_SAMPLE_ULAW]      = (pa_do_mix_func_t) pa_mix_ulaw_c,
    [PA_SAMPLE_S16NE]     = (pa_do_mix_func_t) pa_mix_s16ne_c,
    [PA_SAMPLE_S16RE]     = (pa_do_mix_func_t) pa_mix_s16re_c,
    [PA_SAMPLE_FLOAT32NE] = (pa_do_mix_func_t) pa_mix_float32ne_c,
    [PA_SAMPLE_FLOAT32RE] = (pa_do_mix_func_t) pa_mix_float32re_c,
    [PA_SAMPLE_S32NE]     = (pa_do_mix_func_t) pa_mix_s32ne_c,
    [PA_SAMPLE_S32RE]     = (pa_do_mix_func_t) pa_mix_s32re_c,
    [PA_SAMPLE_S24NE]     = (pa_do_mix_func_t) pa_mix_s24ne_c,
    [PA_SAMPLE_S24RE]     = (pa_do_mix_func_t) pa_mix_s24re_c,
    [PA_SAMPLE_S24_32NE]  = (pa_do_mix_func_t) pa_mix_s24_32ne_c,
    [PA_SAMPLE_S24_32RE]  = (pa_do_mix_func_t) pa_mix_s24_32re_c
};

size_t pa_mix(
        pa_mix_info streams[],
        unsigned nstreams,
        void *data,
        size_t length,
        const pa_sample_spec *spec,
        const pa_cvolume *volume,
        bool mute) {

    pa_cvolume full_volume;
    unsigned k;

    pa_assert(streams);
    pa_assert(data);
    pa_assert(length);
    pa_assert(spec);

    if (!volume)
        volume = pa_cvolume_reset(&full_volume, spec->channels);

    if (mute || pa_cvolume_is_muted(volume) || nstreams <= 0) {
        pa_silence_memory(data, length, spec);
        return length;
    }

    for (k = 0; k < nstreams; k++) {
        streams[k].ptr = pa_memblock_acquire_chunk(&streams[k].chunk);
        if (length > streams[k].chunk.length)
            length = streams[k].chunk.length;
    }

    calc_stream_volumes_table[spec->format](streams, nstreams, volume, spec);
    do_mix_table[spec->format](streams, nstreams, spec->channels, data, length);

    for (k = 0; k < nstreams; k++)
        pa_memblock_release(streams[k].chunk.memblock);

    return length;
}

pa_do_mix_func_t pa_get_mix_func(pa_sample_format_t f) {
    pa_assert(pa_sample_format_valid(f));

    return do_mix_table[f];
}

void pa_set_mix_func(pa_sample_format_t f, pa_do_mix_func_t func) {
    pa_assert(pa_sample_format_valid(f));

    do_mix_table[f] = func;
}

typedef union {
  float f;
  uint32_t i;
} volume_val;

typedef void (*pa_calc_volume_func_t) (void *volumes, const pa_cvolume *volume);

static const pa_calc_volume_func_t calc_volume_table[] = {
  [PA_SAMPLE_U8]        = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_ALAW]      = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_ULAW]      = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_S16LE]     = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_S16BE]     = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_FLOAT32LE] = (pa_calc_volume_func_t) calc_linear_float_volume,
  [PA_SAMPLE_FLOAT32BE] = (pa_calc_volume_func_t) calc_linear_float_volume,
  [PA_SAMPLE_S32LE]     = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_S32BE]     = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_S24LE]     = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_S24BE]     = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_S24_32LE]  = (pa_calc_volume_func_t) calc_linear_integer_volume,
  [PA_SAMPLE_S24_32BE]  = (pa_calc_volume_func_t) calc_linear_integer_volume
};

void pa_volume_memchunk(
        pa_memchunk*c,
        const pa_sample_spec *spec,
        const pa_cvolume *volume) {

    void *ptr;
    volume_val linear[PA_CHANNELS_MAX + VOLUME_PADDING];
    pa_do_volume_func_t do_volume;

    pa_assert(c);
    pa_assert(spec);
    pa_assert(pa_sample_spec_valid(spec));
    pa_assert(pa_frame_aligned(c->length, spec));
    pa_assert(volume);

    if (pa_memblock_is_silence(c->memblock))
        return;

    if (pa_cvolume_channels_equal_to(volume, PA_VOLUME_NORM))
        return;

    if (pa_cvolume_channels_equal_to(volume, PA_VOLUME_MUTED)) {
        pa_silence_memchunk(c, spec);
        return;
    }

    do_volume = pa_get_volume_func(spec->format);
    pa_assert(do_volume);

    calc_volume_table[spec->format] ((void *)linear, volume);

    ptr = pa_memblock_acquire_chunk(c);

    do_volume(ptr, (void *)linear, spec->channels, c->length);

    pa_memblock_release(c->memblock);
}

static void calc_linear_integer_volume_no_mapping(int32_t linear[], float volume[], unsigned nchannels) {
    unsigned channel, padding;

    pa_assert(linear);
    pa_assert(volume);

    for (channel = 0; channel < nchannels; channel++)
        linear[channel] = (int32_t) lrint(volume[channel] * 0x10000U);

    for (padding = 0; padding < VOLUME_PADDING; padding++, channel++)
        linear[channel] = linear[padding];
}

static void calc_linear_float_volume_no_mapping(float linear[], float volume[], unsigned nchannels) {
    unsigned channel, padding;

    pa_assert(linear);
    pa_assert(volume);

    for (channel = 0; channel < nchannels; channel++)
        linear[channel] = volume[channel];

    for (padding = 0; padding < VOLUME_PADDING; padding++, channel++)
        linear[channel] = linear[padding];
}

typedef void (*pa_calc_volume_no_mapping_func_t) (void *volumes, float *volume, int channels);

static const pa_calc_volume_no_mapping_func_t calc_volume_table_no_mapping[] = {
  [PA_SAMPLE_U8]        = (pa_calc_volume_no_mapping_func_t) calc_linear_integer_volume_no_mapping,
  [PA_SAMPLE_ALAW]      = (pa_calc_volume_no_mapping_func_t) calc_linear_integer_volume_no_mapping,
  [PA_SAMPLE_ULAW]      = (pa_calc_volume_no_mapping_func_t) calc_linear_integer_volume_no_mapping,
  [PA_SAMPLE_S16LE]     = (pa_calc_volume_no_mapping_func_t) calc_linear_integer_volume_no_mapping,
  [PA_SAMPLE_S16BE]     = (pa_calc_volume_no_mapping_func_t) calc_linear_integer_volume_no_mapping,
  [PA_SAMPLE_FLOAT32LE] = (pa_calc_volume_no_mapping_func_t) calc_linear_float_volume_no_mapping,
  [PA_SAMPLE_FLOAT32BE] = (pa_calc_volume_no_mapping_func_t) calc_linear_float_volume_no_mapping,
  [PA_SAMPLE_S32LE]     = (pa_calc_volume_no_mapping_func_t) calc_linear_integer_volume_no_mapping,
  [PA_SAMPLE_S32BE]     = (pa_calc_volume_no_mapping_func_t) calc_linear_integer_volume_no_mapping,
  [PA_SAMPLE_S24LE]     = (pa_calc_volume_no_mapping_func_t) calc_linear_integer_volume_no_mapping,
  [PA_SAMPLE_S24BE]     = (pa_calc_volume_no_mapping_func_t) calc_linear_integer_volume_no_mapping,
  [PA_SAMPLE_S24_32LE]  = (pa_calc_volume_no_mapping_func_t) calc_linear_integer_volume_no_mapping,
  [PA_SAMPLE_S24_32BE]  = (pa_calc_volume_no_mapping_func_t) calc_linear_integer_volume_no_mapping
};

static const unsigned format_sample_size_table[] = {
  [PA_SAMPLE_U8]        = 1,
  [PA_SAMPLE_ALAW]      = 1,
  [PA_SAMPLE_ULAW]      = 1,
  [PA_SAMPLE_S16LE]     = 2,
  [PA_SAMPLE_S16BE]     = 2,
  [PA_SAMPLE_FLOAT32LE] = 4,
  [PA_SAMPLE_FLOAT32BE] = 4,
  [PA_SAMPLE_S32LE]     = 4,
  [PA_SAMPLE_S32BE]     = 4,
  [PA_SAMPLE_S24LE]     = 3,
  [PA_SAMPLE_S24BE]     = 3,
  [PA_SAMPLE_S24_32LE]  = 4,
  [PA_SAMPLE_S24_32BE]  = 4
};

static float calc_volume_ramp_linear(pa_volume_ramp_int_t *ramp) {
    pa_assert(ramp);
    pa_assert(ramp->length > 0);

    /* basic linear interpolation */
    return ramp->start + (ramp->length - ramp->left) * (ramp->end - ramp->start) / (float) ramp->length;
}

static float calc_volume_ramp_logarithmic(pa_volume_ramp_int_t *ramp) {
    float x_val, s, e;
    long temp;

    pa_assert(ramp);
    pa_assert(ramp->length > 0);

    if (ramp->end > ramp->start) {
        temp = ramp->left;
        s = ramp->end;
        e = ramp->start;
    } else {
        temp = ramp->length - ramp->left;
        s = ramp->start;
        e = ramp->end;
    }

    x_val = temp == 0 ? 0.0 : powf(temp, 10);

    /* base 10 logarithmic interpolation */
    return s + x_val * (e - s) / powf(ramp->length, 10);
}

static float calc_volume_ramp_cubic(pa_volume_ramp_int_t *ramp) {
    float x_val, s, e;
    long temp;

    pa_assert(ramp);
    pa_assert(ramp->length > 0);

    if (ramp->end > ramp->start) {
        temp = ramp->left;
        s = ramp->end;
        e = ramp->start;
    } else {
        temp = ramp->length - ramp->left;
        s = ramp->start;
        e = ramp->end;
    }

    x_val = temp == 0 ? 0.0 : cbrtf(temp);

    /* cubic interpolation */
    return s + x_val * (e - s) / cbrtf(ramp->length);
}

typedef float (*pa_calc_volume_ramp_func_t) (pa_volume_ramp_int_t *);

static const pa_calc_volume_ramp_func_t calc_volume_ramp_table[] = {
    [PA_VOLUME_RAMP_TYPE_LINEAR] = (pa_calc_volume_ramp_func_t) calc_volume_ramp_linear,
    [PA_VOLUME_RAMP_TYPE_LOGARITHMIC] = (pa_calc_volume_ramp_func_t) calc_volume_ramp_logarithmic,
    [PA_VOLUME_RAMP_TYPE_CUBIC] = (pa_calc_volume_ramp_func_t) calc_volume_ramp_cubic
};

static void calc_volume_ramps(pa_cvolume_ramp_int *ram, float *vol)
{
    int i;

    for (i = 0; i < ram->channels; i++) {
        if (ram->ramps[i].left <= 0) {
            if (ram->ramps[i].target == PA_VOLUME_NORM) {
                vol[i] = 1.0;
            }
        } else {
            vol[i] = ram->ramps[i].curr = calc_volume_ramp_table[ram->ramps[i].type] (&ram->ramps[i]);
            ram->ramps[i].left--;
        }
    }
}

void pa_volume_ramp_memchunk(
        pa_memchunk *c,
        const pa_sample_spec *spec,
        pa_cvolume_ramp_int *ramp) {

    void *ptr;
    volume_val linear[PA_CHANNELS_MAX + VOLUME_PADDING];
    float vol[PA_CHANNELS_MAX + VOLUME_PADDING];
    pa_do_volume_func_t do_volume;
    long length_in_frames;
    int i;

    pa_assert(c);
    pa_assert(spec);
    pa_assert(pa_frame_aligned(c->length, spec));
    pa_assert(ramp);

    length_in_frames = c->length / format_sample_size_table[spec->format] / spec->channels;

    if (pa_memblock_is_silence(c->memblock)) {
        for (i = 0; i < ramp->channels; i++) {
            if (ramp->ramps[i].length > 0)
                ramp->ramps[i].length -= length_in_frames;
        }
        return;
    }

    if (spec->format < 0 || spec->format >= PA_SAMPLE_MAX) {
      pa_log_warn("Unable to change volume of format");
      return;
    }

    do_volume = pa_get_volume_func(spec->format);
    pa_assert(do_volume);

    ptr = (uint8_t*) pa_memblock_acquire(c->memblock) + c->index;

    for (i = 0; i < length_in_frames; i++) {
        calc_volume_ramps(ramp, vol);
        calc_volume_table_no_mapping[spec->format] ((void *)linear, vol, spec->channels);

        /* we only process one frame per iteration */
        do_volume (ptr, (void *)linear, spec->channels, format_sample_size_table[spec->format] * spec->channels);

        /* pa_log_debug("1: %d  2: %d", linear[0], linear[1]); */

        ptr = (uint8_t*)ptr + format_sample_size_table[spec->format] * spec->channels;
    }

    pa_memblock_release(c->memblock);
}

pa_cvolume_ramp_int* pa_cvolume_ramp_convert(const pa_cvolume_ramp *src, pa_cvolume_ramp_int *dst, int sample_rate) {

    int i, j, channels, remaining_channels;
    float temp;

    if (dst->channels < src->channels) {
        channels = dst->channels;
        remaining_channels = 0;
    }
    else {
        channels = src->channels;
        remaining_channels = dst->channels;
    }

    for (i = 0; i < channels; i++) {
        dst->ramps[i].type = src->ramps[i].type;
        /* ms to samples */
        dst->ramps[i].length = src->ramps[i].length * sample_rate / 1000;
        dst->ramps[i].left = dst->ramps[i].length;
        dst->ramps[i].start = dst->ramps[i].end;
        dst->ramps[i].target = src->ramps[i].target;
        /* scale to pulse internal mapping so that when ramp is over there's no glitch in volume */
        temp = src->ramps[i].target / (float)0x10000U;
        dst->ramps[i].end = temp * temp * temp;
    }

    j = i;

    for (i--; j < remaining_channels; j++) {
        dst->ramps[j].type = dst->ramps[i].type;
        dst->ramps[j].length = dst->ramps[i].length;
        dst->ramps[j].left = dst->ramps[i].left;
        dst->ramps[j].start = dst->ramps[i].start;
        dst->ramps[j].target = dst->ramps[i].target;
        dst->ramps[j].end = dst->ramps[i].end;
    }

    return dst;
}

bool pa_cvolume_ramp_active(pa_cvolume_ramp_int *ramp) {
    int i;

    for (i = 0; i < ramp->channels; i++) {
        if (ramp->ramps[i].left > 0)
            return true;
    }

    return false;
}

bool pa_cvolume_ramp_target_active(pa_cvolume_ramp_int *ramp) {
    int i;

    for (i = 0; i < ramp->channels; i++) {
        if (ramp->ramps[i].target != PA_VOLUME_NORM)
            return true;
    }

    return false;
}

pa_cvolume * pa_cvolume_ramp_get_targets(pa_cvolume_ramp_int *ramp, pa_cvolume *volume) {
    int i = 0;

    volume->channels = ramp->channels;

    for (i = 0; i < ramp->channels; i++)
        volume->values[i] = ramp->ramps[i].target;

    return volume;
}

pa_cvolume_ramp_int* pa_cvolume_ramp_start_from(pa_cvolume_ramp_int *src, pa_cvolume_ramp_int *dst) {
    int i;

    for (i = 0; i < src->channels; i++) {
        /* if new vols are invalid, copy old ramp i.e. no effect */
        if (dst->ramps[i].target == PA_VOLUME_INVALID)
            dst->ramps[i] = src->ramps[i];
        /* if there's some old ramp still left */
        else if (src->ramps[i].left > 0)
            dst->ramps[i].start = src->ramps[i].curr;
    }

    return dst;
}

pa_cvolume_ramp_int* pa_cvolume_ramp_int_init(pa_cvolume_ramp_int *src, pa_volume_t vol, int channels) {
    int i;
    float temp;

    src->channels = channels;

    for (i = 0; i < channels; i++) {
        src->ramps[i].type = PA_VOLUME_RAMP_TYPE_LINEAR;
        src->ramps[i].length = 0;
        src->ramps[i].left = 0;
        if (vol == PA_VOLUME_NORM) {
            src->ramps[i].start = 1.0;
            src->ramps[i].end = 1.0;
            src->ramps[i].curr = 1.0;
        }
        else if (vol == PA_VOLUME_MUTED) {
            src->ramps[i].start = 0.0;
            src->ramps[i].end = 0.0;
            src->ramps[i].curr = 0.0;
        }
        else {
            temp = vol / (float)0x10000U;
            src->ramps[i].start = temp * temp * temp;
            src->ramps[i].end = src->ramps[i].start;
            src->ramps[i].curr = src->ramps[i].start;
        }
        src->ramps[i].target = vol;
    }

    return src;
}
