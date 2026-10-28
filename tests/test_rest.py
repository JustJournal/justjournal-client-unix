#!/usr/bin/env python3
"""Exercise both real clients against a temporary, certificate-verified HTTPS API.

The compiled harness routes HTTPS to localhost and trusts only the test CA.
No production URLs, credentials, or TLS options are changed in the clients.
"""
import http.server
import fcntl
import json
import os
from pathlib import Path
import pty
import select
import shlex
import shutil
import signal
import ssl
import subprocess
import sys
import tempfile
import termios
import threading
import time
import unittest

ROOT = Path(__file__).resolve().parent.parent


class RestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="jj-rest-test-")
        cls.work = Path(cls.temp.name)
        cls.requests = []
        cls.mode = "ok"
        cls.login_count = 0
        cert = cls.work / "cert.pem"
        key = cls.work / "key.pem"
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-keyout", str(key), "-out", str(cert), "-days", "1",
            "-subj", "/CN=www.justjournal.com", "-addext",
            "subjectAltName=DNS:www.justjournal.com",
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

        class Handler(http.server.BaseHTTPRequestHandler):
            def log_message(self, *args):
                pass

            def session_cookie(self):
                session = "renewed-session" if cls.mode == "edit_expired" and cls.login_count > 1 else "test-session"
                return "JSESSIONID=" + session

            def reply(self, status, body, cookie=False, content_type="application/json"):
                data = body if isinstance(body, bytes) else json.dumps(body).encode()
                self.send_response(status)
                self.send_header("Content-Type", content_type)
                self.send_header("Content-Length", str(len(data)))
                if cookie:
                    self.send_header("Set-Cookie", self.session_cookie() + "; Path=/; Secure; HttpOnly")
                self.end_headers()
                self.wfile.write(data)

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                cls.requests.append((self.path, dict(self.headers), body))
                if self.path == "/api/login":
                    cls.login_count += 1
                    if cls.mode == "login_500":
                        return self.reply(500, {"error": "Unknown error occurred. Please try again later."})
                    if cls.mode == "unauthorized":
                        return self.reply(401, {"status": "JJ.LOGIN.FAIL"})
                    if cls.mode == "login_redirect":
                        return self.reply(302, b"<html>login</html>")
                    if cls.mode == "bad_login_json":
                        return self.reply(200, b"not json")
                    if cls.mode == "login_fail":
                        return self.reply(200, {"status": "JJ.LOGIN.FAIL"})
                    return self.reply(200, {"status": "JJ.LOGIN.OK", "username": body["username"]}, cls.mode != "no_cookie")
                if self.path != "/api/entry":
                    return self.reply(404, {})
                if self.headers.get("Cookie") != self.session_cookie():
                    return self.reply(403, {})
                # Production rejects INSERT when omitted music becomes null.
                if not isinstance(body.get("music"), str):
                    return self.reply(500, {"error": "Column 'music' cannot be null"})
                # EntryController passes tags directly to applyTags after saving.
                # An absent/null field triggers an exception with the server's
                # explicit EntryTo constructor and Lombok @Builder.Default.
                if not isinstance(body.get("tags"), list):
                    return self.reply(500, {"error": "Unknown error occurred. Please try again later."})
                if cls.mode == "post_500":
                    return self.reply(500, {"error": "Unknown error occurred. Please try again later."})
                if cls.mode == "post_fail":
                    return self.reply(400, {"error": "rejected"})
                if cls.mode == "post_error":
                    return self.reply(200, {"status": "error"})
                if cls.mode == "post_invalid":
                    return self.reply(200, b"<html>error</html>")
                if cls.mode == "post_no_id":
                    return self.reply(200, {"status": "ok"})
                return self.reply(200, {"status": "ok", "id": "42"})

            def do_GET(self):
                cls.requests.append((self.path, dict(self.headers), None))
                if self.headers.get("Cookie") != self.session_cookie():
                    return self.reply(403, {})
                entry_id = 5000000000 if cls.mode == "large_id" else 42
                if self.path == f"/api/entry/testuser/eid/{entry_id}":
                    if cls.mode == "edit_bad_entry":
                        return self.reply(200, {"entryId": 41})
                    return self.reply(200, {
                        "entryId": entry_id, "date": "2026-10-26T12:30:00Z",
                        "subject": "Existing title", "body": "Existing body",
                        "security": {"id": 1, "name": "friends"},
                        "format": "MARKDOWN", "autoFormat": False,
                        "allowComments": False, "emailComments": False,
                        "draft": True, "mood": 19, "location": 3,
                        "music": "Existing music", "trackback": "",
                        "tags": ["existing"], "comments": []})
                entries = [{"entryId": entry_id, "date": "2026-10-26T12:30:00Z",
                            "subject": 'Title "one"', "body": "Body\n雪",
                            "music": "Existing music", "tags": ["existing"]}]
                if cls.mode == "recent_missing_id":
                    del entries[0]["entryId"]
                if cls.mode == "recent_controls":
                    controls = "".join(chr(c) for c in range(1, 32) if c not in (9, 10))
                    controls += chr(127) + "".join(chr(c) for c in range(128, 160))
                    text = "雪Û😀\n\t" + controls + "\x1b[2J\x1b]0;title\x07\u009b2J\u009d0;title\u009c"
                    entries = [{"date": text, "subject": text, "body": text}]
                if cls.mode == "recent_twenty":
                    entries = [{"date": "2026-10-26T12:30:00Z",
                                "subject": f"Title {i}", "body": f"Body {i}"}
                               for i in range(20)]
                if cls.mode == "recent_empty":
                    return self.reply(200, {"page": {"totalElements": 0}})
                if cls.mode == "recent_bad":
                    return self.reply(200, {"content": [{"date": "2026-10-26T12:30:00Z",
                                                         "subject": "missing body"}]})
                if cls.mode == "recent_bad_date":
                    return self.reply(200, {"content": [{"date": None,
                                                         "subject": "Title", "body": "Body"}]})
                if cls.mode == "recent_bad_shape":
                    return self.reply(200, {})
                if cls.mode == "recent_hal":
                    return self.reply(200, {"_embedded": {"entryToList": entries}, "page": {"totalElements": 1}})
                return self.reply(200, {"content": entries, "page": {"totalElements": 1}})

            def do_PUT(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                cls.requests.append((self.path, dict(self.headers), body))
                if cls.mode == "edit_proxy_html":
                    return self.reply(403, b"<html><h1>Forbidden</h1></html>",
                                      content_type="text/html")
                if self.path != "/api/entry" or self.headers.get("Cookie") != self.session_cookie():
                    return self.reply(403, {})
                if cls.mode in ("edit_expired", "edit_expired_cancel") and cls.login_count == 1:
                    return self.reply(403, {"error": "The login timed out or is invalid."})
                if cls.mode == "edit_500":
                    return self.reply(500, {"error": "failed"})
                if cls.mode == "edit_wrong_id":
                    return self.reply(200, {"id": "41"})
                return self.reply(200, {"id": "5000000000" if cls.mode == "large_id" else "42"})

            def do_DELETE(self):
                cls.requests.append((self.path, dict(self.headers), None))
                entry_id = 5000000000 if cls.mode == "large_id" else 42
                if self.path != f"/api/entry/{entry_id}" or self.headers.get("Cookie") != self.session_cookie():
                    return self.reply(403, {})
                if cls.mode == "delete_500":
                    return self.reply(500, {"error": "failed"})
                if cls.mode == "delete_wrong_id":
                    return self.reply(200, {"id": "41"})
                return self.reply(200, {"id": str(entry_id)})

        cls.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(cert, key)
        cls.server.socket = context.wrap_socket(cls.server.socket, server_side=True)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        # The shim changes routing and CA trust only in the test executable.
        shim = '''
#include "jj_rest.c"
static jj_rest *test_open(const char *base, const char *agent, char *error, size_t size)
{
    jj_rest *client = jj_rest_open(base, agent, error, size);
    if (client) {
        struct curl_slist *routes = curl_slist_append(NULL, ":443:127.0.0.1:PORT");
        curl_easy_setopt(client->curl, CURLOPT_CONNECT_TO, routes);
        curl_easy_setopt(client->curl, CURLOPT_CAINFO, "CERT");
        curl_easy_setopt(client->curl, CURLOPT_PROXY, "");
        /* CONNECT_TO's list must survive the request; process lifetime in this shim. */
    }
    return client;
}
#define jj_rest_open test_open
'''.replace("PORT", str(cls.server.server_port)).replace("CERT", str(cert))
        cls.cli = cls.work / "jjclient"
        cls.build("cli.c", shim + '\n#include "jj.c"\n', cls.cli,
                  ["libcurl", "jansson"], [ROOT / "jj_password.c"])
        cls.gtk = None
        cls.xvfb = None
        cls.gui_env = os.environ.copy()
        if shutil.which("Xvfb") and subprocess.run(["pkg-config", "--exists", "gtk+-3.0"]).returncode == 0:
            # -displayfd lets Xvfb choose an unused display without disturbing a session.
            read_fd, write_fd = os.pipe()
            cls.xvfb = subprocess.Popen(["Xvfb", "-displayfd", str(write_fd), "-screen", "0", "800x600x24"], pass_fds=(write_fd,), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            os.close(write_fd)
            with os.fdopen(read_fd) as pipe:
                display = pipe.readline().strip()
            if not display:
                raise RuntimeError("Xvfb did not start")
            cls.gui_env["DISPLAY"] = ":" + display
            cls.gtk = cls.work / "gtkjj-test"
            cls.build("gui.c", shim + '''
#define main gtkjj_main
#include "gtk/gtkjj.c"
#undef main
static gboolean answer_delete(gpointer accept)
{
    GList *windows = gtk_window_list_toplevels();
    for (GList *item = windows; item; item = item->next) {
        if (GTK_IS_MESSAGE_DIALOG(item->data)) {
            gtk_dialog_response(GTK_DIALOG(item->data),
                GPOINTER_TO_INT(accept) ? GTK_RESPONSE_ACCEPT : GTK_RESPONSE_CANCEL);
            break;
        }
    }
    g_list_free(windows);
    return G_SOURCE_REMOVE;
}
static gboolean answer_relogin(gpointer accept)
{
    GList *windows = gtk_window_list_toplevels();
    for (GList *item = windows; item; item = item->next) {
        GtkWidget *dialog = item->data;
        if (!GTK_IS_DIALOG(dialog) ||
            strcmp(gtk_window_get_title(GTK_WINDOW(dialog)), "Session expired") != 0)
            continue;
        GList *children = gtk_container_get_children(GTK_CONTAINER(
            gtk_dialog_get_content_area(GTK_DIALOG(dialog))));
        for (GList *child = children; child; child = child->next) {
            if (GTK_IS_ENTRY(child->data))
                gtk_entry_set_text(GTK_ENTRY(child->data), "testpass");
        }
        g_list_free(children);
        gtk_dialog_response(GTK_DIALOG(dialog), GPOINTER_TO_INT(accept) ?
            GTK_RESPONSE_ACCEPT : GTK_RESPONSE_CANCEL);
        break;
    }
    g_list_free(windows);
    return G_SOURCE_REMOVE;
}
int main(int argc, char **argv)
{
    const char *mode = argv[1];
    bool large = g_str_has_prefix(mode, "large-");
    gint64 expected_id = large ? G_GINT64_CONSTANT(5000000000) : 42;
    bool empty_metadata = argc > 2 && strcmp(argv[2], "empty-metadata") == 0;
    bool empty_title = argc > 2 && strcmp(argv[2], "empty-title") == 0;
    Editor *editor;
    GtkListBoxRow *row;
    gtk_init(&argc, &argv);
    create_main_window();
    if (strcmp(gtk_stack_get_visible_child_name(GTK_STACK(app.stack)), "login") != 0)
        return 1;
    gtk_entry_set_text(GTK_ENTRY(app.username_entry), "testuser");
    gtk_entry_set_text(GTK_ENTRY(app.password_entry), "testpass");
    login_clicked(NULL, NULL);
    if (gtk_entry_get_text(GTK_ENTRY(app.password_entry))[0] != '\\0')
        return 2;
    if (strcmp(mode, "login-fail") == 0)
        return app.client == NULL &&
            strcmp(gtk_stack_get_visible_child_name(GTK_STACK(app.stack)), "login") == 0 ? 0 : 3;
    if (!app.client || strcmp(gtk_stack_get_visible_child_name(GTK_STACK(app.stack)), "entries") != 0 ||
        json_array_size(app.entries) != 1)
        return 4;
    row = gtk_list_box_get_row_at_index(GTK_LIST_BOX(app.entry_list), 0);
    if (!row || *(gint64 *)g_object_get_data(G_OBJECT(row), "entry-id") != expected_id)
        return 5;
    GList *labels = gtk_container_get_children(GTK_CONTAINER(gtk_bin_get_child(GTK_BIN(row))));
    const char *heading = gtk_label_get_text(GTK_LABEL(labels->data));
    const char *body_text = gtk_label_get_text(GTK_LABEL(labels->next->data));
    if (!strstr(heading, "2026-10-26T12:30:00Z") || !strstr(heading, "Title \\"one\\"") ||
        strcmp(body_text, "Body\\n雪") != 0)
        return 9;
    if (strcmp(mode, "select-click") == 0) {
        GtkWidget *content = GTK_WIDGET(labels->next->data);
        gint x, y, origin_x, origin_y, status;
        while (gtk_events_pending())
            gtk_main_iteration();
        if (!gtk_widget_translate_coordinates(content, app.window,
                gtk_widget_get_allocated_width(content) / 2,
                gtk_widget_get_allocated_height(content) / 2, &x, &y))
            return 10;
        gdk_window_get_origin(gtk_widget_get_window(app.window), &origin_x, &origin_y);
        gchar *screen_x = g_strdup_printf("%d", origin_x + x);
        gchar *screen_y = g_strdup_printf("%d", origin_y + y);
        gchar *click[] = {"xdotool", "mousemove", "--sync", screen_x, screen_y, "click", "1", NULL};
        gboolean sent = g_spawn_sync(NULL, click, NULL, G_SPAWN_SEARCH_PATH,
            NULL, NULL, NULL, NULL, &status, NULL);
        g_free(screen_x);
        g_free(screen_y);
        if (!sent || status != 0)
            return 10;
        for (int attempt = 0; attempt < 20 && selected_entry_id() == 0; attempt++) {
            g_main_context_iteration(NULL, FALSE);
            g_usleep(50000);
        }
        return selected_entry_id() == expected_id &&
            gtk_widget_get_sensitive(app.edit_button) &&
            gtk_widget_get_sensitive(app.delete_button) ? 0 : 11;
    }
    g_list_free(labels);
    if (strcmp(mode, "view") == 0 || strcmp(mode, "large-view") == 0)
        return 0;
    if (strcmp(mode, "delete") == 0 || strcmp(mode, "large-delete") == 0 ||
        strcmp(mode, "delete-cancel") == 0) {
        gtk_list_box_select_row(GTK_LIST_BOX(app.entry_list), row);
        g_idle_add(answer_delete, GINT_TO_POINTER(strcmp(mode, "delete-cancel") != 0));
        delete_clicked(NULL, NULL);
        return 0;
    }
    bool editing = strcmp(mode, "edit") == 0 || strcmp(mode, "large-edit") == 0 ||
        strcmp(mode, "edit-expired") == 0 || strcmp(mode, "edit-expired-cancel") == 0 ||
        strcmp(mode, "edit-proxy-html") == 0;
    editor = open_editor(editing ? expected_id : 0);
    if (editing &&
        (strcmp(gtk_entry_get_text(GTK_ENTRY(editor->title)), "Title \\"one\\"") != 0 ||
         strcmp(gtk_entry_get_text(GTK_ENTRY(editor->music)), "Existing music") != 0 ||
         strcmp(gtk_entry_get_text(GTK_ENTRY(editor->tags)), "existing") != 0 ||
         gtk_text_buffer_get_char_count(editor->body) == 0))
        return 7;
    gtk_entry_set_text(GTK_ENTRY(editor->title), empty_title ? "" : "GTK title");
    gtk_entry_set_text(GTK_ENTRY(editor->music), empty_metadata ? "" : "Artist - Song 雪");
    gtk_entry_set_text(GTK_ENTRY(editor->tags), empty_metadata ? " , , " : "C, UNIX  Gtk,, CAFÉ");
    gtk_text_buffer_set_text(editor->body, "GTK body", -1);
    if (g_str_has_prefix(mode, "edit-expired"))
        g_idle_add(answer_relogin, GINT_TO_POINTER(strcmp(mode, "edit-expired") == 0));
    editor_saved(NULL, editor);
    if (strcmp(mode, "edit-expired-cancel") == 0 || strcmp(mode, "edit-proxy-html") == 0)
        return gtk_text_buffer_get_char_count(editor->body) == 8 &&
            gtk_widget_get_visible(editor->window) ? 0 : 12;
    if (strcmp(mode, "post-fail") == 0)
        return gtk_text_buffer_get_char_count(editor->body) > 0 &&
            strcmp(gtk_entry_get_text(GTK_ENTRY(editor->title)), "GTK title") == 0 ? 0 : 8;
    if (strcmp(mode, "repeat") == 0) {
        editor = open_editor(0);
        gtk_entry_set_text(GTK_ENTRY(editor->title), "Second title");
        gtk_entry_set_text(GTK_ENTRY(editor->music), "Second song");
        gtk_entry_set_text(GTK_ENTRY(editor->tags), "Later");
        gtk_text_buffer_set_text(editor->body, "Second body", -1);
        editor_saved(NULL, editor);
    }
    return 0;
}
''', cls.gtk, ["gtk+-3.0", "libcurl", "jansson"])

    @classmethod
    def build(cls, source, text, target, packages, extra_sources=()):
        path = cls.work / source
        path.write_text(text)
        flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", *packages], text=True))
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c99", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Wno-unused-parameter", "-pedantic", "-I", str(ROOT), str(path), *map(str, extra_sources), "-o", str(target), *flags], check=True)

    @classmethod
    def tearDownClass(cls):
        if cls.xvfb:
            cls.xvfb.terminate()
            cls.xvfb.wait(timeout=10)
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()
        cls.temp.cleanup()

    def setUp(self):
        type(self).requests = []
        type(self).mode = "ok"
        type(self).login_count = 0

    def cli_run(self, *args, body="Body\n雪", ok=True):
        result = subprocess.run([str(self.cli), "-u", "testuser", "-p", "testpass", *args], input=body, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode == 0, ok, result.stderr)
        self.assertNotIn("testpass", result.stdout + result.stderr)
        return result

    def password_fd_run(self, password, equals=False, extra_args=()):
        read_fd, write_fd = os.pipe()
        try:
            os.write(write_fd, password)
            os.close(write_fd)
            write_fd = None
            option = (["--password-fd=" + str(read_fd)] if equals else
                      ["--password-fd", str(read_fd)])
            return subprocess.run(
                [str(self.cli), "-u", "testuser", *option, *extra_args],
                input=b"Separate body\n", capture_output=True,
                pass_fds=(read_fd,), timeout=10)
        finally:
            os.close(read_fd)
            if write_fd is not None:
                os.close(write_fd)

    def start_prompt(self):
        master, slave = pty.openpty()
        echo_read, echo_write = os.pipe()
        attrs = termios.tcgetattr(slave)
        attrs[3] |= termios.ECHO
        termios.tcsetattr(slave, termios.TCSANOW, attrs)

        def controlling_terminal():
            os.setsid()
            fcntl.ioctl(slave, termios.TIOCSCTTY, 0)

        # BSD revokes the slave when its session leader exits. Keep a supervisor
        # in that session so it can inspect the real CLI child's terminal state
        # after child exit, before revocation. Forward signals to the CLI only.
        supervisor = '''
import os, signal, subprocess, sys, termios
child = subprocess.Popen(sys.argv[3:])
def forward(signum, frame):
    child.send_signal(signum)
signal.signal(signal.SIGINT, forward)
signal.signal(signal.SIGTERM, forward)
status = child.wait()
echo = bool(termios.tcgetattr(int(sys.argv[1]))[3] & termios.ECHO)
os.write(int(sys.argv[2]), b"1" if echo else b"0")
sys.exit(status if status >= 0 else 128 - status)
'''
        try:
            process = subprocess.Popen(
                [sys.executable, "-c", supervisor, str(slave), str(echo_write),
                 str(self.cli), "-u", "testuser"], stdin=subprocess.PIPE,
                stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                pass_fds=(slave, echo_write), preexec_fn=controlling_terminal)
        except BaseException:
            os.close(master)
            os.close(slave)
            os.close(echo_read)
            os.close(echo_write)
            raise
        os.close(echo_write)
        self.addCleanup(os.close, master)
        self.addCleanup(os.close, slave)
        self.addCleanup(os.close, echo_read)

        def stop_process():
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=5)

        self.addCleanup(stop_process)
        output = b""
        deadline = time.monotonic() + 5
        while b"Password:" not in output:
            remaining = deadline - time.monotonic()
            self.assertGreater(remaining, 0, "Password prompt did not appear")
            ready, _, _ = select.select([master], [], [], remaining)
            self.assertTrue(ready, "Password prompt did not appear")
            output += os.read(master, 4096)
        self.assertFalse(termios.tcgetattr(slave)[3] & termios.ECHO)
        return process, master, echo_read, output

    @staticmethod
    def drain_terminal(master):
        output = b""
        while select.select([master], [], [], 0)[0]:
            try:
                chunk = os.read(master, 4096)
            except OSError as error:
                # A revoked PTY may report EIO/ENXIO instead of EOF on BSD/Linux.
                import errno
                if error.errno in (errno.EIO, errno.ENXIO):
                    break
                raise
            if not chunk:
                break
            output += chunk
        return output

    def test_password_fd_sources_preserve_password_and_body(self):
        for password, expected, equals in (
            (b"testpass\n", "testpass", False),
            (b" test pass \r\n", " test pass ", True),
            (b"testpass", "testpass", True),
            (b"a" * 30 + b"\r\n", "a" * 30, False),
        ):
            with self.subTest(password=password, equals=equals):
                type(self).requests = []
                result = self.password_fd_run(password, equals)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.requests[0][2]["password"], expected)
                self.assertEqual(self.requests[-1][2]["body"], "Separate body\n")
                self.assertNotIn(expected.encode(), result.stdout + result.stderr)

    def test_password_fd_zero_preserves_following_entry(self):
        result = subprocess.run(
            [str(self.cli), "-u", "testuser", "--password-fd", "0"],
            input=b"testpass\nBody from same descriptor\n", capture_output=True,
            timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.requests[0][2]["password"], "testpass")
        self.assertEqual(self.requests[-1][2]["body"], "Body from same descriptor\n")

    def test_password_fd_rejects_invalid_password_without_requests(self):
        for password in (b"", b"short\n", b"a" * 31 + b"\n",
                         b"test\x00pass\n", b"test:pass\n"):
            with self.subTest(password=password):
                result = self.password_fd_run(password)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b"password", result.stderr.lower())
                self.assertEqual(self.requests, [])

    def test_password_source_option_errors_do_not_read_stdin(self):
        cases = (
            ["--password-fd"], ["--password-fd="],
            ["--password-fd", "-1"], ["--password-fd", "abc"],
            ["--password-fd", "3junk"],
            ["--password-fd", "9999999999999999999999999"],
            ["--password-fd", "999999"],
            ["-p", "testpass", "--password-fd", "0"],
            ["--password-fd", "0", "-p", "testpass"],
        )
        for args in cases:
            with self.subTest(args=args), tempfile.TemporaryFile() as body:
                body.write(b"testpass\nEntry body")
                body.seek(0)
                result = subprocess.run(
                    [str(self.cli), "-u", "testuser", *args], stdin=body,
                    capture_output=True, timeout=5, start_new_session=True)
                self.assertNotEqual(result.returncode, 0)
                self.assertTrue(result.stderr)
                self.assertNotIn(b"testpass", result.stdout + result.stderr)
                self.assertEqual(body.tell(), 0)
                self.assertEqual(self.requests, [])

    def test_missing_terminal_does_not_read_stdin(self):
        with tempfile.TemporaryFile() as body:
            body.write(b"Entry body")
            body.seek(0)
            result = subprocess.run(
                [str(self.cli), "-u", "testuser"], stdin=body,
                capture_output=True, timeout=5, start_new_session=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"password", result.stderr.lower())
            self.assertEqual(body.tell(), 0)
            self.assertEqual(self.requests, [])

    def test_terminal_password_is_hidden_and_restores_echo(self):
        process, master, echo_read, terminal_output = self.start_prompt()
        os.write(master, b"testpass\n")
        stdout, stderr = process.communicate(b"Redirected entry body\n", timeout=10)
        self.assertEqual(process.returncode, 0, stderr)
        self.assertEqual(os.read(echo_read, 1), b"1", "Terminal echo was not restored")
        terminal_output += self.drain_terminal(master)
        self.assertNotIn(b"testpass", terminal_output + stdout + stderr)
        self.assertEqual(self.requests[0][2]["password"], "testpass")
        self.assertEqual(self.requests[-1][2]["body"], "Redirected entry body\n")

    def test_terminal_signals_restore_echo_without_requests(self):
        for signum in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signal=signum):
                process, master, echo_read, terminal_output = self.start_prompt()
                os.write(master, b"partialpass")
                process.send_signal(signum)
                stdout, stderr = process.communicate(timeout=5)
                self.assertNotEqual(process.returncode, 0)
                self.assertEqual(os.read(echo_read, 1), b"1", "Terminal echo was not restored")
                terminal_output += self.drain_terminal(master)
                self.assertNotIn(b"partialpass", terminal_output + stdout + stderr)
                self.assertEqual(self.requests, [])

    def test_post_json_and_defaults(self):
        self.cli_run("-s", 'Subject "quoted" \\ 雪', "-d", body='body "quote" \\ \n\t雪')
        self.assertEqual([r[0] for r in self.requests], ["/api/login", "/api/entry"])
        self.assertEqual(self.requests[0][2], {"username": "testuser", "password": "testpass"})
        self.assertEqual(self.requests[0][1]["Accept"], "*/*")
        self.assertIn("2.2.0", self.requests[0][1]["User-Agent"])
        self.assertEqual(self.requests[1][2], {
            "subject": 'Subject "quoted" \\ 雪', "body": 'body "quote" \\ \n\t雪',
            "security": 2, "format": "TEXT", "autoFormat": True,
            "allowComments": True, "emailComments": True, "draft": False,
            "mood": 12, "location": 0, "music": "", "trackback": "", "tags": [],
        })
        self.assertEqual(self.requests[1][1]["Cookie"], "JSESSIONID=test-session")

    def test_login_failures_stop_before_post(self):
        for mode in ("unauthorized", "login_redirect", "bad_login_json", "login_fail"):
            with self.subTest(mode=mode):
                type(self).mode = mode
                type(self).requests = []
                self.cli_run(ok=False)
                self.assertEqual(len(self.requests), 1)

    def test_post_security_options(self):
        for visibility, expected in (("public", 2), ("friends", 1), ("private", 0)):
            for option in (["--security", visibility], ["--security=" + visibility]):
                with self.subTest(option=option):
                    self.cli_run(*option)
                    self.assertEqual(self.requests[-1][2]["security"], expected)

    def test_post_format_options(self):
        for format_name in ("text", "markdown", "html"):
            for option in (["--format", format_name], ["--format=" + format_name]):
                with self.subTest(option=option):
                    self.cli_run(*option, body="Body *formatted* <em>text</em>\n")
                    entry = self.requests[-1][2]
                    self.assertEqual(entry["format"], format_name.upper())
                    self.assertIs(entry["autoFormat"], format_name == "text")
                    self.assertEqual(entry["body"], "Body *formatted* <em>text</em>\n")

    def test_post_date_preserves_iso8601(self):
        dates = (
            "2026-10-26T12:34:56Z", "2026-10-26T12:34:56+05:30",
            "2026-10-26T12:34:56-04:00", "2026-10-26T12:34:56.1Z",
            "2026-10-26T12:34:56.12+00:00", "2026-10-26T12:34:56.123-00:00",
            "2000-02-29T00:00:00Z", "2024-02-29T23:59:59Z",
            "0001-01-01T00:00:00+23:59", "9999-12-31T23:59:59-23:59",
        )
        for date in dates:
            for option in (["--date", date], ["--date=" + date]):
                with self.subTest(option=option):
                    self.cli_run(*option)
                    self.assertEqual(self.requests[-1][2]["date"], date)

    def test_repeated_post_options_use_last_value(self):
        date = "2024-02-29T12:34:56.123+01:00"
        self.cli_run("--date", "2026-10-26T00:00:00Z", "--date=" + date,
                     "--security", "public", "--security=private",
                     "--format", "text", "--format=markdown")
        entry = self.requests[-1][2]
        self.assertEqual(entry["date"], date)
        self.assertEqual(entry["security"], 0)
        self.assertEqual(entry["format"], "MARKDOWN")
        self.assertIs(entry["autoFormat"], False)

    def test_nontext_body_preserves_legacy_markup(self):
        body = "<title>Literal title</title>Body<music>Literal song</music>\n"
        for format_name in ("markdown", "html"):
            for subject_args, expected_subject in (([], ""), (["-s", "Explicit title"], "Explicit title")):
                with self.subTest(format=format_name, subject=expected_subject):
                    self.cli_run("--format", format_name, *subject_args, body=body)
                    entry = self.requests[-1][2]
                    self.assertEqual(entry["body"], body)
                    self.assertEqual(entry["subject"], expected_subject)
                    self.assertEqual(entry["music"], "")

    def test_explicit_text_keeps_legacy_extraction(self):
        self.cli_run("--format=text", body="<title>Old title</title>Body<music>Song</music>")
        entry = self.requests[-1][2]
        self.assertEqual(entry["subject"], "Old title")
        self.assertEqual(entry["music"], "Song")
        self.assertEqual(entry["body"], "Body<music>Song</music>")

    def test_post_option_errors_precede_password_and_body_input(self):
        invalid_dates = (
            "", "2026-10-26", "2026-10-26T12:34:56", "2026-10-26 12:34:56Z",
            "2026-10-26t12:34:56z", "0000-01-01T00:00:00Z",
            "10000-01-01T00:00:00Z", "1900-02-29T00:00:00Z",
            "2025-02-29T00:00:00Z", "2026-04-31T00:00:00Z",
            "2026-00-01T00:00:00Z", "2026-13-01T00:00:00Z",
            "2026-01-00T00:00:00Z", "2026-01-32T00:00:00Z",
            "2026-10-26T24:00:00Z", "2026-10-26T12:60:00Z",
            "2026-10-26T12:34:60Z", "2026-10-26T12:34:56.Z",
            "2026-10-26T12:34:56.1234Z", "2026-10-26T12:34:56,123Z",
            "2026-10-26T12:34:56+24:00", "2026-10-26T12:34:56-01:60",
            "2026-10-26T12:34:56+0100", "2026-10-26T12:34:56+01",
            "2026-10-26T12:34:56Zjunk", "2026-1-01T00:00:00Z",
        )
        cases = [["--date=" + date] for date in invalid_dates]
        cases += [["--security=" + value] for value in ("", "PUBLIC", "0", "unknown")]
        cases += [["--format=" + value] for value in ("", "HTML", "plain", "unknown")]
        cases += [[option] for option in ("--date", "--security", "--format")]
        for option in (["--date=2026-10-26T00:00:00Z"], ["--security=public"], ["--format=text"]):
            cases += [["-r", *option], [*option, "-r"]]
        for args in cases:
            # Password descriptor 0 would consume this file if validation happened late.
            for password_args in ([], ["--password-fd", "0"]):
                with self.subTest(args=args, password_args=password_args), tempfile.TemporaryFile() as body:
                    body.write(b"testpass\nEntry body")
                    body.seek(0)
                    result = subprocess.run(
                        [str(self.cli), "-u", "testuser", *password_args, *args],
                        stdin=body, capture_output=True, timeout=5, start_new_session=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertTrue(result.stderr)
                    self.assertTrue(any(name in result.stderr.lower()
                                        for name in (b"date", b"security", b"format")),
                                    result.stderr)
                    self.assertNotIn(b"Password:", result.stdout + result.stderr)
                    self.assertNotIn(b"testpass", result.stdout + result.stderr)
                    self.assertEqual(body.tell(), 0)
                    self.assertEqual(self.requests, [])

    def test_post_failures(self):
        for mode in ("post_fail", "post_error", "post_invalid", "post_no_id", "no_cookie"):
            with self.subTest(mode=mode):
                type(self).mode = mode
                self.cli_run(ok=False)

    def test_500_reports_operation_without_retry(self):
        for mode, operation, request_count in (
            ("login_500", "Login", 1), ("post_500", "Saving entry", 2),
        ):
            with self.subTest(mode=mode):
                type(self).mode = mode
                type(self).requests = []
                result = self.cli_run(ok=False)
                self.assertIn(operation + " failed (HTTP 500)", result.stderr)
                self.assertEqual(len(self.requests), request_count)

    def test_tls_hostname_verification(self):
        result = self.cli_run("-h", "localhost", ok=False)
        self.assertIn("SSL", result.stderr)
        self.assertEqual(self.requests, [])

    def test_host_syntax_is_checked_without_resolution(self):
        valid = ("localhost", "127.0.0.1", "unresolvable.invalid", "example.com.")
        invalid = ("", "-host", "host-", "bad..host", "bad/host", "host:443",
                   "a" * 64 + ".example", "a." * 127 + "a")
        for host in valid:
            with self.subTest(host=host):
                result = subprocess.run(
                    [str(self.cli), "-h", host, "-u", "testuser", "-p", "bad/ss"],
                    capture_output=True, text=True, timeout=5)
                self.assertIn("Invalid password", result.stderr)
                self.assertNotIn("Invalid hostname", result.stderr)
        for host in invalid:
            with self.subTest(host=host):
                result = subprocess.run(
                    [str(self.cli), "-h", host, "-u", "testuser", "-p", "bad/ss"],
                    capture_output=True, text=True, timeout=5)
                self.assertIn("Invalid hostname", result.stderr)
        self.assertEqual(self.requests, [])

    def test_recent_entries(self):
        for mode in ("ok", "recent_hal"):
            with self.subTest(mode=mode):
                type(self).mode = mode
                type(self).requests = []
                result = self.cli_run("-r", body="This must not be posted")
                self.assertEqual(result.stdout,
                                 '0 2026-10-26T12:30:00Z Title "one"\n\nBody\n雪\n\n')
                self.assertEqual([request[0] for request in self.requests],
                                 ["/api/login", "/api/entry/testuser/size/20/page/0"])

    def test_recent_twenty_entries(self):
        type(self).mode = "recent_twenty"
        result = self.cli_run("-r")
        self.assertEqual(result.stdout.count("2026-10-26T12:30:00Z"), 20)
        self.assertIn("19 2026-10-26T12:30:00Z Title 19\n\nBody 19\n\n", result.stdout)
        self.assertEqual(self.requests[-1][0], "/api/entry/testuser/size/20/page/0")

    def test_recent_show_ids(self):
        result = self.cli_run("-r", "--show-ids", body="ignored")
        self.assertEqual(result.stdout,
                         'id=42 0 2026-10-26T12:30:00Z Title "one"\n\nBody\n雪\n\n')
        self.assertEqual(len(self.requests), 2)

    def test_recent_show_ids_requires_valid_id(self):
        type(self).mode = "recent_missing_id"
        result = self.cli_run("-r", "--show-ids", ok=False)
        self.assertEqual(result.stdout, "")
        self.assertIn("invalid entry ID", result.stderr)

    def test_large_entry_ids(self):
        type(self).mode = "large_id"
        result = self.cli_run("-r", "--show-ids")
        self.assertIn("id=5000000000", result.stdout)
        type(self).requests = []
        self.cli_run("--edit", "5000000000", body="Updated body")
        self.assertEqual([r[0] for r in self.requests],
                         ["/api/login", "/api/entry/testuser/eid/5000000000", "/api/entry"])
        self.assertEqual(self.requests[-1][2]["entryId"], 5000000000)
        type(self).requests = []
        self.cli_run("--delete", "5000000000")
        self.assertEqual([r[0] for r in self.requests], ["/api/login", "/api/entry/5000000000"])

    def test_entry_action_option_errors_do_not_send_requests(self):
        cases = (("--show-ids",), ("--edit", "0"), ("--delete", "-1"),
                 ("--edit", "42x"), ("--delete", "9223372036854775808"),
                 ("--edit", "42", "--delete", "42"),
                 ("-r", "--edit", "42"), ("-r", "--delete", "42"),
                 ("--delete", "42", "-s", "title"),
                 ("--delete", "42", "--format=html"))
        for args in cases:
            with self.subTest(args=args):
                type(self).requests = []
                self.cli_run(*args, ok=False)
                self.assertEqual(self.requests, [])

    def test_edit_preserves_metadata_and_replaces_body(self):
        result = self.cli_run("--edit", "42", body="New body\n")
        self.assertEqual(result.stdout, "")
        self.assertEqual([r[0] for r in self.requests],
                         ["/api/login", "/api/entry/testuser/eid/42", "/api/entry"])
        update = self.requests[-1][2]
        self.assertEqual(update["entryId"], 42)
        self.assertEqual(update["body"], "New body\n")
        self.assertEqual(update["subject"], "Existing title")
        self.assertEqual(update["security"], 1)
        self.assertEqual(update["format"], "MARKDOWN")
        self.assertEqual(update["date"], "2026-10-26T12:30:00Z")
        self.assertEqual(update["tags"], ["existing"])
        self.assertEqual(update["mood"], 19)
        self.assertEqual(update["location"], 3)
        self.assertIs(update["draft"], True)
        self.assertIs(update["allowComments"], False)

    def test_edit_metadata_overrides(self):
        cases = ((["-s", "New title"], "subject", "New title"),
                 (["--security=private"], "security", 0),
                 (["--format=text"], "format", "TEXT"),
                 (["--date=2026-10-25T18:30:00Z"], "date",
                  "2026-10-25T18:30:00Z"))
        for args, field, expected in cases:
            with self.subTest(field=field):
                type(self).requests = []
                self.cli_run("--edit=42", *args, body="New body")
                update = self.requests[-1][2]
                self.assertEqual(update[field], expected)
                self.assertEqual(update["subject"], "New title" if field == "subject" else "Existing title")
                self.assertEqual(update["security"], 0 if field == "security" else 1)
                self.assertEqual(update["format"], "TEXT" if field == "format" else "MARKDOWN")
                self.assertIs(update["autoFormat"], field == "format")
                self.assertEqual(update["date"], expected if field == "date" else "2026-10-26T12:30:00Z")

    def test_edit_and_delete_failures(self):
        for flag, mode, count in (("--edit", "edit_bad_entry", 2),
                                  ("--edit", "edit_500", 3),
                                  ("--edit", "edit_wrong_id", 3),
                                  ("--delete", "delete_500", 2),
                                  ("--delete", "delete_wrong_id", 2)):
            with self.subTest(flag=flag, mode=mode):
                type(self).mode = mode
                type(self).requests = []
                self.cli_run(flag, "42", ok=False)
                self.assertEqual(len(self.requests), count)

    def test_delete_does_not_read_body(self):
        self.cli_run("--delete", "42", body="must not be posted")
        self.assertEqual([r[0] for r in self.requests], ["/api/login", "/api/entry/42"])
        self.assertIsNone(self.requests[-1][2])

    def test_recent_empty_and_invalid(self):
        type(self).mode = "recent_empty"
        self.assertEqual(self.cli_run("-r").stdout, "")
        for mode in ("recent_bad", "recent_bad_date", "recent_bad_shape"):
            type(self).mode = mode
            self.assertEqual(self.cli_run("-r", ok=False).stdout, "")

    def test_recent_terminal_controls_are_escaped(self):
        type(self).mode = "recent_controls"
        result = self.cli_run("-r", body="")
        controls = "".join("\\x%02x" % c for c in range(1, 32) if c not in (9, 10))
        controls += "\\x7f" + "".join("\\u%04x" % c for c in range(128, 160))
        text = ("雪Û😀\n\t" + controls +
                "\\x1b[2J\\x1b]0;title\\x07\\u009b2J\\u009d0;title\\u009c")
        self.assertEqual(result.stdout, "0 " + text + " " + text + "\n\n" + text + "\n\n")
        for character in result.stdout:
            codepoint = ord(character)
            self.assertFalse((codepoint < 32 and codepoint not in (9, 10)) or
                             127 <= codepoint <= 159)

    def test_input_limits(self):
        self.cli_run(body="", ok=False)
        self.cli_run(body="x" * 32000, ok=False)
        self.assertEqual(self.requests, [])
        self.cli_run(body="x" * 31999)
        self.assertEqual(len(self.requests[-1][2]["body"]), 31999)

    def test_legacy_title(self):
        self.cli_run(body="<title>Old title</title>Body")
        self.assertEqual(self.requests[-1][2]["subject"], "Old title")
        self.assertEqual(self.requests[-1][2]["body"], "Body")

    def test_legacy_music(self):
        self.cli_run(body="Body<music>Song</music>")
        self.assertEqual(self.requests[-1][2]["music"], "Song")
        self.assertEqual(self.requests[-1][2]["body"], "Body<music>Song</music>")

    def test_gtk_multiple_submissions_keep_credentials(self):
        if not self.gtk:
            self.skipTest("GTK 3 and Xvfb are required for GUI callback tests")
        result = subprocess.run([str(self.gtk), "repeat"], env=self.gui_env, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([request[0] for request in self.requests],
                         ["/api/login", "/api/entry/testuser/size/20/page/0",
                          "/api/entry", "/api/entry/testuser/size/20/page/0",
                          "/api/entry", "/api/entry/testuser/size/20/page/0"])
        self.assertEqual(self.requests[2][2]["music"], "Artist - Song 雪")
        self.assertEqual(self.requests[2][2]["tags"], ["c", "unix", "gtk", "café"])
        self.assertEqual(self.requests[4][2]["subject"], "Second title")
        self.assertEqual(self.requests[4][2]["body"], "Second body")
        self.assertEqual(self.requests[4][2]["tags"], ["later"])
        self.assertEqual(self.requests[4][1]["Cookie"], "JSESSIONID=test-session")

    def test_gtk_empty_music_and_tags(self):
        if not self.gtk:
            self.skipTest("GTK 3 and Xvfb are required for GUI callback tests")
        result = subprocess.run([str(self.gtk), "post", "empty-metadata"], env=self.gui_env, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.requests[2][2]["music"], "")
        self.assertEqual(self.requests[2][2]["tags"], [])

    def test_gtk_optional_title(self):
        if not self.gtk:
            self.skipTest("GTK 3 and Xvfb are required for GUI callback tests")
        result = subprocess.run([str(self.gtk), "post", "empty-title"], env=self.gui_env, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.requests[2][2]["subject"], "")
        self.assertEqual(self.requests[2][2]["body"], "GTK body")

    def test_gtk_success_and_failure_drafts(self):
        if not self.gtk:
            self.skipTest("GTK 3 and Xvfb are required for GUI callback tests")
        for mode in ("ok", "unauthorized", "post_fail", "post_error", "post_invalid", "login_500", "post_500"):
            with self.subTest(mode=mode):
                type(self).mode = mode
                type(self).requests = []
                ui_mode = "login-fail" if mode in ("unauthorized", "login_500") else (
                    "post-fail" if mode != "ok" else "post")
                result = subprocess.run([str(self.gtk), ui_mode], env=self.gui_env, capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stderr)
                if mode == "ok":
                    self.assertEqual(self.requests[2][2]["subject"], "GTK title")
                    self.assertEqual(self.requests[2][2]["body"], "GTK body")

    def test_gtk_view_edit_and_delete(self):
        if not self.gtk:
            self.skipTest("GTK 3 and Xvfb are required for GUI callback tests")
        for mode in ("view", "edit", "delete"):
            with self.subTest(mode=mode):
                type(self).requests = []
                result = subprocess.run([str(self.gtk), mode], env=self.gui_env,
                                        capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stderr)
                if mode == "edit":
                    self.assertEqual([r[0] for r in self.requests],
                                     ["/api/login", "/api/entry/testuser/size/20/page/0",
                                      "/api/entry/testuser/eid/42", "/api/entry",
                                      "/api/entry/testuser/size/20/page/0"])
                    update = self.requests[3][2]
                    self.assertEqual(update["entryId"], 42)
                    self.assertEqual(update["subject"], "GTK title")
                    self.assertEqual(update["body"], "GTK body")
                    self.assertEqual(update["music"], "Artist - Song 雪")
                    self.assertEqual(update["tags"], ["c", "unix", "gtk", "café"])
                    self.assertEqual(update["security"], 1)
                    self.assertEqual(update["format"], "MARKDOWN")
                if mode == "delete":
                    self.assertEqual([r[0] for r in self.requests],
                                     ["/api/login", "/api/entry/testuser/size/20/page/0",
                                      "/api/entry/42", "/api/entry/testuser/size/20/page/0"])

    def test_gtk_large_ids_and_delete_cancel(self):
        if not self.gtk:
            self.skipTest("GTK 3 and Xvfb are required for GUI callback tests")
        for mode in ("large-view", "large-edit", "large-delete", "delete-cancel"):
            with self.subTest(mode=mode):
                type(self).mode = "large_id" if mode.startswith("large-") else "ok"
                type(self).requests = []
                result = subprocess.run([str(self.gtk), mode], env=self.gui_env,
                                        capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stderr)
                paths = [r[0] for r in self.requests]
                if mode == "large-edit":
                    self.assertIn("/api/entry/testuser/eid/5000000000", paths)
                    self.assertEqual(self.requests[-2][2]["entryId"], 5000000000)
                elif mode == "large-delete":
                    self.assertIn("/api/entry/5000000000", paths)
                elif mode == "delete-cancel":
                    self.assertNotIn("/api/entry/42", paths)

    def test_gtk_edit_can_clear_music_and_tags(self):
        if not self.gtk:
            self.skipTest("GTK 3 and Xvfb are required for GUI callback tests")
        result = subprocess.run([str(self.gtk), "edit", "empty-metadata"],
                                env=self.gui_env, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        update = self.requests[3][2]
        self.assertEqual(update["music"], "")
        self.assertEqual(update["tags"], [])

    def test_gtk_edit_reauthenticates_without_losing_changes(self):
        if not self.gtk:
            self.skipTest("GTK 3 and Xvfb are required for GUI callback tests")
        type(self).mode = "edit_expired"
        result = subprocess.run([str(self.gtk), "edit-expired"], env=self.gui_env,
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([r[0] for r in self.requests],
                         ["/api/login", "/api/entry/testuser/size/20/page/0",
                          "/api/entry/testuser/eid/42", "/api/entry", "/api/login",
                          "/api/entry/testuser/eid/42", "/api/entry",
                          "/api/entry/testuser/size/20/page/0"])
        self.assertEqual(self.requests[3][2], self.requests[6][2])
        self.assertEqual(self.requests[6][2]["body"], "GTK body")
        self.assertEqual(self.requests[6][1]["Cookie"], "JSESSIONID=renewed-session")

    def test_gtk_edit_reauthentication_cancel_keeps_editor(self):
        if not self.gtk:
            self.skipTest("GTK 3 and Xvfb are required for GUI callback tests")
        type(self).mode = "edit_expired_cancel"
        result = subprocess.run([str(self.gtk), "edit-expired-cancel"], env=self.gui_env,
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([r[0] for r in self.requests],
                         ["/api/login", "/api/entry/testuser/size/20/page/0",
                          "/api/entry/testuser/eid/42", "/api/entry"])

    def test_proxy_403_does_not_prompt_for_password(self):
        type(self).mode = "edit_proxy_html"
        result = self.cli_run("--edit", "42", ok=False)
        self.assertIn("web server refused PUT (HTTP 403)", result.stderr)
        if not self.gtk:
            self.skipTest("GTK 3 and Xvfb are required for GUI callback tests")
        type(self).requests = []
        result = subprocess.run([str(self.gtk), "edit-proxy-html"], env=self.gui_env,
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([r[0] for r in self.requests],
                         ["/api/login", "/api/entry/testuser/size/20/page/0",
                          "/api/entry/testuser/eid/42", "/api/entry"])

    def test_gtk_click_selects_entry(self):
        if not self.gtk or not shutil.which("xdotool"):
            self.skipTest("GTK 3, Xvfb, and xdotool are required for pointer test")
        result = subprocess.run([str(self.gtk), "select-click"], env=self.gui_env,
                                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main(verbosity=2)
