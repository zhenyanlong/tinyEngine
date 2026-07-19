"""End-to-end smoke test through an actual MCP stdio client session."""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import sys
from typing import Any

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


def _structured_result(result: Any) -> dict[str, Any]:
    data = getattr(result, "structuredContent", None)
    if isinstance(data, dict):
        return data
    content = getattr(result, "content", [])
    for item in content:
        text = getattr(item, "text", None)
        if isinstance(text, str):
            parsed = json.loads(text)
            if isinstance(parsed, dict):
                return parsed
    raise RuntimeError("MCP tool did not return a JSON object")


async def run_mcp_smoke(port: int, shutdown: bool) -> dict[str, Any]:
    env = {
        **os.environ,
        "TINYENGINE_MCP_PORT": str(port),
        "TINYENGINE_MCP_TIMEOUT": "5.0",
    }
    params = StdioServerParameters(
        command=sys.executable,
        args=["-m", "mcp_server"],
        env=env,
    )

    async with stdio_client(params) as (read_stream, write_stream):
        async with ClientSession(read_stream, write_stream) as session:
            await session.initialize()
            tools = await session.list_tools()
            ping_result = await session.call_tool("tiny_ping")
            status_result = await session.call_tool("tiny_engine_status")
            snapshot_result = await session.call_tool("tiny_scene_snapshot")
            if ping_result.isError or status_result.isError or snapshot_result.isError:
                raise RuntimeError("one or more MCP read tools failed")

            result = {
                "tools": sorted(tool.name for tool in tools.tools),
                "ping": _structured_result(ping_result),
                "status": _structured_result(status_result),
                "snapshot": _structured_result(snapshot_result),
            }
            if shutdown:
                shutdown_result = await session.call_tool("tiny_engine_shutdown")
                if shutdown_result.isError:
                    raise RuntimeError("tiny_engine_shutdown failed")
                result["shutdown"] = _structured_result(shutdown_result)
            return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", type=int, default=9527)
    parser.add_argument("--shutdown", action="store_true")
    args = parser.parse_args()

    result = asyncio.run(run_mcp_smoke(args.port, args.shutdown))
    summary = {
        "tools": result["tools"],
        "pong": result["ping"].get("pong"),
        "entityCount": result["snapshot"].get("counts", {}).get("entities"),
        "shutdownAccepted": result.get("shutdown", {}).get("accepted"),
    }
    print("[MCP stdio Smoke] PASS")
    print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    main()
