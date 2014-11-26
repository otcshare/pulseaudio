/*
 * Module-murphy-ivi -- PulseAudio module for providing audio routing support
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
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <pulsecore/core-util.h>

#include <murphy/common/macros.h>
#include <murphy/common/mm.h>
#include <murphy/core/lua-utils/object.h>
#include <murphy/core/lua-utils/funcbridge.h>
#include <murphy/core/lua-utils/strarray.h>
#include <murphy/core/lua-utils/funcbridge.h>
#include <murphy/domain-control/client.h>
#include <murphy/resource/data-types.h>
#include <murphy/resource/protocol.h>

#include "scripting.h"
#include "zone.h"
#include "node.h"
#include "router.h"
#include "volume.h"
#include "murphyif.h"
#include "murphy-config.h"

#define IMPORT_CLASS       MRP_LUA_CLASS(mdb, import)
#define NODE_CLASS         MRP_LUA_CLASS(node, instance)
#define ZONE_CLASS         MRP_LUA_CLASS_SIMPLE(zone)
#define RESOURCE_CLASS     MRP_LUA_CLASS_SIMPLE(audio_resource)
#define RTGROUP_CLASS      MRP_LUA_CLASS_SIMPLE(routing_group)
#define APPLICATION_CLASS  MRP_LUA_CLASS_SIMPLE(application_class)
#define VOLLIM_CLASS       MRP_LUA_CLASS_SIMPLE(volume_limit)

#define ARRAY_CLASSID      MRP_LUA_CLASSID_ROOT "mdb_array"

#define USERDATA           "murphy_ivi_userdata"

#undef  MRP_LUA_ENTER
#define MRP_LUA_ENTER                                           \
    pa_log_debug("%s() enter", __FUNCTION__)

#undef  MRP_LUA_LEAVE
#define MRP_LUA_LEAVE(_v)                                       \
    do {                                                        \
        pa_log_debug("%s() leave (%d)", __FUNCTION__, (_v));    \
        return (_v);                                            \
    } while (0)

#undef  MRP_LUA_LEAVE_NOARG
#define MRP_LUA_LEAVE_NOARG                                     \
    pa_log_debug("%s() leave", __FUNCTION__)

typedef void (*update_func_t)(struct userdata *);

struct pa_scripting {
    lua_State *L;
    bool configured;
};

struct scripting_import {
    struct userdata    *userdata;
    const char         *table;
    mrp_lua_strarray_t *columns;
    const char         *condition;
    pa_value           *values;
    mrp_funcbridge_t   *update;
};

struct scripting_node {
    struct userdata    *userdata;
    const char         *id;
    mir_node           *node;
};

typedef struct {
    const char         *recording;
    const char         *playback;
} resource_name_t;

typedef struct {
    const char         *prop;
    mrp_attr_t          def;
} attribute_t;


struct scripting_zone {
    struct userdata    *userdata;
    const char         *name;
    uint32_t            index;
};

struct scripting_resource {
    struct userdata    *userdata;
    resource_name_t    *name;
    attribute_t        *attributes;
};

struct scripting_rtgroup {
    struct userdata    *userdata;
    mir_rtgroup        *rtg;
    mir_direction       type;
    mrp_funcbridge_t   *accept;
    mrp_funcbridge_t   *compare;
};

typedef struct {
    const char        **input;
    const char        **output;
} route_t;

typedef struct {
    const char         *name;
    bool                needres;
    const char         *role;
    pa_nodeset_resdef   resource;
} map_t;

struct scripting_apclass {
    struct userdata    *userdata;
    const char         *name;
    const char         *class;
    mir_node_type       type;
    int                 priority;
    route_t            *route;
    map_t              *roles;
    map_t              *binaries;
    struct {
        bool resource;
    }                   needs;
};

typedef enum {
    vollim_class = 1,
    vollim_generic,
    vollim_maximum
} vollim_type;

typedef struct {
    size_t      nint;
    int        *ints;
} intarray_t;

typedef struct {
    bool   mallocd;
    double     *value;
} limit_data_t;

struct scripting_vollim {
    struct userdata  *userdata;
    const char       *name;
    vollim_type       type;
    intarray_t       *classes;
    limit_data_t     *limit;
    mrp_funcbridge_t *calculate;
    char              args[0];
};


typedef struct {
    const char *name;
    int value;
} const_def_t;

typedef struct {
    const char *name;
    const char *sign;
    mrp_funcbridge_cfunc_t func;
    void *data;
} funcbridge_def_t;

typedef enum {
    NAME = 1,
    TYPE,
    ZONE,
    CLASS,
    INPUT,
    LIMIT,
    ROUTE,
    ROLES,
    TABLE,
    ACCEPT,
    MAXROW,
    OUTPUT,
    TABLES,
    UPDATE,
    COMPARE,
    COLUMNS,
    PRIVACY,
    BINARIES,
    CHANNELS,
    LOCATION,
    PRIORITY,
    AVAILABLE,
    CALCULATE,
    CONDITION,
    DIRECTION,
    IMPLEMENT,
    NODE_TYPE,
    ATTRIBUTES,
    AUTORELEASE,
    DESCRIPTION,
} field_t;

static int  import_create(lua_State *);
static int  import_getfield(lua_State *);
static int  import_setfield(lua_State *);
static int  import_tostring(lua_State *);
static void import_destroy(void *);

static int  import_link(lua_State *);

static void import_data_changed(struct userdata *, const char *,
                                int, mrp_domctl_value_t **);
static bool update_bridge(lua_State *, void *, const char *,
                          mrp_funcbridge_value_t *, char *,
                          mrp_funcbridge_value_t *);

static void array_class_create(lua_State *);
static pa_value *array_create(lua_State *, int, mrp_lua_strarray_t *);
static int  array_getfield(lua_State *);
static int  array_setfield(lua_State *);
static int  array_getlength(lua_State *);

#if 0
static void array_destroy(void *);
#endif

static int  node_create(lua_State *);
static int  node_getfield(lua_State *);
static int  node_setfield(lua_State *);
static int  node_tostring(lua_State *);
static void node_destroy(void *);

static int  zone_create(lua_State *);
static int  zone_getfield(lua_State *);
static int  zone_setfield(lua_State *);
static void zone_destroy(void *);

static int  resource_create(lua_State *);
static int  resource_getfield(lua_State *);
static int  resource_setfield(lua_State *);
static void resource_destroy(void *);

static int  rtgroup_create(lua_State *);
static int  rtgroup_getfield(lua_State *);
static int  rtgroup_setfield(lua_State *);
static int  rtgroup_tostring(lua_State *);
static void rtgroup_destroy(void *);

static bool rtgroup_accept(struct userdata *, mir_rtgroup *, mir_node *);
static int  rtgroup_compare(struct userdata *, mir_rtgroup *,
                            mir_node *, mir_node *);

static bool accept_bridge(lua_State *, void *, const char *,
                          mrp_funcbridge_value_t *, char *,
                          mrp_funcbridge_value_t *);
static bool compare_bridge(lua_State *, void *, const char *,
                           mrp_funcbridge_value_t *, char *,
                           mrp_funcbridge_value_t *);
static bool change_bridge(lua_State *, void *, const char *,
                           mrp_funcbridge_value_t *, char *,
                           mrp_funcbridge_value_t *);

static int  apclass_create(lua_State *);
static int  apclass_getfield(lua_State *);
static int  apclass_setfield(lua_State *);
static int  apclass_tostring(lua_State *);
static void apclass_destroy(void *);

static route_t *route_check(lua_State *, int);
static int route_push(lua_State *, route_t *);
static void route_destroy(route_t *);

static int  vollim_create(lua_State *);
static int  vollim_getfield(lua_State *);
static int  vollim_setfield(lua_State *);
static int  vollim_tostring(lua_State *);
static void vollim_destroy(void *);

static double vollim_calculate(struct userdata *, int, mir_node *, void *);
static bool calculate_bridge(lua_State *, void *, const char *,
                             mrp_funcbridge_value_t *, char *,
                             mrp_funcbridge_value_t *);

static limit_data_t *limit_data_check(lua_State *, int);

#if 0
static int limit_data_push(lua_State *, limit_data_t *);
#endif

static void limit_data_destroy(limit_data_t *);

static intarray_t *intarray_check(lua_State *, int, int, int);
static int intarray_push(lua_State *, intarray_t *);
static void intarray_destroy(intarray_t *);

static resource_name_t *resource_names_check(lua_State *, int);
static void resource_names_destroy(resource_name_t *);

static attribute_t *attributes_check(lua_State *, int);
static void attributes_destroy(attribute_t *);

static map_t *map_check(lua_State *, int);
static int map_push(lua_State *, map_t *);
static void map_destroy(map_t *);

static field_t field_check(lua_State *, int, const char **);
static field_t field_name_to_type(const char *, size_t);
static int make_id(char *buf, size_t len, const char *fmt, ...);

static void setup_murphy_interface(struct userdata *);
static char *comma_separated_list(mrp_lua_strarray_t *, char *, int);

static bool define_constants(lua_State *);
static bool register_methods(lua_State *);

static void *alloc(void *, void *, size_t, size_t);
static int panic(lua_State *);


MRP_LUA_METHOD_LIST_TABLE (
    import_methods,           /* methodlist name */
    MRP_LUA_METHOD_CONSTRUCTOR  (import_create)
    MRP_LUA_METHOD        (link, import_link  )
);

MRP_LUA_METHOD_LIST_TABLE (
    node_methods,             /* methodlist name */
    MRP_LUA_METHOD_CONSTRUCTOR  (node_create)
);


MRP_LUA_METHOD_LIST_TABLE (
    import_overrides,        /* methodlist_name */
    MRP_LUA_OVERRIDE_CALL       (import_create)
    MRP_LUA_OVERRIDE_GETFIELD   (import_getfield)
    MRP_LUA_OVERRIDE_SETFIELD   (import_setfield)
    MRP_LUA_OVERRIDE_STRINGIFY  (import_tostring)
);

MRP_LUA_METHOD_LIST_TABLE (
    array_overrides,         /* methodlist_name */
    MRP_LUA_OVERRIDE_GETFIELD   (array_getfield)
    MRP_LUA_OVERRIDE_SETFIELD   (array_setfield)
    MRP_LUA_OVERRIDE_GETLENGTH  (array_getlength)
);

MRP_LUA_METHOD_LIST_TABLE (
    node_overrides,           /* methodlist name */
    MRP_LUA_OVERRIDE_CALL       (node_create)
    MRP_LUA_OVERRIDE_GETFIELD   (node_getfield)
    MRP_LUA_OVERRIDE_SETFIELD   (node_setfield)
    MRP_LUA_OVERRIDE_STRINGIFY  (node_tostring)
);


MRP_LUA_CLASS_DEF (
   mdb,                         /* class name */
   import,                      /* constructor name */
   scripting_import,            /* userdata type */
   import_destroy,              /* userdata destructor */
   import_methods,              /* class methods */
   import_overrides             /* override methods */
);

MRP_LUA_CLASS_DEF (
   node,                        /* class name */
   instance,                    /* constructor name */
   scripting_node,              /* userdata type */
   node_destroy,                /* userdata destructor */
   node_methods,                /* class methods */
   node_overrides               /* override methods */
);

MRP_LUA_CLASS_DEF_SIMPLE (
   zone,                         /* class name */
   scripting_zone,               /* userdata type */
   zone_destroy,                 /* userdata destructor */
   MRP_LUA_METHOD_LIST (         /* methods */
      MRP_LUA_METHOD_CONSTRUCTOR  (zone_create)
   ),
   MRP_LUA_METHOD_LIST (         /* overrides */
      MRP_LUA_OVERRIDE_CALL       (zone_create)
      MRP_LUA_OVERRIDE_GETFIELD   (zone_getfield)
      MRP_LUA_OVERRIDE_SETFIELD   (zone_setfield)
   )
);

MRP_LUA_CLASS_DEF_SIMPLE (
   audio_resource,               /* class name */
   scripting_resource,           /* userdata type */
   resource_destroy,             /* userdata destructor */
   MRP_LUA_METHOD_LIST (         /* methods */
      MRP_LUA_METHOD_CONSTRUCTOR  (resource_create)
   ),
   MRP_LUA_METHOD_LIST (         /* overrides */
      MRP_LUA_OVERRIDE_CALL       (resource_create)
      MRP_LUA_OVERRIDE_GETFIELD   (resource_getfield)
      MRP_LUA_OVERRIDE_SETFIELD   (resource_setfield)
   )
);

MRP_LUA_CLASS_DEF_SIMPLE (
   routing_group,                /* class name */
   scripting_rtgroup,            /* userdata type */
   rtgroup_destroy,              /* userdata destructor */
   MRP_LUA_METHOD_LIST (         /* methods */
      MRP_LUA_METHOD_CONSTRUCTOR  (rtgroup_create)
   ),
   MRP_LUA_METHOD_LIST (         /* overrides */
      MRP_LUA_OVERRIDE_CALL       (rtgroup_create)
      MRP_LUA_OVERRIDE_GETFIELD   (rtgroup_getfield)
      MRP_LUA_OVERRIDE_SETFIELD   (rtgroup_setfield)
      MRP_LUA_OVERRIDE_STRINGIFY  (rtgroup_tostring)
   )
);

MRP_LUA_CLASS_DEF_SIMPLE (
   application_class,            /* class name */
   scripting_apclass,            /* userdata type */
   apclass_destroy,              /* userdata destructor */
   MRP_LUA_METHOD_LIST (         /* methods */
      MRP_LUA_METHOD_CONSTRUCTOR  (apclass_create)
   ),
   MRP_LUA_METHOD_LIST (        /* overrides */
      MRP_LUA_OVERRIDE_CALL       (apclass_create)
      MRP_LUA_OVERRIDE_GETFIELD   (apclass_getfield)
      MRP_LUA_OVERRIDE_SETFIELD   (apclass_setfield)
      MRP_LUA_OVERRIDE_STRINGIFY  (apclass_tostring)
   )
);

MRP_LUA_CLASS_DEF_SIMPLE (
   volume_limit,                 /* class name */
   scripting_vollim,             /* userdata type */
   vollim_destroy,               /* userdata destructor */
   MRP_LUA_METHOD_LIST (         /* methods */
      MRP_LUA_METHOD_CONSTRUCTOR  (vollim_create)
   ),
   MRP_LUA_METHOD_LIST (        /* overrides */
      MRP_LUA_OVERRIDE_CALL       (vollim_create)
      MRP_LUA_OVERRIDE_GETFIELD   (vollim_getfield)
      MRP_LUA_OVERRIDE_SETFIELD   (vollim_setfield)
      MRP_LUA_OVERRIDE_STRINGIFY  (vollim_tostring)
   )
);


pa_scripting *pa_scripting_init(struct userdata *u)
{
    pa_scripting *scripting;
    lua_State *L;

    pa_assert(u);

    scripting = pa_xnew0(pa_scripting, 1);

    if (!(L = lua_newstate(alloc, u)))
        pa_log("failed to initialize Lua");
    else {
        lua_atpanic(L, &panic);
        luaL_openlibs(L);

        mrp_create_funcbridge_class(L);
        mrp_lua_create_object_class(L, IMPORT_CLASS);
        mrp_lua_create_object_class(L, NODE_CLASS);
        mrp_lua_create_object_class(L, ZONE_CLASS);
        mrp_lua_create_object_class(L, RESOURCE_CLASS);
        mrp_lua_create_object_class(L, RTGROUP_CLASS);
        mrp_lua_create_object_class(L, APPLICATION_CLASS);
        mrp_lua_create_object_class(L, VOLLIM_CLASS);

        array_class_create(L);

        define_constants(L);
        register_methods(L);

        lua_pushlightuserdata(L, u);
        lua_setglobal(L, USERDATA);

        scripting->L = L;
        scripting->configured = false;
    }

    return scripting;
}

void pa_scripting_done(struct userdata *u)
{
    pa_scripting *scripting;

    if (u && (scripting = u->scripting)) {
        pa_xfree(scripting);
        u->scripting = NULL;
    }
}

bool pa_scripting_dofile(struct userdata *u, const char *file)
{
    pa_scripting *scripting;
    lua_State *L;
    bool success;

    pa_assert(u);
    pa_assert(file);

    pa_assert_se((scripting = u->scripting));
    pa_assert_se((L = scripting->L));

    if (luaL_loadfile(L, file) || lua_pcall(L, 0, 0, 0)) {
        success = false;
        pa_log("%s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    else {
        success =true;
        scripting->configured = true;
        setup_murphy_interface(u);
        pa_zoneset_update_module_property(u);
    }

    return success;
}

static int import_create(lua_State *L)
{
    struct userdata *u = NULL;
    pa_scripting *scripting;
    size_t fldnamlen;
    const char *fldnam;
    scripting_import *imp;
    const char *table = NULL;
    mrp_lua_strarray_t *columns = NULL;
    const char *condition = NULL;
    unsigned int maxrow = 0;
    mrp_funcbridge_t *update = NULL;
    size_t maxcol;
    pa_value **rows;
    pa_value **cols;
    unsigned int i,j;
    int top;

    MRP_LUA_ENTER;

    top = lua_gettop(L);

    lua_getglobal(L, USERDATA);
    if (!lua_islightuserdata(L, -1) || !(u = lua_touserdata(L, -1)))
        luaL_error(L, "missing or invalid global '" USERDATA "'");

    pa_assert_se((scripting = u->scripting));

    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {

        switch (field_name_to_type(fldnam, fldnamlen)) {
        case TABLE:      table = luaL_checkstring(L, -1);                break;
        case COLUMNS:    columns = mrp_lua_check_strarray(L, -1);        break;
        case CONDITION:  condition = luaL_checkstring(L, -1);            break;
        case MAXROW:     maxrow = luaL_checkint(L, -1);                  break;
        case UPDATE:     update = mrp_funcbridge_create_luafunc(L, -1);  break;
        default:         luaL_error(L, "bad field '%s'", fldnam);        break;
        }

    } /* MRP_LUA_FOREACH_FIELD */

    lua_settop(L, top);

    if (!table)
        luaL_error(L, "missing table field");
    if (!columns)
        luaL_error(L, "missing columns field");
    if (maxrow < 1 || maxrow >= MQI_QUERY_RESULT_MAX)
        luaL_error(L, "missing or invalid maxrow field");
    if (!update)
        luaL_error(L, "missing update function");

    maxcol = columns->nstring;

    if (maxcol >= MQI_COLUMN_MAX)
        luaL_error(L, "too many columns (max %d allowed)", MQI_COLUMN_MAX);

    if (scripting->configured)
        luaL_error(L, "refuse to import '%s' after configuration phase",table);

    imp = (scripting_import *)mrp_lua_create_object(L, IMPORT_CLASS, table,0);

    imp->userdata = u;
    imp->table = pa_xstrdup(table);
    imp->columns = columns;
    imp->condition = condition;
    imp->values = array_create(L, maxrow, NULL);
    imp->update = update;

    for (i = 0, rows = imp->values->array;  i < maxrow;   i++) {
        cols = (rows[i] = array_create(L, maxcol, columns))->array;
        lua_rawseti(L, -3, i+1); /* we add this to the import */
        for (j = 0;  j < maxcol;  j++)
            cols[j] = pa_xnew0(pa_value, 1);
    }

    lua_rawseti(L, -2, MQI_QUERY_RESULT_MAX);

    MRP_LUA_LEAVE(1);
}

static int import_getfield(lua_State *L)
{
    scripting_import *imp;
    pa_value *values;
    int colidx;
    field_t fld;

    MRP_LUA_ENTER;

    if (!(imp = (scripting_import *)mrp_lua_check_object(L, IMPORT_CLASS, 1)))
        lua_pushnil(L);
    else {
        pa_assert_se((values = imp->values));

        if (lua_type(L, 2) == LUA_TNUMBER) {
            colidx = lua_tointeger(L, 2);

            if (colidx < 1 || colidx > -values->type)
                lua_pushnil(L);
            else
                lua_rawgeti(L, 1, colidx);
        }
        else {
            fld = field_check(L, 2, NULL);
            lua_pop(L, 1);

            switch (fld) {
            case TABLE:       lua_pushstring(L, imp->table);             break;
            case COLUMNS:     mrp_lua_push_strarray(L, imp->columns);    break;
            case CONDITION:   lua_pushstring(L, imp->condition);         break;
            case MAXROW:      lua_pushinteger(L, -imp->values->type);    break;
            default:          lua_pushnil(L);                            break;
            }
        }
    }

    MRP_LUA_LEAVE(1);
}

static int import_setfield(lua_State *L)
{
    const char *f;

    MRP_LUA_ENTER;

    f = luaL_checkstring(L, 2);
    luaL_error(L, "attempt to set '%s' field of read-only mdb.import", f);

    MRP_LUA_LEAVE(0);
}

static int import_tostring(lua_State *L)
{
    scripting_import *imp;

    MRP_LUA_ENTER;

    imp = (scripting_import *)mrp_lua_check_object(L, IMPORT_CLASS, 1);

    lua_pushstring(L, imp->table);

    MRP_LUA_LEAVE(1);
}

static void import_destroy(void *data)
{
    scripting_import *imp = (scripting_import *)data;

    MRP_LUA_ENTER;

    pa_xfree((void *)imp->table);
    mrp_lua_free_strarray(imp->columns);
    pa_xfree((void *)imp->condition);

    MRP_LUA_LEAVE_NOARG;
}

static int import_link(lua_State *L)
{
    scripting_import *imp;
    mrp_lua_strarray_t *columns;
    pa_value *values;
    const char *colnam;
    int rowidx;
    size_t colidx;
    pa_value *row;
    pa_value *col;

    MRP_LUA_ENTER;

    imp = (scripting_import *)mrp_lua_check_object(L, IMPORT_CLASS, 1);
    rowidx = luaL_checkint(L, 2) - 1;
    colnam = luaL_checkstring(L, 3);

    pa_assert(imp);
    pa_assert_se((columns = imp->columns));

    col = NULL;

    if (rowidx >= 0 && rowidx < -imp->values->type) {
        for (colidx = 0;   colidx < columns->nstring;   colidx++) {
            if (!strcmp(colnam, columns->strings[colidx])) {
                pa_assert_se((values = imp->values));
                pa_assert_se((row = values->array[rowidx]));
                pa_assert(colidx < (size_t)-row->type);
                pa_assert_se((col = row->array[colidx]));
                break;
            }
        }
    }

    pa_log_debug("userdata: type:%d", col->type);

    lua_pushlightuserdata(L, col);

    MRP_LUA_LEAVE(1);
}

static void import_data_changed(struct userdata *u,
                                const char *table,
                                int nrow,
                                mrp_domctl_value_t **mval)
{
    static mrp_domctl_value_t empty;

    pa_scripting *scripting;
    lua_State *L;
    scripting_import *imp;
    mrp_domctl_value_t *mrow;
    mrp_domctl_value_t *mcol;
    pa_value *ptval, *prval, *pcval;
    pa_value **prow;
    pa_value **pcol;
    int maxcol;
    int maxrow;
    mrp_funcbridge_value_t arg;
    mrp_funcbridge_value_t ret;
    char t;
    int i,j;

    pa_assert(u);
    pa_assert(table);
    pa_assert(mval || nrow == 0);
    pa_assert_se((scripting = u->scripting));
    pa_assert_se((L = scripting->L));

    pa_log_debug("table '%s' data changed: got %d rows", table, nrow);

    mrp_lua_get_class_table(L, IMPORT_CLASS);

    if (!lua_istable(L, -1)){
        luaL_error(L, "internal error: failed to find '%s' table",
                   (IMPORT_CLASS)->constructor);
    }

    lua_pushstring(L, table);
    lua_rawget(L, -2);

    if (!(imp = mrp_lua_to_object(L, IMPORT_CLASS, -1)))
        pa_log("can't find import '%s'", table);
    else {
        pa_assert(!strcmp(table, imp->table));
        pa_assert(imp->columns);
        pa_assert(imp->update);
        pa_assert_se((ptval = imp->values));
        pa_assert_se((prow = ptval->array));

        maxrow = -ptval->type;
        maxcol = imp->columns->nstring;

        pa_assert(maxrow >= 0);
        pa_assert(nrow <= maxrow);

        pa_log_debug("import '%s' found", imp->table);

        for (i = 0; i < maxrow;  i++) {
            pa_assert_se((prval = prow[i]));
            pa_assert_se((pcol = prval->array));
            pa_assert(prval->type < 0);
            pa_assert(maxcol == -prval->type);

            mrow = (i < nrow) ? mval[i] : NULL;

            for (j = 0;  j < maxcol;  j++) {
                pcval = pcol[j];
                mcol = mrow ? mrow + j : &empty;

                switch (mcol->type) {
                case MRP_DOMCTL_STRING:
                    pa_assert(!pcval->type || pcval->type == pa_value_string);
                    pa_xfree((void *)pcval->string);
                    pcval->type = pa_value_string;
                    pcval->string = pa_xstrdup(mcol->str);
                    break;
                case MRP_DOMCTL_INTEGER:
                    pa_assert(!pcval->type || pcval->type == pa_value_integer);
                    pcval->type = pa_value_integer;
                    pcval->integer = mcol->s32;
                    break;
                case MRP_DOMCTL_UNSIGNED:
                    pa_assert(!pcval->type || pcval->type == pa_value_unsignd);
                    pcval->type = pa_value_unsignd;
                    pcval->unsignd = mcol->u32;
                    break;
                case MRP_DOMCTL_DOUBLE:
                    pa_assert(!pcval->type || pcval->type ==pa_value_floating);
                    pcval->type = pa_value_floating;
                    pcval->floating = mcol->dbl;
                    break;
                default:
                    if (pcval->type == pa_value_string)
                        pa_xfree((void *)pcval->string);
                    memset(pcval, 0, sizeof(pa_value));
                    break;
                }
            }
        }

        arg.pointer = imp;

        if (!mrp_funcbridge_call_from_c(L, imp->update, "o", &arg, &t, &ret)) {
            pa_log("failed to call %s:update method (%s)",
                   imp->table, ret.string);
            pa_xfree((void *)ret.string);
        }
    }

    lua_pop(L, 2);
}


static bool update_bridge(lua_State *L, void *data, const char *signature,
                          mrp_funcbridge_value_t *args,
                          char *ret_type, mrp_funcbridge_value_t *ret_val)
{
    update_func_t update;
    scripting_import *imp;
    struct userdata *u;
    bool success;

    (void)L;

    pa_assert(signature);
    pa_assert(args);
    pa_assert(ret_type);
    pa_assert(ret_val);

    pa_assert_se((update = (update_func_t)data));

    if (strcmp(signature, "o"))
        success = false;
    else {
        pa_assert_se((imp = args[0].pointer));
        pa_assert_se((u = imp->userdata));

        success = true;
        *ret_type = MRP_FUNCBRIDGE_NO_DATA;
        memset(ret_val, 0, sizeof(mrp_funcbridge_value_t));
        update(u);
    }

    return success;
}



static void array_class_create(lua_State *L)
{
    /* create a metatable for row's */
    luaL_newmetatable(L, ARRAY_CLASSID);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -2);
    lua_settable(L, -3);        /* metatable.__index = metatable */
    luaL_openlib(L, NULL, array_overrides, 0);
}


static pa_value *array_create(lua_State *L, int dimension,
                              mrp_lua_strarray_t *names)
{
    pa_value  *value;
    pa_value **array;

    pa_assert(L);
    pa_assert(dimension >= 0);
    pa_assert(dimension < MQI_QUERY_RESULT_MAX);

    array = pa_xnew0(pa_value *, dimension + 1);
    value = lua_newuserdata(L, sizeof(pa_value));
    value->type = -dimension;
    value->array = array;

    array[dimension] = (pa_value *)names;

    luaL_getmetatable(L, ARRAY_CLASSID);
    lua_setmetatable(L, -2);

    return value;
}

static int array_getfield(lua_State *L)
{
    pa_value *arr, *value;
    unsigned int dimension;
    int key_type;
    const char *key;
    mrp_lua_strarray_t *names;
    int idx;
    size_t i;

    MRP_LUA_ENTER;

    pa_assert(L);

    arr = (pa_value *)luaL_checkudata(L, 1, ARRAY_CLASSID);

    pa_assert(arr->type < 0);

    dimension = -arr->type;
    key_type = lua_type(L, 2);

    switch (key_type) {
    case LUA_TNUMBER:
        idx = lua_tointeger(L, 2) - 1;
        break;
    case LUA_TSTRING:
        idx = -1;
        if ((names = (mrp_lua_strarray_t *)arr->array[dimension])) {
            pa_assert(dimension == names->nstring);
            key = lua_tostring(L, 2);
            pa_assert(key);
            for (i = 0;  i < dimension;  i++) {
                if (!strcmp(key, names->strings[i])) {
                    idx = i;
                    break;
                }
            }
        }
        break;
    default:
        idx = -1;
        break;
    }


    if (idx < 0 || (unsigned int)idx >= dimension || !(value = arr->array[idx]))
        lua_pushnil(L);
    else if (value->type < 0)
        lua_rawgeti(L, 1, 1 - value->type);
    else {
        switch (value->type) {
        case pa_value_string:   lua_pushstring(L, value->string);   break;
        case pa_value_integer:  lua_pushinteger(L, value->integer); break;
        case pa_value_unsignd:  lua_pushinteger(L, value->unsignd); break;
        case pa_value_floating: lua_pushnumber(L, value->floating); break;
        default:                lua_pushnil(L);                     break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int array_setfield(lua_State *L)
{
    MRP_LUA_ENTER;

    pa_assert(L);

    luaL_error(L, "attempt to write to a read-only object");

    MRP_LUA_LEAVE(0);
}

static int array_getlength(lua_State *L)
{
    MRP_LUA_ENTER;

    pa_assert(L);

    MRP_LUA_LEAVE(1);
}

#if 0
static void array_destroy(void *data)
{
    pa_value *value = (pa_value *)data;

    MRP_LUA_ENTER;

    if (value) {
        pa_assert(value->type < 0);
        pa_xfree(value->array);
    }

    MRP_LUA_LEAVE_NOARG;
}
#endif

scripting_node *pa_scripting_node_create(struct userdata *u, mir_node *node)
{
    pa_scripting *scripting;
    lua_State *L;
    scripting_node *sn;
    char id[256];

    pa_assert(u);
    pa_assert(node);
    pa_assert(node->amname);

    pa_assert_se((scripting = u->scripting));
    pa_assert_se((L = scripting->L));

    make_id(id, sizeof(id), "%s_%d", node->amname, node->index);

    if ((sn = (scripting_node *)mrp_lua_create_object(L, NODE_CLASS, id,0))) {
        sn->userdata = u;
        sn->id = pa_xstrdup(id);
        sn->node = node;
    }

    return sn;
}

void pa_scripting_node_destroy(struct userdata *u, mir_node *node)
{
    pa_scripting *scripting;
    lua_State *L;
    scripting_node *sn;

    MRP_LUA_ENTER;

    pa_assert(u);
    pa_assert(node);

    pa_assert_se((scripting = u->scripting));
    pa_assert_se((L = scripting->L));

    if ((sn = node->scripting)) {
        mrp_lua_destroy_object(L, sn->id,0, sn);
        sn->node = NULL;
        node->scripting = NULL;
    }

    MRP_LUA_LEAVE_NOARG;
}

static int node_create(lua_State *L)
{
    MRP_LUA_ENTER;

    lua_pushnil(L);

    MRP_LUA_LEAVE(1);
}

static int node_getfield(lua_State *L)
{
    scripting_node *sn;
    mir_node *node;
    field_t fld;

    MRP_LUA_ENTER;

    fld = field_check(L, 2, NULL);
    lua_pop(L, 1);

    if (!(sn = (scripting_node *)mrp_lua_check_object(L, NODE_CLASS, 1)))
        lua_pushnil(L);
    else {
        pa_assert_se((node = sn->node));

        switch (fld) {
        case NAME:           lua_pushstring(L, node->amname);         break;
        case DESCRIPTION:    lua_pushstring(L, node->amdescr);        break;
        case DIRECTION:      lua_pushinteger(L, node->direction);     break;
        case IMPLEMENT:      lua_pushinteger(L, node->implement);     break;
        case CHANNELS:       lua_pushinteger(L, node->channels);      break;
        case LOCATION:       lua_pushinteger(L, node->location);      break;
        case PRIVACY:        lua_pushinteger(L, node->privacy);       break;
        case ZONE:           lua_pushstring(L, node->zone);           break;
        case TYPE:           lua_pushinteger(L, node->type);          break;
        case AVAILABLE:      lua_pushboolean(L, node->available);     break;
        default:             lua_pushnil(L);                          break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int node_setfield(lua_State *L)
{
    const char *f;

    MRP_LUA_ENTER;

    f = luaL_checkstring(L, 2);
    luaL_error(L, "attempt to set '%s' field of read-only node", f);

    MRP_LUA_LEAVE(0);
}

static int node_tostring(lua_State *L)
{
    scripting_node *sn;

    MRP_LUA_ENTER;

    sn = (scripting_node *)mrp_lua_check_object(L, NODE_CLASS, 1);

    lua_pushstring(L, (sn && sn->id) ? sn->id : "<unknown node>");

    MRP_LUA_LEAVE(1);
}

static void node_destroy(void *data)
{
    scripting_node *sn = (scripting_node *)data;
    mir_node *node;

    MRP_LUA_ENTER;

    if ((node = sn->node) && sn == node->scripting)
        node->scripting = NULL;

    pa_xfree((void *)sn->id);

    MRP_LUA_LEAVE_NOARG;
}


static int zone_create(lua_State *L)
{
    static uint32_t index;

    struct userdata *u;
    size_t fldnamlen;
    const char *fldnam;
    scripting_zone *zone;
    const char *name = NULL;
    /* attribute_t *attributes = NULL; */

    MRP_LUA_ENTER;

    lua_getglobal(L, USERDATA);
    if (!lua_islightuserdata(L, -1) || !(u = lua_touserdata(L, -1)))
        luaL_error(L, "missing or invalid global '" USERDATA "'");
    lua_pop(L, 1);


    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {

        switch (field_name_to_type(fldnam, fldnamlen)) {
        case NAME:         name = luaL_checkstring(L, -1);           break;
            /* case ATTRIBUTES:   attributes = attributes_check(L, -1);     break; */
        default:           luaL_error(L, "bad field '%s'", fldnam);  break;
        }

    } /* MRP_LUA_FOREACH_FIELD */

    if (!name)
        luaL_error(L, "missing or invalid name field");

    if (pa_zoneset_add_zone(u, name, index+1))
        luaL_error(L, "attempt to define zone '%s' multiple times", name);

    zone = (scripting_zone *)mrp_lua_create_object(L, ZONE_CLASS, name, 0);

    zone->userdata = u;
    zone->name = pa_xstrdup(name);
    zone->index = ++index;


    MRP_LUA_LEAVE(1);
}

static int zone_getfield(lua_State *L)
{
    scripting_zone *zone;
    field_t fld;

    MRP_LUA_ENTER;

    fld = field_check(L, 2, NULL);
    lua_pop(L, 1);

    if (!(zone = (scripting_zone *)mrp_lua_check_object(L, ZONE_CLASS, 1)))
        lua_pushnil(L);
    else {
        switch (fld) {
        case NAME:           lua_pushstring(L, zone->name);         break;
#if 0
        case ATTRIBUTES:     lua_pushinteger(L, rtgs->type);       break;
#endif
        default:             lua_pushnil(L);                       break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int zone_setfield(lua_State *L)
{
    const char *f;

    MRP_LUA_ENTER;

    f = luaL_checkstring(L, 2);
    luaL_error(L, "attempt to set '%s' field of read-only zone", f);

    MRP_LUA_LEAVE(0);
}

static void zone_destroy(void *data)
{
    scripting_zone *zone = (scripting_zone *)data;

    MRP_LUA_ENTER;

    pa_xfree((void *)zone->name);

    zone->name = NULL;

    MRP_LUA_LEAVE_NOARG;
}


static int resource_create(lua_State *L)
{
    struct userdata *u;
    size_t fldnamlen;
    const char *fldnam;
    scripting_resource *res;
    resource_name_t *name = NULL;
    attribute_t *attributes = NULL;
    attribute_t *attr;

    MRP_LUA_ENTER;

    lua_getglobal(L, USERDATA);
    if (!lua_islightuserdata(L, -1) || !(u = lua_touserdata(L, -1)))
        luaL_error(L, "missing or invalid global '" USERDATA "'");
    lua_pop(L, 1);


    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {

        switch (field_name_to_type(fldnam, fldnamlen)) {
        case NAME:         name = resource_names_check(L, -1);       break;
        case ATTRIBUTES:   attributes = attributes_check(L, -1);     break;
        default:           luaL_error(L, "bad field '%s'", fldnam);  break;
        }

    } /* MRP_LUA_FOREACH_FIELD */

    if (!name)
        luaL_error(L, "missing or invalid name field");

    pa_murphyif_add_audio_resource(u, mir_input,  name->playback);
    pa_murphyif_add_audio_resource(u, mir_output, name->recording);

    if (attributes) {
        for (attr = attributes;   attr->prop && attr->def.name;   attr++) {
            switch (attr->def.type) {
            case mqi_string:
                pa_murphyif_add_audio_attribute(u, attr->prop,
                                                attr->def.name,
                                                attr->def.type,
                                                attr->def.value.string);
                break;
            case mqi_integer:
                pa_murphyif_add_audio_attribute(u, attr->prop,
                                                attr->def.name,
                                                attr->def.type,
                                                attr->def.value.integer);
                break;
            case mqi_unsignd:
                pa_murphyif_add_audio_attribute(u, attr->prop,
                                                attr->def.name,
                                                attr->def.type,
                                                attr->def.value.unsignd);
                break;
            case mqi_floating:
                pa_murphyif_add_audio_attribute(u, attr->prop,
                                                attr->def.name,
                                                attr->def.type,
                                                attr->def.value.floating);
                break;
            default:
                luaL_error(L, "invalid audio resource attribute '%s'",
                           attr->def.name);
                break;
            }
        }
    }

    res = (scripting_resource *)mrp_lua_create_object(L, RTGROUP_CLASS,
                                                      "definition",0);

    res->userdata = u;
    res->name = name;
    res->attributes = attributes;

    MRP_LUA_LEAVE(1);
}

static int resource_getfield(lua_State *L)
{
    /* field_t fld; */

    MRP_LUA_ENTER;

    /* fld = field_check(L, 2, NULL);*/
    lua_pop(L, 1);

    if (!mrp_lua_check_object(L,RESOURCE_CLASS,1))
        lua_pushnil(L);
    else {
#if 0
        switch (fld) {
        case NAME:           lua_pushstring(L, rtg->name);         break;
        case ATTRIBUTES:     lua_pushinteger(L, rtgs->type);       break;
        default:             lua_pushnil(L);                       break;
        }
#else
        lua_pushnil(L);
#endif
    }

    MRP_LUA_LEAVE(1);
}

static int resource_setfield(lua_State *L)
{
    const char *f;

    MRP_LUA_ENTER;

    f = luaL_checkstring(L, 2);
    luaL_error(L, "attempt to set '%s' field of read-only resource_class", f);

    MRP_LUA_LEAVE(0);
}

static void resource_destroy(void *data)
{
    scripting_resource *res = (scripting_resource *)data;

    MRP_LUA_ENTER;

    resource_names_destroy(res->name);
    attributes_destroy(res->attributes);

    res->name = NULL;
    res->attributes = NULL;

    MRP_LUA_LEAVE_NOARG;
}


static int rtgroup_create(lua_State *L)
{
    struct userdata *u;
    size_t fldnamlen;
    const char *fldnam;
    mir_rtgroup *rtg;
    scripting_rtgroup *rtgs;
    const char *name = NULL;
    mir_direction type = 0;
    mrp_funcbridge_t *accept = NULL;
    mrp_funcbridge_t *compare = NULL;
    char id[256];

    MRP_LUA_ENTER;

    lua_getglobal(L, USERDATA);
    if (!lua_islightuserdata(L, -1) || !(u = lua_touserdata(L, -1)))
        luaL_error(L, "missing or invalid global '" USERDATA "'");
    lua_pop(L, 1);


    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {

        switch (field_name_to_type(fldnam, fldnamlen)) {
        case NAME:      name    = luaL_checkstring(L, -1);               break;
        case NODE_TYPE: type    = luaL_checkint(L, -1);                  break;
        case ACCEPT:    accept  = mrp_funcbridge_create_luafunc(L, -1);  break;
        case COMPARE:   compare = mrp_funcbridge_create_luafunc(L, -1);  break;
        default:        luaL_error(L, "bad field '%s'", fldnam);         break;
        }

    } /* MRP_LUA_FOREACH_FIELD */

    if (!name)
        luaL_error(L, "missing name field");
    if (type != mir_input && type != mir_output)
        luaL_error(L, "missing or invalid node_type");
    if (!accept)
        luaL_error(L, "missing or invalid accept field");
    if (!compare)
        luaL_error(L, "missing or invalid compare field");

    make_id(id,sizeof(id), "%s_%sput", name, (type == mir_input) ? "in":"out");

    rtgs = (scripting_rtgroup *)mrp_lua_create_object(L, RTGROUP_CLASS, id,0);

    rtg  = mir_router_create_rtgroup(u, type, pa_xstrdup(name),
                                     rtgroup_accept, rtgroup_compare);
    if (!rtgs || !rtg)
        luaL_error(L, "failed to create routing group '%s'", id);

    rtg->scripting = rtgs;

    rtgs->userdata = u;
    rtgs->rtg = rtg;
    rtgs->type = type;
    rtgs->accept = accept;
    rtgs->compare = compare;

    MRP_LUA_LEAVE(1);
}

static int rtgroup_getfield(lua_State *L)
{
    scripting_rtgroup *rtgs;
    mir_rtgroup *rtg;
    field_t fld;

    MRP_LUA_ENTER;

    fld = field_check(L, 2, NULL);
    lua_pop(L, 1);

    if (!(rtgs = (scripting_rtgroup *)mrp_lua_check_object(L,RTGROUP_CLASS,1)))
        lua_pushnil(L);
    else {
        pa_assert_se((rtg = rtgs->rtg));

        switch (fld) {
        case NAME:           lua_pushstring(L, rtg->name);         break;
        case NODE_TYPE:      lua_pushinteger(L, rtgs->type);       break;
        default:             lua_pushnil(L);                       break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int rtgroup_setfield(lua_State *L)
{
    const char *f;

    MRP_LUA_ENTER;

    f = luaL_checkstring(L, 2);
    luaL_error(L, "attempt to set '%s' field of read-only routing_group", f);

    MRP_LUA_LEAVE(0);
}

static int rtgroup_tostring(lua_State *L)
{
    scripting_rtgroup *rtgs;
    mir_rtgroup *rtg;

    MRP_LUA_ENTER;

    rtgs = (scripting_rtgroup *)mrp_lua_check_object(L, RTGROUP_CLASS, 1);
    pa_assert_se((rtg = rtgs->rtg));

    lua_pushstring(L, rtg->name);

    MRP_LUA_LEAVE(1);
}

static void rtgroup_destroy(void *data)
{
    scripting_rtgroup *rtgs = (scripting_rtgroup *)data;
    mir_rtgroup *rtg;

    MRP_LUA_ENTER;

    pa_assert_se((rtg = rtgs->rtg));
    pa_assert(rtgs == rtg->scripting);

    rtg->scripting = NULL;

    MRP_LUA_LEAVE_NOARG;
}


static bool rtgroup_accept(struct userdata *u,
                                mir_rtgroup *rtg,
                                mir_node *node)
{
    pa_scripting *scripting;
    lua_State *L;
    scripting_rtgroup *rtgs;
    mrp_funcbridge_value_t  args[2];
    char rt;
    mrp_funcbridge_value_t  rv;
    bool accept;

    pa_assert(u);
    pa_assert_se((scripting = u->scripting));
    pa_assert_se((L = scripting->L));
    pa_assert(rtg);
    pa_assert_se((rtgs = rtg->scripting));
    pa_assert(u == rtgs->userdata);
    pa_assert(rtgs->accept);
    pa_assert(node);

    accept = false;

    if ((rtgs = rtg->scripting) && node->scripting) {

        args[0].pointer = rtgs;
        args[1].pointer = node->scripting;

        if (!mrp_funcbridge_call_from_c(L, rtgs->accept, "oo",args, &rt,&rv)) {
            if (rt != MRP_FUNCBRIDGE_STRING)
                pa_log("call to accept function failed");
            else {
                pa_log("call to accept function failed: %s", rv.string);
                mrp_free((void *)rv.string);
            }
        }
        else {
            if (rt != MRP_FUNCBRIDGE_BOOLEAN)
                pa_log("accept function returned invalid type");
            else
                accept = rv.boolean;
        }
    }

    return accept;
}

static int rtgroup_compare(struct userdata *u,
                           mir_rtgroup *rtg,
                           mir_node *node1,
                           mir_node *node2)
{
    pa_scripting *scripting;
    lua_State *L;
    scripting_rtgroup *rtgs;
    mrp_funcbridge_value_t  args[3];
    char rt;
    mrp_funcbridge_value_t  rv;
    int result;

    pa_assert(u);
    pa_assert_se((scripting = u->scripting));
    pa_assert_se((L = scripting->L));
    pa_assert(rtg);
    pa_assert_se((rtgs = rtg->scripting));
    pa_assert(u == rtgs->userdata);
    pa_assert(rtgs->compare);
    pa_assert(node1);
    pa_assert(node2);

    result = -1;

    if ((rtgs = rtg->scripting) && node1->scripting && node2->scripting) {

        args[0].pointer = rtgs;
        args[1].pointer = node1->scripting;
        args[2].pointer = node2->scripting;

        if (!mrp_funcbridge_call_from_c(L, rtgs->compare, "ooo",args, &rt,&rv))
            pa_log("failed to call compare function");
        else {
            if (rt != MRP_FUNCBRIDGE_FLOATING)
                pa_log("compare function returned invalid type");
            else
                result = rv.floating;
        }
    }

    return result;
}


static bool accept_bridge(lua_State *L, void *data,
                          const char *signature, mrp_funcbridge_value_t *args,
                          char *ret_type, mrp_funcbridge_value_t *ret_val)
{
    mir_rtgroup_accept_t accept;
    scripting_rtgroup *rtgs;
    scripting_node *ns;
    struct userdata *u;
    mir_rtgroup *rtg;
    mir_node *node;
    bool success;

    (void)L;

    pa_assert(signature);
    pa_assert(args);
    pa_assert(ret_type);
    pa_assert(ret_val);

    pa_assert_se((accept = (mir_rtgroup_accept_t)data));

    if (strcmp(signature, "oo"))
        success = false;
    else {
        pa_assert_se((rtgs = args[0].pointer));
        pa_assert_se((u = rtgs->userdata));
        pa_assert_se((ns = args[1].pointer));

        if (!(rtg = rtgs->rtg) || !(node = ns->node))
            success = false;
        else {
            success = true;
            *ret_type = MRP_FUNCBRIDGE_BOOLEAN;
            ret_val->boolean = accept(u, rtg, node);
        }
    }

    return success;
}


static bool compare_bridge(lua_State *L, void *data,
                           const char *signature, mrp_funcbridge_value_t *args,
                           char *ret_type, mrp_funcbridge_value_t *ret_val)
{
    mir_rtgroup_compare_t compare;
    scripting_rtgroup *rtgs;
    scripting_node *ns1, *ns2;
    struct userdata *u;
    mir_rtgroup *rtg;
    mir_node *node1, *node2;
    bool success;

    (void)L;

    pa_assert(signature);
    pa_assert(args);
    pa_assert(ret_type);
    pa_assert(ret_val);

    pa_assert_se((compare = (mir_rtgroup_compare_t)data));

    if (strcmp(signature, "ooo"))
        success = false;
    else {
        pa_assert_se((rtgs = args[0].pointer));
        pa_assert_se((u = rtgs->userdata));
        pa_assert_se((ns1 = args[1].pointer));
        pa_assert_se((ns2 = args[2].pointer));


        if (!(rtg = rtgs->rtg) || !(node1 = ns1->node) || !(node2 = ns2->node))
            success = false;
        else {
            success = true;
            *ret_type = MRP_FUNCBRIDGE_FLOATING;
            ret_val->floating = compare(u, rtg, node1, node2);
        }
    }

    return success;
}


static bool change_bridge(lua_State *L, void *data,
                          const char *signature, mrp_funcbridge_value_t *args,
                          char *ret_type, mrp_funcbridge_value_t *ret_val)
{
    mir_change_value_t change;
    scripting_import *imp;
    struct userdata *u;
    bool success;
    const char *s = "default";

    (void)L;

    pa_assert(signature);
    pa_assert(args);
    pa_assert(ret_type);
    pa_assert(ret_val);

    pa_assert_se((change = (mir_change_value_t)data));

    if (strcmp(signature, "o"))
        success = false;
    else {
        pa_assert_se((imp = args[0].pointer));
        pa_assert_se((u = imp->userdata));

        /* FIXME: is this how it is supposed to be done?! */

        if (imp->values && imp->values->array && imp->values->array[0] &&
              imp->values->array[0]->array &&
              imp->values->array[0]->array[0] &&
              imp->values->array[0]->array[0]->type == pa_value_string)
            pa_assert_se((s = imp->values->array[0]->array[0]->string));

        success = true;
        *ret_type = MRP_FUNCBRIDGE_NO_DATA;
        memset(ret_val, 0, sizeof(mrp_funcbridge_value_t));
        change(u, s);
    }

    return success;
}


static int apclass_create(lua_State *L)
{
    struct userdata *u;
    size_t fldnamlen;
    const char *fldnam;
    scripting_apclass *ac;
    char name[256];
    const char *class = NULL;
    mir_node_type type = -1;
    int priority = -1;
    route_t *route = NULL;
    map_t *roles = NULL;
    map_t *binaries = NULL;
    pa_nodeset_resdef *resdef;
    map_t *r, *b;
    size_t i;
    const char *n;
    bool ir, or;

    MRP_LUA_ENTER;

    lua_getglobal(L, USERDATA);
    if (!lua_islightuserdata(L, -1) || !(u = lua_touserdata(L, -1)))
        luaL_error(L, "missing or invalid global '" USERDATA "'");
    lua_pop(L, 1);


    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {

        switch (field_name_to_type(fldnam, fldnamlen)) {
        case CLASS:       class = luaL_checkstring(L, -1);             break;
        case NODE_TYPE:   type = luaL_checkint(L, -1);                 break;
        case PRIORITY:    priority = luaL_checkint(L, -1);             break;
        case ROUTE:       route = route_check(L, -1);                  break;
        case ROLES:       roles = map_check(L, -1);                    break;
        case BINARIES:    binaries = map_check(L, -1);                 break;
        default:          luaL_error(L, "bad field '%s'", fldnam);     break;
        }

    } /* MRP_LUA_FOREACH_FIELD */

    if (type < mir_application_class_begin ||
        type >= mir_application_class_end    )
        luaL_error(L, "missing or invalid node_type %d", type);
    if (priority < 0)
        luaL_error(L, "missing or invalid priority field");
    if (!route)
        luaL_error(L, "missing or invalid route field");
    if (!roles && !binaries)
        luaL_error(L, "missing roles or binaries");

    make_id(name, sizeof(name), "%s", mir_node_type_str(type));

    mir_router_assign_class_priority(u, type, priority);

    ir = or = true;

    if (route->input) {
        for (i = 0;  i < MRP_ZONE_MAX;  i++) {
            if ((n = route->input[i]))
                ir &= mir_router_assign_class_to_rtgroup(u,type,i,mir_input,n);
        }
    }

    if (route->output) {
        for (i = 0;  i < MRP_ZONE_MAX;  i++) {
            if ((n = route->output[i]))
                or &= mir_router_assign_class_to_rtgroup(u,type,i,mir_output,n);
        }
    }

    ac = (scripting_apclass *)mrp_lua_create_object(L, APPLICATION_CLASS,
                                                    name, 0);

    if (!ir || !or || !ac)
        luaL_error(L, "failed to create application class '%s'", name);

    ac->userdata = u;
    ac->name = pa_xstrdup(name);
    ac->class = class ? pa_xstrdup(class) : NULL;
    ac->type = type;
    ac->priority = priority;
    ac->route = route;
    ac->roles = roles;
    ac->binaries = binaries;

    if (class) {
        if (pa_nodeset_add_class(u, type, class)) {
            luaL_error(L, "node type '%s' is defined multiple times",
                       mir_node_type_str(type));
        }
    }

    if (roles) {
        for (r = roles;  r->name;  r++) {
            resdef = r->needres ? &r->resource : NULL;

            if (r->role && strcmp(r->role, r->name)) {
                luaL_error(L, "conflicting roles in role definition '%s' (%s)",
                           r->name, r->role);
            }

            if (pa_nodeset_add_role(u, r->name, type, resdef)) {
                luaL_error(L, "role '%s' is added to mutiple application "
                           "classes", r->name);
            }
        }
    }

    if (binaries) {
        for (b = binaries;  b->name;  b++) {
            resdef = b->needres ? &b->resource : NULL;

            if (pa_nodeset_add_binary(u, b->name, type, b->role, resdef)) {
                luaL_error(L, "binary '%s' is added to multiple application "
                           "classes", b->name);
            }
        }
    }

    MRP_LUA_LEAVE(1);
}

static int apclass_getfield(lua_State *L)
{
    scripting_apclass *ac;
    field_t fld;

    MRP_LUA_ENTER;

    fld = field_check(L, 2, NULL);
    lua_pop(L, 1);

    if (!(ac=(scripting_apclass *)mrp_lua_check_object(L,APPLICATION_CLASS,1)))
        lua_pushnil(L);
    else {
        switch (fld) {
        case NAME:           lua_pushstring(L, ac->name);          break;
        case NODE_TYPE:      lua_pushinteger(L, ac->type);         break;
        case PRIORITY:       lua_pushinteger(L, ac->priority);     break;
        case ROUTE:          route_push(L, ac->route);             break;
        case ROLES:          map_push(L, ac->roles);               break;
        case BINARIES:       map_push(L, ac->binaries);            break;
        default:             lua_pushnil(L);                       break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int apclass_setfield(lua_State *L)
{
    const char *f;

    MRP_LUA_ENTER;

    f = luaL_checkstring(L, 2);
    luaL_error(L,"attempt to set '%s' field of read-only application class",f);

    MRP_LUA_LEAVE(0);
}

static int apclass_tostring(lua_State *L)
{
    scripting_apclass *ac;

    MRP_LUA_ENTER;

    ac = (scripting_apclass *)mrp_lua_check_object(L, APPLICATION_CLASS, 1);

    lua_pushstring(L, ac->name);

    MRP_LUA_LEAVE(1);
}

static void apclass_destroy(void *data)
{
    scripting_apclass *ac = (scripting_apclass *)data;
    struct userdata *u;
    map_t *r, *b;

    MRP_LUA_ENTER;

    pa_assert(ac);
    pa_assert_se((u = ac->userdata));

    route_destroy(ac->route);
    ac->route = NULL;

    pa_xfree((void *)ac->name);
    ac->name = NULL;

    pa_nodeset_delete_class(u, ac->type);
    pa_xfree((void *)ac->class);
    ac->class = NULL;

    if (ac->roles) {
        for (r = ac->roles;  r->name;  r++)
            pa_nodeset_delete_role(u, r->name);

        map_destroy(ac->roles);
        ac->roles = NULL;
    }

    if (ac->binaries) {
        for (b = ac->binaries;  b->name;  b++)
            pa_nodeset_delete_binary(u, b->name);

        map_destroy(ac->binaries);
        ac->binaries = NULL;
    }

    MRP_LUA_LEAVE_NOARG;
}

static const char **route_definition_check(lua_State *L, int idx)
{
    size_t zonelen;
    const char *zonenam;
    scripting_zone *zone;
    scripting_rtgroup *rtgs;
    mir_rtgroup *rtg;
    const char **defs;
    const char *rtgnam;
    int ndef;

    idx = (idx < 0) ? lua_gettop(L) + idx + 1 : idx;

    luaL_checktype(L, idx, LUA_TTABLE);

    defs = pa_xnew0(const char *, MRP_ZONE_MAX);
    ndef = 0;

    MRP_LUA_FOREACH_FIELD(L, idx, zonenam, zonelen) {
        if (!zonenam[0])
            luaL_error(L, "invalid route definition");

        mrp_lua_find_object(L, ZONE_CLASS, zonenam);

        if (!(zone = mrp_lua_check_object(L, NULL, -1)))
            luaL_error(L, "can't find zone '%s'", zonenam);

        lua_pop(L, 1);

        if (zone->index >= MRP_ZONE_MAX)
            luaL_error(L, "Internal error: zone index overflow");

        switch (lua_type(L, -1)) {
        case LUA_TSTRING:
            rtgnam = lua_tostring(L, -1);
            break;
        case LUA_TTABLE:
            rtgs = (scripting_rtgroup*)mrp_lua_check_object(L,RTGROUP_CLASS,-1);
            if (!rtgs || !(rtg = rtgs->rtg))
                rtgnam = NULL;
            else
                rtgnam = rtg->name;
            break;
        default:
            rtgnam = NULL;
            break;
        }

        if (!rtgnam)
            luaL_error(L, "missing or invalid routing group");

        defs[zone->index] = pa_xstrdup(rtgnam);
        ndef++;
    }

    if (!ndef)
        luaL_error(L, "empty definition");

    return defs;
}

static int route_definition_push(lua_State *L, const char **defs)
{
    int i;

    lua_createtable(L, MRP_ZONE_MAX, 0);

    for (i = 0;  i < MRP_ZONE_MAX;  i++) {
    }

    return 1;
}


#if 0
static void route_definition_free(const char **defs)
{
    int i;

    if (defs) {
        for (i = 0;  i < MRP_ZONE_MAX;  i++)
            pa_xfree((void *)defs[i]);
        pa_xfree((void *)defs);
    }
}
#endif

static route_t *route_check(lua_State *L, int idx)
{
    size_t fldnamlen;
    const char *fldnam;
    route_t *rt;
    const char **input = NULL;
    const char **output = NULL;

    idx = (idx < 0) ? lua_gettop(L) + idx + 1 : idx;

    luaL_checktype(L, idx, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, idx, fldnam, fldnamlen) {
        switch (field_name_to_type(fldnam, fldnamlen)) {
        case        INPUT:  input  = route_definition_check(L, -1);      break;
        case        OUTPUT: output = route_definition_check(L, -1);      break;
        default:    luaL_error(L, "invalid field '%s'", fldnam);         break;
        }
    } /* MRP_LUA_FOREACH_FIELD */

    if (!input && !output)
        luaL_error(L, "neither input nor output routing group were specified");

    rt = pa_xmalloc(sizeof(route_t));
    rt->input  = input;
    rt->output = output;

    return rt;
}

static int route_push(lua_State *L, route_t *rt)
{
    if (!rt || (!rt->input && !rt->output))
        lua_pushnil(L);
    else {
        lua_createtable(L, 0, 2);

        if (rt->input) {
            lua_pushstring(L, "input");
            route_definition_push(L, rt->input);
            lua_settable(L, -3);
        }

        if (rt->output) {
            lua_pushstring(L, "output");
            route_definition_push(L, rt->output);
            lua_settable(L, -3);
        }
    }

    return 1;
}

static void route_destroy(route_t *rt)
{
    if (rt) {
        pa_xfree((void *)rt->input);
        pa_xfree((void *)rt->output);
        pa_xfree((void *)rt);
    }
}


static int vollim_create(lua_State *L)
{
    static int min = mir_application_class_begin;
    static int max = mir_application_class_end;

    struct userdata *u;
    size_t fldnamlen;
    const char *fldnam;
    scripting_vollim *vlim;
    const char *name = NULL;
    vollim_type type = 0;
    limit_data_t *limit = NULL;
    mrp_funcbridge_t *calculate = NULL;
    intarray_t *classes = NULL;
    bool suppress = false;
    bool correct = false;
    size_t arglgh = 0;
    size_t i;
    int class;
    uint32_t mask, clmask;
    char id[256];

    MRP_LUA_ENTER;

    lua_getglobal(L, USERDATA);
    if (!lua_islightuserdata(L, -1) || !(u = lua_touserdata(L, -1)))
        luaL_error(L, "missing or invalid global '" USERDATA "'");
    lua_pop(L, 1);


    MRP_LUA_FOREACH_FIELD(L, 2, fldnam, fldnamlen) {

        switch (field_name_to_type(fldnam, fldnamlen)) {
        case NAME:      name      = luaL_checkstring(L, -1);             break;
        case TYPE:      type      = luaL_checkint(L, -1);                break;
        case NODE_TYPE: classes = intarray_check(L, -1, min, max);       break;
        case LIMIT:     limit     = limit_data_check(L, -1);             break;
        case CALCULATE: calculate = mrp_funcbridge_create_luafunc(L,-1); break;
        default:        luaL_error(L, "bad field '%s'", fldnam);         break;
        }

    } /* MRP_LUA_FOREACH_FIELD */

    if (!name)
        luaL_error(L, "missing name field");
    if (type != vollim_class && type != vollim_generic && type != vollim_maximum)
        luaL_error(L, "missing or invalid type");
    if ((type == vollim_class || type == vollim_maximum) && !classes)
        luaL_error(L, "missing or invalid node_type for class/maximum limit");
    if (type == vollim_generic && classes)
        luaL_error(L, "can't specify node_type for generic volume limit");
    if (!limit)
        luaL_error(L, "missing or invalid limit");
    if (type != vollim_maximum && !calculate)
        luaL_error(L, "missing calculate field");
    if (type != vollim_maximum) {
        if (calculate->type == MRP_C_FUNCTION) {
            if (strcmp(calculate->c.signature, "odo"))
                luaL_error(L,"invalid calculate field (mismatching signature)");
            if (calculate->c.data == mir_volume_suppress) {
                if (type != vollim_class)
                    luaL_error(L, "attempt to make generic volume supression");
                suppress = true;
                arglgh = sizeof(mir_volume_suppress_arg);
            }
            else if (calculate->c.data == mir_volume_correction) {
                if (type != vollim_generic) {
                    luaL_error(L, "attempt to make class based volume"
                               "correction");
                }
                correct = true;
                arglgh = sizeof(double *);
            }
            else {
                luaL_error(L, "invalid builtin.method for calculate");
            }
        }
        else {
            switch (type) {
            case vollim_class:
                suppress = true;
                arglgh = sizeof(mir_volume_suppress_arg);
                break;
            case vollim_generic:
                correct = true;
                arglgh = sizeof(double *);
                break;
            default:
                break;
            }
        }
    }

    make_id(id, sizeof(id), "%s", name);

    (VOLLIM_CLASS)->userdata_size = sizeof(scripting_vollim) + arglgh;
    vlim = (scripting_vollim *)mrp_lua_create_object(L, VOLLIM_CLASS, id,0);
    (VOLLIM_CLASS)->userdata_size = sizeof(scripting_vollim);

    vlim->userdata = u;
    vlim->name = pa_xstrdup(name);
    vlim->type = type;
    vlim->classes = classes;
    vlim->limit = limit;
    vlim->calculate = calculate;

    if (suppress) {
        mir_volume_suppress_arg *args = (mir_volume_suppress_arg *)(void *)vlim->args;
        size_t size = sizeof(int) * classes->nint;
        size_t n = mir_application_class_end - mir_application_class_begin;

        for (i = 0, clmask = 0;   i < classes->nint;   i++) {
            class = classes->ints[i];

            if (class <= mir_application_class_begin ||
                class >  mir_application_class_end     )
            {
                pa_log("invalid triggering class id %d", class);
                clmask = 0;
                classes->nint = n = 0;
                break;
            }

            mask = ((uint32_t)1) << (class - mir_application_class_begin);

            if (!(clmask & mask) && n > 0)
                n--;

            clmask |= mask;
        }

        args->attenuation = limit->value;
        args->trigger.nclass = classes->nint;
        args->trigger.classes = pa_xmalloc(size);
        args->trigger.clmask = clmask;

        memcpy(args->trigger.classes, classes->ints, size);

        if (n > classes->nint)
            classes->ints = pa_xrealloc(classes->ints, sizeof(int) * n);
        classes->nint = n;

        for (i = mir_application_class_begin, n = 0;
             i < mir_application_class_end;
             i++)
        {
            if (!(clmask & (((uint32_t)1) << (i-mir_application_class_begin))))
                classes->ints[n++] = i;
        }
    }
    else if (correct) {
        /* *(double **)vlim->args = limit->value; */

        memcpy(vlim->args, &limit->value, sizeof(limit->value));
    }

    switch (type) {
    case vollim_generic:
        mir_volume_add_generic_limit(u, vollim_calculate, vlim->args);
        break;
    case vollim_class:
        for (i = 0;  i < classes->nint;  i++) {
            mir_volume_add_class_limit(u, classes->ints[i], vollim_calculate,
                                       vlim->args);
        }
        break;
    case vollim_maximum:
        mir_volume_add_maximum_limit(u, *(vlim->limit->value),
                                     classes->nint, classes->ints);
        break;
    default:
        break;
    }

    MRP_LUA_LEAVE(1);
}

static int vollim_getfield(lua_State *L)
{
    scripting_vollim *vlim;
    field_t fld;

    MRP_LUA_ENTER;

    fld = field_check(L, 2, NULL);
    lua_pop(L, 1);

    if (!(vlim = (scripting_vollim *)mrp_lua_check_object(L, VOLLIM_CLASS, 1)))
        lua_pushnil(L);
    else {
        switch (fld) {
        case NAME:         lua_pushstring(L, vlim->name);           break;
        case TYPE:         lua_pushinteger(L, vlim->type);          break;
        case NODE_TYPE:    intarray_push(L, vlim->classes);         break;
        case LIMIT:        lua_pushnumber(L, *vlim->limit->value);  break;
        default:           lua_pushnil(L);                          break;
        }
    }

    MRP_LUA_LEAVE(1);
}

static int vollim_setfield(lua_State *L)
{
    const char *f;

    MRP_LUA_ENTER;

    f = luaL_checkstring(L, 2);
    luaL_error(L, "attempt to set '%s' field of read-only volume_limit", f);

    MRP_LUA_LEAVE(0);
}

static int vollim_tostring(lua_State *L)
{
    scripting_vollim *vlim;

    MRP_LUA_ENTER;

    vlim = (scripting_vollim *)mrp_lua_check_object(L, VOLLIM_CLASS, 1);

    lua_pushstring(L, vlim->name);

    MRP_LUA_LEAVE(1);
}

static void vollim_destroy(void *data)
{
    scripting_vollim *vlim = (scripting_vollim *)data;

    MRP_LUA_ENTER;

    pa_xfree((void *)vlim->name);
    intarray_destroy(vlim->classes);
    limit_data_destroy(vlim->limit);

    MRP_LUA_LEAVE_NOARG;
}


static double vollim_calculate(struct userdata *u, int class, mir_node *node,
                               void *data)
{
    static int offset = ((scripting_vollim *)0)->args - (char *)0;

    pa_scripting *scripting;
    lua_State *L;
    scripting_vollim *vlim;
    mrp_funcbridge_value_t args[3];
    char rt;
    mrp_funcbridge_value_t  rv;
    double limit;

    pa_assert(u);
    pa_assert_se((scripting = u->scripting));
    pa_assert_se((L = scripting->L));
    pa_assert(!class || (class >= mir_application_class_begin &&
                         class <  mir_application_class_end)     );
    pa_assert(node);

    vlim = (scripting_vollim *)(void*)((char *)data - offset);

    pa_assert(u == vlim->userdata);

    limit = -90.0;

    if (node->scripting) {

        args[0].pointer = vlim;
        args[1].integer = class;
        args[2].pointer = node->scripting;

        if (!mrp_funcbridge_call_from_c(L,vlim->calculate,"odo",args,&rt,&rv))
            pa_log("failed to call calculate function");
        else {
            if (rt != MRP_FUNCBRIDGE_FLOATING)
                pa_log("accept function returned invalid type");
            else
                limit = rv.floating;
        }
    }

    return limit;
}

static bool calculate_bridge(lua_State *L, void *data, const char *signature,
                             mrp_funcbridge_value_t *args,
                             char *ret_type, mrp_funcbridge_value_t *ret_val)
{
    mir_volume_func_t calculate;
    scripting_vollim *vlim;
    scripting_node *ns;
    struct userdata *u;
    int class;
    mir_node *node;
    bool success;

    (void)L;

    pa_assert(signature);
    pa_assert(args);
    pa_assert(ret_type);
    pa_assert(ret_val);

    pa_assert_se((calculate = (mir_volume_func_t)data));

    if (strcmp(signature, "odo"))
        success = false;
    else {
        pa_assert_se((vlim = args[0].pointer));
        pa_assert_se((u = vlim->userdata));
        pa_assert_se((ns = args[2].pointer));

        class = args[1].integer;

        pa_assert(!class || (class >= mir_application_class_begin &&
                             class <  mir_application_class_end));

        if (!(node = ns->node))
            success = false;
        else {
            success = true;
            *ret_type = MRP_FUNCBRIDGE_FLOATING;
            /* TODO: check the vollim type vs. c function */
            ret_val->floating = calculate(u, class, node, vlim->args);
        }
    }

    return success;
}

static limit_data_t *limit_data_check(lua_State *L, int idx)
{
    static double nolimit = 0.0;

    limit_data_t *ld = NULL;
    double value;
    pa_value *v;

    switch (lua_type(L, idx)) {
    case LUA_TNUMBER:
        if ((value = lua_tonumber(L, idx)) > 0.0)
            luaL_error(L, "volume limit is in dB and can't be positive");
        else {
            ld = pa_xnew0(limit_data_t, 1);
            ld->mallocd = true;
            ld->value = pa_xnew0(double, 1);
            *ld->value = value;
        }
        break;
    case LUA_TLIGHTUSERDATA:
        if (!(v = lua_touserdata(L, idx)) || v->type < 0)
            luaL_error(L, "broken link for volume limit value");
        else {
            ld = pa_xnew0(limit_data_t, 1);
            ld->mallocd = false;
            ld->value = &v->floating;
        }
        break;
    default:
        ld->mallocd = false;
        ld->value = &nolimit;
        break;
    }

    return ld;
}

#if 0
static int limit_data_push(lua_State *L, limit_data_t *ld)
{
    if (ld)
        lua_pushnumber(L, *ld->value);
    else
        lua_pushnil;

    return 1;
}
#endif

static void limit_data_destroy(limit_data_t *ld)
{
    if (ld) {
        if (ld->mallocd)
            pa_xfree(ld->value);
        pa_xfree(ld);
    }
}


static intarray_t *intarray_check(lua_State *L, int idx, int min, int max)
{
    size_t len;
    size_t size;
    intarray_t *arr;
    int val;
    size_t i;

    idx = (idx < 0) ? lua_gettop(L) + idx + 1 : idx;

    luaL_checktype(L, idx, LUA_TTABLE);

    if ((len = luaL_getn(L, idx)) < 1)
        arr = NULL;
    else {
        size = sizeof(intarray_t) + sizeof(int) * len;
        arr  = pa_xmalloc0(sizeof(intarray_t));

        arr->nint = len;
        arr->ints = pa_xmalloc0(size);

        for (i = 0;  i < len;  i++) {
            lua_pushnumber(L, (int)(i+1));
            lua_gettable(L, idx);

            val = luaL_checkint(L, -1);

            lua_pop(L, 1);

            if (val < min || val >= max)
                luaL_error(L, "array [%u]: out of range value (%d)", i, val);

            arr->ints[i] = val;
        }
    }

    return arr;
}

static int intarray_push(lua_State *L, intarray_t *arr)
{
    size_t i;

    if (!arr)
        lua_pushnil(L);
    else {
        lua_createtable(L, arr->nint, 0);

        for (i = 0;  i < arr->nint;  i++) {
            lua_pushinteger(L, (int)(i+1));
            lua_pushinteger(L, arr->ints[i]);
            lua_settable(L, -3);
        }
    }

    return 1;
}

static void intarray_destroy(intarray_t *arr)
{
    if (arr) {
        pa_xfree(arr->ints);
        pa_xfree(arr);
    }
}

static resource_name_t *resource_names_check(lua_State *L, int tbl)
{
    resource_name_t *name;
    size_t fldnamlen;
    const char *fldnam;
    const char *value;

    tbl = (tbl < 0) ? lua_gettop(L) + tbl + 1 : tbl;

    luaL_checktype(L, tbl, LUA_TTABLE);

    name = pa_xnew0(resource_name_t, 1);

    MRP_LUA_FOREACH_FIELD(L, tbl, fldnam, fldnamlen) {
        value = luaL_checkstring(L, -1);

        if (!strcmp(fldnam, "recording"))
            name->recording = pa_xstrdup(value);
        else if (!strcmp(fldnam, "playback"))
            name->playback = pa_xstrdup(value);
        else {
            luaL_error(L, "invalid field '%s' in resource name definition",
                       fldnam);
        }
    }

    return name;
}

static void resource_names_destroy(resource_name_t *name)
{
    if (name) {
        pa_xfree((void *)name->recording);
        pa_xfree((void *)name->playback);
        pa_xfree(name);
    }
}

static attribute_t *attributes_check(lua_State *L, int tbl)
{
    int def;
    size_t fldnamlen;
    const char *fldnam;
    attribute_t *attr, *attrs = NULL;
    size_t nattr = 0;
    mrp_attr_value_t *v;
    size_t i, len;

    tbl = (tbl < 0) ? lua_gettop(L) + tbl + 1 : tbl;

    luaL_checktype(L, tbl, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, tbl, fldnam, fldnamlen) {
        if (!fldnam[0])
            luaL_error(L, "invalid attribute definition");

        attrs = pa_xrealloc(attrs, sizeof(attribute_t) * (nattr + 2));
        memset(attrs + nattr, 0, sizeof(attribute_t) * 2);

        attr = attrs + nattr++;
        v = &attr->def.value;
        def = lua_gettop(L);

        attr->def.name = pa_xstrdup(fldnam);

        if ((len = luaL_getn(L, def)) != 3)
            luaL_error(L, "invalid attribute definition '%s'", fldnam);

        for (i = 0;  i < len;  i++) {
            lua_pushnumber(L, (int)(i+1));
            lua_gettable(L, def);

            switch (i) {
            case 0:  attr->prop = pa_xstrdup(luaL_checkstring(L,-1));    break;
            case 1:  attr->def.type = luaL_checkint(L,-1);               break;
            case 2:
                switch (attr->def.type) {
                case mqi_string:   v->string = luaL_checkstring(L,-1);   break;
                case mqi_integer:  v->integer = luaL_checkint(L,-1);     break;
                case mqi_unsignd:  v->integer = luaL_checkint(L,-1);     break;
                case mqi_floating: v->floating = luaL_checknumber(L,-1); break;
                default:           memset(v, 0, sizeof(*v));             break;
                }
            }

            lua_pop(L, 1);
        }

        if (!attr->prop)
            luaL_error(L, "missing property name definition from '%s'",fldnam);
        if (attr->def.type != mqi_string  && attr->def.type != mqi_integer &&
            attr->def.type != mqi_unsignd && attr->def.type != mqi_floating)
        {
            luaL_error(L, "invalid attribute type %d for '%s'",
                       attr->def.type, fldnam);
        }
        if (attr->def.type == mqi_unsignd && attr->def.value.integer < 0) {
            luaL_error(L, "attempt to give negative value (%d) for field '%s'",
                       attr->def.value.integer, fldnam);
        }
    }

    return attrs;
}

static void attributes_destroy(attribute_t *attrs)
{
    attribute_t *attr;

    if (attrs) {
        for (attr = attrs;  attr->prop && attr->def.name;  attr++) {
            pa_xfree((void *)attr->prop);
            pa_xfree((void *)attr->def.name);
            if (attr->def.type == mqi_string)
                pa_xfree((void *)attr->def.value.string);
        }
        pa_xfree(attrs);
    }
}

static map_t *map_check(lua_State *L, int tbl)
{
    int def;
    size_t namlen;
    const char *name;
    const char *option;
    map_t *m, *map = NULL;
    size_t n = 0;
    size_t i, len;
    int priority;
    pa_nodeset_resdef *rd;

    tbl = (tbl < 0) ? lua_gettop(L) + tbl + 1 : tbl;

    luaL_checktype(L, tbl, LUA_TTABLE);

    MRP_LUA_FOREACH_FIELD(L, tbl, name, namlen) {
        if (!name[0])
            luaL_error(L, "invalid role or binary definition");

        map = pa_xrealloc(map, sizeof(map_t) * (n + 2));
        memset(map + n, 0, sizeof(map_t) * 2);

        m = map + n++;
        def = lua_gettop(L);

        m->name = pa_xstrdup(name);

        switch (lua_type(L, -1)) {

        case LUA_TNUMBER:
            m->needres = false;
            break;

        case LUA_TSTRING:
            m->needres = false;
            m->role = mrp_strdup(lua_tostring(L, def));
            break;

        case LUA_TTABLE:
            m->needres = true;

            if ((len = luaL_getn(L, def)) < 1)
                luaL_error(L, "invalid resource definition '%s'", name);

            for (i = 1;  i < len+1;  i++) {
                lua_pushnumber(L, (int)i);
                lua_gettable(L, def);

                if (i == 1) {
                    priority = luaL_checkint(L, -1);

                    if (priority < 0 || priority > 7) {
                        luaL_error(L, "invalid priority %d for '%s'",
                                   priority, name);
                    }

                    m->resource.priority = priority;
                }
                else {
                    option = luaL_checkstring(L, -1);
                    rd = &m->resource;

                    if (pa_streq(option, "autorelease"))
                        rd->flags.rset |= RESPROTO_RSETFLAG_AUTORELEASE;
                    else if (pa_streq(option, "mandatory"))
                        rd->flags.audio |= RESPROTO_RESFLAG_MANDATORY;
                    else if (pa_streq(option, "shared"))
                        rd->flags.audio |= RESPROTO_RESFLAG_SHARED;
                    else if (!pa_streq(option, "optional") &&
                             !pa_streq(option, "exclusive") )
                    {
                        if (!m->role)
                            m->role = pa_xstrdup(option);
                        else {
                            luaL_error(L, "multiple role definition '%s','%s'",
                                       m->role, option);
                        }
                    }
                }

                lua_pop(L, 1);
            }

            break;

        default:
            luaL_error(L, "invalid resource specification. "
                       "Should be either 'no_resource' or a table");
            break;
        }
    } /* FOREACH_FIELD */

    return map;
}

static int map_push(lua_State *L, map_t *map)
{
    map_t *m;

    if (!map)
        lua_pushnil(L);
    else {
        lua_newtable(L);

        for (m = map;  m->name;  m++) {
            if (!m->needres) {
                if (m->role)
                    lua_pushstring(L, m->role);
                else
                    lua_pushnumber(L, 0);
            }
            else {
                lua_newtable(L);
                lua_pushinteger(L, m->resource.priority);
                if (m->role)
                    lua_pushstring(L, m->role);
                if (m->resource.flags.rset & RESPROTO_RSETFLAG_AUTORELEASE)
                    lua_pushstring(L, "autorelease");
                if (m->resource.flags.audio & RESPROTO_RESFLAG_MANDATORY)
                    lua_pushstring(L, "mandatory");
                else
                    lua_pushstring(L, "optional");
                if (m->resource.flags.audio & RESPROTO_RESFLAG_SHARED)
                    lua_pushstring(L, "shared");
                else
                    lua_pushstring(L, "exclusive");
            }
            lua_setfield(L, -2, m->name);
        }
    }

    return 1;
}

static void map_destroy(map_t *map)
{
    map_t *m;

    if (map) {
        for (m = map;  m->name;  m++) {
            pa_xfree((void *)m->name);
            pa_xfree((void *)m->role);
        }
        pa_xfree(map);
    }
}


static field_t field_check(lua_State *L, int idx, const char **ret_fldnam)
{
    const char *fldnam;
    size_t fldnamlen;
    field_t fldtyp;

    if (!(fldnam = lua_tolstring(L, idx, &fldnamlen)))
        fldtyp = 0;
    else
        fldtyp = field_name_to_type(fldnam, fldnamlen);

    if (ret_fldnam)
        *ret_fldnam = fldnam;

    return fldtyp;
}

static field_t field_name_to_type(const char *name, size_t len)
{
    switch (len) {

    case 4:
        switch (name[0]) {
        case 'n':
            if (!strcmp(name, "name"))
                return NAME;
            break;
        case 't':
            if (!strcmp(name, "type"))
                return TYPE;
            break;
        case 'z':
            if (!strcmp(name, "zone"))
                return ZONE;
            break;
        default:
            break;
        }
        break;

    case 5:
        switch (name[0]) {
        case 'c':
            if (!strcmp(name, "class"))
                return CLASS;
            break;
        case 'i':
            if (!strcmp(name, "input"))
                return INPUT;
            break;
        case 'l':
            if (!strcmp(name, "limit"))
                return LIMIT;
            break;
        case 'r':
            if (!strcmp(name, "route"))
                return ROUTE;
            if (!strcmp(name, "roles"))
                return ROLES;
            break;
        case 't':
            if (!strcmp(name, "table"))
                return TABLE;
            break;
        default:
            break;
        }
        break;

    case 6:
        switch (name[0]) {
        case 'a':
            if (!strcmp(name, "accept"))
                return ACCEPT;
            break;
        case 'm':
            if (!strcmp(name, "maxrow"))
                return MAXROW;
            break;
        case 'o':
            if (!strcmp(name, "output"))
                return OUTPUT;
            break;
        case 't':
            if (!strcmp(name, "tables"))
                return TABLES;
            break;
        case 'u':
            if (!strcmp(name, "update"))
                return UPDATE;
            break;
        default:
            break;
        }
        break;

    case 7:
        switch (name[0]) {
        case 'c':
            if (!strcmp(name, "compare"))
                return COMPARE;
            if (!strcmp(name, "columns"))
                return COLUMNS;
            break;
        case 'p':
            if (!strcmp(name, "privacy"))
                return PRIVACY;
            break;
        default:
            break;
        }
        break;

    case 8:
        switch (name[0]) {
        case 'b':
            if (!strcmp(name, "binaries"))
                return BINARIES;
            break;
        case 'c':
            if (!strcmp(name, "channels"))
                return CHANNELS;
            break;
        case 'l':
            if (!strcmp(name, "location"))
                return LOCATION;
            break;
        case 'p':
            if (!strcmp(name, "priority"))
                return PRIORITY;
            break;
        default:
            break;
        }
        break;

    case 9:
        switch (name[0]) {
        case 'a':
            if (!strcmp(name, "available"))
                return AVAILABLE;
            break;
        case 'c':
            if (!strcmp(name, "calculate"))
                return CALCULATE;
            if (!strcmp(name, "condition"))
                return CONDITION;
            break;
        case 'd':
            if (!strcmp(name, "direction"))
                return DIRECTION;
            break;
        case 'i':
            if (!strcmp(name, "implement"))
                return IMPLEMENT;
            break;
        case 'n':
            if (!strcmp(name, "node_type"))
                return NODE_TYPE;
            break;
        default:
            break;
        }
        break;

    case 10:
        if (!strcmp(name, "attributes"))
            return ATTRIBUTES;
        break;

    case 11:
        if (!strcmp(name, "autorelease"))
            return AUTORELEASE;
        if (!strcmp(name, "description"))
            return DESCRIPTION;
        break;

    default:
        break;
    }

    return 0;
}

static int make_id(char *buf, size_t len, const char *fmt, ...)
{
    va_list ap;
    int l;
    char *p, c;

    va_start(ap, fmt);
    l = vsnprintf(buf, len, fmt, ap);
    va_end(ap);

    for (p = buf;  (c = *p);  p++) {
        if (isalpha(c))
            c = tolower(c);
        else if (!isdigit(c))
            c = '_';
        *p = c;
    }

    return l;
}


static void setup_murphy_interface(struct userdata *u)
{
    pa_scripting *scripting;
    scripting_import *imp;
    const char *key;
    lua_State *L;
    int class;
    bool need_domainctl;
    char buf[8192];
    const char *columns;
    int top;
    pa_value *values;

    MRP_LUA_ENTER;

    pa_assert(u);
    pa_assert_se((scripting = u->scripting));
    pa_assert_se((L = scripting->L));

    top = lua_gettop(L);

    mrp_lua_get_class_table(L, IMPORT_CLASS);
    class = lua_gettop(L);

    if (!lua_istable(L, class)){
        luaL_error(L, "internal error: failed to find '%s' table",
                   (IMPORT_CLASS)->constructor);
    }

    need_domainctl = false;

    lua_pushnil(L);
    while (lua_next(L, class)) {
        if (lua_isstring(L, -2)) {
            if ((imp = mrp_lua_to_object(L, IMPORT_CLASS, -1))) {
                key = lua_tostring(L, -2);

                pa_assert(!strcmp(key, imp->table));
                pa_assert_se((values = imp->values));

                pa_log_debug("adding import '%s'", imp->table);

                need_domainctl = true;
                columns = comma_separated_list(imp->columns, buf,sizeof(buf));

                pa_murphyif_add_watch(u, imp->table, columns, imp->condition,
                                      -values->type);
            }
        }
        lua_pop(L, 1);
    }

    if (need_domainctl)
        pa_murphyif_setup_domainctl(u, import_data_changed);

    lua_settop(L, top);

    MRP_LUA_LEAVE_NOARG;
}


static char *comma_separated_list(mrp_lua_strarray_t *arr, char *buf, int len)
{
    char *p, *e;
    size_t i;

    pa_assert(arr);
    pa_assert(buf);
    pa_assert(len > 0);

    for (i = 0, e = (p = buf) + len;   i < arr->nstring && p < e;    i++)
        p += snprintf(p, e-p, "%s%s", (p == buf ? "" : ","), arr->strings[i]);

    return (p < e) ? buf : NULL;
}


static bool define_constants(lua_State *L)
{
    static const_def_t mdb_const[] = {
        { "string"           , mqi_string           },
        { "integer"          , mqi_integer          },
        { "unsigned"         , mqi_unsignd          },
        { "floating"         , mqi_floating         },
        {       NULL         ,         0            }
    };

    static const_def_t node_const[] = {
        { "input"            , mir_input            },
        { "output"           , mir_output           },
        { "device"           , mir_device           },
        { "stream"           , mir_stream           },
        { "internal"         , mir_internal         },
        { "external"         , mir_external         },
        { "radio"            , mir_radio            },
        { "player"           , mir_player           },
        { "navigator"        , mir_navigator        },
        { "game"             , mir_game             },
        { "browser"          , mir_browser          },
        { "camera"           , mir_camera           },
        { "phone"            , mir_phone            },
        { "alert"            , mir_alert            },
        { "event"            , mir_event            },
        { "system"           , mir_system           },
        { "speakers"         , mir_speakers         },
        { "microphone"       , mir_microphone       },
        { "jack"             , mir_jack             },
        { "spdif"            , mir_spdif            },
        { "hdmi"             , mir_hdmi             },
        { "wired_headset"    , mir_wired_headset    },
        { "wired_headphone"  , mir_wired_headphone  },
        { "usb_headset"      , mir_usb_headset      },
        { "usb_headphone"    , mir_usb_headphone    },
        { "bluetooth_sco"    , mir_bluetooth_sco    },
        { "bluetooth_a2dp"   , mir_bluetooth_a2dp   },
        { "bluetooth_carkit" , mir_bluetooth_carkit },
        { "bluetooth_source" , mir_bluetooth_source },
        { "bluetooth_sink"   , mir_bluetooth_sink   },
        {       NULL         ,         0            }
    };

    static const_def_t vollim_const[] = {
        { "class"            , vollim_class         },
        { "generic"          , vollim_generic       },
        { "maximum"          , vollim_maximum       },
        {       NULL         ,         0            }
    };

    const_def_t *cd;
    bool success = true;


    lua_getglobal(L, "mdb");

    if (!lua_istable(L, -1))
        success = false;
    else {
        for (cd = mdb_const;   cd->name;   cd++) {
            lua_pushstring(L, cd->name);
            lua_pushinteger(L, cd->value);
            lua_rawset(L, -3);
        }
        lua_pop(L, 1);
    }



    lua_getglobal(L, "node");

    if (!lua_istable(L, -1))
        success = false;
    else {
        for (cd = node_const;   cd->name;   cd++) {
            lua_pushstring(L, cd->name);
            lua_pushinteger(L, cd->value);
            lua_rawset(L, -3);
        }
        lua_pop(L, 1);
    }


    lua_getglobal(L, "volume_limit");

    if (!lua_istable(L, -1))
        success = false;
    else {
        for (cd = vollim_const;   cd->name;   cd++) {
            lua_pushstring(L, cd->name);
            lua_pushinteger(L, cd->value);
            lua_rawset(L, -3);
        }
        lua_pop(L, 1);
    }

    lua_pushnumber(L, 0);
    lua_setglobal(L, "no_resource");

    return success;
}


static bool register_methods(lua_State *L)
{
    static funcbridge_def_t funcbridge_defs[] = {
        {"make_routes"    ,"o"  , update_bridge   ,mir_router_make_routing   },
        {"make_volumes"   ,"o"  , update_bridge   ,mir_volume_make_limiting  },
        {"accept_default" ,"oo" , accept_bridge   ,mir_router_default_accept },
        {"compare_default","ooo", compare_bridge  ,mir_router_default_compare},
        {"accept_phone"   ,"oo" , accept_bridge   ,mir_router_phone_accept   },
        {"compare_phone"  ,"ooo", compare_bridge  ,mir_router_phone_compare  },
        {"volume_supress" ,"odo", calculate_bridge,mir_volume_suppress       },
        {"volume_correct" ,"odo", calculate_bridge,mir_volume_correction     },
        {"change_volume_context","o",change_bridge,mir_volume_change_context},
        {       NULL      , NULL,      NULL       ,         NULL             }
    };

    funcbridge_def_t *d;
    bool success = true;

    for (d = funcbridge_defs;   d->name;    d++) {
        if (!mrp_funcbridge_create_cfunc(L,d->name,d->sign,d->func,d->data)) {
            pa_log("%s: failed to register builtin function '%s'",
                   __FILE__, d->name);
            success = false;
        }
    }

    return success;
}



static void *alloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    void *mem;

    (void)ud;
    (void)osize;

    if (nsize)
        mem = pa_xrealloc(ptr, nsize);
    else {
        mem = NULL;
        pa_xfree(ptr);
    }

    return mem;
}

static int panic(lua_State *L)
{
    (void)L;

    pa_log("PANIC: unprotected error in call to Lua API (%s)",
           lua_tostring(L,-1));

    return 0;
}


/*
 * Local Variables:
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 *
 */
