from __future__ import annotations

import unittest
from unittest.mock import patch

from mcp_server import server


class RecordingClient:
    def __init__(self) -> None:
        self.calls: list[tuple[str, dict | None]] = []

    def call(self, method: str, params: dict | None = None) -> dict:
        self.calls.append((method, params))
        return {"method": method, "params": params or {}}


class SceneToolTests(unittest.TestCase):
    def test_asset_list_forwards_filters(self) -> None:
        client = RecordingClient()
        with patch.object(server, "_client", client):
            server.tiny_asset_list("robot", "characters")
        self.assertEqual(
            client.calls,
            [("asset.list", {"keyword": "robot", "folder": "characters"})],
        )

    def test_place_translates_python_names_to_engine_protocol(self) -> None:
        client = RecordingClient()
        with patch.object(server, "_client", client):
            server.tiny_model_place(
                "content/robot.mesh.ast",
                position=[1.0, 2.0, 3.0],
                rotation_euler_deg=[0.0, 90.0, 0.0],
                scale=[2.0, 2.0, 2.0],
            )
        self.assertEqual(
            client.calls[0],
            (
                "entity.place",
                {
                    "astRelPath": "content/robot.mesh.ast",
                    "position": [1.0, 2.0, 3.0],
                    "rotationEulerDeg": [0.0, 90.0, 0.0],
                    "scale": [2.0, 2.0, 2.0],
                },
            ),
        )

    def test_partial_transform_preserves_omitted_fields_in_protocol(self) -> None:
        client = RecordingClient()
        with patch.object(server, "_client", client):
            server.tiny_model_set_transform(1001, position=[4.0, 5.0, 6.0])
        self.assertEqual(
            client.calls,
            [("entity.setTransform", {"entityId": 1001, "position": [4.0, 5.0, 6.0]})],
        )

    def test_get_and_delete_use_entity_id(self) -> None:
        client = RecordingClient()
        with patch.object(server, "_client", client):
            server.tiny_model_get_transform(1002)
            server.tiny_model_delete(1002)
        self.assertEqual(
            client.calls,
            [
                ("entity.getTransform", {"entityId": 1002}),
                ("entity.delete", {"entityId": 1002}),
            ],
        )


if __name__ == "__main__":
    unittest.main()
