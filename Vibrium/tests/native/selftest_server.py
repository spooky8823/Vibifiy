#!/usr/bin/env python3
"""Local server for `vibrium --selftest` (standard library only).

Serves tests/fixtures/ and answers /redirect-to-ad.js with a 302 to a host
that the selftest filter list blocks.

    python tests/native/selftest_server.py [port]
"""
import functools
import http.server
import pathlib
import sys

FIXTURES = pathlib.Path(__file__).resolve().parent.parent / "fixtures"
REDIRECT_TARGET = "http://ads.vibrium-selftest.test/redirected.js"


class Handler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path.split("?")[0] == "/redirect-to-ad.js":
            self.send_response(302)
            self.send_header("Location", REDIRECT_TARGET)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        super().do_GET()

    def log_message(self, fmt, *args):
        sys.stderr.write("[selftest-server] " + fmt % args + "\n")


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8123
    handler = functools.partial(Handler, directory=str(FIXTURES))
    with http.server.ThreadingHTTPServer(("127.0.0.1", port), handler) as server:
        print(f"serving {FIXTURES} on http://localhost:{port}/", flush=True)
        server.serve_forever()


if __name__ == "__main__":
    main()
