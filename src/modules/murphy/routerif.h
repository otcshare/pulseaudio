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
#ifndef foorouteriffoo
#define foorouteriffoo

#include "userdata.h"


enum am_method {
    audiomgr_unknown_method = 0,

    audiomgr_register_domain,
    audiomgr_domain_complete,
    audiomgr_deregister_domain,

    audiomgr_register_source,
    audiomgr_deregister_source,

    audiomgr_register_sink,
    audiomgr_deregister_sink,

    audiomgr_implicit_connection,
    audiomgr_implicit_connections,

    audiomgr_connect,
    audiomgr_connect_ack,

    audiomgr_disconnect,
    audiomgr_disconnect_ack,

    audiomgr_setsinkvol_ack,
    audiomgr_setsrcvol_ack,
    audiomgr_sinkvoltick_ack,
    audiomgr_srcvoltick_ack,
    audiomgr_setsinkprop_ack,

    audiomgr_method_dim
};


#ifdef WITH_DBUS
pa_routerif *pa_routerif_init(struct userdata *, const char *,
                              const char *, const char *);
#else
pa_routerif *pa_routerif_init(struct userdata *, const char *,
                              const char *, const char *);

#endif

void pa_routerif_done(struct userdata *);


bool pa_routerif_register_domain(struct userdata *,
                                      am_domainreg_data *);
bool pa_routerif_domain_complete(struct userdata *, uint16_t);
bool pa_routerif_unregister_domain(struct userdata *, uint16_t);

bool pa_routerif_register_node(struct userdata *, am_method,
                                    am_nodereg_data *);
bool pa_routerif_unregister_node(struct userdata *, am_method,
                                      am_nodeunreg_data *);

bool pa_routerif_acknowledge(struct userdata *, am_method, am_ack_data *);

bool pa_routerif_register_implicit_connection(struct userdata *,
                                                   am_connect_data *);
bool pa_routerif_register_implicit_connections(struct userdata *, int,
                                                    am_connect_data *);

#endif  /* foorouteriffoo */

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
