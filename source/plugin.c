/*
 * plugin.c
 *
 * Copyright 2013 Matthew <mbrush@codebrainz.ca>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include "plugin.h"
#include "config.h"
#include "format.h"
#include "prefs.h"
#include "style.h"

#ifndef _
#define _(s) s
#endif

GeanyPlugin *geany_plugin;
GeanyData *geany_data;

PLUGIN_VERSION_CHECK(211)

PLUGIN_SET_INFO(_("Code Format"), _("Format source code using clang-format."),
                _(VERSION), _("Matthew Brush <matt@geany.org>"))

enum
{
  FORMAT_KEY_REGION,
  FORMAT_KEY_DOCUMENT,
  FORMAT_KEY_SESSION,
};

static GtkWidget *main_menu_item = NULL;

static bool fmt_is_supported_ft(GeanyDocument *doc)
{
  int id;
  if (!DOC_VALID(doc))
    doc = document_get_current();
  if (!DOC_VALID(doc))
    return false;
  id = doc->file_type->id;
  return (id == GEANY_FILETYPES_C || id == GEANY_FILETYPES_CPP ||
          id == GEANY_FILETYPES_OBJECTIVEC || id == GEANY_FILETYPES_JAVA ||
          id == GEANY_FILETYPES_JS || id == GEANY_FILETYPES_CS);
}

static void do_format(GeanyDocument *doc, bool entire_doc, bool autof);
static void do_format_session(void);

bool on_key_binding(int key_id)
{
  if (!fmt_is_supported_ft(NULL))
    return true;
  switch (key_id)
  {
    case FORMAT_KEY_REGION:
      do_format(NULL, false, false);
      break;
    case FORMAT_KEY_DOCUMENT:
      do_format(NULL, true, false);
      break;
    case FORMAT_KEY_SESSION:
      do_format_session();
      break;
    default:
      return false;
  }
  return true;
}

static gboolean on_editor_notify(G_GNUC_UNUSED GObject *obj,
                                 G_GNUC_UNUSED GeanyEditor *editor,
                                 SCNotification *notif,
                                 G_GNUC_UNUSED gpointer user_data)
{
  if (fmt_prefs_get_auto_format() && fmt_is_supported_ft(editor->document) &&
      notif->nmhdr.code == SCN_CHARADDED)
  {
    if (strchr(fmt_prefs_get_trigger(), notif->ch) != NULL)
      do_format(NULL, true, true); // FIXME: is it better to use region/line
                                   // for auto-format?
  }
  return false;
}

static void on_plugin_configure_response(G_GNUC_UNUSED GtkDialog *dialog,
                                         int response, GtkWidget *panel)
{
  if (response == GTK_RESPONSE_OK || response == GTK_RESPONSE_ACCEPT)
    fmt_prefs_save_panel(panel, false);
}

static void on_menu_item_activate(G_GNUC_UNUSED GtkMenuItem *item,
                                  gpointer user_data)
{
  on_key_binding(GPOINTER_TO_INT(user_data));
}

static void on_project_dialog_open(GObject *obj, GtkWidget *notebook,
                                   gpointer user_data)
{
  GtkWidget *pnl;

  pnl = fmt_prefs_create_panel(true);
  if (!pnl)
    return;

  g_object_set_data(G_OBJECT(notebook), "code-format-panel", pnl);

  gtk_notebook_append_page(GTK_NOTEBOOK(notebook), pnl,
                           gtk_label_new(_("Code Format")));
}

static void on_project_dialog_confirmed(G_GNUC_UNUSED GObject *obj,
                                        GtkWidget *notebook,
                                        G_GNUC_UNUSED gpointer user_data)
{
  GtkWidget *pnl = g_object_get_data(G_OBJECT(notebook), "code-format-panel");
  if (GTK_IS_CONTAINER(pnl))
    fmt_prefs_save_panel(pnl, true);
}

static void on_project_dialog_close(GObject *obj, GtkWidget *notebook,
                                    gpointer user_data)
{
  GtkWidget *wid = g_object_get_data(G_OBJECT(notebook), "code-format-panel");
  if (GTK_IS_CONTAINER(wid))
  {
    gtk_notebook_remove_page(
        GTK_NOTEBOOK(notebook),
        gtk_notebook_page_num(GTK_NOTEBOOK(notebook), wid));
  }
}

static void on_project_open(GObject *obj, GKeyFile *kf, gpointer user_data)
{
  fmt_prefs_open_project(kf);
}

static void on_project_close(GObject *obj, GKeyFile *kf, gpointer user_data)
{
  fmt_prefs_close_project();
}

static void on_project_save(GObject *obj, GKeyFile *kf, gpointer user_data)
{
  fmt_prefs_save_project(kf);
}

static void on_open_config_file(GtkMenuItem *item, gpointer user_data)
{
  char *fn;
  GeanyDocument *doc = document_get_current();

  if (!DOC_VALID(doc) || !doc->file_name)
    return;

  fn = fmt_lookup_clang_format_dot_file(doc->file_name);
  if (fn)
  {
    document_open_file(fn, false, filetypes[GEANY_FILETYPES_YAML], NULL);
    g_free(fn);
  }
}

static void on_open_config_item_map(GtkWidget *wid, gpointer user_data)
{
  GeanyDocument *doc = document_get_current();

  if (!DOC_VALID(doc) || !doc->file_name)
    return;

  gtk_widget_set_sensitive(wid,
                           fmt_can_find_clang_format_dot_file(doc->file_name));
}

static void on_tools_item_map(GtkWidget *wid, gpointer user_data)
{
  gtk_widget_set_sensitive(wid, fmt_is_supported_ft(NULL));
}

static void on_auto_format_item_toggled(GtkCheckMenuItem *item,
                                        gpointer user_data)
{
  fmt_prefs_set_auto_format(gtk_check_menu_item_get_active(item));
}

static void on_auto_format_item_map(GtkCheckMenuItem *item, gpointer user_data)
{
  gtk_check_menu_item_set_active(item, fmt_prefs_get_auto_format());
}

static void on_document_before_save(GObject *obj, GeanyDocument *doc,
                                    gpointer user_data)
{
  if (fmt_prefs_get_format_on_save() && fmt_is_supported_ft(doc))
    do_format(doc, true, false);
}

void plugin_init(G_GNUC_UNUSED GeanyData *data)
{
  GeanyKeyGroup *group;
  GtkWidget *menu, *item;

  fmt_prefs_init();

#define CONNECT(sig, cb) \
  plugin_signal_connect(geany_plugin, NULL, sig, TRUE, G_CALLBACK(cb), NULL)

  CONNECT("editor-notify", on_editor_notify);
  CONNECT("project-dialog-open", on_project_dialog_open);
  CONNECT("project-dialog-close", on_project_dialog_close);
  CONNECT("project-dialog-confirmed", on_project_dialog_confirmed);
  CONNECT("project-open", on_project_open);
  CONNECT("project-close", on_project_close);
  CONNECT("project-save", on_project_save);
  CONNECT("document-before-save", on_document_before_save);

#undef CONNECT

  group = plugin_set_key_group(geany_plugin, _("Code Formatting"), 3,
                               (GeanyKeyGroupCallback)on_key_binding);

  main_menu_item = gtk_menu_item_new_with_label(_("Code Format"));
  ui_add_document_sensitive(main_menu_item);
  g_signal_connect(main_menu_item, "map", G_CALLBACK(on_tools_item_map), NULL);
  menu = gtk_menu_new();
  gtk_menu_item_set_submenu(GTK_MENU_ITEM(main_menu_item), menu);

  item = gtk_check_menu_item_new_with_label(_("Auto-Formatting"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  g_signal_connect(item, "toggled", G_CALLBACK(on_auto_format_item_toggled),
                   NULL);
  g_signal_connect(item, "map", G_CALLBACK(on_auto_format_item_map), NULL);

  item = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

  item = gtk_menu_item_new_with_label(_("Current Line or Selection"));
  g_signal_connect(item, "activate", G_CALLBACK(on_menu_item_activate),
                   GINT_TO_POINTER(FORMAT_KEY_REGION));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  keybindings_set_item(group, FORMAT_KEY_REGION, NULL, 0, 0, "format_region",
                       _("Format current line or selection"), item);

  item = gtk_menu_item_new_with_label(_("Entire Document"));
  g_signal_connect(item, "activate", G_CALLBACK(on_menu_item_activate),
                   GINT_TO_POINTER(FORMAT_KEY_DOCUMENT));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  keybindings_set_item(group, FORMAT_KEY_DOCUMENT, NULL, 0, 0,
                       "format_document", _("Format entire document"), item);

  item = gtk_menu_item_new_with_label(_("Entire Session"));
  g_signal_connect(item, "activate", G_CALLBACK(on_menu_item_activate),
                   GINT_TO_POINTER(FORMAT_KEY_SESSION));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  keybindings_set_item(group, FORMAT_KEY_SESSION, NULL, 0, 0, "format_session",
                       _("Format entire session"), item);

  item = gtk_separator_menu_item_new();
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);

  item = gtk_menu_item_new_with_label(_("Open Configuration File"));
  gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  g_signal_connect(item, "activate", G_CALLBACK(on_open_config_file), NULL);
  g_signal_connect(item, "map", G_CALLBACK(on_open_config_item_map), NULL);

  gtk_widget_show_all(main_menu_item);

  gtk_menu_shell_append(GTK_MENU_SHELL(geany_data->main_widgets->tools_menu),
                        main_menu_item);
}

void plugin_cleanup(void)
{
  fmt_prefs_deinit();
  gtk_widget_destroy(main_menu_item);
}

GtkWidget *plugin_configure(GtkDialog *dlg)
{
  GtkWidget *pnl = fmt_prefs_create_panel(false);
  g_signal_connect(dlg, "response", G_CALLBACK(on_plugin_configure_response),
                   pnl);
  return pnl;
}

void plugin_help(void)
{
  // FIXME: if FMT_README_FILE isn't defined or not found on disk, open the
  // web page version in the browser instead.
  document_open_file(FMT_README_FILE, true, filetypes[GEANY_FILETYPES_MARKDOWN],
                     NULL);
}

static void do_format(GeanyDocument *doc, bool entire_doc, bool autof)
{
  GString *formatted;
  ScintillaObject *sci;
  size_t offset = 0, length = 0, sci_len;
  size_t cursor_pos, old_first_line, new_first_line, line_delta;
  const char *sci_buf;
  bool changed = true;
  bool was_changed;

  if (doc == NULL)
    doc = document_get_current();

  if (!DOC_VALID(doc))
  {
    g_warning("Cannot format with no documents open");
    return;
  }
  sci = doc->editor->sci;
  was_changed = doc->changed;

  // FIXME: instead of failing, ask user to save the document once
  if (!doc->real_path)
  {
    g_warning("Cannot format document that's never been saved");
    return;
  }

  if (!entire_doc)
  {
    if (sci_has_selection(sci))
    { // format selection
      offset = sci_get_selection_start(sci);
      length = sci_get_selection_end(sci) - offset;
    }
    else
    { // format current line
      size_t cur_line = sci_get_current_line(sci);
      offset = sci_get_position_from_line(sci, cur_line);
      length = sci_get_line_end_position(sci, cur_line) - offset;
    }
  }
  else
  { // format entire document
    offset = 0;
    length = sci_get_length(sci);
  }

  cursor_pos = sci_get_current_position(sci);
  sci_len = sci_get_length(sci);
  sci_buf =
      (const char *)scintilla_send_message(sci, SCI_GETCHARACTERPOINTER, 0, 0);

  formatted = fmt_clang_format(doc->file_name, sci_buf, sci_len, &cursor_pos,
                               offset, length, false);

  // FIXME: handle better
  if (formatted == NULL)
    return;

  if (!autof)
  {
    changed = (formatted->len != sci_len) ||
              (g_strcmp0(formatted->str, sci_buf) != 0);
  }

  old_first_line = scintilla_send_message(sci, SCI_GETFIRSTVISIBLELINE, 0, 0);

  // Replace document text and move cursor to new position
  scintilla_send_message(sci, SCI_BEGINUNDOACTION, 0, 0);
  scintilla_send_message(sci, SCI_CLEARALL, 0, 0);
  scintilla_send_message(sci, SCI_ADDTEXT, formatted->len,
                         (sptr_t)formatted->str);
  scintilla_send_message(sci, SCI_GOTOPOS, cursor_pos, 0);
  new_first_line = scintilla_send_message(sci, SCI_GETFIRSTVISIBLELINE, 0, 0);
  line_delta = new_first_line - old_first_line;
  scintilla_send_message(sci, SCI_LINESCROLL, 0, -line_delta);
  scintilla_send_message(sci, SCI_ENDUNDOACTION, 0, 0);

  document_set_text_changed(doc, (was_changed || changed));

  g_string_free(formatted, true);
}

static void do_format_session(void)
{
  guint i;
  foreach_document(i)
  {
    GeanyDocument *doc = documents[i];
    if (fmt_is_supported_ft(doc))
      do_format(doc, true, false);
  }
}
