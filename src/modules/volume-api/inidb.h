#ifndef fooinidbhfoo
#define fooinidbhfoo

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

#include <pulsecore/core.h>

typedef struct pa_inidb pa_inidb;
typedef struct pa_inidb_cell pa_inidb_cell;
typedef struct pa_inidb_row pa_inidb_row;
typedef struct pa_inidb_table pa_inidb_table;

/* If there's no object with the given name, the implementation is expected to
 * create a new object (or at least try to). */
typedef int (*pa_inidb_get_object_cb_t)(pa_inidb *db, const char *name, void **_r);

/* The implementation is expected to parse the value, and set the parsed value
 * on the object. */
typedef int (*pa_inidb_parse_cb_t)(pa_inidb *db, const char *value, void *object);

pa_inidb *pa_inidb_new(pa_core *core, const char *name, void *userdata);
void pa_inidb_free(pa_inidb *db);

void *pa_inidb_get_userdata(pa_inidb *db);
pa_inidb_table *pa_inidb_add_table(pa_inidb *db, const char *name, pa_inidb_get_object_cb_t get_object_cb);
void pa_inidb_load(pa_inidb *db);

void pa_inidb_table_add_column(pa_inidb_table *table, const char *name, pa_inidb_parse_cb_t parse_cb);
pa_inidb_row *pa_inidb_table_add_row(pa_inidb_table *table, const char *row_id);

pa_inidb_cell *pa_inidb_row_get_cell(pa_inidb_row *row, const char *column_name);

void pa_inidb_cell_set_value(pa_inidb_cell *cell, const char *value);

#endif
