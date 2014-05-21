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

#define PA_VOLUME_API_VERSION 1
#define PA_VOLUME_API_EXTENSION_NAME "module-volume-api"

enum {
    PA_VOLUME_API_COMMAND_CONNECT,
    PA_VOLUME_API_COMMAND_DISCONNECT,
    PA_VOLUME_API_COMMAND_SUBSCRIBE,
    PA_VOLUME_API_COMMAND_SUBSCRIBE_EVENT,
    PA_VOLUME_API_COMMAND_GET_SERVER_INFO,
    PA_VOLUME_API_COMMAND_GET_VOLUME_CONTROL_INFO,
    PA_VOLUME_API_COMMAND_GET_VOLUME_CONTROL_INFO_LIST,
    PA_VOLUME_API_COMMAND_SET_VOLUME_CONTROL_VOLUME,
    PA_VOLUME_API_COMMAND_GET_MUTE_CONTROL_INFO,
    PA_VOLUME_API_COMMAND_GET_MUTE_CONTROL_INFO_LIST,
    PA_VOLUME_API_COMMAND_SET_MUTE_CONTROL_MUTE,
    PA_VOLUME_API_COMMAND_GET_DEVICE_INFO,
    PA_VOLUME_API_COMMAND_GET_DEVICE_INFO_LIST,
    PA_VOLUME_API_COMMAND_GET_STREAM_INFO,
    PA_VOLUME_API_COMMAND_GET_STREAM_INFO_LIST,
    PA_VOLUME_API_COMMAND_GET_AUDIO_GROUP_INFO,
    PA_VOLUME_API_COMMAND_GET_AUDIO_GROUP_INFO_LIST
};
