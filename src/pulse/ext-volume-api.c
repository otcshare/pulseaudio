/***
  This file is part of PulseAudio.

  Copyright 2014 Intel Corporation

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

#include "ext-volume-api.h"

#include <pulsecore/core-util.h>
#include <pulsecore/i18n.h>
#include <pulsecore/macro.h>

#include <math.h>

int pa_ext_volume_api_balance_valid(double balance) {
    return balance >= 0.0 && balance <= 1.0;
}

int pa_ext_volume_api_bvolume_valid(const pa_ext_volume_api_bvolume *volume, int check_volume, int check_balance) {
    unsigned channel;

    pa_assert(volume);

    if (check_volume && !PA_VOLUME_IS_VALID(volume->volume))
        return 0;

    if (!check_balance)
        return 1;

    if (!pa_channel_map_valid(&volume->channel_map))
        return 0;

    for (channel = 0; channel < volume->channel_map.channels; channel++) {
        if (!pa_ext_volume_api_balance_valid(volume->balance[channel]))
            return 0;
    }

    return 1;
}

void pa_ext_volume_api_bvolume_init_invalid(pa_ext_volume_api_bvolume *volume) {
    unsigned i;

    pa_assert(volume);

    volume->volume = PA_VOLUME_INVALID;

    for (i = 0; i < PA_CHANNELS_MAX; i++)
        volume->balance[i] = -1.0;

    pa_channel_map_init(&volume->channel_map);
}

void pa_ext_volume_api_bvolume_init_mono(pa_ext_volume_api_bvolume *bvolume, pa_volume_t volume) {
    pa_assert(bvolume);
    pa_assert(PA_VOLUME_IS_VALID(volume));

    bvolume->volume = volume;
    bvolume->balance[0] = 1.0;
    pa_channel_map_init_mono(&bvolume->channel_map);
}

int pa_ext_volume_api_bvolume_equal(const pa_ext_volume_api_bvolume *a, const pa_ext_volume_api_bvolume *b,
                                    int check_volume, int check_balance) {
    unsigned i;

    pa_assert(a);
    pa_assert(b);

    if (check_volume && a->volume != b->volume)
        return 0;

    if (!check_balance)
        return 1;

    if (!pa_channel_map_equal(&a->channel_map, &b->channel_map))
        return 0;

    for (i = 0; i < a->channel_map.channels; i++) {
        if (fabs(a->balance[i] - b->balance[i]) > 0.00001)
            return 0;
    }

    return 1;
}

void pa_ext_volume_api_bvolume_from_cvolume(pa_ext_volume_api_bvolume *bvolume, const pa_cvolume *cvolume,
                                            const pa_channel_map *map) {
    unsigned i;

    pa_assert(bvolume);
    pa_assert(cvolume);
    pa_assert(map);
    pa_assert(cvolume->channels == map->channels);

    bvolume->volume = pa_cvolume_max(cvolume);
    bvolume->channel_map = *map;

    for (i = 0; i < map->channels; i++) {
        if (bvolume->volume != PA_VOLUME_MUTED)
            bvolume->balance[i] = ((double) cvolume->values[i]) / ((double) bvolume->volume);
        else
            bvolume->balance[i] = 1.0;
    }
}

void pa_ext_volume_api_bvolume_to_cvolume(const pa_ext_volume_api_bvolume *bvolume, pa_cvolume *cvolume) {
    unsigned i;

    pa_assert(bvolume);
    pa_assert(cvolume);
    pa_assert(pa_ext_volume_api_bvolume_valid(bvolume, true, true));

    cvolume->channels = bvolume->channel_map.channels;

    for (i = 0; i < bvolume->channel_map.channels; i++)
        cvolume->values[i] = bvolume->volume * bvolume->balance[i];
}

void pa_ext_volume_api_bvolume_copy_balance(pa_ext_volume_api_bvolume *to,
                                            const pa_ext_volume_api_bvolume *from) {
    pa_assert(to);
    pa_assert(from);

    memcpy(to->balance, from->balance, sizeof(from->balance));
    to->channel_map = from->channel_map;
}

void pa_ext_volume_api_bvolume_reset_balance(pa_ext_volume_api_bvolume *volume, const pa_channel_map *map) {
    unsigned i;

    pa_assert(volume);
    pa_assert(map);
    pa_assert(pa_channel_map_valid(map));

    for (i = 0; i < map->channels; i++)
        volume->balance[i] = 1.0;

    volume->channel_map = *map;
}

void pa_ext_volume_api_bvolume_remap(pa_ext_volume_api_bvolume *volume, const pa_channel_map *to) {
    unsigned i;
    pa_cvolume cvolume;

    pa_assert(volume);
    pa_assert(to);
    pa_assert(pa_ext_volume_api_bvolume_valid(volume, false, true));
    pa_assert(pa_channel_map_valid(to));

    cvolume.channels = volume->channel_map.channels;

    for (i = 0; i < cvolume.channels; i++)
        cvolume.values[i] = volume->balance[i] * (double) PA_VOLUME_NORM;

    pa_cvolume_remap(&cvolume, &volume->channel_map, to);

    for (i = 0; i < to->channels; i++)
        volume->balance[i] = (double) cvolume.values[i] / (double) PA_VOLUME_NORM;

    volume->channel_map = *to;
}

double pa_ext_volume_api_bvolume_get_left_right_balance(const pa_ext_volume_api_bvolume *volume) {
    pa_ext_volume_api_bvolume bvolume;
    pa_cvolume cvolume;
    double ret;

    pa_assert(volume);

    bvolume.volume = PA_VOLUME_NORM;
    pa_ext_volume_api_bvolume_copy_balance(&bvolume, volume);
    pa_ext_volume_api_bvolume_to_cvolume(&bvolume, &cvolume);
    ret = pa_cvolume_get_balance(&cvolume, &volume->channel_map);

    return ret;
}

void pa_ext_volume_api_bvolume_set_left_right_balance(pa_ext_volume_api_bvolume *volume, double balance) {
    pa_cvolume cvolume;
    pa_volume_t old_volume;

    pa_assert(volume);

    if (!pa_channel_map_can_balance(&volume->channel_map))
        return;

    pa_cvolume_reset(&cvolume, volume->channel_map.channels);
    pa_cvolume_set_balance(&cvolume, &volume->channel_map, balance);
    old_volume = volume->volume;
    pa_ext_volume_api_bvolume_from_cvolume(volume, &cvolume, &volume->channel_map);
    volume->volume = old_volume;
}

double pa_ext_volume_api_bvolume_get_rear_front_balance(const pa_ext_volume_api_bvolume *volume) {
    pa_ext_volume_api_bvolume bvolume;
    pa_cvolume cvolume;
    double ret;

    pa_assert(volume);

    bvolume.volume = PA_VOLUME_NORM;
    pa_ext_volume_api_bvolume_copy_balance(&bvolume, volume);
    pa_ext_volume_api_bvolume_to_cvolume(&bvolume, &cvolume);
    ret = pa_cvolume_get_fade(&cvolume, &volume->channel_map);

    return ret;
}

void pa_ext_volume_api_bvolume_set_rear_front_balance(pa_ext_volume_api_bvolume *volume, double balance) {
    pa_cvolume cvolume;
    pa_volume_t old_volume;

    pa_assert(volume);

    if (!pa_channel_map_can_fade(&volume->channel_map))
        return;

    pa_cvolume_reset(&cvolume, volume->channel_map.channels);
    pa_cvolume_set_fade(&cvolume, &volume->channel_map, balance);
    old_volume = volume->volume;
    pa_ext_volume_api_bvolume_from_cvolume(volume, &cvolume, &volume->channel_map);
    volume->volume = old_volume;
}

char *pa_ext_volume_api_bvolume_snprint_balance(char *buf, size_t buf_len,
                                                const pa_ext_volume_api_bvolume *volume) {
    char *e;
    unsigned channel;
    bool first = true;

    pa_assert(buf);
    pa_assert(buf_len > 0);
    pa_assert(volume);

    pa_init_i18n();

    if (!pa_ext_volume_api_bvolume_valid(volume, true, true)) {
        pa_snprintf(buf, buf_len, _("(invalid)"));
        return buf;
    }

    *(e = buf) = 0;

    for (channel = 0; channel < volume->channel_map.channels && buf_len > 1; channel++) {
        buf_len -= pa_snprintf(e, buf_len, "%s%s: %u%%",
                               first ? "" : ", ",
                               pa_channel_position_to_string(volume->channel_map.map[channel]),
                               (unsigned) (volume->balance[channel] * 100 + 0.5));

        e = strchr(e, 0);
        first = false;
    }

    return buf;
}
