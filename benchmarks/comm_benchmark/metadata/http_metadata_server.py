#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# ----------------------------------------------------------------------------
# Copyright (c) 2026 Huawei Technologies Co., Ltd.
# This program is free software, you can redistribute it and/or modify it under the terms and conditions of
# CANN Open Software License Agreement Version 2.0 (the "License").
# Please refer to the License for details. You may not use this file except in compliance with the License.
# THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
# See LICENSE in the root of the software repository for the full text of the License.
# ----------------------------------------------------------------------------

"""Small HTTP metadata server for benchmark coordination."""

import argparse
import json
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Lock
from urllib.parse import unquote, urlparse


class MetadataStore:
    def __init__(self):
        self._items = {}
        self._lock = Lock()

    def put(self, key, value):
        with self._lock:
            self._items[key] = value

    def get(self, key):
        with self._lock:
            return self._items.get(key)

    def delete_prefix(self, prefix):
        with self._lock:
            for key in list(self._items):
                if key.startswith(prefix):
                    del self._items[key]


def make_handler(store):
    class Handler(BaseHTTPRequestHandler):
        def _send_json(self, code, payload):
            data = json.dumps(payload, sort_keys=True).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            key = unquote(urlparse(self.path).path)
            value = store.get(key)
            if value is None:
                self._send_json(404, {"error": "not found", "key": key})
                return
            self._send_json(200, {"key": key, "value": value})

        def do_PUT(self):
            key = unquote(urlparse(self.path).path)
            length = int(self.headers.get("Content-Length", "0"))
            body = self.rfile.read(length).decode("utf-8")
            store.put(key, json.loads(body) if body else {})
            self._send_json(200, {"ok": True, "key": key})

        def do_DELETE(self):
            prefix = unquote(urlparse(self.path).path)
            store.delete_prefix(prefix)
            self._send_json(200, {"ok": True, "prefix": prefix})

        def log_message(self, fmt, *args):
            return

    return Handler


def main():
    parser = argparse.ArgumentParser(description="HIXL benchmark metadata server")
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--port", type=int, default=18080)
    args = parser.parse_args()
    server = ThreadingHTTPServer((args.host, args.port), make_handler(MetadataStore()))
    print(f"[INFO] metadata server listening on {args.host}:{args.port}", flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
