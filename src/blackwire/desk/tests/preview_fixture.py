"""Loopback-only UI fixture. It never connects to a PS5 or an SMB share.

Run with --port 0 for an available local port. The canonical embedded page is
served unchanged; only API responses and neutral generated cover images are fake.
This is a development test fixture, not part of Blackwire's installation.
"""

import argparse
from copy import deepcopy
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
from threading import Lock
import time
from urllib.parse import parse_qs, urlsplit


PAGE = Path(__file__).resolve().parents[1] / "index.html"
MIB = 1024 ** 2
GIB = 1024 ** 3
LOCK = Lock()
CALLS = []
TOKEN = "f" * 32
OPTIONS = {}
OPERATION = {"id": "", "itemId": "", "phase": "idle", "message": "No fixture action submitted.", "active": False}
NEXT_OPERATION = 1


def initial_state(scenario="idle"):
    def source(name, title, number, ready=False, supported=True, container=False):
        return {
            "name": name, "source": name, "title": title, "titleId": "PPSA9%04d" % number,
            "sourceKind": "container" if container else "folder", "cover": number != 5,
            "canPlay": ready and supported, "canPrepare": supported and not ready, "canStart": supported,
            "startupReady": ready, "prepared": ready, "onConsole": ready,
            "sendBytes": (720 + number * 96) * MIB, "stayBytes": (52 + number * 7) * GIB,
            "consoleAllocatedBytes": (720 + number * 96) * MIB if ready else None,
            "storageVerifiedAt": "preparation" if ready else "",
            "preparationReason": "" if supported else "This source layout has not been validated by the reader.",
            "playReason": "" if supported else "Choose a supported source or check the preparation report.",
        }

    games = [source("Northstar", "Northstar", 1, True),
             source("Compressed/Signal & Dust.ffpfsc", "Signal & Dust", 2, True, container=True),
             source("Collection/Ashore", "Ashore", 3),
             source("Glass Harbor", "Glass Harbor", 4, supported=False),
             source("Stone Circuit", "Stone Circuit", 5),
             source("Collection/The Longest Journey Through the Silent Valley", "The Longest Journey Through the Silent Valley", 6),
             source("Night Flight", "Night Flight", 7)]
    state = {
        "backend": "native", "settings": {"server": "192.0.2.10", "share": "PreservedLibrary"},
        "setup": {"configured": True, "supported": True, "revision": 3, "token": TOKEN, "editable": True,
                  "busy": False, "phase": "saved", "hasSavedCredentials": True, "message": "PC share connected."},
        "library": {"phase": "ready", "generation": 7, "stale": False}, "share": {"shared": True},
        "games": games, "jobs": [], "stream": {"phase": "stopped"},
    }
    if scenario == "active":
        state["stream"] = {"phase": "running", "game": "Northstar", "runId": "2" * 32,
                           "canClose": True, "gamePid": 777, "readerPid": 0,
                           "dataCountersObserved": False, "elapsedMs": 78000,
                           "message": "Game launched; private reader inputs assigned. Delivery and the game screen are not yet verified."}
    elif scenario == "preparing":
        state["jobs"] = [{"id": "1" * 32, "name": "Northstar", "state": "running", "message": "Checking startup files."}]
    elif scenario == "offline":
        state["library"].update(phase="error", stale=True)
        state["share"]["shared"] = False
        state["setup"]["message"] = "The PC share could not be reached. Check that the PC is awake."
        for game in games:
            game.update(canPlay=False, canStart=False, canPrepare=False,
                        preparationReason="Reconnect the PC share before sending files.",
                        playReason="Reconnect the PC share before launching.")
    elif scenario == "empty":
        state["games"] = []
    elif scenario != "idle":
        raise ValueError("Unknown fixture scenario")
    return state


STATE = initial_state()


def dashboard_catalog():
    """Protocol fixture only. No real registration, Sony identity or SMB proof."""
    items = []
    for game in STATE["games"]:
        item_id = "pc:%s:%s" % (game["titleId"], hashlib.sha256(("fixture-PC:" + game["source"]).encode()).hexdigest())
        items.append({"id": item_id, "title": game["title"], "titleId": game["titleId"], "source": game["source"], "kind": 1,
                      "startupReady": game["startupReady"], "canPlay": game["canPlay"], "canSend": game["canPrepare"],
                      "playReason": game["playReason"], "sendReason": game["preparationReason"],
                      "libraryGeneration": STATE["library"]["generation"], "settingsRevision": STATE["setup"]["revision"],
                      "cover": "/api/dashboard/cover?id=" + item_id})
    if OPTIONS.get("scenario") != "empty":
        for title, title_id, source_path, kind, supported in [
            ("Console Northstar", "PPSA90001", "/data/homebrew/Games/Console Northstar", 2, True),
            ("Moonlit Arcade", "CUSA90012", "/user/app/CUSA90012/app.pkg", 3, True),
            ("Remote Archive", "PPSA90013", "/data/homebrew/Games/Remote Archive.ffpfs", 4, False),
        ]:
            prefix = {2: "folder", 3: "pkg", 4: "image"}[kind]
            item_id = "%s:%s:%s" % (prefix, title_id, hashlib.sha256(("fixture-console:" + source_path).encode()).hexdigest())
            items.append({"id": item_id, "title": title, "titleId": title_id, "source": source_path, "kind": kind,
                          "startupReady": supported, "canPlay": supported, "canSend": False,
                          "playReason": "" if supported else "Local image launch verification is not available in this build.",
                          "sendReason": "This game is already on the PS5; Send files applies to PC sources.",
                          "libraryGeneration": 0, "settingsRevision": 0, "cover": "/api/dashboard/cover?id=" + item_id})
    result = {"schema": 1, "catalogGeneration": OPTIONS.get("generation", 9),
              "pcConnected": STATE["library"]["phase"] == "ready", "consoleConnected": True,
              "items": items, "operation": deepcopy(OPERATION)}
    result.update(deepcopy(OPTIONS.get("catalogPatch", {})))
    return result


def finish_operation():
    if OPERATION["phase"] == "idle":
        return
    item = next((entry for entry in dashboard_catalog()["items"] if entry["id"] == OPERATION["itemId"]), None)
    if item and item["kind"] == 1:
        for game in STATE["games"]:
            if game["source"] == item["source"]:
                game.update(startupReady=True, prepared=True, onConsole=True, canPlay=True, canPrepare=False)
        for job in STATE["jobs"]:
            job.update(state="done", message="Fixture startup check ended.")
    OPERATION.update(phase="stopped", active=False, message="The fixture operation ended; its reservation was released.")


def cover_svg(game):
    number = int(game["titleId"][-1])
    colors = ["#53798b", "#8c7664", "#627a68", "#856b83", "#777777", "#6c7291", "#7e8355"]
    color = colors[(number - 1) % len(colors)]
    initials = "".join(word[0] for word in game["title"].split() if word[0].isalnum())[:2]
    return ('<svg xmlns="http://www.w3.org/2000/svg" width="512" height="512" viewBox="0 0 512 512">'
            '<rect width="512" height="512" fill="#15191e"/>'
            f'<path d="M0 512 512 0v512z" fill="{color}"/>'
            f'<circle cx="256" cy="226" r="130" fill="#15191e" stroke="{color}" stroke-width="2"/>'
            f'<text x="256" y="258" text-anchor="middle" font-size="94" font-family="sans-serif" fill="#e5e9ef">{initials}</text>'
            '</svg>').encode()


class Fixture(BaseHTTPRequestHandler):
    def log_message(self, *_args):
        pass

    def respond(self, status, body, content_type="application/json"):
        data = json.dumps(body).encode() if content_type == "application/json" else body
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-store")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        url = urlsplit(self.path)
        if url.path in ("/", "/ps5"):
            return self.respond(200, PAGE.read_bytes(), "text/html; charset=utf-8")
        if url.path == "/assets/blackwire-logo.png":
            logo = PAGE.parents[1] / "library" / "icon0.png"
            return self.respond(200, logo.read_bytes(), "image/png")
        if url.path in ("/api/state", "/__fixture"):
            with LOCK:
                body = deepcopy(STATE if url.path == "/api/state" else {"fixture": "blackwire-combined-ui-preview-v2", "state": STATE,
                                  "dashboard": dashboard_catalog(), "options": OPTIONS, "calls": CALLS})
            return self.respond(200, body)
        if url.path == "/api/dashboard/library":
            with LOCK:
                body = dashboard_catalog()
                unavailable = OPTIONS.get("libraryUnavailable", False)
            return self.respond(503 if unavailable else 200, {"error": "Combined fixture library unavailable."} if unavailable else body)
        if url.path in ("/cover", "/api/dashboard/cover"):
            name = parse_qs(url.query).get("name", [""])[0]
            with LOCK:
                if url.path == "/cover":
                    game = next((g for g in STATE["games"] if g["name"] == name), None)
                else:
                    item_id = parse_qs(url.query).get("id", [""])[0]
                    game = next((g for g in dashboard_catalog()["items"] if g["id"] == item_id), None)
                missing = game and game["titleId"] == "PPSA90005"
            if not game or missing:
                return self.respond(404, {"error": "Fixture cover unavailable"})
            return self.respond(200, cover_svg(game), "image/svg+xml")
        if url.path == "/favicon.ico":
            return self.respond(204, b"", "image/x-icon")
        return self.respond(404, {"error": "Unknown fixture route"})

    def do_POST(self):
        global STATE, OPTIONS, OPERATION, NEXT_OPERATION
        if self.headers.get("Content-Length", "0").isdigit() is False or int(self.headers.get("Content-Length", "0")) > 16384:
            return self.respond(413, {"error": "Fixture input too large"})
        try:
            body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", "0"))))
        except (ValueError, TypeError):
            return self.respond(400, {"error": "Invalid fixture JSON"})
        if self.path == "/__fixture":
            try:
                with LOCK:
                    if "scenario" in body:
                        STATE = initial_state(body["scenario"])
                        OPTIONS = {"scenario": body["scenario"]}
                        OPERATION = {"id": "", "itemId": "", "phase": "idle", "message": "No fixture action submitted.", "active": False}
                        NEXT_OPERATION = 1
                        CALLS.clear()
                    if "patch" in body:
                        STATE.update(body["patch"])
                    if "options" in body:
                        OPTIONS.update(body["options"])
                    if "operation" in body:
                        OPERATION.update(body["operation"])
                    if body.get("finishOperation"):
                        finish_operation()
                    result = deepcopy(STATE)
            except ValueError as error:
                return self.respond(400, {"error": str(error)})
            return self.respond(200, result)
        if self.path == "/api/dashboard/action":
            with LOCK:
                CALLS.append({"route": self.path, "body": body, "token": self.headers.get("X-Blackwire-Token", "")})
                if self.headers.get("X-Blackwire-Token", "") != TOKEN:
                    return self.respond(403, {"ok": False, "error": "Refresh the fixture token."})
                if not isinstance(body, dict) or set(body) != {"id", "kind", "action", "catalogGeneration"}:
                    return self.respond(400, {"ok": False, "error": "Choose one exact current catalog action."})
                item = next((entry for entry in dashboard_catalog()["items"] if entry["id"] == body["id"] and entry["kind"] == body["kind"]), None)
                if (OPTIONS.get("refuseAction") or OPERATION["active"] or not item or body["catalogGeneration"] != OPTIONS.get("generation", 9)
                        or body["action"] not in ("play", "send") or not item["canPlay" if body["action"] == "play" else "canSend"]
                        or body["action"] == "send" and item["kind"] != 1):
                    return self.respond(409, {"ok": False, "error": "The selected source or catalog generation changed; refresh and choose it again."})
                operation_id = "%032x" % (0xA000 + NEXT_OPERATION)
                NEXT_OPERATION += 1
                phase = "waiting-for-dashboard-exit" if OPTIONS.get("waitForFrontend") else "accepted"
                message = "Waiting for the native fixture dashboard to exit normally." if OPTIONS.get("waitForFrontend") else "The selected fixture operation was accepted."
                OPERATION = {"id": operation_id, "itemId": item["id"], "phase": phase, "message": message, "active": True,
                             "exitRequested": OPTIONS.get("waitForFrontend", False), "frontendPid": 701 if OPTIONS.get("waitForFrontend") else 0}
                if body["action"] == "send":
                    STATE["jobs"] = [{"id": operation_id, "name": item["source"], "state": "running", "message": "Checking fixture startup files."}]
                result = {"ok": True, "accepted": True, "id": operation_id, "phase": phase, "message": message}
                malformed = OPTIONS.get("malformedAck")
                drop = OPTIONS.get("dropAck")
            time.sleep(0.3)
            if drop:
                self.close_connection = True
                return
            return self.respond(200 if malformed else 202, result)
        if self.path not in ("/api/send", "/api/start", "/api/play", "/api/close", "/api/refresh"):
            return self.respond(404, {"error": "This test fixture does not change settings"})
        with LOCK:
            CALLS.append({"route": self.path, "body": body, "token": self.headers.get("X-Blackwire-Token", "")})
            if self.path == "/api/send":
                STATE["jobs"] = [{"id": "1" * 32, "name": body["name"], "state": "running", "message": "Checking startup files."}]
                result = {"ok": True, "jobId": "1" * 32, "job": deepcopy(STATE["jobs"][0]), "message": "Startup file check queued."}
            elif self.path in ("/api/start", "/api/play"):
                STATE["stream"] = {"phase": "preflight", "game": body["name"], "runId": "2" * 32,
                                   "canClose": True, "gamePid": 0, "readerPid": 0, "message": "Checking startup files before launch."}
                result = {"ok": True, "runId": "2" * 32}
            elif self.path == "/api/close":
                STATE["stream"] = {"phase": "stopped", "game": STATE["stream"].get("game", ""),
                                   "runId": "2" * 32, "canClose": False, "gamePid": 0, "readerPid": 0}
                result = {"ok": True}
            else:
                result = {"ok": True}
        time.sleep(0.3)  # Makes the client's in-flight duplicate guard observable.
        return self.respond(200, result)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=0)
    args = parser.parse_args()
    server = ThreadingHTTPServer(("127.0.0.1", args.port), Fixture)
    print("BLACKWIRE_UI_FIXTURE http://127.0.0.1:%d/" % server.server_port, flush=True)
    server.serve_forever()
