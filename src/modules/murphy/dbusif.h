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
#ifndef foodbusiffoo
#define foodbusiffoo

#include "routerif.h"

/*
 * audiomanager router methods
 */
#define AUDIOMGR_REGISTER_DOMAIN      "registerDomain"
#define AUDIOMGR_DOMAIN_COMPLETE      "hookDomainRegistrationComplete"
#define AUDIOMGR_DEREGISTER_DOMAIN    "deregisterDomain"

#define AUDIOMGR_REGISTER_SOURCE      "registerSource"
#define AUDIOMGR_DEREGISTER_SOURCE    "deregisterSource"

#define AUDIOMGR_REGISTER_SINK        "registerSink"
#define AUDIOMGR_DEREGISTER_SINK      "deregisterSink"

#define AUDIOMGR_CONNECT              "asyncConnect"
#define AUDIOMGR_CONNECT_ACK          "ackConnect"

#define AUDIOMGR_DISCONNECT           "asyncDisconnect"
#define AUDIOMGR_DISCONNECT_ACK       "ackDisconnect"

#define AUDIOMGR_SETSINKVOL_ACK       "ackSetSinkVolume"
#define AUDIOMGR_SETSRCVOL_ACK        "ackSetSourceVolume"
#define AUDIOMGR_SINKVOLTICK_ACK      "ackSinkVolumeTick"
#define AUDIOMGR_SRCVOLTICK_ACK       "ackSourceVolumeTick"
#define AUDIOMGR_SETSINKPROP_ACK      "ackSetSinkSoundProperty"

/*
 * audiomanager control methods
 */
#define AUDIOMGR_IMPLICIT_CONNECTION  "connect"
#define AUDIOMGR_IMPLICIT_CONNECTIONS "disconnect"

#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
