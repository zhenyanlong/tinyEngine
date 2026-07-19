from __future__ import annotations

import json
import socket
import threading
import unittest
from typing import Any

from mcp_server.ipc_client import IpcClient, IpcError


class FakeEngineServer:
    def __init__(self) -> None:
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.bind(("127.0.0.1", 0))
        self.socket.listen(1)
        self.port = int(self.socket.getsockname()[1])
        self.running = True
        self.thread = threading.Thread(target=self._serve, daemon=True)
        self.thread.start()

    def close(self) -> None:
        self.running = False
        try:
            socket.create_connection(("127.0.0.1", self.port), 0.1).close()
        except OSError:
            pass
        self.socket.close()
        self.thread.join(timeout=1.0)

    def _serve(self) -> None:
        try:
            connection, _ = self.socket.accept()
        except OSError:
            return
        with connection:
            stream = connection.makefile("rwb")
            while self.running:
                line = stream.readline()
                if not line:
                    break
                request = json.loads(line)
                response = self._response(request)
                stream.write(json.dumps(response).encode("utf-8") + b"\n")
                stream.flush()

    @staticmethod
    def _response(request: dict[str, Any]) -> dict[str, Any]:
        if request["method"] == "fail":
            return {
                "id": request["id"],
                "result": None,
                "error": {"code": "expected_failure", "message": "boom"},
            }
        return {
            "id": request["id"],
            "result": {"method": request["method"], "params": request["params"]},
            "error": None,
        }


class IpcClientTests(unittest.TestCase):
    def setUp(self) -> None:
        self.server = FakeEngineServer()
        self.client = IpcClient(port=self.server.port, timeout=1.0)

    def tearDown(self) -> None:
        self.client.close()
        self.server.close()

    def test_round_trip_uses_persistent_connection(self) -> None:
        first = self.client.call("ping")
        second = self.client.call("scene.snapshot", {"detail": "summary"})
        self.assertEqual(first, {"method": "ping", "params": {}})
        self.assertEqual(
            second,
            {"method": "scene.snapshot", "params": {"detail": "summary"}},
        )

    def test_engine_error_preserves_code_and_message(self) -> None:
        with self.assertRaises(IpcError) as raised:
            self.client.call("fail")
        self.assertEqual(raised.exception.code, "expected_failure")
        self.assertEqual(raised.exception.message, "boom")

    def test_rejects_invalid_arguments(self) -> None:
        with self.assertRaises(ValueError):
            self.client.call("")
        with self.assertRaises(TypeError):
            self.client.call("ping", [])  # type: ignore[arg-type]


if __name__ == "__main__":
    unittest.main()
