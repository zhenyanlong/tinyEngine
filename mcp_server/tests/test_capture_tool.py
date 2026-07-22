from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from mcp_server import server


class FakeCaptureClient:
    def __init__(self, png_path: Path) -> None:
        self.png_path = png_path

    def call(self, method: str, params: dict | None = None) -> dict:
        if method == "capture.frame":
            self.capture_params = params
            return {"jobId": 7, "status": "pending", "requestedFrame": 10,
                    "includeUi": params["includeUi"] if params else True}
        if method == "capture.get":
            return {
                "jobId": 7,
                "status": "ready",
                "width": 2,
                "height": 1,
                "bytes": self.png_path.stat().st_size,
                "capturedFrame": 11,
                "includeUi": self.capture_params["includeUi"],
                "path": str(self.png_path),
            }
        raise AssertionError(f"unexpected method: {method}")


class CapturePathTests(unittest.TestCase):
    def test_rejects_paths_outside_capture_root(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            root = base / "captures"
            root.mkdir()
            outside = base / "outside.png"
            outside.write_bytes(b"png")

            with self.assertRaisesRegex(Exception, "outside the allowed directory"):
                server._validated_capture_path(str(outside), root)


class CaptureToolTests(unittest.IsolatedAsyncioTestCase):
    async def test_returns_image_content_and_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp) / "captures"
            root.mkdir()
            png = root / "frame.png"
            png.write_bytes(b"\x89PNG\r\n\x1a\nMVP")

            with patch.object(server, "_client", FakeCaptureClient(png)), \
                 patch.object(server, "_CAPTURE_ROOT", root):
                result = await server.tiny_capture_frame(include_ui=False)

            self.assertEqual(result.content[0].type, "image")
            self.assertEqual(result.content[0].mimeType, "image/png")
            self.assertEqual(result.structuredContent["jobId"], 7)
            self.assertEqual(result.structuredContent["width"], 2)
            self.assertFalse(result.structuredContent["includeUi"])


if __name__ == "__main__":
    unittest.main()
