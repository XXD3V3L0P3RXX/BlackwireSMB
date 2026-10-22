"""Loopback protocol fixture checks; never contacts PS5, Sony or an SMB share."""
import importlib.util
import json
from pathlib import Path
from threading import Thread
from urllib.error import HTTPError
from urllib.request import Request, urlopen
import unittest


class CombinedFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location("blackwire_combined_preview_fixture", Path(__file__).with_name("preview_fixture.py"))
        cls.fixture = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.fixture)
        cls.server = cls.fixture.ThreadingHTTPServer(("127.0.0.1", 0), cls.fixture.Fixture)
        cls.server.daemon_threads = True
        cls.thread = Thread(target=cls.server.serve_forever)
        cls.thread.start()
        cls.base = "http://127.0.0.1:%d" % cls.server.server_port

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=5)
        assert not cls.thread.is_alive()

    def request(self, route, body=None, token=None):
        headers = {"Content-Type": "application/json"}
        if token is not None:
            headers["X-Blackwire-Token"] = token
        request = Request(self.base + route, data=None if body is None else json.dumps(body).encode(), headers=headers)
        try:
            with urlopen(request, timeout=5) as response:
                return response.status, json.load(response)
        except HTTPError as response:
            return response.code, json.load(response)

    def setUp(self):
        self.request("/__fixture", {"scenario": "idle"})

    def action(self, item, action="play", generation=9):
        return {"id": item["id"], "kind": item["kind"], "action": action, "catalogGeneration": generation}

    def catalog(self):
        status, body = self.request("/api/dashboard/library")
        self.assertEqual(status, 200)
        return body

    def test_combined_response_does_not_put_console_entries_in_pc_state(self):
        _, state = self.request("/api/state")
        catalog = self.catalog()
        self.assertEqual(len(state["games"]), 7)
        self.assertTrue(all(not game["source"].startswith("/") for game in state["games"]))
        self.assertEqual(len(catalog["items"]), 10)
        self.assertEqual(sum(item["kind"] != 1 for item in catalog["items"]), 3)
        self.assertTrue(all(item["cover"] == "/api/dashboard/cover?id=" + item["id"] for item in catalog["items"]))

    def test_console_play_accepts_only_typed_current_catalog_action(self):
        item = next(item for item in self.catalog()["items"] if item["kind"] == 3)
        status, result = self.request("/api/dashboard/action", self.action(item), self.fixture.TOKEN)
        self.assertEqual(status, 202)
        self.assertTrue(result["ok"] and result["accepted"])
        self.assertRegex(result["id"], "^[0-9a-f]{32}$")
        self.assertEqual(self.catalog()["operation"]["itemId"], item["id"])
        self.assertEqual(self.request("/api/dashboard/action", self.action(item), self.fixture.TOKEN)[0], 409)

    def test_console_send_wrong_token_stale_generation_and_old_body_are_refused(self):
        item = next(item for item in self.catalog()["items"] if item["kind"] == 2)
        self.assertEqual(self.request("/api/dashboard/action", self.action(item, "send"), self.fixture.TOKEN)[0], 409)
        self.assertEqual(self.request("/api/dashboard/action", self.action(item), "incorrect")[0], 403)
        self.assertEqual(self.request("/api/dashboard/action", self.action(item, generation=8), self.fixture.TOKEN)[0], 409)
        self.assertEqual(self.request("/api/dashboard/action", {"name": item["source"]}, self.fixture.TOKEN)[0], 400)

    def test_browser_queue_records_wait_for_frontend_without_local_stream_fabrication(self):
        self.request("/__fixture", {"options": {"waitForFrontend": True}})
        item = next(item for item in self.catalog()["items"] if item["kind"] == 2)
        status, result = self.request("/api/dashboard/action", self.action(item), self.fixture.TOKEN)
        self.assertEqual(status, 202)
        self.assertEqual(result["phase"], "waiting-for-dashboard-exit")
        operation = self.catalog()["operation"]
        self.assertTrue(operation["active"] and operation["exitRequested"])
        self.assertEqual(operation["frontendPid"], 701)
        self.assertEqual(self.request("/api/state")[1]["stream"]["phase"], "stopped")

    def test_pc_preparation_changes_send_only_to_play_only(self):
        ready = next(item for item in self.catalog()["items"] if item["kind"] == 1 and item["startupReady"])
        self.assertTrue(ready["canPlay"])
        self.assertFalse(ready["canSend"])
        self.assertEqual(self.request("/api/dashboard/action", self.action(ready, "send"), self.fixture.TOKEN)[0], 409)
        item = next(item for item in self.catalog()["items"] if item["kind"] == 1 and not item["startupReady"] and item["canSend"])
        self.assertFalse(item["canPlay"])
        self.assertEqual(self.request("/api/dashboard/action", self.action(item, "play"), self.fixture.TOKEN)[0], 409)
        status, first = self.request("/api/dashboard/action", self.action(item, "send"), self.fixture.TOKEN)
        self.assertEqual(status, 202)
        self.assertTrue(first["accepted"])
        self.request("/__fixture", {"finishOperation": True})
        prepared = next(row for row in self.catalog()["items"] if row["id"] == item["id"])
        self.assertTrue(prepared["startupReady"] and prepared["canPlay"])
        self.assertFalse(prepared["canSend"])
        self.assertEqual(self.request("/api/dashboard/action", self.action(prepared, "send"), self.fixture.TOKEN)[0], 409)
        self.assertEqual(self.request("/api/dashboard/action", self.action(prepared), self.fixture.TOKEN)[0], 202)

    def test_prepared_pc_never_advertises_send(self):
        item = next(item for item in self.catalog()["items"] if item["kind"] == 1 and item["startupReady"])
        self.assertFalse(item["canSend"])
        self.assertEqual(self.request("/api/dashboard/action", self.action(item, "send"), self.fixture.TOKEN)[0], 409)


if __name__ == "__main__":
    unittest.main()
