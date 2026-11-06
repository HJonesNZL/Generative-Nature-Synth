"""Consumer-side bridge for the future UI/audio worker. Never call from an audio callback."""
import json
import urllib.request
import urllib.parse
from pathlib import Path


class Client:
    def __init__(self, state_dir):
        state_dir = Path(state_dir)
        self.connection = json.loads((state_dir / "connection.json").read_text())
        port = self.connection["port"]
        if type(port) is not int or not 1 <= port <= 65535:
            raise ValueError("invalid local port")
        self.base = "http://127.0.0.1:" + str(port)
        self.token = (state_dir / "api.token").read_text().strip()
        self.store_id = self.connection.get("store_id")
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))

    def get(self, route):
        if not route.startswith("/v1/"):
            raise ValueError("invalid protocol route")
        req = urllib.request.Request(self.base + route, headers={"Authorization": "Bearer " + self.token})
        with self.opener.open(req, timeout=32) as response:
            payload = response.read(8 * 1024 * 1024 + 1)
        if len(payload) > 8 * 1024 * 1024:
            raise ValueError("response exceeds client capacity")
        return json.loads(payload)

    def changes(self, after_seq, wait=25, limit=200):
        suffix = "&store_id=" + urllib.parse.quote(self.store_id, safe="") if self.store_id else ""
        response = self.get("/v1/changes?after_seq=%d&wait=%d&limit=%d" % (after_seq, wait, limit) + suffix)
        self.store_id = response["store_id"]
        return response

    def post(self, route, body):
        if not route.startswith("/v1/"):
            raise ValueError("invalid protocol route")
        request = urllib.request.Request(self.base + route, data=json.dumps(body).encode(),
                  headers={"Authorization": "Bearer " + self.token, "Content-Type": "application/json"})
        with self.opener.open(request, timeout=32) as response:
            return json.loads(response.read(1024 * 1024))
