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
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "userdata.h"
#include "socketif.h"
#include "audiomgr.h"


struct pa_routerif {
    int  sock;
};



static const char *method_str(am_method);

pa_routerif *pa_routerif_init(struct userdata *u,
                              const char      *socktyp,
                              const char      *addr,
                              const char      *port)
{
    pa_module      *m = u->module;
    pa_routerif    *routerif = NULL;

    routerif = pa_xnew0(pa_routerif, 1);
    routerif->sock = -1;

    return routerif;
}


void pa_routerif_done(struct userdata *u)
{
    pa_routerif *routerif;

    if (u && (routerif = u->routerif)) {
        if (routerif->sock >= 0)
            close(routerif->sock);

        pa_xfree(routerif);

        u->routerif = NULL;
    }
}


bool pa_routerif_register_domain(struct userdata   *u,
                                           am_domainreg_data *dr)
{
    pa_routerif *routerif;
    int          success = true;

    pa_assert(u);
    pa_assert(dr);
    pa_assert_se((routerif = u->routerif));
    pa_assert(routerif->sock >= 0);

    pa_log_info("%s: registering to AudioManager", __FILE__);

    return success;
}

bool pa_routerif_domain_complete(struct userdata *u, uint16_t domain)
{
    pa_routerif *routerif;
    bool    success = true;

    pa_assert(u);
    pa_assert_se((routerif = u->routerif));
    pa_assert(routerif->sock >= 0);

    pa_log_debug("%s: domain %u AudioManager %s", __FUNCTION__,
                 domain, method_str(audiomgr_domain_complete));

    return success;
}

bool pa_routerif_unregister_domain(struct userdata *u, uint16_t domain)
{
    pa_routerif *routerif;
    bool    success = true;

    pa_assert(u);
    pa_assert_se((routerif = u->routerif));
    pa_assert(routerif->sock >= 0);

    pa_log_info("%s: deregistreing domain %u from AudioManager",
                __FILE__, domain);

    return success;
}


bool pa_routerif_register_node(struct userdata *u,
                                    am_method m,
                                    am_nodereg_data *rd)
{
    const char      *method = method_str(m);
    pa_routerif     *routerif;
    bool        success = true;

    pa_assert(u);
    pa_assert(rd);
    pa_assert_se((routerif = u->routerif));
    pa_assert(routerif->sock >= 0);

    pa_log_debug("%s: %s '%s' to AudioManager", __FUNCTION__, method,rd->name);

    return success;
}


bool pa_routerif_unregister_node(struct userdata *u,
                                      am_method m,
                                      am_nodeunreg_data *ud)
{
    const char  *method = method_str(m);
    pa_routerif *routerif;
    bool    success = true;

    pa_assert(u);
    pa_assert(ud);
    pa_assert_se((routerif = u->routerif));
    pa_assert(routerif->sock >= 0);

    pa_log_debug("%s: %s '%s' to AudioManager", __FUNCTION__, method,ud->name);

    return success;
}

bool pa_routerif_register_implicit_connection(struct userdata *u,
                                                   am_connect_data *conn) {
    return false;
}

bool pa_routerif_register_implicit_connections(struct userdata *u,
                                                    int              nconn,
                                                    am_connect_data *conns)
{
    return false;
}

bool pa_routerif_acknowledge(struct userdata *u, am_method m,
                                  struct am_ack_data *ad)
{
    const char     *method = method_str(m);
    pa_routerif    *routerif;
    bool       success = true;

    pa_assert(u);
    pa_assert(method);
    pa_assert_se((routerif = u->routerif));
    pa_assert(routerif->sock >= 0);

    pa_log_debug("%s: sending %s", __FILE__, method);

    return success;
}


static const char *method_str(am_method m)
{
    switch (m) {
    case audiomgr_register_domain:      return "register_domain";
    case audiomgr_domain_complete:      return "domain_complete";
    case audiomgr_deregister_domain:    return "deregister_domain";
    case audiomgr_register_source:      return "register_source";
    case audiomgr_deregister_source:    return "deregister_source";
    case audiomgr_register_sink:        return "register_sink";
    case audiomgr_deregister_sink:      return "deregister_sink";
    case audiomgr_implicit_connection:  return "register_implicit_connection";
    case audiomgr_implicit_connections: return "replace_implicit_connections";
    case audiomgr_connect:              return "connect";
    case audiomgr_connect_ack:          return "connect_ack";
    case audiomgr_disconnect:           return "disconnect";
    case audiomgr_disconnect_ack:       return "disconnect_ack";
    case audiomgr_setsinkvol_ack:       return "setsinkvol_ack";
    case audiomgr_setsrcvol_ack:        return "setsrcvol_ack";
    case audiomgr_sinkvoltick_ack:      return "sinkvoltick_ack";
    case audiomgr_srcvoltick_ack:       return "srcvoltick_ack";
    case audiomgr_setsinkprop_ack:      return "setsinkprop_ack";
    default:                            return "invalid_method";
    }
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
