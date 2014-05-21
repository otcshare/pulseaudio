#ifndef foobvolumehfoo
#define foobvolumehfoo

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

#include <pulse/ext-volume-api.h>

typedef pa_ext_volume_api_bvolume pa_bvolume;

#define pa_balance_valid pa_ext_volume_api_balance_valid
#define pa_bvolume_valid pa_ext_volume_api_bvolume_valid
#define pa_bvolume_init_invalid pa_ext_volume_api_bvolume_init_invalid
#define pa_bvolume_init pa_ext_volume_api_bvolume_init
#define pa_bvolume_init_mono pa_ext_volume_api_bvolume_init_mono
#define pa_bvolume_parse_balance pa_ext_volume_api_bvolume_parse_balance
#define pa_bvolume_equal pa_ext_volume_api_bvolume_equal
#define pa_bvolume_from_cvolume pa_ext_volume_api_bvolume_from_cvolume
#define pa_bvolume_to_cvolume pa_ext_volume_api_bvolume_to_cvolume
#define pa_bvolume_copy_balance pa_ext_volume_api_bvolume_copy_balance
#define pa_bvolume_reset_balance pa_ext_volume_api_bvolume_reset_balance
#define pa_bvolume_remap pa_ext_volume_api_bvolume_remap
#define pa_bvolume_balance_to_string pa_ext_volume_api_bvolume_balance_to_string

#define PA_BVOLUME_SNPRINT_BALANCE_MAX PA_EXT_VOLUME_API_BVOLUME_SNPRINT_BALANCE_MAX
#define pa_bvolume_snprint_balance pa_ext_volume_api_bvolume_snprint_balance

#endif
