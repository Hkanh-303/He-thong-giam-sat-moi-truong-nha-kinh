#!/usr/bin/env python3
"""Small HTTP receiver for JSON sensor samples sent by the ESP32."""

import json
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer


class SensorReceiver(BaseHTTPRequestHandler):
    def _send_json(self, status, body):
        response = json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(response)))
        self.end_headers()
        self.wfile.write(response)

    def do_GET(self):
        if self.path == "/health":
            self._send_json(200, {"status": "ok"})
        else:
            self._send_json(404, {"error": "not found"})

    def do_POST(self):
        if self.path != "/sensors":
            self._send_json(404, {"error": "not found"})
            return

        try:
            content_length = int(self.headers.get("Content-Length", "0"))
            if content_length <= 0 or content_length > 65536:
                raise ValueError("invalid content length")
            payload = json.loads(self.rfile.read(content_length))
            if not isinstance(payload, dict):
                raise ValueError("JSON body must be an object")
        except (ValueError, json.JSONDecodeError) as exc:
            self._send_json(400, {"error": str(exc)})
            return

        received_at = datetime.now(timezone.utc).isoformat(timespec="seconds")
        print(json.dumps({"received_at": received_at, **payload}, ensure_ascii=False), flush=True)
        self._send_json(200, {"status": "received"})

    def log_message(self, fmt, *args):
        # Keep the console focused on sensor samples and errors.
        return


def main():
    address = ("0.0.0.0", 5000)
    server = ThreadingHTTPServer(address, SensorReceiver)
    print(f"Sensor receiver listening on http://{address[0]}:{address[1]}", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("Stopping sensor receiver", flush=True)
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
