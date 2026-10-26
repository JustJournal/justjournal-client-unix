#!/usr/bin/env python3
"""Exercise both real clients against a temporary, certificate-verified HTTPS API.

The compiled harness routes HTTPS to localhost and trusts only the test CA.
No production URLs, credentials, or TLS options are changed in the clients.
"""
import http.server
import json
import os
from pathlib import Path
import shlex
import shutil
import ssl
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parent.parent


class RestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="jj-rest-test-")
        cls.work = Path(cls.temp.name)
        cls.requests = []
        cls.mode = "ok"
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

            def reply(self, status, body, cookie=False):
                data = body if isinstance(body, bytes) else json.dumps(body).encode()
                self.send_response(status)
                self.send_header("Content-Type", "application/json")
                self.send_header("Content-Length", str(len(data)))
                if cookie:
                    self.send_header("Set-Cookie", "JSESSIONID=test-session; Path=/; Secure; HttpOnly")
                self.end_headers()
                self.wfile.write(data)

            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
                cls.requests.append((self.path, dict(self.headers), body))
                if self.path == "/api/login":
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
                if self.headers.get("Cookie") != "JSESSIONID=test-session":
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
                if self.headers.get("Cookie") != "JSESSIONID=test-session":
                    return self.reply(403, {})
                entries = [{"subject": 'Title "one"', "body": "Body\n雪"}]
                if cls.mode == "recent_empty":
                    return self.reply(200, {"page": {"totalElements": 0}})
                if cls.mode == "recent_bad":
                    return self.reply(200, {"content": [{"subject": "missing body"}]})
                if cls.mode == "recent_bad_shape":
                    return self.reply(200, {})
                if cls.mode == "recent_hal":
                    return self.reply(200, {"_embedded": {"entryToList": entries}, "page": {"totalElements": 1}})
                return self.reply(200, {"content": entries, "page": {"totalElements": 1}})

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
        cls.build("cli.c", shim + '\n#include "jj.c"\n', cls.cli, ["libcurl", "jansson"])
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
int main(int argc, char **argv)
{
    bool success = strcmp(argv[1], "success") == 0;
    bool repeat = argc > 2 && strcmp(argv[2], "repeat") == 0;
    bool empty_metadata = argc > 2 && strcmp(argv[2], "empty-metadata") == 0;
    const char *test_title = argc > 2 && !repeat && !empty_metadata ? "" : "GTK title";
    const char *test_music = empty_metadata ? "" : "Artist - Song 雪";
    const char *test_tags = empty_metadata ? " , ,  " : "C, UNIX  Gtk,, CAFÉ";
    gtk_init(&argc, &argv);
    user = gtk_entry_new();
    pass = gtk_entry_new();
    title = gtk_entry_new();
    music = gtk_entry_new();
    tags = gtk_entry_new();
    buffer = gtk_text_buffer_new(NULL);
    gtk_entry_set_text(GTK_ENTRY(user), "testuser");
    gtk_entry_set_text(GTK_ENTRY(pass), "testpass");
    for (int i = 0; i < (repeat ? 2 : 1); i++) {
        const char *next_title = i == 0 ? test_title : "Second title";
        const char *next_music = i == 0 ? test_music : "Second song";
        const char *next_tags = i == 0 ? test_tags : "Later";
        gtk_entry_set_text(GTK_ENTRY(title), next_title);
        gtk_entry_set_text(GTK_ENTRY(music), next_music);
        gtk_entry_set_text(GTK_ENTRY(tags), next_tags);
        gtk_text_buffer_set_text(buffer, i == 0 ? "GTK body" : "Second body", -1);
        button_clicked(NULL, NULL);
        if (strcmp(gtk_entry_get_text(GTK_ENTRY(user)), "testuser") != 0 ||
            strcmp(gtk_entry_get_text(GTK_ENTRY(pass)), "testpass") != 0)
            return 1;
        if (success) {
            if (gtk_text_buffer_get_char_count(buffer) != 0 ||
                strlen(gtk_entry_get_text(GTK_ENTRY(title))) != 0 ||
                strlen(gtk_entry_get_text(GTK_ENTRY(music))) != 0 ||
                strlen(gtk_entry_get_text(GTK_ENTRY(tags))) != 0)
                return 1;
        } else {
            return gtk_text_buffer_get_char_count(buffer) > 0 &&
                strcmp(gtk_entry_get_text(GTK_ENTRY(title)), next_title) == 0 &&
                strcmp(gtk_entry_get_text(GTK_ENTRY(music)), next_music) == 0 &&
                strcmp(gtk_entry_get_text(GTK_ENTRY(tags)), next_tags) == 0 ? 0 : 1;
        }
    }
    return 0;
}
''', cls.gtk, ["gtk+-3.0", "libcurl", "jansson"])

    @classmethod
    def build(cls, source, text, target, packages):
        path = cls.work / source
        path.write_text(text)
        flags = shlex.split(subprocess.check_output(["pkg-config", "--cflags", "--libs", *packages], text=True))
        subprocess.run([*shlex.split(os.environ.get("CC", "cc")), "-std=c99", "-D_DEFAULT_SOURCE", "-Wall", "-Wextra", "-Wno-unused-parameter", "-pedantic", "-I", str(ROOT), str(path), "-o", str(target), *flags], check=True)

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

    def cli_run(self, *args, body="Body\n雪", ok=True):
        result = subprocess.run([str(self.cli), "-u", "testuser", "-p", "testpass", *args], input=body, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode == 0, ok, result.stderr)
        self.assertNotIn("testpass", result.stdout + result.stderr)
        return result

    def test_post_json_and_defaults(self):
        self.cli_run("-s", 'Subject "quoted" \\ 雪', "-d", body='body "quote" \\ \n\t雪')
        self.assertEqual([r[0] for r in self.requests], ["/api/login", "/api/entry"])
        self.assertEqual(self.requests[0][2], {"username": "testuser", "password": "testpass"})
        self.assertEqual(self.requests[0][1]["Accept"], "*/*")
        self.assertIn("2.1.0", self.requests[0][1]["User-Agent"])
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

    def test_recent_entries(self):
        for mode in ("ok", "recent_hal"):
            with self.subTest(mode=mode):
                type(self).mode = mode
                result = self.cli_run("-r", body="")
                self.assertEqual(result.stdout, '0 Title "one"\n\nBody\n雪\n\n')
                self.assertEqual(self.requests[-1][0], "/api/entry/testuser/size/15/page/0")

    def test_recent_empty_and_invalid(self):
        type(self).mode = "recent_empty"
        self.assertEqual(self.cli_run("-r").stdout, "")
        for mode in ("recent_bad", "recent_bad_shape"):
            type(self).mode = mode
            self.assertEqual(self.cli_run("-r", ok=False).stdout, "")

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
        result = subprocess.run([str(self.gtk), "success", "repeat"], env=self.gui_env, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual([request[0] for request in self.requests],
                         ["/api/login", "/api/entry", "/api/login", "/api/entry"])
        self.assertEqual(self.requests[2][2], {"username": "testuser", "password": "testpass"})
        self.assertEqual(self.requests[3][2]["subject"], "Second title")
        self.assertEqual(self.requests[3][2]["body"], "Second body")
        self.assertEqual(self.requests[1][2]["music"], "Artist - Song 雪")
        self.assertEqual(self.requests[1][2]["tags"], ["c", "unix", "gtk", "café"])
        self.assertEqual(self.requests[3][2]["music"], "Second song")
        self.assertEqual(self.requests[3][2]["tags"], ["later"])

    def test_gtk_empty_music_and_tags(self):
        if not self.gtk:
            self.skipTest("GTK 3 and Xvfb are required for GUI callback tests")
        result = subprocess.run([str(self.gtk), "success", "empty-metadata"], env=self.gui_env, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.requests[-1][2]["music"], "")
        self.assertEqual(self.requests[-1][2]["tags"], [])

    def test_gtk_optional_title(self):
        if not self.gtk:
            self.skipTest("GTK 3 and Xvfb are required for GUI callback tests")
        result = subprocess.run([str(self.gtk), "success", "empty-title"], env=self.gui_env, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.requests[-1][2]["subject"], "")
        self.assertEqual(self.requests[-1][2]["body"], "GTK body")

    def test_gtk_success_and_failure_drafts(self):
        if not self.gtk:
            self.skipTest("GTK 3 and Xvfb are required for GUI callback tests")
        for mode in ("ok", "unauthorized", "post_fail", "post_error", "post_invalid", "login_500", "post_500"):
            with self.subTest(mode=mode):
                type(self).mode = mode
                result = subprocess.run([str(self.gtk), "success" if mode == "ok" else "failure"], env=self.gui_env, capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, 0, result.stderr)
                if mode == "ok":
                    self.assertEqual(self.requests[-1][2]["subject"], "GTK title")
                    self.assertEqual(self.requests[-1][2]["body"], "GTK body")
                    self.assertEqual(self.requests[-1][2]["music"], "Artist - Song 雪")
                    self.assertEqual(self.requests[-1][2]["tags"], ["c", "unix", "gtk", "café"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
