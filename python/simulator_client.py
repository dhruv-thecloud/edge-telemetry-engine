"""Portable, bounded-wait client for the engine's line-oriented protocol."""
from __future__ import annotations
import json
import math
from pathlib import Path
import queue
import subprocess
import threading
from typing import Any


class EngineClient:
    """Own one engine subprocess. Requests must come from one calling thread."""

    def __init__(self, executable: str | Path, timeout: float = 5.0):
        if not math.isfinite(timeout) or timeout <= 0:
            raise ValueError("timeout must be positive and finite")
        self.timeout = timeout
        self._closed = False
        self._responses: queue.Queue[str | None] = queue.Queue()
        self._process = subprocess.Popen(
            [str(Path(executable).resolve())], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, text=True, encoding="utf-8", bufsize=1,
        )  # stderr is inherited so diagnostics cannot fill an unread pipe.
        self._reader = threading.Thread(target=self._read, daemon=True)
        self._reader.start()

    def _read(self) -> None:
        assert self._process.stdout is not None
        try:
            for line in self._process.stdout:
                self._responses.put(line)
        finally:
            self._responses.put(None)

    def request(self, command: str) -> dict[str, Any]:
        if self._closed:
            raise RuntimeError("engine client is closed")
        if "\n" in command or "\r" in command or len(command.encode("utf-8")) > 4096:
            raise ValueError("command must be a single line of at most 4096 bytes")
        assert self._process.stdin is not None
        try:
            self._process.stdin.write(command + "\n")
            self._process.stdin.flush()
            line = self._responses.get(timeout=self.timeout)
            if line is None:
                raise RuntimeError("engine exited before replying")
            response = json.loads(line)
            if not isinstance(response, dict) or "type" not in response:
                raise ValueError("invalid engine response")
        except queue.Empty as error:
            self.close()  # Never reuse a stream whose response ordering is uncertain.
            raise TimeoutError("engine response timed out") from error
        except (OSError, ValueError, RuntimeError):
            self.close()
            raise
        if response["type"] == "error":
            raise ValueError(response.get("message", "engine rejected command"))
        return response

    def poll(self) -> dict[str, Any]:
        return self.request("poll")

    def calibrate(self, sensor_id: int, offset: float) -> dict[str, Any]:
        if not 0 <= sensor_id <= 0xFFFFFFFF or not math.isfinite(offset):
            raise ValueError("invalid sensor id or offset")
        return self.request(f"calibrate {sensor_id} {offset:.17g}")

    def close(self) -> None:
        if self._closed:
            return
        self._closed = True
        process = self._process
        if process.poll() is None:
            try:
                assert process.stdin is not None
                process.stdin.write("quit\n")
                process.stdin.flush()
                process.wait(timeout=self.timeout)
            except (OSError, subprocess.TimeoutExpired):
                process.terminate()
                try:
                    process.wait(timeout=self.timeout)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        if process.stdin:
            try:
                process.stdin.close()
            except OSError:
                pass  # A child may have exited with buffered input still pending.
        self._reader.join(timeout=self.timeout)
        if process.stdout:
            process.stdout.close()

    def __enter__(self) -> EngineClient:
        return self

    def __exit__(self, *_: object) -> None:
        self.close()
