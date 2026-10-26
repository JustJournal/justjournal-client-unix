/* $Id: gtkjj.c,v 1.4 2008/09/27 02:06:02 laffer1 Exp $ */
/*-
Copyright (C) 2008, 2016 Lucas Holt. All rights reserved.

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

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "../jj_rest.h"

/* Portable secure memory zeroing */
#include <strings.h>
#if defined(HAVE_EXPLICIT_BZERO)
#define secure_bzero explicit_bzero
#else
/* Fallback for platforms without explicit_bzero (e.g., musl, older glibc, macOS) */
static inline void secure_bzero(void *s, size_t n) {
    memset(s, 0, n);
    /* Compiler barrier to prevent optimization */
    __asm__ __volatile__("" : : "r"(s), "r"(n) : "memory");
}
#endif

#define NAME "JustJournal/GTK"
#define VERSION JJ_VERSION

GtkWidget *user, *pass, *title, *music, *tags; /* textboxes */
GtkTextBuffer *buffer;

static void button_clicked (GtkButton *button, GtkWindow *parent);
static void msgbox( GtkWindow *parent, char * msg );
static void cut_clicked (GtkButton*, GtkTextView*);
static void copy_clicked (GtkButton*, GtkTextView*);
static void paste_clicked (GtkButton*, GtkTextView*);

void gtk_box_pack_start_defaults(GtkBox *box, GtkWidget *widget)  {
	gtk_box_pack_start(box, widget, TRUE, TRUE, 0);
}

int main( int argc, char *argv[] )
{
    GtkWidget *window, *vbox, *authbox, *authbox2, *vauthbox, *hboxccp;
    GtkWidget *lbluser, *lblpass, *lbltitle, *lblmusic, *lbltags; /* labels */
    GtkWidget *titlebox, *musicbox, *tagsbox;
    GtkWidget *submit, *cut, *copy, *paste; /* buttons */
    GtkWidget *scrolled_win, *textview = NULL;

    gtk_init( &argc, &argv );

    window = gtk_window_new( GTK_WINDOW_TOPLEVEL );
    gtk_window_set_title( GTK_WINDOW (window), NAME );
    gtk_container_set_border_width( GTK_CONTAINER (window), 10 );
    gtk_widget_set_size_request( window, 400, 400 );

    g_signal_connect (G_OBJECT (window), "destroy",
                  G_CALLBACK (gtk_main_quit), NULL);

    cut = gtk_button_new_from_icon_name("edit-cut", GTK_ICON_SIZE_SMALL_TOOLBAR);
    copy = gtk_button_new_from_icon_name("edit-copy", GTK_ICON_SIZE_SMALL_TOOLBAR);
    paste = gtk_button_new_from_icon_name("edit-paste", GTK_ICON_SIZE_SMALL_TOOLBAR);

    hboxccp = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start (GTK_BOX (hboxccp), cut, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (hboxccp), copy, TRUE, TRUE, 0);
    gtk_box_pack_start (GTK_BOX (hboxccp), paste, TRUE, TRUE, 0);


    g_signal_connect (G_OBJECT (cut), "clicked",
                    G_CALLBACK (cut_clicked),
                    (gpointer) textview);
    g_signal_connect (G_OBJECT (copy), "clicked",
                    G_CALLBACK (copy_clicked),
                    (gpointer) textview);
    g_signal_connect (G_OBJECT (paste), "clicked",
                    G_CALLBACK (paste_clicked),
                    (gpointer) textview);

    submit = gtk_button_new_with_mnemonic("_Post");
    g_signal_connect (G_OBJECT (submit), "clicked",
                    G_CALLBACK (button_clicked),
                    (gpointer) window);

    lbluser = gtk_label_new( "Username" );
    lblpass = gtk_label_new( "Password" );
    user = gtk_entry_new();
    pass = gtk_entry_new();
    gtk_entry_set_visibility( GTK_ENTRY (pass), FALSE );
    gtk_entry_set_invisible_char( GTK_ENTRY (pass), '*' );

    lbltitle = gtk_label_new_with_mnemonic("_Title");
    title = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(title), 255);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbltitle), title);
    titlebox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start_defaults(GTK_BOX(titlebox), lbltitle);
    gtk_box_pack_start_defaults(GTK_BOX(titlebox), title);

    lblmusic = gtk_label_new_with_mnemonic("_Music");
    music = gtk_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(music), 125);
    gtk_label_set_mnemonic_widget(GTK_LABEL(lblmusic), music);
    musicbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start_defaults(GTK_BOX(musicbox), lblmusic);
    gtk_box_pack_start_defaults(GTK_BOX(musicbox), music);

    lbltags = gtk_label_new_with_mnemonic("Ta_gs");
    tags = gtk_entry_new();
    gtk_widget_set_tooltip_text(tags, "Separate tags with spaces or commas.");
    gtk_label_set_mnemonic_widget(GTK_LABEL(lbltags), tags);
    tagsbox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_box_pack_start_defaults(GTK_BOX(tagsbox), lbltags);
    gtk_box_pack_start_defaults(GTK_BOX(tagsbox), tags);

    /* setup textview */
    textview = gtk_text_view_new();
    buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (textview));
    scrolled_win = gtk_scrolled_window_new (NULL, NULL);
    gtk_container_add (GTK_CONTAINER (scrolled_win), textview);
    gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_win),
                                  GTK_POLICY_AUTOMATIC, GTK_POLICY_ALWAYS);

    /* create username hbox */
    authbox = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 5 );
    gtk_box_pack_start_defaults( GTK_BOX (authbox), lbluser );
    gtk_box_pack_start_defaults( GTK_BOX (authbox), user );

    /* create password hbox */
    authbox2 = gtk_box_new( GTK_ORIENTATION_HORIZONTAL, 5 );
    gtk_box_pack_start_defaults( GTK_BOX (authbox2), lblpass );
    gtk_box_pack_start_defaults( GTK_BOX (authbox2), pass );

    vauthbox = gtk_box_new( GTK_ORIENTATION_VERTICAL, 5 );
    gtk_box_pack_start_defaults( GTK_BOX (vauthbox), authbox );
    gtk_box_pack_start_defaults( GTK_BOX (vauthbox), authbox2 );

    /* Setup the final box for layout in the window */
    vbox = gtk_box_new( GTK_ORIENTATION_VERTICAL, 5 );
    gtk_box_pack_start( GTK_BOX (vbox), hboxccp, FALSE, TRUE, 5 );
    gtk_box_pack_start( GTK_BOX (vbox), vauthbox, FALSE, TRUE, 5 );
    gtk_box_pack_start( GTK_BOX (vbox), titlebox, FALSE, TRUE, 5 );
    gtk_box_pack_start( GTK_BOX (vbox), musicbox, FALSE, TRUE, 5 );
    gtk_box_pack_start( GTK_BOX (vbox), tagsbox, FALSE, TRUE, 5 );
    gtk_box_pack_start( GTK_BOX (vbox), scrolled_win, TRUE, TRUE, 5 );
    gtk_box_pack_start( GTK_BOX (vbox), submit, FALSE, TRUE, 5 );

    gtk_container_add( GTK_CONTAINER (window), vbox );
    gtk_widget_show_all( window );

    gtk_main();
    return 0;
}

/* Match the website's comma/whitespace-separated, lowercase tag input. */
static json_t *get_tags(void)
{
    gchar *normalized = g_utf8_strdown(gtk_entry_get_text(GTK_ENTRY(tags)), -1);
    gchar **values = g_strsplit_set(normalized, ", \t\r\n\f\v", -1);
    json_t *result = json_array();

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

static void button_clicked( GtkButton *button, GtkWindow *parent )
{
    jj_rest *client;
    const gchar *u, *p, *subject, *music_text;
    json_t *tag_list;
    char *c;
    GtkTextIter start, end;
    char error[256];
    bool success;

    (void)button;
    u = gtk_entry_get_text(GTK_ENTRY(user));
    p = gtk_entry_get_text(GTK_ENTRY(pass));
    subject = gtk_entry_get_text(GTK_ENTRY(title));
    music_text = gtk_entry_get_text(GTK_ENTRY(music));
    tag_list = get_tags();
    if (!tag_list) {
        msgbox(parent, "Unable to encode tags.");
        return;
    }
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    c = gtk_text_iter_get_text(&start, &end);

    client = jj_rest_open(JJ_BASE_URL, NAME "/" VERSION, error, sizeof(error));
    success = client && jj_rest_login(client, u, p, error, sizeof(error));
    if (success)
        success = jj_rest_post(client, subject, c, music_text, tag_list, error, sizeof(error));
    jj_rest_close(client);
    json_decref(tag_list);

    if (success)
    {
        gtk_entry_set_text(GTK_ENTRY(title), "");
        gtk_entry_set_text(GTK_ENTRY(music), "");
        gtk_entry_set_text(GTK_ENTRY(tags), "");
        gtk_text_buffer_set_text(buffer, "", -1);
    }
    else
        msgbox(parent, error);

    secure_bzero(c, strlen(c));
    g_free(c);
}

static void msgbox( GtkWindow * parent, char * msg )
{
    GtkWidget *dialog, *label, *image, *hbox;

    /* Create a non-modal dialog with one OK button. */
    dialog = gtk_dialog_new_with_buttons ("Information", parent,
                                        GTK_DIALOG_DESTROY_WITH_PARENT,
					   ("_OK"),
                                      	GTK_RESPONSE_ACCEPT,
                                      	NULL);

    //gtk_dialog_set_has_separator (GTK_DIALOG (dialog), FALSE);

    label = gtk_label_new (msg);
    image = gtk_image_new_from_icon_name("dialog-information", GTK_ICON_SIZE_DIALOG);

    hbox = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5);
    gtk_container_set_border_width (GTK_CONTAINER (hbox), 10);
    gtk_box_pack_start_defaults (GTK_BOX (hbox), image);
    gtk_box_pack_start_defaults (GTK_BOX (hbox), label);

    gtk_box_pack_start_defaults (GTK_BOX (gtk_dialog_get_content_area (GTK_DIALOG (dialog))), hbox);
    gtk_widget_show_all (dialog);

    g_signal_connect (G_OBJECT (dialog), "response",
                    G_CALLBACK (gtk_widget_destroy), NULL);
}

/* Copy the selected text to the clipboard and remove it from the buffer. */
static void
cut_clicked (GtkButton *cut,
             GtkTextView *textview)
{
    GtkClipboard *clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

    gtk_text_buffer_cut_clipboard (buffer, clipboard, TRUE);
}

/* Copy the selected text to the clipboard. */
static void
copy_clicked (GtkButton *copy,
              GtkTextView *textview)
{
    GtkClipboard *clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

    gtk_text_buffer_copy_clipboard (buffer, clipboard);
}

/* Insert the text from the clipboard into the text buffer. */
static void
paste_clicked (GtkButton *paste,
               GtkTextView *textview)
{
    GtkClipboard *clipboard = gtk_clipboard_get (GDK_SELECTION_CLIPBOARD);

    gtk_text_buffer_paste_clipboard (buffer, clipboard, NULL, TRUE);
}
