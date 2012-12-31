#include <gtk/gtksqlstore.h>
#include <string.h>

struct _GtkSqlStorePrivate
{
	GtkListStore *store;

	sqlite3 *db;
	gboolean should_close_db;

	gchar *table;

	gint n_columns;
	gchar **columns;
};

static void gtk_sql_store_tree_model_init(GtkTreeModelIface *iface);
static void gtk_sql_store_finalize(GObject *object);

static void gtk_sql_store_ensure_table_exists(GtkSqlStore *sql_store);

/* TreeModel interface */
static GtkTreeModelFlags gtk_sql_store_get_flags(GtkTreeModel *tree_model);
static gint gtk_sql_store_get_n_columns(GtkTreeModel *tree_model);
static GType gtk_sql_store_get_column_type(GtkTreeModel *tree_model,
                                           gint index);
static gboolean gtk_sql_store_get_iter(GtkTreeModel *tree_model,
                                       GtkTreeIter *iter,
                                       GtkTreePath *path);
static GtkTreePath *gtk_sql_store_get_path(GtkTreeModel *tree_model,
                                           GtkTreeIter *iter);
static void gtk_sql_store_get_value(GtkTreeModel *tree_model,
                                    GtkTreeIter *iter,
                                    gint column,
                                    GValue *value);
static gboolean gtk_sql_store_iter_next(GtkTreeModel *tree_model,
                                        GtkTreeIter *iter);
static gboolean gtk_sql_store_iter_previous(GtkTreeModel *tree_model,
                                            GtkTreeIter *iter);
static gboolean gtk_sql_store_iter_children(GtkTreeModel *tree_model,
                                            GtkTreeIter *iter,
                                            GtkTreeIter *parent);
static gboolean gtk_sql_store_iter_has_child(GtkTreeModel *tree_model,
                                             GtkTreeIter *iter);
static gint gtk_sql_store_iter_n_children(GtkTreeModel *tree_model,
                                          GtkTreeIter *iter);
static gboolean gtk_sql_store_iter_nth_child(GtkTreeModel *tree_model,
                                             GtkTreeIter *iter,
                                             GtkTreeIter *parent,
                                             gint n);
static gboolean gtk_sql_store_iter_parent(GtkTreeModel *tree_model,
                                          GtkTreeIter *iter,
                                          GtkTreeIter *child);
static void gtk_sql_store_ref_node(GtkTreeModel *tree_model,
                                   GtkTreeIter *iter);
static void gtk_sql_store_unref_node(GtkTreeModel *tree_model,
                                     GtkTreeIter *iter);

G_DEFINE_TYPE_WITH_CODE(GtkSqlStore, gtk_sql_store, G_TYPE_OBJECT,
		G_IMPLEMENT_INTERFACE(GTK_TYPE_TREE_MODEL,
			gtk_sql_store_tree_model_init))

static void gtk_sql_store_class_init(GtkSqlStoreClass *class)
{
	GObjectClass *object_class;

	object_class = (GObjectClass *)class;

	object_class->finalize = gtk_sql_store_finalize;

	g_type_class_add_private(class, sizeof(GtkSqlStorePrivate));
}

static void gtk_sql_store_tree_model_init(GtkTreeModelIface *iface)
{
	iface->get_flags = gtk_sql_store_get_flags;
	iface->get_n_columns = gtk_sql_store_get_n_columns;
	iface->get_column_type = gtk_sql_store_get_column_type;
	iface->get_iter = gtk_sql_store_get_iter;
	iface->get_path = gtk_sql_store_get_path;
	iface->get_value = gtk_sql_store_get_value;
	iface->iter_next = gtk_sql_store_iter_next;
	iface->iter_previous = gtk_sql_store_iter_previous;
	iface->iter_children = gtk_sql_store_iter_children;
	iface->iter_has_child = gtk_sql_store_iter_has_child;
	iface->iter_n_children = gtk_sql_store_iter_n_children;
	iface->iter_nth_child = gtk_sql_store_iter_nth_child;
	iface->iter_parent = gtk_sql_store_iter_parent;
	iface->ref_node = gtk_sql_store_ref_node;
	iface->unref_node = gtk_sql_store_unref_node;
}

static void gtk_sql_store_init(GtkSqlStore *sql_store)
{
	sql_store->priv = G_TYPE_INSTANCE_GET_PRIVATE(sql_store, GTK_TYPE_SQL_STORE, GtkSqlStorePrivate);
}

static void gtk_sql_store_finalize(GObject *object)
{
	GtkSqlStore *sql_store = (GtkSqlStore *)object;
	GtkSqlStorePrivate *priv = sql_store->priv;
	int i;

	g_object_unref(priv->store);
	if (priv->should_close_db)
		sqlite3_close(priv->db);
	g_free(priv->table);
	for (i = 0; i < priv->n_columns; ++i)
		g_free(priv->columns[i]);
	g_free(priv->columns);

	G_OBJECT_CLASS(gtk_sql_store_parent_class)->finalize(object);
}

static void read_sql_column(GValue *value, sqlite3_stmt *stmt, int col)
{
	int type = sqlite3_column_type(stmt, col);

	switch (type) {
	case SQLITE_INTEGER:
		g_value_init(value, G_TYPE_INT64);
		g_value_set_int64(value, sqlite3_column_int64(stmt, col));
		break;
	case SQLITE_FLOAT:
		g_value_init(value, G_TYPE_DOUBLE);
		g_value_set_double(value, sqlite3_column_double(stmt, col));
		break;
	case SQLITE_TEXT:
		g_value_init(value, G_TYPE_STRING);
		g_value_set_string(value, (const gchar *)sqlite3_column_text(stmt, col));
		break;
	case SQLITE_BLOB:
		g_value_init(value, G_TYPE_BYTES);
		g_value_take_boxed(value,
			g_bytes_new(sqlite3_column_blob(stmt, col),
				sqlite3_column_bytes(stmt, col)));
		break;
	case SQLITE_NULL:
		g_value_init(value, G_TYPE_NONE);
		break;
	default:
		g_warning("unknown sqlite column type: %d", type);
		break;
	}
}

static void bind_sql_param(sqlite3_stmt *stmt, int col, GValue *value)
{
	if (G_VALUE_HOLDS_STRING(value)) {
		sqlite3_bind_text(stmt, col, g_value_get_string(value), -1, SQLITE_TRANSIENT);
	} else if (G_VALUE_HOLDS_DOUBLE(value)) {
		sqlite3_bind_double(stmt, col, g_value_get_double(value));
	} else if (G_VALUE_HOLDS_INT(value)) {
		sqlite3_bind_int(stmt, col, g_value_get_int(value));
	} else if (G_VALUE_HOLDS_INT64(value)) {
		sqlite3_bind_int64(stmt, col, g_value_get_int64(value));
	} else if (G_VALUE_HOLDS(value, G_TYPE_BYTES)) {
		GBytes *bytes = (GBytes *)g_value_get_boxed(value);
		sqlite3_bind_blob(stmt, col,
			g_bytes_get_data(bytes, NULL),
			g_bytes_get_size(bytes),
			SQLITE_TRANSIENT);
	} else {
		g_warning("unsupported value type %s", g_type_name(G_VALUE_TYPE(value)));
	}
}

static GValue arg_to_value(va_list *ap, GType type)
{
	GValue value = G_VALUE_INIT;

	switch (type) {
	case G_TYPE_STRING:
		g_value_init(&value, G_TYPE_STRING);
		g_value_set_string(&value, va_arg(*ap, const gchar *));
		break;
	case G_TYPE_DOUBLE:
		g_value_init(&value, G_TYPE_DOUBLE);
		g_value_set_double(&value, va_arg(*ap, gdouble));
		break;
	case G_TYPE_INT:
		g_value_init(&value, G_TYPE_INT);
		g_value_set_int(&value, va_arg(*ap, gint));
		break;
	case G_TYPE_INT64:
		g_value_init(&value, G_TYPE_INT64);
		g_value_set_int64(&value, va_arg(*ap, gint64));
		break;
	default:
		g_warning("unsupported value type %s", g_type_name(type));
		break;
	}

	return value;
}

GtkSqlStore *gtk_sql_store_new(sqlite3 *db,
                               const gchar *table,
                               gint n_columns,
                               ...)
{
	int i;
	va_list ap;
	const gchar **columns;
	GType *types;
	GtkSqlStore *sql_store;

	g_warn_if_fail(n_columns > 0);

	columns = g_malloc(n_columns * sizeof(const gchar *));
	types = g_malloc(n_columns * sizeof(GType));
	va_start(ap, n_columns);
	for (i = 0; i < n_columns; ++i) {
		columns[i] = va_arg(ap, const gchar *);
		types[i] = va_arg(ap, GType);
	}
	va_end(ap);

	sql_store = gtk_sql_store_newv(db, table, n_columns, columns, types);

	g_free(columns);

	return sql_store;
}

GtkSqlStore *gtk_sql_store_newv(sqlite3 *db,
                                const gchar *table,
                                gint n_columns,
                                const gchar **columns,
                                GType *types)
{
	GtkSqlStore *sql_store;
	GtkSqlStorePrivate *priv;
	GType *sub_types;
	int i;

	g_warn_if_fail(n_columns > 0);

	sql_store = g_object_new(gtk_sql_store_get_type(), NULL);
	priv = sql_store->priv;

	sub_types = g_malloc((1 + n_columns) * sizeof(GType));
	sub_types[0] = G_TYPE_INT64;
	memcpy(sub_types + 1, types, n_columns * sizeof(GType));

	// XXX: connect signals
	priv->store = gtk_list_store_newv(1 + n_columns, sub_types);
	priv->db = db;
	priv->should_close_db = FALSE;
	priv->table = g_strdup(table);
	priv->n_columns = n_columns;
	priv->columns = g_malloc(n_columns * sizeof(gchar *));
	for (i = 0; i < n_columns; ++i)
		priv->columns[i] = g_strdup(columns[i]);

	gtk_sql_store_ensure_table_exists(sql_store);
	gtk_sql_store_requery(sql_store);

	return sql_store;
}

GtkSqlStore *gtk_sql_store_new_with_file(const gchar *filename,
                                         const gchar *table,
                                         gint n_columns,
                                         ...)
{
	int i;
	va_list ap;
	const gchar **columns;
	GType *types;
	GtkSqlStore *sql_store;

	g_warn_if_fail(n_columns > 0);

	columns = g_malloc(n_columns * sizeof(const gchar *));
	types = g_malloc(n_columns * sizeof(GType));
	va_start(ap, n_columns);
	for (i = 0; i < n_columns; ++i) {
		columns[i] = va_arg(ap, const gchar *);
		types[i] = va_arg(ap, GType);
	}
	va_end(ap);

	sql_store = gtk_sql_store_new_with_filev(filename, table, n_columns, columns, types);

	g_free(columns);

	return sql_store;
}

GtkSqlStore *gtk_sql_store_new_with_filev(const gchar *filename,
                                          const gchar *table,
                                          gint n_columns,
                                          const gchar **columns,
                                          GType *types)
{
	sqlite3 *db;
	GtkSqlStore *sql_store;
	GtkSqlStorePrivate *priv;
	GType *sub_types;
	int i;

	g_warn_if_fail(n_columns > 0);

	if (sqlite3_open(filename, &db)) {
		g_warning("Failed to open database file: %s", sqlite3_errmsg(db));
		sqlite3_close(db);
		return NULL;
	}

	sql_store = g_object_new(gtk_sql_store_get_type(), NULL);
	priv = sql_store->priv;

	sub_types = g_malloc((1 + n_columns) * sizeof(GType));
	sub_types[0] = G_TYPE_INT64;
	memcpy(sub_types + 1, types, n_columns * sizeof(GType));

	// XXX: connect signals
	priv->store = gtk_list_store_newv(1 + n_columns, sub_types);
	priv->db = db;
	priv->should_close_db = TRUE;
	priv->table = g_strdup(table);
	priv->n_columns = n_columns;
	priv->columns = g_malloc(n_columns * sizeof(gchar *));
	for (i = 0; i < n_columns; ++i)
		priv->columns[i] = g_strdup(columns[i]);

	gtk_sql_store_ensure_table_exists(sql_store);
	gtk_sql_store_requery(sql_store);

	return sql_store;
}

static void gtk_sql_store_ensure_table_exists(GtkSqlStore *sql_store)
{
	GtkSqlStorePrivate *priv = sql_store->priv;
	GString *sql;
	int i;

	sql = g_string_new("CREATE TABLE IF NOT EXISTS ");
	g_string_append_printf(sql, "\"%s\" (", priv->table);
	for (i = 0; i < priv->n_columns; ++i) {
		if (i != 0)
			g_string_append_printf(sql, ", ");
		g_string_append_printf(sql, "\"%s\"", priv->columns[i]);
	}
	g_string_append_printf(sql, ");");

	if (sqlite3_exec(priv->db, sql->str, NULL, NULL, NULL) != SQLITE_OK) {
		g_warning("SQLite error: %s", sqlite3_errmsg(priv->db));
	}

	g_string_free(sql, TRUE);
}

void gtk_sql_store_requery(GtkSqlStore *sql_store)
{
	GtkSqlStorePrivate *priv = sql_store->priv;
	gchar *column_selection;
	gchar *sql;
	sqlite3_stmt *stmt;
	int n_cols;
	gint *insert_columns;
	GValue *insert_values;
	int i;
	int ret;

	{
		GString *cols = g_string_new("_ROWID_");
		for (i = 0; i < priv->n_columns; ++i)
			g_string_append_printf(cols, ", \"%s\"", priv->columns[i]);
		column_selection = g_string_free(cols, FALSE);
	}

	sql = g_strdup_printf("SELECT %s FROM \"%s\";", column_selection, priv->table);
	g_free(column_selection);

	ret = sqlite3_prepare_v2(priv->db, sql, -1, &stmt, NULL);
	g_free(sql);

	if (ret != SQLITE_OK) {
		g_warning("SQLite error: %s", sqlite3_errmsg(priv->db));
		sqlite3_finalize(stmt);
		return;
	}

	n_cols = sqlite3_column_count(stmt);
	insert_columns = g_malloc(n_cols * sizeof(gint));
	insert_values = g_malloc0(n_cols * sizeof(GValue));
	for (i = 0; i < n_cols; ++i) {
		insert_columns[i] = i;
		g_value_init(&insert_values[i],
			gtk_tree_model_get_column_type((GtkTreeModel *)priv->store, i));
	}

	gtk_list_store_clear(priv->store);

	while ((ret = sqlite3_step(stmt)) == SQLITE_ROW) {
		for (i = 0; i < n_cols; ++i) {
			if (sqlite3_column_type(stmt, i) != SQLITE_NULL) {
				GValue value = G_VALUE_INIT;
				read_sql_column(&value, stmt, i);
				g_value_transform(&value, &insert_values[i]);
				g_value_unset(&value);
			} else {
				g_value_reset(&insert_values[i]);
			}
		}

		gtk_list_store_insert_with_valuesv(priv->store,
			NULL,
			-1,
			insert_columns,
			insert_values,
			n_cols);

	}

	if (ret != SQLITE_DONE)
		g_warning("SQLite error: %s", sqlite3_errmsg(priv->db));

	sqlite3_finalize(stmt);

	for (i = 0; i < n_cols; ++i)
		g_value_unset(&insert_values[i]);

	g_free(insert_columns);
	g_free(insert_values);
}

void gtk_sql_store_set_value(GtkSqlStore *sql_store,
                             GtkTreeIter *iter,
                             gint column,
                             GValue value)
{
	gtk_sql_store_set_valuesv(sql_store, iter, &column, &value, 1);
}

void gtk_sql_store_set(GtkSqlStore *sql_store,
                       GtkTreeIter *iter,
                       ...)
{
	GArray *columns;
	GArray *values;
	gint n_values;
	va_list ap;

	columns = g_array_new(FALSE, FALSE, sizeof(gint));
	values = g_array_new(FALSE, TRUE, sizeof(GValue));

	va_start(ap, iter);
	for (n_values = 0; ; ++n_values) {
		gint col = va_arg(ap, gint);
		GValue val;
		GType type;

		if (col < 0)
			break;

		type = gtk_sql_store_get_column_type((GtkTreeModel *)sql_store, col);
		val = arg_to_value(&ap, type);

		g_array_append_val(columns, col);
		g_array_append_val(values, val);
	}
	va_end(ap);

	gtk_sql_store_set_valuesv(sql_store,
		iter,
		&g_array_index(columns, gint, 0),
		&g_array_index(values, GValue, 0),
		n_values);

	while (n_values--)
		g_value_unset(&g_array_index(values, GValue, n_values));

	g_array_free(columns, TRUE);
	g_array_free(values, TRUE);
}

void gtk_sql_store_set_valuesv(GtkSqlStore *sql_store,
                               GtkTreeIter *iter,
                               gint *columns,
                               GValue *values,
                               gint n_values)
{
	GtkSqlStorePrivate *priv = sql_store->priv;
	GString *sql;
	sqlite3_stmt *stmt;
	GValue rowid_val = G_VALUE_INIT;
	int i;
	int ret;

	gtk_tree_model_get_value((GtkTreeModel *)priv->store, iter, 0, &rowid_val);

	sql = g_string_new("");
	g_string_printf(sql, "UPDATE \"%s\" SET ", priv->table);
	for (i = 0; i < n_values; ++i) {
		if (i != 0)
			g_string_append(sql, ", ");
		g_string_append_printf(sql, "\"%s\" = ?", priv->columns[columns[i]]);
	}
	g_string_append(sql, " WHERE _ROWID_ = ?;");

	ret = sqlite3_prepare_v2(priv->db, sql->str, sql->len + 1, &stmt, NULL);
	if (ret == SQLITE_OK) {
		for (i = 0; i < n_values; ++i)
			bind_sql_param(stmt, i + 1, &values[i]);
		sqlite3_bind_int64(stmt, i + 1, g_value_get_int64(&rowid_val));
	}

	if (ret == SQLITE_OK)
		ret = sqlite3_step(stmt);

	if (ret == SQLITE_DONE) {
		GArray *sub_columns = g_array_sized_new(FALSE, FALSE, sizeof(gint), n_values);

		g_array_append_vals(sub_columns, columns, n_values);
		for (i = 0; i < n_values; ++i)
			++g_array_index(sub_columns, gint, i);

		gtk_list_store_set_valuesv(priv->store, iter,
			&g_array_index(sub_columns, gint, 0),
			&g_array_index(values, GValue, 0),
			n_values);

		g_array_free(sub_columns, TRUE);
	} else {
		g_warning("SQLite error: %s", sqlite3_errmsg(priv->db));
	}

	g_string_free(sql, TRUE);
	sqlite3_finalize(stmt);
}

void gtk_sql_store_remove(GtkSqlStore *sql_store,
                          GtkTreeIter *iter)
{
	GtkSqlStorePrivate *priv = sql_store->priv;
	GValue row_id = G_VALUE_INIT;
	gchar *sql;
	sqlite3_stmt *stmt;
	int ret;

	gtk_tree_model_get_value((GtkTreeModel *)priv->store, iter, 0, &row_id);

	sql = g_strdup_printf("DELETE FROM \"%s\" WHERE _ROWID_ = ?;", priv->table);
	ret = sqlite3_prepare_v2(priv->db, sql, -1, &stmt, NULL);
	g_free(sql);

	if (ret == SQLITE_OK) {
		sqlite3_bind_int64(stmt, 1, g_value_get_int64(&row_id));
		ret = sqlite3_step(stmt);
	}

	if (ret == SQLITE_DONE) {
		gtk_list_store_remove(priv->store, iter);
	} else {
		g_warning("SQLite error: %s", sqlite3_errmsg(priv->db));
	}

	sqlite3_finalize(stmt);
	g_value_unset(&row_id);
}

void gtk_sql_store_insert(GtkSqlStore *sql_store,
                          GtkTreeIter *iter)
{
	gtk_sql_store_insert_with_valuesv(sql_store, iter, NULL, NULL, 0);
}

void gtk_sql_store_insert_with_values(GtkSqlStore *sql_store,
                                      GtkTreeIter *iter,
                                      ...)
{
	GArray *columns;
	GArray *values;
	gint n_values;
	va_list ap;

	columns = g_array_new(FALSE, FALSE, sizeof(gint));
	values = g_array_new(FALSE, TRUE, sizeof(GValue));

	va_start(ap, iter);
	for (n_values = 0; ; ++n_values) {
		gint col = va_arg(ap, gint);
		GValue val;
		GType type;

		if (col < 0)
			break;

		type = gtk_sql_store_get_column_type((GtkTreeModel *)sql_store, col);
		val = arg_to_value(&ap, type);

		g_array_append_val(columns, col);
		g_array_append_val(values, val);
	}
	va_end(ap);

	gtk_sql_store_insert_with_valuesv(sql_store,
		iter,
		&g_array_index(columns, gint, 0),
		&g_array_index(values, GValue, 0),
		n_values);

	while (n_values--)
		g_value_unset(&g_array_index(values, GValue, n_values));

	g_array_free(columns, TRUE);
	g_array_free(values, TRUE);
}

void gtk_sql_store_insert_with_valuesv(GtkSqlStore *sql_store,
                                       GtkTreeIter *iter,
                                       gint *columns,
                                       GValue *values,
                                       gint n_values)
{
	GtkSqlStorePrivate *priv = sql_store->priv;
	GString *sql;
	sqlite3_stmt *stmt;
	int i;
	int ret;

	sql = g_string_new("");
	g_string_printf(sql, "INSERT INTO \"%s\"(", priv->table);
	for (i = 0; i < n_values; ++i) {
		if (i != 0)
			g_string_append(sql, ", ");
		g_string_append_printf(sql, "\"%s\"", priv->columns[columns[i]]);
	}
	g_string_append(sql, ") VALUES (");
	for (i = 0; i < n_values; ++i) {
		if (i != 0)
			g_string_append(sql, ", ");
		g_string_append(sql, "?");
	}
	g_string_append(sql, ");");

	ret = sqlite3_prepare_v2(priv->db, sql->str, sql->len + 1, &stmt, NULL);
	if (ret == SQLITE_OK) {
		for (i = 0; i < n_values; ++i)
			bind_sql_param(stmt, i + 1, &values[i]);
	}

	if (ret == SQLITE_OK)
		ret = sqlite3_step(stmt);

	if (ret == SQLITE_DONE) {
		GArray *sub_columns = g_array_sized_new(FALSE, FALSE, sizeof(gint), n_values + 1);
		GArray *sub_values = g_array_sized_new(FALSE, FALSE, sizeof(GValue), n_values + 1);
		gint rowid_col = 0;
		GValue rowid_val = G_VALUE_INIT;

		g_value_init(&rowid_val, G_TYPE_INT64);
		g_value_set_int64(&rowid_val, sqlite3_last_insert_rowid(priv->db));

		g_array_append_val(sub_columns, rowid_col);
		g_array_append_vals(sub_columns, columns, n_values);
		for (i = 0; i < n_values; ++i)
			++g_array_index(sub_columns, gint, i + 1);

		g_array_append_val(sub_values, rowid_val);
		g_array_append_vals(sub_values, values, n_values);

		gtk_list_store_insert_with_valuesv(priv->store, iter, -1,
			&g_array_index(sub_columns, gint, 0),
			&g_array_index(sub_values, GValue, 0),
			n_values + 1);

		g_array_free(sub_columns, TRUE);
		g_array_free(sub_values, TRUE);
	} else {
		g_warning("SQLite error: %s", sqlite3_errmsg(priv->db));
	}

	g_string_free(sql, TRUE);
	sqlite3_finalize(stmt);
}

void gtk_sql_store_clear(GtkSqlStore *sql_store)
{
	GtkSqlStorePrivate *priv = sql_store->priv;
	gchar *sql;

	sql = g_strdup_printf("DELETE FROM \"%s\";", priv->table);
	if (sqlite3_exec(priv->db, sql, NULL, NULL, NULL) != SQLITE_OK)
		g_warning("SQLite error: %s", sqlite3_errmsg(priv->db));
	g_free(sql);
	gtk_list_store_clear(priv->store);
}

gboolean gtk_sql_store_iter_is_valid(GtkSqlStore *sql_store,
                                     GtkTreeIter *iter)
{
	GtkSqlStorePrivate *priv = sql_store->priv;
	return gtk_list_store_iter_is_valid(priv->store, iter);
}

static GtkTreeModelFlags gtk_sql_store_get_flags(GtkTreeModel *tree_model)
{
	GtkSqlStore *sql_store = (GtkSqlStore *)tree_model;
	GtkSqlStorePrivate *priv = sql_store->priv;
	return gtk_tree_model_get_flags((GtkTreeModel *)priv->store);
}

static gint gtk_sql_store_get_n_columns(GtkTreeModel *tree_model)
{
	GtkSqlStore *sql_store = (GtkSqlStore *)tree_model;
	GtkSqlStorePrivate *priv = sql_store->priv;
	return priv->n_columns;
}

static GType gtk_sql_store_get_column_type(GtkTreeModel *tree_model,
                                           gint index)
{
	GtkSqlStore *sql_store = (GtkSqlStore *)tree_model;
	GtkSqlStorePrivate *priv = sql_store->priv;
	return gtk_tree_model_get_column_type((GtkTreeModel *)priv->store, index + 1);
}

static gboolean gtk_sql_store_get_iter(GtkTreeModel *tree_model,
                                       GtkTreeIter *iter,
                                       GtkTreePath *path)
{
	GtkSqlStore *sql_store = (GtkSqlStore *)tree_model;
	GtkSqlStorePrivate *priv = sql_store->priv;
	return gtk_tree_model_get_iter((GtkTreeModel *)priv->store, iter, path);
}

static GtkTreePath *gtk_sql_store_get_path(GtkTreeModel *tree_model,
                                           GtkTreeIter *iter)
{
	GtkSqlStore *sql_store = (GtkSqlStore *)tree_model;
	GtkSqlStorePrivate *priv = sql_store->priv;
	return gtk_tree_model_get_path((GtkTreeModel *)priv->store, iter);
}

static void gtk_sql_store_get_value(GtkTreeModel *tree_model,
                                    GtkTreeIter *iter,
                                    gint column,
                                    GValue *value)
{
	GtkSqlStore *sql_store = (GtkSqlStore *)tree_model;
	GtkSqlStorePrivate *priv = sql_store->priv;
	gtk_tree_model_get_value((GtkTreeModel *)priv->store, iter, column + 1, value);
}

static gboolean gtk_sql_store_iter_next(GtkTreeModel *tree_model,
                                        GtkTreeIter *iter)
{
	GtkSqlStore *sql_store = (GtkSqlStore *)tree_model;
	GtkSqlStorePrivate *priv = sql_store->priv;
	return gtk_tree_model_iter_next((GtkTreeModel *)priv->store, iter);
}

static gboolean gtk_sql_store_iter_previous(GtkTreeModel *tree_model,
                                            GtkTreeIter *iter)
{
	GtkSqlStore *sql_store = (GtkSqlStore *)tree_model;
	GtkSqlStorePrivate *priv = sql_store->priv;
	return gtk_tree_model_iter_previous((GtkTreeModel *)priv->store, iter);
}

static gboolean gtk_sql_store_iter_children(GtkTreeModel *tree_model,
                                            GtkTreeIter *iter,
                                            GtkTreeIter *parent)
{
	GtkSqlStore *sql_store = (GtkSqlStore *)tree_model;
	GtkSqlStorePrivate *priv = sql_store->priv;
	return gtk_tree_model_iter_children((GtkTreeModel *)priv->store, iter, parent);
}

static gboolean gtk_sql_store_iter_has_child(GtkTreeModel *tree_model,
                                             GtkTreeIter *iter)
{
	GtkSqlStore *sql_store = (GtkSqlStore *)tree_model;
	GtkSqlStorePrivate *priv = sql_store->priv;
	return gtk_tree_model_iter_has_child((GtkTreeModel *)priv->store, iter);
}

static gint gtk_sql_store_iter_n_children(GtkTreeModel *tree_model,
                                          GtkTreeIter *iter)
{
	GtkSqlStore *sql_store = (GtkSqlStore *)tree_model;
	GtkSqlStorePrivate *priv = sql_store->priv;
	return gtk_tree_model_iter_n_children((GtkTreeModel *)priv->store, iter);
}

static gboolean gtk_sql_store_iter_nth_child(GtkTreeModel *tree_model,
                                             GtkTreeIter *iter,
                                             GtkTreeIter *parent,
                                             gint n)
{
	GtkSqlStore *sql_store = (GtkSqlStore *)tree_model;
	GtkSqlStorePrivate *priv = sql_store->priv;
	return gtk_tree_model_iter_nth_child((GtkTreeModel *)priv->store, iter, parent, n);
}

static gboolean gtk_sql_store_iter_parent(GtkTreeModel *tree_model,
                                          GtkTreeIter *iter,
                                          GtkTreeIter *child)
{
	GtkSqlStore *sql_store = (GtkSqlStore *)tree_model;
	GtkSqlStorePrivate *priv = sql_store->priv;
	return gtk_tree_model_iter_parent((GtkTreeModel *)priv->store, iter, child);
}

static void gtk_sql_store_ref_node(GtkTreeModel *tree_model,
                                   GtkTreeIter *iter)
{
	GtkSqlStore *sql_store = (GtkSqlStore *)tree_model;
	GtkSqlStorePrivate *priv = sql_store->priv;
	return gtk_tree_model_ref_node((GtkTreeModel *)priv->store, iter);
}

static void gtk_sql_store_unref_node(GtkTreeModel *tree_model,
                                     GtkTreeIter *iter)
{
	GtkSqlStore *sql_store = (GtkSqlStore *)tree_model;
	GtkSqlStorePrivate *priv = sql_store->priv;
	return gtk_tree_model_unref_node((GtkTreeModel *)priv->store, iter);
}

