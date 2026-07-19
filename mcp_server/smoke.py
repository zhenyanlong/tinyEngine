"""Command-line smoke test for a running tinyEngine MCP IPC endpoint."""

from __future__ import annotations

import argparse
import json
import time
from typing import Any

from .ipc_client import IpcClient, IpcError


def run_smoke(host: str, port: int, timeout: float, shutdown: bool) -> dict[str, Any]:
    """Connect with retry, call every MVP read endpoint, and optionally shut down."""

    client = IpcClient(host=host, port=port, timeout=min(timeout, 5.0))
    deadline = time.monotonic() + timeout
    last_error: IpcError | None = None
    while time.monotonic() < deadline:
        try:
            ping = client.call("ping")
            break
        except IpcError as error:
            last_error = error
            client.close()
            time.sleep(0.1)
    else:
        message = str(last_error) if last_error else "connection timed out"
        raise IpcError("smoke_timeout", message)

    try:
        result: dict[str, Any] = {
            "ping": ping,
            "status": client.call("engine.status"),
            "snapshot": client.call("scene.snapshot"),
        }
        if shutdown:
            result["shutdown"] = client.call("engine.shutdown")
        return result
    finally:
        client.close()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9527)
    parser.add_argument("--timeout", type=float, default=20.0)
    parser.add_argument("--shutdown", action="store_true")
    args = parser.parse_args()

    try:
        result = run_smoke(args.host, args.port, args.timeout, args.shutdown)
    except (IpcError, ValueError) as error:
        parser.exit(1, f"[MCP Smoke] FAIL: {error}\n")

    print("[MCP Smoke] PASS")
    print(json.dumps(result, indent=2, ensure_ascii=False))


if __name__ == "__main__":
    main()
