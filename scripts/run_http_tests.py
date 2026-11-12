#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (C) 2026 davoda1
"""Run the compiled core suite against the local, synthetic HTTP fixture."""
import argparse
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import threading


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("--public-defaults", action="store_true")
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    sys.dont_write_bytecode = True
    fixture = Path(__file__).resolve().parents[1] / "tests/http_fixture.py"
    spec = importlib.util.spec_from_file_location("cpv_http_fixture", fixture)
    if spec is None or spec.loader is None:
        raise RuntimeError("Cannot load the local HTTP fixture")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    server = module.Server(("127.0.0.1", 0), module.Handler)
    server.gui = False
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    command = [str(executable)]
    if args.public_defaults:
        command.append("--public-defaults")
    command += ["--http-fixture", f"http://127.0.0.1:{server.server_port}/"]
    environment = os.environ.copy()
    for key in ("NO_PROXY", "no_proxy"):
        previous = environment.get(key, "")
        environment[key] = previous + ("," if previous else "") + "127.0.0.1,localhost,::1"
    try:
        return subprocess.run(command, env=environment, timeout=50, check=False).returncode
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print("HTTP fixture could not complete: " + type(error).__name__, file=sys.stderr)
        raise SystemExit(1)
