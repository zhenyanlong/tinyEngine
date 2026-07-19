"""Synchronous client for tinyEngine's newline-delimited JSON IPC protocol."""

from __future__ import annotations

import json
import os
import socket
import threading
from itertools import count
from typing import Any


class IpcError(RuntimeError):
    """An error returned by tinyEngine or raised by the IPC transport."""

    def __init__(self, code: str, message: str) -> None:
        super().__init__(f"{code}: {message}")
        self.code = code
        self.message = message


class IpcClient:
    """Thread-safe, persistent client for one tinyEngine IPC endpoint."""

    max_response_bytes = 16 * 1024 * 1024

    def __init__(self, host: str = "127.0.0.1", port: int = 9527,
                 timeout: float = 5.0) -> None:
        if not 1 <= port <= 65535:
            raise ValueError(f"port must be in [1, 65535], got {port}")
        if timeout <= 0:
            raise ValueError("timeout must be positive")
        self.host = host
        self.port = port
        self.timeout = timeout
        self._socket: socket.socket | None = None
        self._recv_buffer = bytearray()
        self._ids = count(1)
        self._lock = threading.Lock()

    @classmethod
    def from_environment(cls) -> "IpcClient":
        """Create a client from TINYENGINE_MCP_HOST/PORT/TIMEOUT."""

        host = os.environ.get("TINYENGINE_MCP_HOST", "127.0.0.1")
        port = int(os.environ.get("TINYENGINE_MCP_PORT", "9527"))
        timeout = float(os.environ.get("TINYENGINE_MCP_TIMEOUT", "5.0"))
        return cls(host=host, port=port, timeout=timeout)

    def connect(self) -> None:
        """Connect if no live socket is currently held."""

        if self._socket is not None:
            return
        try:
            sock = socket.create_connection((self.host, self.port), self.timeout)
            sock.settimeout(self.timeout)
        except OSError as error:
            raise IpcError(
                "engine_unavailable",
                f"cannot connect to tinyEngine at {self.host}:{self.port}: {error}",
            ) from error
        self._socket = sock
        self._recv_buffer.clear()

    def close(self) -> None:
        """Close the current socket; the next call reconnects lazily."""

        sock, self._socket = self._socket, None
        self._recv_buffer.clear()
        if sock is not None:
            try:
                sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            sock.close()

    def call(self, method: str, params: dict[str, Any] | None = None,
             timeout: float | None = None) -> Any:
        """Call one engine method and return its result or raise IpcError."""

        if not method:
            raise ValueError("method must be non-empty")
        if params is not None and not isinstance(params, dict):
            raise TypeError("params must be a dict or None")

        with self._lock:
            self.connect()
            assert self._socket is not None
            request_id = next(self._ids)
            request = {
                "id": request_id,
                "method": method,
                "params": params or {},
            }
            payload = (json.dumps(request, separators=(",", ":")) + "\n").encode("utf-8")
            previous_timeout = self._socket.gettimeout()
            self._socket.settimeout(timeout if timeout is not None else self.timeout)

            try:
                self._socket.sendall(payload)
                response = self._read_response()
            except IpcError:
                self.close()
                raise
            except socket.timeout as error:
                self.close()
                raise IpcError("ipc_timeout", f"{method} timed out") from error
            except (OSError, EOFError, UnicodeError, json.JSONDecodeError) as error:
                self.close()
                raise IpcError("ipc_transport", f"{method} failed: {error}") from error
            finally:
                if self._socket is not None:
                    self._socket.settimeout(previous_timeout)

            if not isinstance(response, dict):
                raise IpcError("invalid_response", "response must be a JSON object")
            if response.get("id") != request_id:
                self.close()
                raise IpcError(
                    "invalid_response",
                    f"expected response id {request_id}, got {response.get('id')!r}",
                )

            error = response.get("error")
            if error is not None:
                if not isinstance(error, dict):
                    raise IpcError("invalid_response", "error must be an object or null")
                raise IpcError(
                    str(error.get("code", "engine_error")),
                    str(error.get("message", "tinyEngine command failed")),
                )
            return response.get("result")

    def _read_response(self) -> Any:
        assert self._socket is not None
        while True:
            newline = self._recv_buffer.find(b"\n")
            if newline >= 0:
                line = bytes(self._recv_buffer[:newline])
                del self._recv_buffer[:newline + 1]
                if not line:
                    continue
                return json.loads(line.decode("utf-8"))

            chunk = self._socket.recv(4096)
            if not chunk:
                raise EOFError("engine closed the connection")
            self._recv_buffer.extend(chunk)
            if len(self._recv_buffer) > self.max_response_bytes:
                raise IpcError("response_too_large", "response exceeds 16 MiB")

    def __enter__(self) -> "IpcClient":
        self.connect()
        return self

    def __exit__(self, *_: object) -> None:
        self.close()
