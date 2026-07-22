"""stdio MCP facade for a tinyEngine instance listening on localhost."""

from __future__ import annotations

import asyncio
import json
import time
from pathlib import Path
from typing import Any

from mcp.server.fastmcp import FastMCP, Image
from mcp.server.fastmcp.exceptions import ToolError
from mcp.types import CallToolResult, TextContent

from .ipc_client import IpcClient, IpcError


mcp = FastMCP(
    "tinyEngine",
    instructions=(
        "Control and inspect a local tinyEngine editor. Start tinyEngine with "
        "--mcp before calling tools. All engine operations execute on its main thread."
    ),
)
_client = IpcClient.from_environment()
_CAPTURE_ROOT = (Path(__file__).resolve().parents[1] / "res" / "bin" / "verify" / "captures").resolve()
_MAX_CAPTURE_BYTES = 16 * 1024 * 1024


def _call(method: str, params: dict[str, Any] | None = None) -> Any:
    try:
        return _client.call(method, params)
    except IpcError as error:
        raise ToolError(
            f"tinyEngine IPC error [{error.code}]: {error.message}"
        ) from error


@mcp.tool()
def tiny_ping() -> dict[str, Any]:
    """Check that the tinyEngine main-thread command bridge is responsive."""

    return _call("ping")


@mcp.tool()
def tiny_engine_status() -> dict[str, Any]:
    """Return engine liveness, frame count, endpoint, and scene counts."""

    return _call("engine.status")


@mcp.tool()
def tiny_scene_snapshot() -> dict[str, Any]:
    """Return a read-only snapshot of entities, boxes, camera, and animation state."""

    return _call("scene.snapshot")


@mcp.tool()
def tiny_asset_list(keyword: str = "", folder: str = "") -> dict[str, Any]:
    """List registered, placeable model assets; use astRelPath with tiny_model_place."""

    return _call("asset.list", {"keyword": keyword, "folder": folder})


def _transform_params(
    position: list[float] | None,
    rotation_euler_deg: list[float] | None,
    rotation_quaternion: list[float] | None,
    scale: list[float] | None,
) -> dict[str, Any]:
    params: dict[str, Any] = {}
    if position is not None:
        params["position"] = position
    if rotation_euler_deg is not None:
        params["rotationEulerDeg"] = rotation_euler_deg
    if rotation_quaternion is not None:
        params["rotationQuaternion"] = rotation_quaternion
    if scale is not None:
        params["scale"] = scale
    return params


@mcp.tool()
def tiny_model_place(
    ast_rel_path: str,
    position: list[float] | None = None,
    rotation_euler_deg: list[float] | None = None,
    rotation_quaternion: list[float] | None = None,
    scale: list[float] | None = None,
) -> dict[str, Any]:
    """Place a registered model asset and return its new entity ID and applied transform.

    Rotations accept either XYZ Euler degrees or an [x, y, z, w] quaternion.
    """

    params = {"astRelPath": ast_rel_path}
    params.update(_transform_params(position, rotation_euler_deg, rotation_quaternion, scale))
    return _call("entity.place", params)


@mcp.tool()
def tiny_model_get_transform(entity_id: int) -> dict[str, Any]:
    """Return one scene entity's position, quaternion rotation, and scale."""

    return _call("entity.getTransform", {"entityId": entity_id})


@mcp.tool()
def tiny_model_set_transform(
    entity_id: int,
    position: list[float] | None = None,
    rotation_euler_deg: list[float] | None = None,
    rotation_quaternion: list[float] | None = None,
    scale: list[float] | None = None,
) -> dict[str, Any]:
    """Update any supplied transform components while preserving omitted components."""

    params = {"entityId": entity_id}
    params.update(_transform_params(position, rotation_euler_deg, rotation_quaternion, scale))
    return _call("entity.setTransform", params)


@mcp.tool()
def tiny_model_delete(entity_id: int) -> dict[str, Any]:
    """Delete a model entity from the current scene."""

    return _call("entity.delete", {"entityId": entity_id})


def _validated_capture_path(path_value: Any, capture_root: Path | None = None) -> Path:
    """Resolve an engine-produced PNG without allowing paths outside its capture root."""

    if not isinstance(path_value, str) or not path_value:
        raise ToolError("tinyEngine returned an invalid capture path")

    root = (capture_root or _CAPTURE_ROOT).resolve()
    try:
        path = Path(path_value).resolve(strict=True)
        path.relative_to(root)
    except (OSError, ValueError) as error:
        raise ToolError("tinyEngine capture path is missing or outside the allowed directory") from error

    if path.suffix.lower() != ".png" or not path.is_file():
        raise ToolError("tinyEngine capture result is not a PNG file")
    if path.stat().st_size > _MAX_CAPTURE_BYTES:
        raise ToolError("tinyEngine capture exceeds the 16 MiB MCP image limit")
    return path


@mcp.tool()
async def tiny_capture_frame(
    include_ui: bool = True,
    timeout_seconds: float = 10.0,
) -> CallToolResult:
    """Capture tinyEngine and return a PNG, optionally omitting editor UI for one frame."""

    if not 0.1 <= timeout_seconds <= 30.0:
        raise ToolError("timeout_seconds must be between 0.1 and 30.0")

    started = time.monotonic()
    created = await asyncio.to_thread(_call, "capture.frame", {"includeUi": include_ui})
    if not isinstance(created, dict) or not isinstance(created.get("jobId"), int):
        raise ToolError("tinyEngine returned an invalid capture job")

    job_id = created["jobId"]
    deadline = started + timeout_seconds
    result: dict[str, Any] | None = None
    while time.monotonic() < deadline:
        polled = await asyncio.to_thread(_call, "capture.get", {"jobId": job_id})
        if not isinstance(polled, dict):
            raise ToolError("tinyEngine returned an invalid capture status")
        result = polled
        if result.get("status") == "ready":
            break
        await asyncio.sleep(0.05)
    else:
        raise ToolError(f"tinyEngine frame capture timed out after {timeout_seconds:.1f}s")

    assert result is not None
    path = _validated_capture_path(result.get("path"))
    metadata = {
        "jobId": job_id,
        "width": result.get("width"),
        "height": result.get("height"),
        "bytes": path.stat().st_size,
        "capturedFrame": result.get("capturedFrame"),
        "includeUi": result.get("includeUi", include_ui),
        "elapsedMs": round((time.monotonic() - started) * 1000.0, 2),
        "path": str(path),
    }
    return CallToolResult(
        content=[
            Image(path=path).to_image_content(),
            TextContent(type="text", text=json.dumps(metadata, ensure_ascii=False)),
        ],
        structuredContent=metadata,
    )


@mcp.tool()
def tiny_engine_shutdown() -> dict[str, Any]:
    """Request a graceful tinyEngine shutdown after the current frame."""

    return _call("engine.shutdown")


def main() -> None:
    """Run the MCP server over stdio for Codex, Claude, or another MCP host."""

    mcp.run(transport="stdio")


if __name__ == "__main__":
    main()
