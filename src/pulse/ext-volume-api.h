#ifndef fooextvolumeapihfoo
#define fooextvolumeapihfoo

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

#include <pulse/cdecl.h>
#include <pulse/context.h>
#include <pulse/volume.h>

/* This API is temporary, and has no stability guarantees whatsoever. Think
 * twice before making anything that relies on this API. This is undocumented
 * for a reason. */

PA_C_DECL_BEGIN

typedef struct pa_ext_volume_api_bvolume pa_ext_volume_api_bvolume;

struct pa_ext_volume_api_bvolume {
    pa_volume_t volume;
    double balance[PA_CHANNELS_MAX];
    pa_channel_map channel_map;
};

int pa_ext_volume_api_balance_valid(double balance) PA_GCC_CONST;
int pa_ext_volume_api_bvolume_valid(const pa_ext_volume_api_bvolume *volume, int check_volume, int check_balance)
        PA_GCC_PURE;
void pa_ext_volume_api_bvolume_init_invalid(pa_ext_volume_api_bvolume *volume);
void pa_ext_volume_api_bvolume_init_mono(pa_ext_volume_api_bvolume *bvolume, pa_volume_t volume);
int pa_ext_volume_api_bvolume_equal(const pa_ext_volume_api_bvolume *a, const pa_ext_volume_api_bvolume *b,
                                    int check_volume, int check_balance) PA_GCC_PURE;
void pa_ext_volume_api_bvolume_from_cvolume(pa_ext_volume_api_bvolume *bvolume, const pa_cvolume *cvolume,
                                            const pa_channel_map *map);
void pa_ext_volume_api_bvolume_to_cvolume(const pa_ext_volume_api_bvolume *bvolume, pa_cvolume *cvolume);
void pa_ext_volume_api_bvolume_copy_balance(pa_ext_volume_api_bvolume *to,
                                            const pa_ext_volume_api_bvolume *from);
void pa_ext_volume_api_bvolume_reset_balance(pa_ext_volume_api_bvolume *volume, const pa_channel_map *map);
void pa_ext_volume_api_bvolume_remap(pa_ext_volume_api_bvolume *volume, const pa_channel_map *to);
double pa_ext_volume_api_bvolume_get_left_right_balance(const pa_ext_volume_api_bvolume *volume) PA_GCC_PURE;
void pa_ext_volume_api_bvolume_set_left_right_balance(pa_ext_volume_api_bvolume *volume, double balance);
double pa_ext_volume_api_bvolume_get_rear_front_balance(const pa_ext_volume_api_bvolume *volume) PA_GCC_PURE;
void pa_ext_volume_api_bvolume_set_rear_front_balance(pa_ext_volume_api_bvolume *volume, double balance);

#define PA_EXT_VOLUME_API_BVOLUME_SNPRINT_BALANCE_MAX 500
char *pa_ext_volume_api_bvolume_snprint_balance(char *buf, size_t buf_size,
                                                const pa_ext_volume_api_bvolume *volume);

PA_C_DECL_END

#endif
