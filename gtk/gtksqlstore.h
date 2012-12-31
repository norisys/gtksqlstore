#ifndef __GTK_SQL_STORE_H__
#define __GTK_SQL_STORE_H__

#include <gtk/gtk.h>
#include <sqlite3.h>

G_BEGIN_DECLS

#define GTK_TYPE_SQL_STORE              (gtk_sql_store_get_type ())
#define GTK_SQL_STORE(obj)              (G_TYPE_CHECK_INSTANCE_CAST ((obj), GTK_TYPE_SQL_STORE, GtkSqlStore))
#define GTK_SQL_STORE_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass), GTK_TYPE_SQL_STORE, GtkSqlStoreClass))
#define GTK_IS_SQL_STORE(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GTK_TYPE_SQL_STORE))
#define GTK_IS_SQL_STORE_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass), GTK_TYPE_SQL_STORE))
#define GTK_SQL_STORE_GET_CLASS(obj)    (G_TYPE_INSTANCE_GET_CLASS ((klass), GTK_TYPE_SQL_STORE, GtkSqlStoreClass))

typedef struct _GtkSqlStore             GtkSqlStore;
typedef struct _GtkSqlStorePrivate      GtkSqlStorePrivate;
typedef struct _GtkSqlStoreClass        GtkSqlStoreClass;

struct _GtkSqlStore
{
  GObject parent;

  GtkSqlStorePrivate *priv;
};

struct _GtkSqlStoreClass
{
  GObjectClass parent_class;
};

GType           gtk_sql_store_get_type          (void) G_GNUC_CONST;
GtkSqlStore    *gtk_sql_store_new               (sqlite3       *db,
                                                 const gchar   *table,
                                                 gint           n_columns,
                                                 ...);
GtkSqlStore    *gtk_sql_store_newv              (sqlite3       *db,
                                                 const gchar   *table,
                                                 gint           n_columns,
                                                 const gchar  **columns,
                                                 GType         *types);
GtkSqlStore    *gtk_sql_store_new_with_file     (const gchar   *filename,
                                                 const gchar   *table,
                                                 gint           n_columns,
                                                 ...);
GtkSqlStore    *gtk_sql_store_new_with_filev    (const gchar   *filename,
                                                 const gchar   *table,
                                                 gint           n_columns,
                                                 const gchar  **columns,
                                                 GType         *types);
void            gtk_sql_store_requery           (GtkSqlStore   *sql_store);
void            gtk_sql_store_set_value         (GtkSqlStore   *sql_store,
                                                 GtkTreeIter   *iter,
                                                 gint           column,
                                                 GValue         value);
void            gtk_sql_store_set               (GtkSqlStore   *sql_store,
                                                 GtkTreeIter   *iter,
                                                 ...);
void            gtk_sql_store_set_valuesv       (GtkSqlStore   *sql_store,
                                                 GtkTreeIter   *iter,
                                                 gint          *columns,
                                                 GValue        *values,
                                                 gint           n_values);
void            gtk_sql_store_remove            (GtkSqlStore   *sql_store,
                                                 GtkTreeIter   *iter);
void            gtk_sql_store_insert            (GtkSqlStore   *sql_store,
                                                 GtkTreeIter   *iter);
void            gtk_sql_store_insert_with_values(GtkSqlStore   *sql_store,
                                                 GtkTreeIter   *iter,
                                                 ...);
void            gtk_sql_store_insert_with_valuesv(GtkSqlStore  *sql_store,
                                                 GtkTreeIter   *iter,
                                                 gint          *columns,
                                                 GValue        *values,
                                                 gint           n_values);
void            gtk_sql_store_clear             (GtkSqlStore   *sql_store);
gboolean        gtk_sql_store_iter_is_valid     (GtkSqlStore   *sql_store,
                                                 GtkTreeIter   *iter);

G_END_DECLS

#endif /* __GTK_SQL_STORE_H__ */

