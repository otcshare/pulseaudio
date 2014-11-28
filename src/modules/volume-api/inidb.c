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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "inidb.h"

#include <pulse/mainloop-api.h>
#include <pulse/rtclock.h>
#include <pulse/timeval.h>
#include <pulse/xmalloc.h>

#include <pulsecore/conf-parser.h>
#include <pulsecore/core-error.h>
#include <pulsecore/core-rtclock.h>
#include <pulsecore/core-util.h>
#include <pulsecore/hashmap.h>
#include <pulsecore/macro.h>
#include <pulsecore/namereg.h>

#include <errno.h>

#define SAVE_INTERVAL_USEC (10 * PA_USEC_PER_SEC)

struct pa_inidb {
    pa_core *core;
    char *name;
    char *file_path;
    char *tmp_file_path;
    pa_hashmap *tables; /* table name -> pa_inidb_table */
    pa_time_event *time_event;
    bool failed;
    void *userdata;
};

struct pa_inidb_table {
    pa_inidb *db;
    char *name;
    pa_hashmap *columns; /* column name -> column */
    pa_hashmap *rows; /* row id -> pa_inidb_row */
    pa_inidb_get_object_cb_t get_object;
};

struct column {
    char *name;
    pa_inidb_parse_cb_t parse;
};

struct pa_inidb_row {
    char *id;
    char *header;
    pa_hashmap *cells; /* column name -> cell */
};

struct pa_inidb_cell {
    pa_inidb *db;
    struct column *column;
    char *value;
    char *assignment;
};

static void save(pa_inidb *db);

static pa_inidb_table *table_new(pa_inidb *db, const char *name, pa_inidb_get_object_cb_t get_object_cb);
static void table_free(pa_inidb_table *table);
static pa_inidb_row *table_add_row_internal(pa_inidb_table *table, const char *row_id);

static struct column *column_new(const char *name, pa_inidb_parse_cb_t parse_cb);
static void column_free(struct column *column);

static pa_inidb_row *row_new(pa_inidb_table *table, const char *id);
static void row_free(pa_inidb_row *row);

static pa_inidb_cell *cell_new(pa_inidb *db, struct column *column);
static void cell_free(pa_inidb_cell *cell);
static void cell_set_value_internal(pa_inidb_cell *cell, const char *value);

static int parse_assignment(pa_config_parser_state *state) {
    pa_inidb *db;
    const char *end_of_table_name;
    size_t table_name_len;
    char *table_name;
    pa_inidb_table *table;
    const char *row_id;
    pa_inidb_row *row;
    int r;
    void *object;
    struct column *column;
    pa_inidb_cell *cell;

    pa_assert(state);

    db = state->userdata;

    /* FIXME: pa_config_parser should be improved so that it could parse the
     * table name and row id for us in the section header. */
    end_of_table_name = strchr(state->section, ' ');
    if (!end_of_table_name) {
        pa_log("[%s:%u] Failed to parse table name and row id in section \"%s\"", state->filename, state->lineno,
               state->section);
        return -PA_ERR_INVALID;
    }

    table_name_len = end_of_table_name - state->section;
    table_name = pa_xstrndup(state->section, table_name_len);
    table = pa_hashmap_get(db->tables, table_name);
    if (!table)
        pa_log("[%s:%u] Unknown table name: \"%s\"", state->filename, state->lineno, table_name);
    pa_xfree(table_name);
    if (!table)
        return -PA_ERR_INVALID;

    row_id = end_of_table_name + 1;
    if (!pa_namereg_is_valid_name(row_id)) {
        pa_log("[%s:%u] Invalid row id: \"%s\"", state->filename, state->lineno, row_id);
        return -PA_ERR_INVALID;
    }

    /* This is not strictly necessary, but we do this to avoid saving the
     * database when there is no actual change. Without this, the get_object()
     * callback would cause redundant saving whenever creating new objects. */
    if (!(row = pa_hashmap_get(table->rows, row_id)))
        row = table_add_row_internal(table, row_id);

    r = table->get_object(db, row_id, &object);
    if (r < 0) {
        pa_log("[%s:%u] Failed to create object %s.", state->filename, state->lineno, row_id);
        return r;
    }

    column = pa_hashmap_get(table->columns, state->lvalue);
    if (!column) {
        pa_log("[%s:%u] Unknown column name: \"%s\"", state->filename, state->lineno, state->lvalue);
        return -PA_ERR_INVALID;
    }

    /* This is not strictly necessary, but we do this to avoid saving the
     * database when there is no actual change. Without this, the parse()
     * callback would cause redundant saving whenever setting the cell value
     * for the first time. */
    cell = pa_hashmap_get(row->cells, column->name);
    cell_set_value_internal(cell, state->rvalue);

    r = column->parse(db, state->rvalue, object);
    if (r < 0) {
        pa_log("[%s:%u] Failed to parse %s value \"%s\".", state->filename, state->lineno, column->name, state->rvalue);
        return r;
    }

    return 0;
}

pa_inidb *pa_inidb_new(pa_core *core, const char *name, void *userdata) {
    pa_inidb *db;
    int r;

    pa_assert(core);
    pa_assert(name);

    db = pa_xnew0(pa_inidb, 1);
    db->core = core;
    db->name = pa_xstrdup(name);

    r = pa_append_to_config_home_dir(name, true, &db->file_path);
    if (r < 0) {
        pa_log("Failed to find the file location for database \"%s\". The database will start empty, and updates will not be "
               "saved on disk.", name);
        db->failed = true;
    }

    if (db->file_path)
        db->tmp_file_path = pa_sprintf_malloc("%s.tmp", db->file_path);

    db->tables = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL,
                                     (pa_free_cb_t) table_free);
    db->userdata = userdata;

    return db;
}

void pa_inidb_free(pa_inidb *db) {
    pa_assert(db);

    if (db->time_event) {
        db->core->mainloop->time_free(db->time_event);
        save(db);
    }

    if (db->tables)
        pa_hashmap_free(db->tables);

    pa_xfree(db->tmp_file_path);
    pa_xfree(db->file_path);
    pa_xfree(db->name);
    pa_xfree(db);
}

void *pa_inidb_get_userdata(pa_inidb *db) {
    pa_assert(db);

    return db->userdata;
}

pa_inidb_table *pa_inidb_add_table(pa_inidb *db, const char *name, pa_inidb_get_object_cb_t get_object_cb) {
    pa_inidb_table *table;

    pa_assert(db);
    pa_assert(name);
    pa_assert(get_object_cb);

    table = table_new(db, name, get_object_cb);
    pa_assert_se(pa_hashmap_put(db->tables, table->name, table) >= 0);

    return table;
}

void pa_inidb_load(pa_inidb *db) {
    unsigned n_config_items;
    pa_inidb_table *table;
    void *state;
    pa_config_item *config_items;
    unsigned i;

    pa_assert(db);

    if (db->failed)
        return;

    n_config_items = 0;
    PA_HASHMAP_FOREACH(table, db->tables, state)
        n_config_items += pa_hashmap_size(table->columns);

    config_items = pa_xnew0(pa_config_item, n_config_items + 1);

    i = 0;
    PA_HASHMAP_FOREACH(table, db->tables, state) {
        struct column *column;
        void *state2;

        PA_HASHMAP_FOREACH(column, table->columns, state2) {
            config_items[i].lvalue = column->name;
            config_items[i].parse = parse_assignment;
            i++;
        }
    }

    pa_config_parse(db->file_path, NULL, config_items, NULL, db);
    pa_xfree(config_items);
}

static void save(pa_inidb *db) {
    FILE *f;
    pa_inidb_table *table;
    void *state;
    int r;

    pa_assert(db);

    if (db->failed)
        return;

    f = pa_fopen_cloexec(db->tmp_file_path, "w");
    if (!f) {
        pa_log("pa_fopen_cloexec() failed: %s", pa_cstrerror(errno));
        goto fail;
    }

    PA_HASHMAP_FOREACH(table, db->tables, state) {
        pa_inidb_row *row;
        void *state2;

        PA_HASHMAP_FOREACH(row, table->rows, state2) {
            size_t len;
            size_t items_written;
            pa_inidb_cell *cell;
            void *state3;

            len = strlen(row->header);
            items_written = fwrite(row->header, len, 1, f);

            if (items_written != 1) {
                pa_log("fwrite() failed: %s", pa_cstrerror(errno));
                goto fail;
            }

            PA_HASHMAP_FOREACH(cell, row->cells, state3) {
                if (!cell->assignment)
                    continue;

                len = strlen(cell->assignment);
                items_written = fwrite(cell->assignment, len, 1, f);

                if (items_written != 1) {
                    pa_log("fwrite() failed: %s", pa_cstrerror(errno));
                    goto fail;
                }
            }

            items_written = fwrite("\n", 1, 1, f);

            if (items_written != 1) {
                pa_log("fwrite() failed: %s", pa_cstrerror(errno));
                goto fail;
            }
        }
    }

    r = fclose(f);
    f = NULL;
    if (r < 0) {
        pa_log("fclose() failed: %s", pa_cstrerror(errno));
        goto fail;
    }

    r = rename(db->tmp_file_path, db->file_path);
    if (r < 0) {
        pa_log("rename() failed: %s", pa_cstrerror(errno));
        goto fail;
    }

    pa_log_debug("Database \"%s\" saved.", db->name);

    return;

fail:
    if (f)
        fclose(f);

    db->failed = true;
    pa_log("Saving database \"%s\" failed, current and future database changes will not be written to the disk.", db->name);
}

static pa_inidb_table *table_new(pa_inidb *db, const char *name, pa_inidb_get_object_cb_t get_object_cb) {
    pa_inidb_table *table;

    pa_assert(db);
    pa_assert(name);
    pa_assert(get_object_cb);

    table = pa_xnew0(pa_inidb_table, 1);
    table->db = db;
    table->name = pa_xstrdup(name);
    table->columns = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL,
                                         (pa_free_cb_t) column_free);
    table->rows = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL,
                                      (pa_free_cb_t) row_free);
    table->get_object = get_object_cb;

    return table;
}

static void table_free(pa_inidb_table *table) {
    pa_assert(table);

    if (table->rows)
        pa_hashmap_free(table->rows);

    if (table->columns)
        pa_hashmap_free(table->columns);

    pa_xfree(table->name);
    pa_xfree(table);
}

void pa_inidb_table_add_column(pa_inidb_table *table, const char *name, pa_inidb_parse_cb_t parse_cb) {
    struct column *column;

    pa_assert(table);
    pa_assert(name);
    pa_assert(parse_cb);

    column = column_new(name, parse_cb);
    pa_assert_se(pa_hashmap_put(table->columns, column->name, column) >= 0);
}

static pa_inidb_row *table_add_row_internal(pa_inidb_table *table, const char *row_id) {
    pa_inidb_row *row;

    pa_assert(table);
    pa_assert(row_id);

    row = row_new(table, row_id);
    pa_assert_se(pa_hashmap_put(table->rows, row->id, row) >= 0);

    return row;
}

static void time_cb(pa_mainloop_api *api, pa_time_event *event, const struct timeval *tv, void *userdata) {
    pa_inidb *db = userdata;

    pa_assert(api);
    pa_assert(db);

    api->time_free(event);
    db->time_event = NULL;

    save(db);
}

static void trigger_save(pa_inidb *db) {
    struct timeval tv;

    pa_assert(db);

    if (db->time_event)
        return;

    pa_timeval_rtstore(&tv, pa_rtclock_now() + SAVE_INTERVAL_USEC, true);
    db->time_event = db->core->mainloop->time_new(db->core->mainloop, &tv, time_cb, db);
}

pa_inidb_row *pa_inidb_table_add_row(pa_inidb_table *table, const char *row_id) {
    pa_inidb_row *row;

    pa_assert(table);
    pa_assert(row_id);

    row = pa_hashmap_get(table->rows, row_id);
    if (row)
        return row;

    row = table_add_row_internal(table, row_id);
    trigger_save(table->db);

    return row;
}

static struct column *column_new(const char *name, pa_inidb_parse_cb_t parse_cb) {
    struct column *column;

    pa_assert(name);
    pa_assert(parse_cb);

    column = pa_xnew(struct column, 1);
    column->name = pa_xstrdup(name);
    column->parse = parse_cb;

    return column;
}

static void column_free(struct column *column) {
    pa_assert(column);

    pa_xfree(column->name);
    pa_xfree(column);
}

static pa_inidb_row *row_new(pa_inidb_table *table, const char *id) {
    pa_inidb_row *row;
    struct column *column;
    void *state;

    pa_assert(table);
    pa_assert(id);

    row = pa_xnew0(pa_inidb_row, 1);
    row->id = pa_xstrdup(id);
    row->header = pa_sprintf_malloc("[%s %s]\n", table->name, id);
    row->cells = pa_hashmap_new_full(pa_idxset_string_hash_func, pa_idxset_string_compare_func, NULL,
                                     (pa_free_cb_t) cell_free);

    PA_HASHMAP_FOREACH(column, table->columns, state) {
        pa_inidb_cell *cell;

        cell = cell_new(table->db, column);
        pa_hashmap_put(row->cells, cell->column->name, cell);
    }

    return row;
}

static void row_free(pa_inidb_row *row) {
    pa_assert(row);

    pa_xfree(row->header);
    pa_xfree(row->id);
    pa_xfree(row);
}

pa_inidb_cell *pa_inidb_row_get_cell(pa_inidb_row *row, const char *column_name) {
    pa_inidb_cell *cell;

    pa_assert(row);
    pa_assert(column_name);

    pa_assert_se(cell = pa_hashmap_get(row->cells, column_name));

    return cell;
}

static pa_inidb_cell *cell_new(pa_inidb *db, struct column *column) {
    pa_inidb_cell *cell;

    pa_assert(db);
    pa_assert(column);

    cell = pa_xnew0(pa_inidb_cell, 1);
    cell->db = db;
    cell->column = column;

    return cell;
}

static void cell_free(pa_inidb_cell *cell) {
    pa_assert(cell);

    pa_xfree(cell->assignment);
    pa_xfree(cell->value);
    pa_xfree(cell);
}

static void cell_set_value_internal(pa_inidb_cell *cell, const char *value) {
    pa_assert(cell);
    pa_assert(value);

    pa_xfree(cell->value);
    cell->value = pa_xstrdup(value);

    pa_xfree(cell->assignment);
    if (value)
        cell->assignment = pa_sprintf_malloc("%s = %s\n", cell->column->name, value);
    else
        cell->assignment = NULL;
}

void pa_inidb_cell_set_value(pa_inidb_cell *cell, const char *value) {
    pa_assert(cell);

    if (pa_safe_streq(value, cell->value))
        return;

    cell_set_value_internal(cell, value);
    trigger_save(cell->db);
}
