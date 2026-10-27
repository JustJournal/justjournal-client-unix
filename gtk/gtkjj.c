/*-
Copyright (C) 2008, 2016, 2026 Lucas Holt. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:
1. Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright
   notice, this list of conditions and the following disclaimer in the
   documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
SUCH DAMAGE.
*/

#include <gtk/gtk.h>

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "../jj_rest.h"

#define NAME "JustJournal/GTK"
#define VERSION JJ_VERSION
#define RECENT_POST_COUNT 20

static void secure_bzero(void *data, size_t size)
{
    volatile unsigned char *p = data;
    while (size--)
        *p++ = 0;
}

typedef struct {
    GtkWidget *window;
    GtkWidget *stack;
    GtkWidget *username_entry;
    GtkWidget *password_entry;
    GtkWidget *entry_list;
    GtkWidget *edit_button;
    GtkWidget *delete_button;
    char *username;
    jj_rest *client;
    json_t *entries;
} App;

typedef struct {
    GtkWidget *window;
    GtkWidget *title;
    GtkWidget *music;
    GtkWidget *tags;
    GtkTextBuffer *body;
    int64_t entry_id; /* Zero means a new entry. */
} Editor;

static App app;

static void show_message(GtkWindow *parent, const char *message)
{
    GtkWidget *dialog = gtk_message_dialog_new(parent, GTK_DIALOG_DESTROY_WITH_PARENT,
        GTK_MESSAGE_INFO, GTK_BUTTONS_OK, "%s", message);
    g_signal_connect(dialog, "response", G_CALLBACK(gtk_widget_destroy), NULL);
    gtk_widget_show(dialog);
}

static json_t *get_tags(GtkWidget *entry)
{
    gchar *normalized = g_utf8_strdown(gtk_entry_get_text(GTK_ENTRY(entry)), -1);
    gchar **values;
    json_t *result;

    if (!normalized)
        return NULL;
    values = g_strsplit_set(normalized, ", \t\r\n\f\v", -1);
    result = json_array();
    for (size_t i = 0; result && values[i]; i++) {
        if (values[i][0] != '\0' &&
            json_array_append_new(result, json_string(values[i])) != 0) {
            json_decref(result);
            result = NULL;
        }
    }
    g_strfreev(values);
    g_free(normalized);
    return result;
}

static int64_t selected_entry_id(void)
{
    GtkListBoxRow *row = gtk_list_box_get_selected_row(GTK_LIST_BOX(app.entry_list));
    gint64 *id = row ? g_object_get_data(G_OBJECT(row), "entry-id") : NULL;
    return id ? (int64_t)*id : 0;
}

static void selection_changed(GtkListBox *list, GtkListBoxRow *row, gpointer data)
{
    (void)list;
    (void)data;
    gtk_widget_set_sensitive(app.edit_button, row != NULL);
    gtk_widget_set_sensitive(app.delete_button, row != NULL);
}

static bool refresh_entries(char *error, size_t error_size)
{
    json_t *entries = jj_rest_recent(app.client, app.username, RECENT_POST_COUNT,
        error, error_size);
    GList *children, *item;

    if (!entries)
        return false;
    for (size_t i = 0; i < json_array_size(entries); i++) {
        json_t *entry = json_array_get(entries, i);
        json_t *id = json_object_get(entry, "entryId");
        if (!json_is_integer(id) || json_integer_value(id) < 1 ||
            !json_is_array(json_object_get(entry, "tags")) ||
            !(json_is_string(json_object_get(entry, "music")) ||
                json_is_null(json_object_get(entry, "music")))) {
            g_snprintf(error, error_size, "Server returned an invalid entry ID.");
            json_decref(entries);
            return false;
        }
    }

    children = gtk_container_get_children(GTK_CONTAINER(app.entry_list));
    for (item = children; item; item = item->next)
        gtk_widget_destroy(GTK_WIDGET(item->data));
    g_list_free(children);
    if (app.entries)
        json_decref(app.entries);
    app.entries = entries;

    for (size_t i = 0; i < json_array_size(entries); i++) {
        json_t *entry = json_array_get(entries, i);
        int64_t id = (int64_t)json_integer_value(json_object_get(entry, "entryId"));
        const char *date = json_string_value(json_object_get(entry, "date"));
        const char *subject = json_string_value(json_object_get(entry, "subject"));
        const char *body = json_string_value(json_object_get(entry, "body"));
        GtkWidget *row = gtk_list_box_row_new();
        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
        GtkWidget *heading, *content;
        gchar *heading_text = g_strdup_printf("#%" G_GINT64_FORMAT "  %s  %s",
            (gint64)id, date, subject);
        gint64 *stored_id = g_new(gint64, 1);

        heading = gtk_label_new(heading_text);
        content = gtk_label_new(body);
        g_free(heading_text);
        gtk_label_set_xalign(GTK_LABEL(heading), 0);
        gtk_label_set_xalign(GTK_LABEL(content), 0);
        gtk_label_set_line_wrap(GTK_LABEL(heading), TRUE);
        gtk_label_set_line_wrap(GTK_LABEL(content), TRUE);
        gtk_widget_set_margin_start(box, 8);
        gtk_widget_set_margin_end(box, 8);
        gtk_widget_set_margin_top(box, 8);
        gtk_widget_set_margin_bottom(box, 8);
        gtk_box_pack_start(GTK_BOX(box), heading, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(box), content, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(row), box);
        *stored_id = (gint64)id;
        g_object_set_data_full(G_OBJECT(row), "entry-id", stored_id, g_free);
        gtk_container_add(GTK_CONTAINER(app.entry_list), row);
    }
    gtk_widget_show_all(app.entry_list);
    selection_changed(GTK_LIST_BOX(app.entry_list),
        gtk_list_box_get_selected_row(GTK_LIST_BOX(app.entry_list)), NULL);
    return true;
}

static void refresh_clicked(GtkButton *button, gpointer data)
{
    char error[256];
    (void)button;
    (void)data;
    if (!refresh_entries(error, sizeof(error)))
        show_message(GTK_WINDOW(app.window), error);
}

static void login_clicked(GtkButton *button, gpointer data)
{
    const char *username = gtk_entry_get_text(GTK_ENTRY(app.username_entry));
    const char *password = gtk_entry_get_text(GTK_ENTRY(app.password_entry));
    char error[256];
    jj_rest *client;

    (void)button;
    (void)data;
    client = jj_rest_open(JJ_BASE_URL, NAME "/" VERSION, error, sizeof(error));
    if (!client || !jj_rest_login(client, username, password, error, sizeof(error))) {
        jj_rest_close(client);
        gtk_entry_set_text(GTK_ENTRY(app.password_entry), "");
        show_message(GTK_WINDOW(app.window), error);
        return;
    }
    app.username = g_strdup(username);
    app.client = client;
    gtk_entry_set_text(GTK_ENTRY(app.password_entry), "");
    gtk_stack_set_visible_child_name(GTK_STACK(app.stack), "entries");
    if (!refresh_entries(error, sizeof(error)))
        show_message(GTK_WINDOW(app.window), error);
}

static void editor_destroyed(GtkWidget *window, Editor *editor)
{
    (void)window;
    g_free(editor);
}

static void clipboard_clicked(GtkButton *button, Editor *editor)
{
    const char *action = g_object_get_data(G_OBJECT(button), "clipboard-action");
    GtkClipboard *clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);

    if (strcmp(action, "cut") == 0)
        gtk_text_buffer_cut_clipboard(editor->body, clipboard, TRUE);
    else if (strcmp(action, "copy") == 0)
        gtk_text_buffer_copy_clipboard(editor->body, clipboard);
    else
        gtk_text_buffer_paste_clipboard(editor->body, clipboard, NULL, TRUE);
}

/* Keep the editor open while obtaining a fresh in-memory session. */
static int reauthenticate(Editor *editor, char *error, size_t error_size)
{
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Session expired",
        GTK_WINDOW(editor->window), GTK_DIALOG_MODAL,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Log in", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *label = gtk_label_new("Enter your password to save these changes:");
    GtkWidget *password = gtk_entry_new();
    int response, result = 0;

    gtk_entry_set_visibility(GTK_ENTRY(password), FALSE);
    gtk_entry_set_input_purpose(GTK_ENTRY(password), GTK_INPUT_PURPOSE_PASSWORD);
    gtk_entry_set_activates_default(GTK_ENTRY(password), TRUE);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    gtk_box_pack_start(GTK_BOX(content), label, FALSE, FALSE, 6);
    gtk_box_pack_start(GTK_BOX(content), password, FALSE, FALSE, 6);
    gtk_widget_show_all(dialog);
    gtk_widget_grab_focus(password);
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response == GTK_RESPONSE_ACCEPT) {
        result = jj_rest_login(app.client, app.username,
            gtk_entry_get_text(GTK_ENTRY(password)), error, error_size) ? 1 : -1;
    }
    gtk_entry_set_text(GTK_ENTRY(password), "");
    gtk_widget_destroy(dialog);
    return result;
}

static void editor_saved(GtkButton *button, Editor *editor)
{
    GtkTextIter start, end;
    gchar *body;
    json_t *tags;
    char error[256];
    bool success;

    (void)button;
    gtk_text_buffer_get_bounds(editor->body, &start, &end);
    body = gtk_text_buffer_get_text(editor->body, &start, &end, FALSE);
    tags = get_tags(editor->tags);
    if (!tags) {
        show_message(GTK_WINDOW(editor->window), "Unable to encode tags.");
        g_free(body);
        return;
    }

    if (editor->entry_id) {
        success = jj_rest_edit_with_metadata(app.client, app.username,
            editor->entry_id, gtk_entry_get_text(GTK_ENTRY(editor->title)), body,
            gtk_entry_get_text(GTK_ENTRY(editor->music)), tags, error, sizeof(error));
        if (!success && jj_rest_last_auth_failure(app.client)) {
            int login_result = reauthenticate(editor, error, sizeof(error));
            if (login_result == 1)
                success = jj_rest_edit_with_metadata(app.client, app.username,
                    editor->entry_id, gtk_entry_get_text(GTK_ENTRY(editor->title)), body,
                    gtk_entry_get_text(GTK_ENTRY(editor->music)), tags, error, sizeof(error));
            else if (login_result == 0) {
                secure_bzero(body, strlen(body));
                g_free(body);
                json_decref(tags);
                return;
            }
        }
    } else {
        success = jj_rest_post(app.client, gtk_entry_get_text(GTK_ENTRY(editor->title)),
            body, gtk_entry_get_text(GTK_ENTRY(editor->music)), tags,
            error, sizeof(error));
    }
    secure_bzero(body, strlen(body));
    g_free(body);
    json_decref(tags);
    if (!success) {
        show_message(GTK_WINDOW(editor->window), error);
        return;
    }
    gtk_widget_destroy(editor->window);
    if (!refresh_entries(error, sizeof(error))) {
        char message[320];
        g_snprintf(message, sizeof(message), "Entry saved, but the recent list could not refresh: %s", error);
        show_message(GTK_WINDOW(app.window), message);
    }
}

static Editor *open_editor(int64_t entry_id)
{
    Editor *editor = g_new0(Editor, 1);
    GtkWidget *layout, *grid, *scrolled, *toolbar, *button, *label, *text_view;
    const char *names[] = {"_Cut", "_Copy", "_Paste"};
    const char *actions[] = {"cut", "copy", "paste"};

    editor->entry_id = entry_id;
    editor->window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(editor->window), entry_id ? "Edit entry" : "New entry");
    gtk_window_set_default_size(GTK_WINDOW(editor->window), 560, 480);
    gtk_window_set_transient_for(GTK_WINDOW(editor->window), GTK_WINDOW(app.window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(editor->window), TRUE);
    gtk_container_set_border_width(GTK_CONTAINER(editor->window), 10);
    g_signal_connect(editor->window, "destroy", G_CALLBACK(editor_destroyed), editor);

    layout = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    editor->title = gtk_entry_new();
    editor->music = gtk_entry_new();
    editor->tags = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(editor->title), 255);
    gtk_entry_set_max_length(GTK_ENTRY(editor->music), 125);
    gtk_widget_set_tooltip_text(editor->tags, "Separate tags with spaces or commas.");
    GtkWidget *fields[] = {editor->title, editor->music, editor->tags};
    const char *field_names[] = {"_Title", "_Music", "Ta_gs"};
    for (int i = 0; i < 3; i++) {
        label = gtk_label_new_with_mnemonic(field_names[i]);
        gtk_label_set_mnemonic_widget(GTK_LABEL(label), fields[i]);
        gtk_label_set_xalign(GTK_LABEL(label), 0);
        gtk_widget_set_hexpand(fields[i], TRUE);
        gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), fields[i], 1, i, 1, 1);
    }
    gtk_box_pack_start(GTK_BOX(layout), grid, FALSE, FALSE, 0);

    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    for (int i = 0; i < 3; i++) {
        button = gtk_button_new_with_mnemonic(names[i]);
        g_object_set_data(G_OBJECT(button), "clipboard-action", (gpointer)actions[i]);
        g_signal_connect(button, "clicked", G_CALLBACK(clipboard_clicked), editor);
        gtk_box_pack_start(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    }
    gtk_box_pack_start(GTK_BOX(layout), toolbar, FALSE, FALSE, 0);
    text_view = gtk_text_view_new();
    editor->body = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text_view));
    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), text_view);
    gtk_box_pack_start(GTK_BOX(layout), scrolled, TRUE, TRUE, 0);
    button = gtk_button_new_with_mnemonic(entry_id ? "_Save changes" : "_Post");
    g_signal_connect(button, "clicked", G_CALLBACK(editor_saved), editor);
    gtk_box_pack_start(GTK_BOX(layout), button, FALSE, FALSE, 0);
    gtk_container_add(GTK_CONTAINER(editor->window), layout);

    if (entry_id && app.entries) {
        for (size_t i = 0; i < json_array_size(app.entries); i++) {
            json_t *entry = json_array_get(app.entries, i);
            json_t *entry_tags;
            GString *tag_text;
            if (json_integer_value(json_object_get(entry, "entryId")) != entry_id)
                continue;
            gtk_entry_set_text(GTK_ENTRY(editor->title),
                json_string_value(json_object_get(entry, "subject")));
            gtk_entry_set_text(GTK_ENTRY(editor->music),
                json_string_value(json_object_get(entry, "music")) ?
                json_string_value(json_object_get(entry, "music")) : "");
            gtk_text_buffer_set_text(editor->body,
                json_string_value(json_object_get(entry, "body")), -1);
            tag_text = g_string_new(NULL);
            entry_tags = json_object_get(entry, "tags");
            for (size_t j = 0; json_is_array(entry_tags) && j < json_array_size(entry_tags); j++) {
                const char *tag = json_string_value(json_array_get(entry_tags, j));
                if (tag) {
                    if (tag_text->len)
                        g_string_append(tag_text, ", ");
                    g_string_append(tag_text, tag);
                }
            }
            gtk_entry_set_text(GTK_ENTRY(editor->tags), tag_text->str);
            g_string_free(tag_text, TRUE);
            break;
        }
    }
    gtk_widget_show_all(editor->window);
    return editor;
}

static void add_clicked(GtkButton *button, gpointer data)
{
    (void)button;
    (void)data;
    open_editor(0);
}

static void edit_clicked(GtkButton *button, gpointer data)
{
    int64_t id = selected_entry_id();
    (void)button;
    (void)data;
    if (id)
        open_editor(id);
}

static bool delete_entry(int64_t entry_id, char *error, size_t error_size)
{
    if (!jj_rest_delete(app.client, entry_id, error, error_size))
        return false;
    if (!refresh_entries(error, error_size)) {
        char detail[256];
        g_strlcpy(detail, error, sizeof(detail));
        g_snprintf(error, error_size, "Entry deleted, but the recent list could not refresh: %s", detail);
        return false;
    }
    return true;
}

static void delete_clicked(GtkButton *button, gpointer data)
{
    int64_t id = selected_entry_id();
    GtkWidget *dialog;
    char error[256];
    int response;

    (void)button;
    (void)data;
    if (!id)
        return;
    dialog = gtk_message_dialog_new(GTK_WINDOW(app.window), GTK_DIALOG_MODAL,
        GTK_MESSAGE_WARNING, GTK_BUTTONS_NONE,
        "Delete entry #%" G_GINT64_FORMAT " and its comments?", (gint64)id);
    gtk_dialog_add_buttons(GTK_DIALOG(dialog), "_Cancel", GTK_RESPONSE_CANCEL,
        "_Delete", GTK_RESPONSE_ACCEPT, NULL);
    response = gtk_dialog_run(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    if (response == GTK_RESPONSE_ACCEPT && !delete_entry(id, error, sizeof(error)))
        show_message(GTK_WINDOW(app.window), error);
}

static void create_main_window(void)
{
    GtkWidget *login, *grid, *label, *button, *view, *toolbar, *scrolled;

    app.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.window), NAME);
    gtk_window_set_default_size(GTK_WINDOW(app.window), 700, 520);
    gtk_container_set_border_width(GTK_CONTAINER(app.window), 10);
    g_signal_connect(app.window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    app.stack = gtk_stack_new();
    gtk_container_add(GTK_CONTAINER(app.window), app.stack);

    login = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    app.username_entry = gtk_entry_new();
    app.password_entry = gtk_entry_new();
    gtk_entry_set_visibility(GTK_ENTRY(app.password_entry), FALSE);
    gtk_entry_set_invisible_char(GTK_ENTRY(app.password_entry), '*');
    label = gtk_label_new_with_mnemonic("_Username");
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), app.username_entry);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app.username_entry, 1, 0, 1, 1);
    label = gtk_label_new_with_mnemonic("_Password");
    gtk_label_set_mnemonic_widget(GTK_LABEL(label), app.password_entry);
    gtk_grid_attach(GTK_GRID(grid), label, 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app.password_entry, 1, 1, 1, 1);
    gtk_widget_set_hexpand(app.username_entry, TRUE);
    gtk_widget_set_hexpand(app.password_entry, TRUE);
    gtk_box_pack_start(GTK_BOX(login), grid, FALSE, FALSE, 0);
    button = gtk_button_new_with_mnemonic("_Log in");
    g_signal_connect(button, "clicked", G_CALLBACK(login_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(login), button, FALSE, FALSE, 0);
    gtk_stack_add_named(GTK_STACK(app.stack), login, "login");

    view = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    button = gtk_button_new_with_mnemonic("_Add entry");
    g_signal_connect(button, "clicked", G_CALLBACK(add_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    app.edit_button = gtk_button_new_with_mnemonic("_Edit selected");
    g_signal_connect(app.edit_button, "clicked", G_CALLBACK(edit_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), app.edit_button, FALSE, FALSE, 0);
    app.delete_button = gtk_button_new_with_mnemonic("_Delete selected");
    g_signal_connect(app.delete_button, "clicked", G_CALLBACK(delete_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), app.delete_button, FALSE, FALSE, 0);
    button = gtk_button_new_with_mnemonic("_Refresh");
    g_signal_connect(button, "clicked", G_CALLBACK(refresh_clicked), NULL);
    gtk_box_pack_start(GTK_BOX(toolbar), button, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(view), toolbar, FALSE, FALSE, 0);
    app.entry_list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(app.entry_list), GTK_SELECTION_SINGLE);
    g_signal_connect(app.entry_list, "row-selected", G_CALLBACK(selection_changed), NULL);
    scrolled = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scrolled), app.entry_list);
    gtk_box_pack_start(GTK_BOX(view), scrolled, TRUE, TRUE, 0);
    gtk_stack_add_named(GTK_STACK(app.stack), view, "entries");
    gtk_stack_set_visible_child_name(GTK_STACK(app.stack), "login");
    selection_changed(GTK_LIST_BOX(app.entry_list), NULL, NULL);
    gtk_widget_show_all(app.window);
}

int main(int argc, char *argv[])
{
    gtk_init(&argc, &argv);
    create_main_window();
    gtk_main();
    if (app.entries)
        json_decref(app.entries);
    jj_rest_close(app.client);
    g_free(app.username);
    return 0;
}
