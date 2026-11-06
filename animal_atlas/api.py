"""Versioned loopback JSON API with bounded long-poll subscriptions (no push queue)."""
import base64
import hmac
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlsplit, parse_qs
from .model import canonical
from .sessions import Sessions
from .replay import Timeline


class Server(ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 12
    allow_reuse_address = True

    def __init__(self, store, token, port=0):
        self.store, self.token = store, token
        self.sessions = Sessions(store)
        self.slots = threading.BoundedSemaphore(12)
        self.stopping = threading.Event()
        self.collection_running = False
        super().__init__(("127.0.0.1", port), Handler)

    def process_request(self, request, address):
        if not self.slots.acquire(blocking=False):
            self.shutdown_request(request)
            return
        try:
            super().process_request(request, address)
        except Exception:
            self.slots.release()
            raise

    def process_request_thread(self, request, address):
        try:
            request.settimeout(35)
            super().process_request_thread(request, address)
        finally:
            self.slots.release()

    def handle_error(self, request, client_address):
        pass  # Never print request headers/tokens or internal exception text.


class Handler(BaseHTTPRequestHandler):
    server_version = "AnimalAtlas/1"

    def log_message(self, *args):
        pass

    def reply(self, code, payload):
        body = canonical(payload).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.end_headers()
        self.wfile.write(body)

    def authorized(self):
        expected_host = "127.0.0.1:" + str(self.server.server_port)
        if self.headers.get("Host") != expected_host or self.headers.get("Origin"):
            self.reply(403, {"error": "local_clients_only"})
            return False
        auth = self.headers.get("Authorization", "")
        if not hmac.compare_digest(auth, "Bearer " + self.server.token):
            self.reply(401, {"error": "unauthorized"})
            return False
        if len(self.path) > 4096:
            self.reply(400, {"error": "request_too_large"})
            return False
        return True

    def do_GET(self):
        if not self.authorized():
            return
        try:
            self.route()
        except KeyError:
            self.reply(404 if urlsplit(self.path).path.startswith("/v1/sessions/") else 400,
                       {"error": "session_not_found_or_invalid_request"})
        except (ValueError, TypeError, UnicodeError):
            self.reply(400, {"error": "invalid_request"})
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception:
            self.reply(503, {"error": "storage_unavailable"})

    def do_POST(self):
        if not self.authorized():
            return
        try:
            size = int(self.headers.get("Content-Length", "0"))
            if not 1 <= size <= 8192 or self.headers.get("Transfer-Encoding"):
                raise ValueError()
            if self.headers.get("Content-Type") != "application/json":
                raise ValueError()
            body = json.loads(self.rfile.read(size))
            if not isinstance(body, dict):
                raise ValueError()
            path = urlsplit(self.path).path
            if path == "/v1/sessions/start":
                if set(body) - {"name", "sources", "seed", "duration_seconds"}:
                    raise ValueError()
                result = self.server.sessions.start(**body)
            elif path == "/v1/sessions/snapshot":
                if set(body) - {"name", "sources", "seed", "observed_from", "observed_to"}:
                    raise ValueError()
                result = self.server.sessions.snapshot(**body)
            elif len(path.strip("/").split("/")) == 4 and path.startswith("/v1/sessions/") and path.endswith("/stop"):
                if body != {"stop": True}:
                    raise ValueError()
                result = self.server.sessions.stop(path.split("/")[3])
            else:
                self.reply(404, {"error": "not_found"})
                return
            self.reply(200, result)
        except KeyError:
            self.reply(404, {"error": "session_not_found"})
        except (ValueError, TypeError, UnicodeError):
            self.reply(400, {"error": "invalid_request"})
        except (BrokenPipeError, ConnectionResetError):
            pass
        except Exception:
            self.reply(503, {"error": "session_storage_unavailable"})

    def route(self):
        url = urlsplit(self.path)
        query = parse_qs(url.query, max_num_fields=10)
        def one(key, default):
            values = query.get(key, [default])
            if len(values) != 1:
                raise ValueError()
            return values[0]
        def integer(key, default, low, high):
            value = int(one(key, default))
            if not low <= value <= high:
                raise ValueError()
            return value
        if url.path == "/v1/status":
            self.reply(200, dict(self.server.store.status(), collection_running=self.server.collection_running))
        elif url.path == "/v1/sessions":
            self.reply(200, {"sessions": self.server.sessions.list(integer("limit", "100", 1, 100))})
        elif url.path.startswith("/v1/sessions/"):
            parts = url.path.strip("/").split("/")
            sid = parts[2]
            if len(parts) == 3:
                self.reply(200, self.server.sessions.get(sid))
            elif len(parts) == 4 and parts[3] == "events":
                after = integer("after_ordinal", "-1", -1, 2**63 - 1)
                rows = self.server.sessions.rows(sid, after, integer("limit", "200", 1, 500))
                self.reply(200, {"events": rows, "next_ordinal": rows[-1]["ordinal"] if rows else after})
            elif len(parts) == 4 and parts[3] == "timeline":
                timeline = Timeline(self.server.sessions, sid, one("mode", "observed"), one("speed", "1"))
                self.reply(200, timeline.page(integer("after_index", "-1", -1, 2**63 - 1), integer("limit", "200", 1, 500)))
            else:
                self.reply(404, {"error": "not_found"})
        elif url.path == "/v1/events":
            cursor = one("before", "")
            before = None
            if cursor:
                before = json.loads(base64.urlsafe_b64decode(cursor.encode()).decode())
                if not isinstance(before, list) or len(before) != 2 or not all(isinstance(x, str) for x in before):
                    raise ValueError()
            limit = integer("limit", "200", 1, 500)
            events = self.server.store.events(before, limit, one("source", "") or None)
            next_cursor = None
            if events:
                last = events[-1]
                next_cursor = base64.urlsafe_b64encode(canonical([last["observed_at_utc"], last["event_id"]]).encode()).decode()
            self.reply(200, {"protocol_version": 1, "events": events, "next_cursor": next_cursor})
        elif url.path == "/v1/changes":
            if one("store_id", self.server.store.store_id) != self.server.store.store_id:
                self.reply(409, {"error": "store_changed", "store_id": self.server.store.store_id})
                return
            after = integer("after_seq", "0", 0, 2**63 - 1)
            limit = integer("limit", "200", 1, 500)
            wait = integer("wait", "0", 0, 25)
            high = self.server.store.high_water()
            if after > high:
                self.reply(409, {"error": "cursor_ahead_of_store", "high_water_seq": high})
                return
            deadline = time.monotonic() + wait
            while True:
                changes = self.server.store.changes(after, limit)
                if changes or time.monotonic() >= deadline or self.server.stopping.is_set():
                    break
                self.server.stopping.wait(min(0.25, max(0, deadline - time.monotonic())))
            # Consumer resumes from LAST RETURNED seq, never from high_water_seq.
            self.reply(200, {"protocol_version": 1, "changes": changes,
                             "store_id": self.server.store.store_id,
                             "next_seq": changes[-1]["seq"] if changes else after,
                             "high_water_seq": self.server.store.high_water()})
        else:
            self.reply(404, {"error": "not_found"})
