/*
 *
 *  Copyright (C) 2017  Duc Tran <901stt@gmail.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <geanyplugin.h>
#include "tm_tag.h"


/* uncomment to display each row score (for debugging sort) */

GeanyPlugin      *geany_plugin;
GeanyData        *geany_data;

PLUGIN_VERSION_CHECK(226)

PLUGIN_SET_TRANSLATABLE_INFO (
  LOCALEDIR, GETTEXT_PACKAGE,
  _("Tag Filter"),
  _("Provides a panel for searching Tag and navigating it"),
  VERSION,
  "Duc Tran <901stt@gmail.com>"
)


/* GTK compatibility functions/macros */

#if ! GTK_CHECK_VERSION (2, 18, 0)
# define gtk_widget_get_visible(w) \
  (GTK_WIDGET_VISIBLE (w))
# define gtk_widget_set_can_focus(w, v)               \
  G_STMT_START {                                      \
    GtkWidget *widget = (w);                          \
    if (v) {                                          \
      GTK_WIDGET_SET_FLAGS (widget, GTK_CAN_FOCUS);   \
    } else {                                          \
      GTK_WIDGET_UNSET_FLAGS (widget, GTK_CAN_FOCUS); \
    }                                                 \
  } G_STMT_END
#endif

#if ! GTK_CHECK_VERSION (2, 21, 8)
# define GDK_KEY_Down       GDK_Down
# define GDK_KEY_Escape     GDK_Escape
# define GDK_KEY_ISO_Enter  GDK_ISO_Enter
# define GDK_KEY_KP_Enter   GDK_KP_Enter
# define GDK_KEY_Page_Down  GDK_Page_Down
# define GDK_KEY_Page_Up    GDK_Page_Up
# define GDK_KEY_Return     GDK_Return
# define GDK_KEY_Tab        GDK_Tab
# define GDK_KEY_Up         GDK_Up
#endif


/* Plugin */

enum {
  KB_SHOW_PANEL,
  KB_COUNT
};

struct {
  GtkWidget    *panel;
  GtkWidget    *entry;
  GtkWidget    *view;
  GtkListStore *store;
  GtkTreeModel *sort;

  GtkTreePath  *last_path;
} plugin_data = {
  NULL, NULL, NULL,
  NULL, NULL,
  NULL
};

struct DocumentTracking {
  GeanyDocument *doc;
  glong         line;
} doc_data = {
  NULL, 0
};

typedef enum {
  COL_TYPE_TAG        = 1 << 0,
  COL_TYPE_ANY        = 0xffff
} ColType;

enum {
  COL_LABEL,
  COL_TYPE,
  COL_DOCUMENT,
  COL_LINENUM,
  COL_COUNT
};


#define PATH_SEPARATOR " \342\206\222 " /* right arrow */

#define SEPARATORS        " -_./\\\"'"
#define IS_SEPARATOR(c)   (strchr (SEPARATORS, (c)) != NULL)
#define next_separator(p) (strpbrk (p, SEPARATORS))

/* TODO: be more tolerant regarding unmatched character in the needle.
 * Right now, we implicitly accept unmatched characters at the end of the
 * needle but absolutely not at the start.  e.g. "xpy" won't match "python" at
 * all, though "pyx" will. */
static inline gint
get_score (const gchar *needle,
           const gchar *haystack)
{
  if (! needle || ! haystack) {
    return needle == NULL;
  } else if (! *needle || ! *haystack) {
    return *needle == 0;
  }

  if (IS_SEPARATOR (*haystack)) {
    return get_score (needle + IS_SEPARATOR (*needle), haystack + 1);
  }

  if (IS_SEPARATOR (*needle)) {
    return get_score (needle + 1, next_separator (haystack));
  }

  if (*needle == *haystack) {
    gint a = get_score (needle + 1, haystack + 1) + 1 + IS_SEPARATOR (haystack[1]);
    gint b = get_score (needle, next_separator (haystack));

    return MAX (a, b);
  } else {
    return get_score (needle, next_separator (haystack));
  }
}


static gint
key_score (const gchar *key_,
           const gchar *text_)
{
  gchar  *text  = g_utf8_casefold (text_, -1);
  gchar  *key   = g_utf8_casefold (key_, -1);
  gint    score;

  score = get_score (key, text);

  g_free (text);
  g_free (key);

  return score;
}

static void
tree_view_set_cursor_from_iter (GtkTreeView *view,
                                GtkTreeIter *iter)
{
  GtkTreePath *path;

  path = gtk_tree_model_get_path (gtk_tree_view_get_model (view), iter);
  gtk_tree_view_set_cursor (view, path, NULL, FALSE);
  gtk_tree_path_free (path);
}

static void
tree_view_move_focus (GtkTreeView    *view,
                      GtkMovementStep step,
                      gint            amount)
{
  GtkTreeIter   iter;
  GtkTreePath  *path;
  GtkTreeModel *model = gtk_tree_view_get_model (view);
  gboolean      valid = FALSE;

  gtk_tree_view_get_cursor (view, &path, NULL);
  if (! path) {
    valid = gtk_tree_model_get_iter_first (model, &iter);
  } else {
    switch (step) {
      case GTK_MOVEMENT_BUFFER_ENDS:
        valid = gtk_tree_model_get_iter_first (model, &iter);
        if (valid && amount > 0) {
          GtkTreeIter prev;

          do {
            prev = iter;
          } while (gtk_tree_model_iter_next (model, &iter));
          iter = prev;
        }
        break;

      case GTK_MOVEMENT_PAGES:
        /* FIXME: move by page */
      case GTK_MOVEMENT_DISPLAY_LINES:
        gtk_tree_model_get_iter (model, &iter, path);
        if (amount == 0) {
          valid = TRUE;
        } else if (amount > 0) {
          while ((valid = gtk_tree_model_iter_next (model, &iter)) &&
                 --amount > 0)
            ;
        } else if (amount < 0) {
          while ((valid = gtk_tree_path_prev (path)) && --amount > 0)
            ;

          if (valid) {
            gtk_tree_model_get_iter (model, &iter, path);
          }
        }
        break;

      default:
        g_assert_not_reached ();
    }
    gtk_tree_path_free (path);
  }

  if (valid) {
    gint line_num;
    gtk_tree_model_get (model, &iter,
                        COL_LINENUM, &line_num,
                        -1);
    navqueue_goto_line (doc_data.doc,
                        doc_data.doc,
                        line_num);
    tree_view_set_cursor_from_iter (view, &iter);
  } else {
    gtk_widget_error_bell (GTK_WIDGET (view));
  }
}

static void
tree_view_activate_focused_row (GtkTreeView *view)
{
  GtkTreePath        *path;
  GtkTreeViewColumn  *column;

  gtk_tree_view_get_cursor (view, &path, &column);
  if (path) {
    gtk_tree_view_row_activated (view, path, column);
    gtk_tree_path_free (path);
  }
}


static void
fill_store (GtkListStore *store)
{
  GeanyDocument *doc;
  guint       i;

  doc = document_get_current ();
  if (doc == NULL || !doc->has_tags) {
    return;
  }

  if (! doc->tm_file || ! doc->tm_file->tags_array)
    return;

  for (i = 0; i < doc->tm_file->tags_array->len; ++i)
  {
    TMTag *tag = TM_TAG(doc->tm_file->tags_array->pdata[i]);

    if (G_UNLIKELY(tag == NULL))
      return;

    if (tag->name)
    {
      /*
      gchar *label = g_markup_printf_escaped ("<big>%s</big>\n"
                                              "<small><i>%s</i></small>",
                                              tag->name,
                                              tag->scope);
                                              */
      //fprintf (stdout, "Tag name: %s\n", tag->name);

      gtk_list_store_insert_with_values (store, NULL, -1,
                                         COL_LABEL, tag->name,
                                         COL_TYPE, COL_TYPE_TAG,
                                         COL_DOCUMENT, tag->name,
                                         COL_LINENUM, tag->line,
                                         -1);
      //g_free (label);
    }
  }
}

static gint
sort_func (GtkTreeModel  *model,
           GtkTreeIter   *a,
           GtkTreeIter   *b,
           gpointer       dummy)
{
  gint          scorea;
  gint          scoreb;
  gchar        *patha;
  gchar        *pathb;
  gint          typea;
  gint          typeb;
  const gchar  *key = gtk_entry_get_text (GTK_ENTRY (plugin_data.entry));

  gtk_tree_model_get (model, a, COL_DOCUMENT, &patha, COL_TYPE, &typea, -1);
  gtk_tree_model_get (model, b, COL_DOCUMENT, &pathb, COL_TYPE, &typeb, -1);

  scorea = key_score (key, patha);
  scoreb = key_score (key, pathb);

  if (! typea) {
    scorea -= 0xf000;
  }
  if (! typeb) {
    scoreb -= 0xf000;
  }

  g_free (patha);
  g_free (pathb);

  return scoreb - scorea;
}

static gboolean
on_panel_key_press_event (GtkWidget    *widget,
                          GdkEventKey  *event,
                          gpointer      dummy)
{
  switch (event->keyval) {
    case GDK_KEY_Escape:
      navqueue_goto_line (doc_data.doc,
                          doc_data.doc,
                          doc_data.line);
      gtk_widget_hide (widget);
      return TRUE;

    case GDK_KEY_Tab:
      /* avoid leaving the entry */
      return TRUE;

    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_ISO_Enter:
      tree_view_move_focus (GTK_TREE_VIEW (plugin_data.view),
                            GTK_MOVEMENT_PAGES, 0);
      gtk_widget_hide (widget);
      return TRUE;

    case GDK_KEY_Page_Up:
    case GDK_KEY_Page_Down:
      tree_view_move_focus (GTK_TREE_VIEW (plugin_data.view),
                            GTK_MOVEMENT_PAGES,
                            event->keyval == GDK_KEY_Page_Up ? -1 : 1);
      return TRUE;

    case GDK_KEY_Up:
    case GDK_KEY_Down: {
      tree_view_move_focus (GTK_TREE_VIEW (plugin_data.view),
                            GTK_MOVEMENT_DISPLAY_LINES,
                            event->keyval == GDK_KEY_Up ? -1 : 1);
      return TRUE;
    }
  }

  return FALSE;
}

static void
on_entry_text_notify (GObject    *object,
                      GParamSpec *pspec,
                      gpointer    dummy)
{
  GtkTreeIter   iter;
  GtkTreeView  *view  = GTK_TREE_VIEW (plugin_data.view);
  GtkTreeModel *model = gtk_tree_view_get_model (view);

  /* we force re-sorting the whole model from how it was before, and the
   * back to the new filter.  this is somewhat hackish but since we don't
   * know the original sorting order, and GtkTreeSortable don't have a
   * resort() API anyway. */
  gtk_tree_model_sort_reset_default_sort_func (GTK_TREE_MODEL_SORT (model));
  gtk_tree_sortable_set_default_sort_func (GTK_TREE_SORTABLE (model),
                                           sort_func, NULL, NULL);

  if (gtk_tree_model_get_iter_first (model, &iter)) {
    tree_view_set_cursor_from_iter (view, &iter);
  }
}

static void
on_entry_activate (GtkEntry  *entry,
                   gpointer   dummy)
{
  tree_view_activate_focused_row (GTK_TREE_VIEW (plugin_data.view));
}

static void
on_panel_hide (GtkWidget *widget,
               gpointer   dummy)
{
  GtkTreeView  *view = GTK_TREE_VIEW (plugin_data.view);

  if (plugin_data.last_path) {
    gtk_tree_path_free (plugin_data.last_path);
    plugin_data.last_path = NULL;
  }
  gtk_tree_view_get_cursor (view, &plugin_data.last_path, NULL);

  gtk_list_store_clear (plugin_data.store);
  doc_data.doc = NULL;
  doc_data.line = 0;
}

static void
on_panel_show (GtkWidget *widget,
               gpointer   dummy)
{
  gint current_pos = 0;
  GeanyDocument *doc = document_get_current ();
  GtkTreePath *path;
  GtkTreeView *view = GTK_TREE_VIEW (plugin_data.view);

  fill_store (plugin_data.store);

  if (doc) {
    current_pos = sci_get_current_position (doc->editor->sci);
    doc_data.doc = doc;
    doc_data.line =
      sci_get_line_from_position (doc->editor->sci, current_pos) + 1;
  }

  gtk_widget_grab_focus (plugin_data.entry);

  if (plugin_data.last_path) {
    gtk_tree_view_set_cursor (view, plugin_data.last_path, NULL, FALSE);
    gtk_tree_view_scroll_to_cell (view, plugin_data.last_path, NULL,
                                  TRUE, 0.5, 0.5);
  }
  /* make sure the cursor is set (e.g. if plugin_data.last_path wasn't valid) */
  gtk_tree_view_get_cursor (view, &path, NULL);
  if (path) {
    gtk_tree_path_free (path);
  } else {
    GtkTreeIter iter;

    if (gtk_tree_model_get_iter_first (gtk_tree_view_get_model (view), &iter)) {
      tree_view_set_cursor_from_iter (GTK_TREE_VIEW (plugin_data.view), &iter);
    }
  }
}

static void
on_view_row_activated (GtkTreeView       *view,
                       GtkTreePath       *path,
                       GtkTreeViewColumn *column,
                       gpointer           dummy)
{
  GtkTreeModel *model = gtk_tree_view_get_model (view);
  GtkTreeIter   iter;

  if (gtk_tree_model_get_iter (model, &iter, path)) {
    gint type;

    gtk_tree_model_get (model, &iter, COL_TYPE, &type, -1);
    gtk_widget_hide (plugin_data.panel);
  }
}

static void
create_panel (void)
{
  GtkWidget          *frame;
  GtkWidget          *box;
  GtkWidget          *scroll;
  GtkTreeViewColumn  *col;
  GtkCellRenderer    *cell;

  plugin_data.panel = g_object_new (GTK_TYPE_WINDOW,
                                    "decorated", FALSE,
                                    "default-width", 500,
                                    "default-height", 200,
                                    "transient-for", geany_data->main_widgets->window,
                                    "window-position", GTK_WIN_POS_CENTER_ON_PARENT,
                                    "type-hint", GDK_WINDOW_TYPE_HINT_DIALOG,
                                    "skip-taskbar-hint", TRUE,
                                    "skip-pager-hint", TRUE,
                                    NULL);
  g_signal_connect (plugin_data.panel, "focus-out-event",
                    G_CALLBACK (gtk_widget_hide), NULL);
  g_signal_connect (plugin_data.panel, "show",
                    G_CALLBACK (on_panel_show), NULL);
  g_signal_connect (plugin_data.panel, "hide",
                    G_CALLBACK (on_panel_hide), NULL);
  g_signal_connect (plugin_data.panel, "key-press-event",
                    G_CALLBACK (on_panel_key_press_event), NULL);

  frame = gtk_frame_new (NULL);
  gtk_frame_set_shadow_type (GTK_FRAME (frame), GTK_SHADOW_IN);
  gtk_container_add (GTK_CONTAINER (plugin_data.panel), frame);

  box = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (frame), box);

  plugin_data.entry = gtk_entry_new ();
  gtk_box_pack_start (GTK_BOX (box), plugin_data.entry, FALSE, TRUE, 0);

  plugin_data.store = gtk_list_store_new (COL_COUNT,
                                          G_TYPE_STRING,
                                          G_TYPE_INT,
                                          G_TYPE_STRING,
                                          G_TYPE_ULONG);

  plugin_data.sort = gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (plugin_data.store));
  gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (plugin_data.sort),
                                        GTK_TREE_SORTABLE_DEFAULT_SORT_COLUMN_ID,
                                        GTK_SORT_ASCENDING);

  scroll = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
                         "hscrollbar-policy", GTK_POLICY_AUTOMATIC,
                         "vscrollbar-policy", GTK_POLICY_AUTOMATIC,
                         NULL);
  gtk_box_pack_start (GTK_BOX (box), scroll, TRUE, TRUE, 0);

  plugin_data.view = gtk_tree_view_new_with_model (GTK_TREE_MODEL (plugin_data.sort));
  gtk_widget_set_can_focus (plugin_data.view, FALSE);
  gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (plugin_data.view), FALSE);
  cell = gtk_cell_renderer_text_new ();
  g_object_set (cell, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
  col = gtk_tree_view_column_new_with_attributes (NULL, cell,
                                                  "markup", COL_LABEL,
                                                  NULL);
  gtk_tree_view_append_column (GTK_TREE_VIEW (plugin_data.view), col);
  g_signal_connect (plugin_data.view, "row-activated",
                    G_CALLBACK (on_view_row_activated), NULL);
  gtk_container_add (GTK_CONTAINER (scroll), plugin_data.view);

  /* connect entry signals after the view is created as they use it */
  g_signal_connect (plugin_data.entry, "notify::text",
                    G_CALLBACK (on_entry_text_notify), NULL);
  g_signal_connect (plugin_data.entry, "activate",
                    G_CALLBACK (on_entry_activate), NULL);

  gtk_widget_show_all (frame);
}

static gboolean
on_kb_show_panel (GeanyKeyBinding  *kb,
                  guint             key_id,
                  gpointer          data)
{
  const gchar *prefix = data;

  gtk_widget_show (plugin_data.panel);

  if (prefix) {
    const gchar *key = gtk_entry_get_text (GTK_ENTRY (plugin_data.entry));

    if (! g_str_has_prefix (key, prefix)) {
      gtk_entry_set_text (GTK_ENTRY (plugin_data.entry), prefix);
    }
    /* select the non-prefix part */
    gtk_editable_select_region (GTK_EDITABLE (plugin_data.entry),
                                g_utf8_strlen (prefix, -1), -1);
  }

  return TRUE;
}

static gboolean
on_plugin_idle_init (gpointer dummy)
{
  create_panel ();

  return FALSE;
}

void
plugin_init (GeanyData *data)
{
  GeanyKeyGroup *group;

  group = plugin_set_key_group (geany_plugin, "tagfilter", KB_COUNT, NULL);
  keybindings_set_item_full (group, KB_SHOW_PANEL, 0, 0, "show_panel",
                             _("Show Tag Filter Panel"), NULL,
                             on_kb_show_panel, NULL, NULL);

  /* delay for other plugins to have a chance to load before, so we will
   * include their items */
  plugin_idle_add (geany_plugin, on_plugin_idle_init, NULL);
}

void
plugin_cleanup (void)
{
  if (plugin_data.panel) {
    gtk_widget_destroy (plugin_data.panel);
  }
  if (plugin_data.last_path) {
    gtk_tree_path_free (plugin_data.last_path);
  }
}

void
plugin_help (void)
{
  utils_open_browser (DOCDIR "/" PLUGIN "/README");
}
