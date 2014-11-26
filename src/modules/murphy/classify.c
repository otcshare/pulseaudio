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
Requires(postun): /sbin/ldconfig
ot, write to the
 * Free Software Foundation, Inc., 51 Franklin St - Fifth Floor, Boston,
 * MA 02110-1301 USA.
 *
 */
#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/sink.h>
#include <pulsecore/card.h>
#include <pulsecore/device-port.h>
#include <pulsecore/core-util.h>

#ifdef WITH_AUL
#include <aul.h>
#include <bundle.h>
#endif

#include "classify.h"
#include "node.h"
#include "utils.h"


static int pid2exe(pid_t, char *, size_t);
static char *pid2appid(pid_t, char *, size_t);

void pa_classify_node_by_card(mir_node        *node,
                              pa_card         *card,
                              pa_card_profile *prof,
                              pa_device_port  *port)
{
    const char *bus;
    const char *form;
    /*
    const char *desc;
    */

    pa_assert(node);
    pa_assert(card);

    bus  = pa_utils_get_card_bus(card);
    form = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_FORM_FACTOR);
    /*
    desc = pa_proplist_gets(card->proplist, PA_PROP_DEVICE_DESCRIPTION);
    */

    node->type = mir_node_type_unknown;

    if (form) {
        if (!strcasecmp(form, "internal")) {
            node->location = mir_external;
            if (port && !strcasecmp(bus, "pci")) {
                pa_classify_guess_device_node_type_and_name(node, port->name,
                                                            port->description);
            }
        }
        else if (!strcasecmp(form, "speaker") || !strcasecmp(form, "car")) {
            if (node->direction == mir_output) {
                node->location = mir_internal;
                node->type = mir_speakers;
            }
        }
        else if (!strcasecmp(form, "handset")) {
            node->location = mir_external;
            node->type = mir_phone;
            node->privacy = mir_private;
        }
        else if (!strcasecmp(form, "headset")) {
            node->location = mir_external;
            if (bus) {
                if (!strcasecmp(bus,"usb")) {
                    node->type = mir_usb_headset;
                }
                else if (!strcasecmp(bus,"bluetooth")) {
                    if (prof && !strcmp(prof->name, "a2dp"))
                        node->type = mir_bluetooth_a2dp;
                    else
                        node->type = mir_bluetooth_sco;
                }
                else {
                    node->type = mir_wired_headset;
                }
            }
        }
        else if (!strcasecmp(form, "headphone")) {
            if (node->direction == mir_output) {
                node->location = mir_external;
                if (bus) {
                    if (!strcasecmp(bus,"usb"))
                        node->type = mir_usb_headphone;
                    else if (strcasecmp(bus,"bluetooth"))
                        node->type = mir_wired_headphone;
                }
            }
        }
        else if (!strcasecmp(form, "microphone")) {
            if (node->direction == mir_input) {
                node->location = mir_external;
                node->type = mir_microphone;
            }
        }
        else if (!strcasecmp(form, "phone")) {
            if (bus && !strcasecmp(bus,"bluetooth") && prof) {
                if (!strcmp(prof->name, "a2dp"))
                    node->type = mir_bluetooth_a2dp;
                else if (!strcmp(prof->name, "hsp"))
                    node->type = mir_bluetooth_sco;
                else if (!strcmp(prof->name, "hfgw"))
                    node->type = mir_bluetooth_carkit;
                else if (!strcmp(prof->name, "a2dp_source"))
                    node->type = mir_bluetooth_source;
                else if (!strcmp(prof->name, "a2dp_sink"))
                    node->type = mir_bluetooth_sink;

                if (node->type != mir_node_type_unknown)
                    node->location = mir_external;
            }
        }
    }
    else {
        if (port && !strcasecmp(bus, "pci")) {
            pa_classify_guess_device_node_type_and_name(node, port->name,
                                                        port->description);
        }
        else if (prof && !strcasecmp(bus, "bluetooth")) {
            if (!strcmp(prof->name, "a2dp"))
                node->type = mir_bluetooth_a2dp;
            else if (!strcmp(prof->name, "hsp"))
                node->type = mir_bluetooth_sco;
            else if (!strcmp(prof->name, "hfgw"))
                node->type = mir_bluetooth_carkit;
            else if (!strcmp(prof->name, "a2dp_source"))
                node->type = mir_bluetooth_source;
            else if (!strcmp(prof->name, "a2dp_sink"))
                node->type = mir_bluetooth_sink;
        }
    }

    if (!node->amname[0]) {
       if (node->type != mir_node_type_unknown)
            node->amname = (char *)mir_node_type_str(node->type);
        else if (port && port->description)
            node->amname = port->description;
        else if (port && port->name)
            node->amname = port->name;
        else
            node->amname = node->paname;
    }


    if (node->direction == mir_input)
        node->privacy = mir_privacy_unknown;
    else {
        switch (node->type) {
            /* public */
        default:
        case mir_speakers:
        case mir_front_speakers:
            node->privacy = mir_public;
            break;

            /* private */
        case mir_phone:
        case mir_wired_headset:
        case mir_wired_headphone:
        case mir_usb_headset:
        case mir_usb_headphone:
        case mir_bluetooth_sco:
        case mir_bluetooth_a2dp:
            node->privacy = mir_private;
            break;

            /* unknown */
        case mir_null:
        case mir_jack:
        case mir_spdif:
        case mir_hdmi:
        case mir_bluetooth_sink:
            node->privacy = mir_privacy_unknown;
            break;
        } /* switch */
    }
}

bool pa_classify_node_by_property(mir_node *node, pa_proplist *pl)
{
    typedef struct {
        const char *name;
        mir_node_type value;
    } type_mapping_t;

    static type_mapping_t  map[] = {
        {"speakers"       , mir_speakers         },
        {"front-speakers" , mir_front_speakers   },
        {"rear-speakers"  , mir_rear_speakers    },
        {"microphone"     , mir_microphone       },
        {"jack"           , mir_jack             },
        {"hdmi"           , mir_hdmi             },
        {"gateway_source" , mir_gateway_source   },
        {"gateway_sink"   , mir_gateway_sink     },
        {"spdif"          , mir_spdif            },
        { NULL            , mir_node_type_unknown}
    };

    const char *type;
    int i;

    pa_assert(node);
    pa_assert(pl);

    if ((type = pa_proplist_gets(pl, PA_PROP_NODE_TYPE))) {
        for (i = 0; map[i].name;  i++) {
            if (pa_streq(type, map[i].name)) {
                node->type = map[i].value;
                return true;
            }
        }
    }

    return false;
}

/* data->direction must be set */
void pa_classify_guess_device_node_type_and_name(mir_node   *node,
                                                 const char *name,
                                                 const char *desc)
{
    pa_assert(node);
    pa_assert(name);
    pa_assert(desc);

    if (node->direction == mir_output && strcasestr(name, "headphone")) {
        node->type = mir_wired_headphone;
        node->amname = (char *)desc;
    }
    else if (strcasestr(name, "headset")) {
        node->type = mir_wired_headset;
        node->amname = (char *)desc;
    }
    else if (strcasestr(name, "line")) {
        node->type = mir_jack;
        node->amname = (char *)desc;
    }
    else if (strcasestr(name, "spdif")) {
        node->type = mir_spdif;
        node->amname = (char *)desc;
    }
    else if (strcasestr(name, "hdmi")) {
        node->type = mir_hdmi;
        node->amname = (char *)desc;
    }
    else if (node->direction == mir_input &&
             (strcasestr(name, "microphone") || strcasestr(desc, "microphone")))
    {
        node->type = mir_microphone;
        node->amname = (char *)desc;
    }
    else if (node->direction == mir_output && strcasestr(name,"analog-output"))
        node->type = mir_speakers;
    else if (node->direction == mir_input && strcasestr(name, "analog-input"))
        node->type = mir_jack;
    else {
        node->type = mir_node_type_unknown;
    }
}


mir_node_type pa_classify_guess_stream_node_type(struct userdata *u,
                                                 pa_proplist *pl,
                                                 pa_nodeset_resdef **resdef)
{
    pa_nodeset_map *map = NULL;
    const char     *role;
    const char     *bin;
    char            buf[4096];
    char            appid[PATH_MAX];
    const char     *pidstr;
    const char     *name;
    int             pid;

    pa_assert(u);
    pa_assert(pl);


    do {
        if (!(pidstr = pa_proplist_gets(pl, PA_PROP_APPLICATION_PROCESS_ID)) ||
            (pid = strtol(pidstr, NULL, 10)) < 2)
        {
            pid = 0;
        }

        if ((bin = pa_proplist_gets(pl, PA_PROP_APPLICATION_PROCESS_BINARY))) {
            if (!strcmp(bin, "threaded-ml") ||
                !strcmp(bin, "WebProcess")  ||
                !strcmp(bin,"wrt_launchpad_daemon"))
            {
                if (!pid)
                    break;

#ifdef WITH_AUL
                if (aul_app_get_appid_bypid(pid, buf, sizeof(buf)) < 0 &&
                    pid2exe(pid, buf, sizeof(buf)) < 0)
                {
                    pa_log("can't obtain real application name for wrt '%s' "
                           "(pid %d)", bin, pid);
                    break;
                }
#else
                if (pid2exe(pid, buf, sizeof(buf)) < 0) {
                    pa_log("can't obtain real application name for wrt '%s' "
                           "(pid %d)", bin, pid);
                    break;
                }
#endif
                if ((name = strrchr(buf, '.')))
                    name++;
                else
                    name = buf;

                pa_proplist_sets(pl, PA_PROP_APPLICATION_NAME, name);
                pa_proplist_sets(pl, PA_PROP_APPLICATION_PROCESS_BINARY, buf);

                bin = buf;
            }

            if ((map = pa_nodeset_get_map_by_binary(u, bin))) {
                if (map->role)
                    pa_proplist_sets(pl, PA_PROP_MEDIA_ROLE, map->role);
                break;
            }
        }

        if ((role = pa_proplist_gets(pl, PA_PROP_MEDIA_ROLE)) &&
            (map = pa_nodeset_get_map_by_role(u, role)))
            break;

        if (resdef)
            *resdef = NULL;

        return role ? mir_node_type_unknown : mir_player;

    } while (0);

    if (pid2appid(pid, appid, sizeof(appid)))
        pa_proplist_sets(pl, PA_PROP_RESOURCE_SET_APPID, appid);

    if (resdef)
        *resdef = map ? map->resdef : NULL;

    return map ? map->type : mir_player;
}

static char *get_tag(pid_t pid, const char *tag, char *buf, size_t size)
{
    char path[PATH_MAX];
    char data[8192], *p, *q;
    int  fd, n, tlen;

    fd = -1;
    snprintf(path, sizeof(path), "/proc/%u/status", pid);

    if ((fd = open(path, O_RDONLY)) < 0) {
    fail:
        if (fd >= 0)
            close(fd);
        return NULL;
    }

    if ((n = read(fd, data, sizeof(data) - 1)) <= 0)
        goto fail;
    else
        data[sizeof(data)-1] = '\0';

    close(fd);
    fd = -1;
    tlen = strlen(tag);

    p = data;
    while (*p) {
        if (*p != '\n' && p != data) {
            while (*p && *p != '\n')
                p++;
        }

        if (*p == '\n')
            p++;
        else
            if (p != data)
                goto fail;

        if (!strncmp(p, tag, tlen) && p[tlen] == ':') {
            p += tlen + 1;
            while (*p == ' ' || *p == '\t')
                p++;

            q = buf;
            while (*p != '\n' && *p && size > 1)
                *q++ = *p++;
            *q = '\0';

            return buf;
        }
        else
            p++;
    }

    goto fail;
}


static pid_t get_ppid(pid_t pid)
{
    char  buf[32], *end;
    pid_t ppid;

    if (get_tag(pid, "PPid", buf, sizeof(buf)) != NULL) {
        ppid = strtoul(buf, &end, 10);

        if (end && !*end)
            return ppid;
    }

    return 0;
}


static int pid2exe(pid_t pid, char *buf, size_t len)
{
    pid_t ppid;
    FILE *f;
    char path[PATH_MAX];
    char *p, *q;
    int st = -1;

    if (buf && len > 0) {
        ppid = get_ppid(pid);

        snprintf(path, sizeof(path), "/proc/%u/cmdline", ppid);

        if ((f = fopen(path, "r"))) {
            if (fgets(buf, len-1, f)) {
                if ((p = strchr(buf, ' ')))
                    *p = '\0';
                else if ((p = strchr(buf, '\n')))
                    *p = '\0';
                else
                    p = buf + strlen(buf);

                if ((q = strrchr(buf, '/')))
                    memmove(buf, q+1, p-q);

                st = 0;
            }
            fclose(f);
        }
    }

    if (st < 0)
        pa_log("pid2exe(%u) failed", pid);
    else
        pa_log_debug("pid2exe(%u) => exe %s", pid, buf);

    return st;
}


static char *get_binary(pid_t pid, char *buf, size_t size)
{
    char    path[128];
    ssize_t len;

    snprintf(path, sizeof(path), "/proc/%u/exe", pid);
    if ((len = readlink(path, buf, size - 1)) > 0) {
        buf[len] = '\0';
        return buf;
    }
    else
        return NULL;
}


static char *strprev(char *point, char c, char *base)
{
    while (point > base && *point != c)
        point--;

    if (*point == c)
        return point;
    else
        return NULL;
}


static char *pid2appid(pid_t pid, char *buf, size_t size)
{
    char binary[PATH_MAX];
    char path[PATH_MAX], *dir, *p, *base;
    unsigned int  len;

    if (!pid || !get_binary(pid, binary, sizeof(binary)))
        return NULL;

    strncpy(path, binary, sizeof(path) - 1);
    path[sizeof(path) - 1] = '\0';

    /* fetch basename */
    if ((p = strrchr(path, '/')) == NULL || p == path) {
        strncpy(buf, binary, size - 1);
        buf[size - 1] = '\0';
        return buf;
    }

    base = p-- + 1;

    /* fetch ../bin/<basename> */
    if ((p = strprev(p, '/', path)) == NULL || p == path)
        goto return_base;

    if (strncmp(p + 1, "bin/", 4) != 0)
        goto return_base;
    else
        p--;

    /* fetch dir name above bin */
    if ((dir = strprev(p, '/', path)) == NULL || dir == path)
        goto return_base;

    len = p - dir;

    /* fetch 'apps' dir */
    p = dir - 1;

    if ((p = strprev(p, '/', path)) == NULL)
        goto return_base;

    if (strncmp(p + 1, "apps/", 5) != 0)
        goto return_base;

    if (len + 1 <= size) {
        strncpy(buf, dir + 1, len);
        buf[len] = '\0';

        return buf;
    }

 return_base:
    strncpy(buf, base, size - 1);
    buf[size - 1] = '\0';
    return buf;
}


mir_node_type pa_classify_guess_application_class(mir_node *node)
{
    mir_node_type class;

    pa_assert(node);

    if (node->implement == mir_stream)
        class = node->type;
    else {
        if (node->direction == mir_output)
            class = mir_node_type_unknown;
        else {
            switch (node->type) {
            default:                    class = mir_node_type_unknown;   break;
            case mir_bluetooth_carkit:  class = mir_phone;               break;
            case mir_bluetooth_source:  class = mir_player;              break;
            }
        }
    }

    return class;
}


bool pa_classify_multiplex_stream(mir_node *node)
{
    static bool multiplex[mir_application_class_end] = {
        [ mir_player  ] = true,
        [ mir_game    ] = true,
    };

    mir_node_type class;

    pa_assert(node);

    if (node->implement == mir_stream && node->direction == mir_input) {
        class = node->type;

        if (class > mir_application_class_begin &&
            class < mir_application_class_end)
        {
            return multiplex[class];
        }
    }

    return false;
}

bool pa_classify_ramping_stream(mir_node *node)
{
    static bool ramping[mir_application_class_end] = {
        [ mir_player  ] = true,
    };

    mir_node_type class;

    pa_assert(node);

    if (node->implement == mir_stream && node->direction == mir_input) {
        class = node->type;

        if (class > mir_application_class_begin &&
            class < mir_application_class_end)
        {
            return ramping[class];
        }
    }

    return false;
}

const char *pa_classify_loopback_stream(mir_node *node)
{
    const char *role[mir_device_class_end - mir_device_class_begin] = {
        [ mir_bluetooth_carkit - mir_device_class_begin ] = "phone",
        [ mir_bluetooth_source - mir_device_class_begin ] = "bt_music",
    };

    int class;

    pa_assert(node);

    if (node->implement == mir_device) {
        class = node->type;

        if (class >= mir_device_class_begin && class < mir_device_class_end) {
            return role[class - mir_device_class_begin];
        }
    }

    return NULL;
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
