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
#ifndef foomurphyivisymdeffoo
#define foomurphyivisymdeffoo

#include <pulsecore/core.h>
#include <pulsecore/module.h>
#include <pulsecore/macro.h>

#define pa__init module_murphy_ivi_LTX_pa__init
#define pa__done module_murphy_ivi_LTX_pa__done
#define pa__get_author module_murphy_ivi_LTX_pa__get_author
#define pa__get_description module_murphy_ivi_LTX_pa__get_description
#define pa__get_usage module_murphy_ivi_LTX_pa__get_usage
#define pa__get_version module_murphy_ivi_LTX_pa__get_version
#define pa__load_once module_murphy_ivi_LTX_pa__load_once

int pa__init(pa_module *m);
void pa__done(pa_module *m);

const char* pa__get_author(void);
const char* pa__get_description(void);
const char* pa__get_usage(void);
const char* pa__get_version(void);
bool pa__load_once(void);

#endif
