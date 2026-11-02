#!/usr/bin/env python3
"""Atomically send one runtime probe command and print its status acknowledgment."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import sys
import time

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--command", type=Path, default=Path("/tmp/vcmm-probe.command"))
    parser.add_argument("--timeout", type=float, default=10)
    parser.add_argument("words", nargs="+", help="e.g. status; window status; window native 0; camera restore")
    args = parser.parse_args()
    command = args.command.resolve()
    if not command.parent.is_dir():
        parser.error("command parent directory must already exist")
    request = time.monotonic_ns()
    text = f"{request} {' '.join(args.words)}\n"
    if len(text.encode()) > 1024 or any("\n" in word or "\0" in word for word in args.words):
        parser.error("command too long or contains invalid characters")
    # Refuse to overwrite an unconsumed request. Use one controller at a time.
    if command.exists():
        parser.error(f"pending command already exists: {command}")
    temporary = command.with_name(command.name + f".writer.{os.getpid()}")
    with temporary.open("x") as stream:
        os.chmod(temporary, 0o600)
        stream.write(text)
    os.replace(temporary, command)
    status = Path(str(command) + ".status.json")
    deadline = time.monotonic() + args.timeout
    while time.monotonic() < deadline:
        try:
            value = json.loads(status.read_text())
            if value.get("last_id") == request:
                print(json.dumps(value, indent=2))
                return
        except (OSError, json.JSONDecodeError):
            pass
        time.sleep(0.05)
    print(f"No acknowledgment within {args.timeout:g}s. Pending path: {command}; status: {status}", file=sys.stderr)
    raise SystemExit(1)

if __name__ == "__main__":
    main()
