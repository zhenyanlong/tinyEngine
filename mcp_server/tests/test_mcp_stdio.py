from __future__ import annotations

import os
import sys
import unittest

from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client


class McpStdioTests(unittest.IsolatedAsyncioTestCase):
    async def test_lists_the_mvp_tools(self) -> None:
        params = StdioServerParameters(
            command=sys.executable,
            args=["-m", "mcp_server"],
            env={
                **os.environ,
                "TINYENGINE_MCP_PORT": "1",
                "TINYENGINE_MCP_TIMEOUT": "0.2",
            },
        )

        async with stdio_client(params) as (read_stream, write_stream):
            async with ClientSession(read_stream, write_stream) as session:
                await session.initialize()
                result = await session.list_tools()

                unavailable = await session.call_tool("tiny_ping")

        self.assertEqual(
            {tool.name for tool in result.tools},
            {
                "tiny_ping",
                "tiny_engine_status",
                "tiny_scene_snapshot",
                "tiny_capture_frame",
                "tiny_engine_shutdown",
            },
        )
        self.assertTrue(unavailable.isError)
        self.assertIn("engine_unavailable", str(unavailable.content))


if __name__ == "__main__":
    unittest.main()
