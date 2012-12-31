#include <gtk/gtk.h>
#include <gtk/gtksqlstore.h>

void create_sample_data(GtkSqlStore *store)
{
	gtk_sql_store_insert_with_values(store, NULL, 0, "row 1", 1, 100, 2, "\xe2\x99\xaa", -1);
	gtk_sql_store_insert_with_values(store, NULL, 0, "row 2", 1, 150, -1);
	gtk_sql_store_insert_with_values(store, NULL, 0, "row 3", 1, 180, -1);
}

GtkWidget *create_tree_view()
{
	GtkSqlStore *store = gtk_sql_store_new_with_file("test.db", "test_table", 3,
		"col1", G_TYPE_STRING,
		"col2", G_TYPE_INT,
		"col3", G_TYPE_STRING);

	if (gtk_tree_model_iter_n_children((GtkTreeModel *)store, NULL) == 0)
		create_sample_data(store);

	GtkWidget *tree = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));

	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;

	renderer = gtk_cell_renderer_text_new();
	column = gtk_tree_view_column_new_with_attributes("Column 1", renderer, "text", 0, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

	column = gtk_tree_view_column_new_with_attributes("Column 2", renderer, "text", 1, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

	column = gtk_tree_view_column_new_with_attributes("Column 3", renderer, "text", 2, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(tree), column);

	return tree;
}

int main(int argc, char **argv)
{
	GtkWidget *window;

	gtk_init(&argc, &argv);

	window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window), "gtksqlmodel");
	gtk_window_set_default_size(GTK_WINDOW(window), 640, 480);

	g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	gtk_container_add(GTK_CONTAINER(window), create_tree_view());

	gtk_widget_show_all(window);

	gtk_main();

	return 0;
}

