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
#ifndef fooaudiomgrfoo
#define fooaudiomgrfoo

#include "userdata.h"

#ifdef WITH_DBUS
#include <dbus/dbus.h>
typedef dbus_bool_t    am_bool_t;
typedef dbus_int16_t   am_int16_t;
typedef dbus_uint16_t  am_uint16_t;
typedef dbus_int32_t   am_int32_t;
typedef dbus_uint32_t  am_uint32_t;
#else
#include <stdbool.h>
typedef bool           am_bool_t;
typedef int16_t        am_int16_t;
typedef uint16_t       am_uint16_t;
typedef int32_t        am_int32_t;
typedef uint32_t       am_uint32_t;
#endif


/* error codes */
#define E_OK              0
#define E_UNKNOWN         1
#define E_OUT_OF_RANGE    2
#define E_NOT_USED        3
#define E_DATABSE_ERROR   4
#define E_ALREADY_EXISTS  5
#define E_NO_CHANGE       6
#define E_NOT_POSSIBLE    7
#define E_NON_EXISTENT    8
#define E_ABORTED         9
#define E_WRONG_FORMAT    10



struct am_domainreg_data {
    am_uint16_t  domain_id;
    const char    *name;      /**< domain name in audio manager  */
    const char    *bus_name;  /**< audio manager's internal bus name
                                   (not to confuse this with D-Bus name)  */
    const char    *node_name; /**< node name on audio manager's internal bus*/
    am_bool_t    early;
    am_bool_t    complete;
    am_uint16_t  state;
};

struct am_nodereg_data {
    const char    *key;        /* for node lookup's */
    am_uint16_t  id;
    const char    *name;
    am_uint16_t  domain;
    am_uint16_t  class;
    am_int32_t   state;      /* 1=on, 2=off */
    am_int16_t   volume;
    am_bool_t    visible;
    struct {
        am_int16_t  status; /* 1=available, 2=unavailable */
        am_int16_t  reason; /* 1=newmedia, 2=same media, 3=nomedia */
    } avail;
    am_uint16_t  mute;
    am_uint16_t  mainvol;
    am_uint16_t  interrupt;  /* 1=off, 2=interrupted */
};

struct am_nodeunreg_data {
    am_uint16_t  id;
    const char    *name;
};


struct am_connect_data {
    am_uint16_t  handle;
    am_uint16_t  connection;
    am_uint16_t  source;
    am_uint16_t  sink;
    am_int32_t   format;
};

struct am_ack_data {
    am_uint32_t  handle;
    am_uint16_t  param1;
    am_uint16_t  param2;
    am_uint16_t  error;
};



pa_audiomgr *pa_audiomgr_init(struct userdata *);
void pa_audiomgr_done(struct userdata *);

void pa_audiomgr_register_domain(struct userdata *);
void pa_audiomgr_domain_registered(struct userdata *,  uint16_t, uint16_t,
                                   am_domainreg_data *);

void pa_audiomgr_unregister_domain(struct userdata *, bool);


void pa_audiomgr_register_node(struct userdata *, mir_node *);
void pa_audiomgr_node_registered(struct userdata *, uint16_t, uint16_t,
                                 am_nodereg_data *);

void pa_audiomgr_unregister_node(struct userdata *, mir_node *);
void pa_audiomgr_node_unregistered(struct userdata *, am_nodeunreg_data *);

void pa_audiomgr_delete_default_routes(struct userdata *);
void pa_audiomgr_add_default_route(struct userdata *, mir_node *, mir_node *);
void pa_audiomgr_send_default_routes(struct userdata *);

void pa_audiomgr_connect(struct userdata *, am_connect_data *);
void pa_audiomgr_disconnect(struct userdata *, am_connect_data *);


#endif

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
