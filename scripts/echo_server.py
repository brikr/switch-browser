"""Tiny HTTP server for loader tests: serves files from a directory and logs every request's headers.

Usage: python3 -I scripts/echo_server.py [--port 8080] [--dir scratch]
"""
import argparse
import functools
import http.server
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    def log_request(self, code="-", size="-"):
        sys.stderr.write(f"--- {self.command} {self.path} -> {code}\n")
        for k, v in self.headers.items():
            sys.stderr.write(f"    {k}: {v}\n")
        sys.stderr.flush()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8080)
    ap.add_argument("--dir", default="scratch")
    args = ap.parse_args()
    handler = functools.partial(Handler, directory=args.dir)
    http.server.ThreadingHTTPServer(("127.0.0.1", args.port), handler).serve_forever()


if __name__ == "__main__":
    main()
