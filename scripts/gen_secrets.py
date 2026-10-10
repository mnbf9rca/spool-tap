#!/usr/bin/env python3
"""Generate the ignored build-time credential header."""

import os
from pathlib import Path
import sys
import tempfile


def write_header(path, values):
    keys = ("WIFI_SSID", "WIFI_PASSWORD", "OTA_PASSWORD")
    if any(not values.get(key) or "\0" in values[key] for key in keys):
        raise ValueError("Missing or invalid build-time secret")
    path = Path(path)
    path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    if path.parent.is_symlink():
        raise ValueError("Secret directory must not be a symlink")
    path.parent.chmod(0o700)
    lines = ["#pragma once", "", "namespace BuildSecrets {"]
    for key in keys:
        name = key.lower()
        literal = '"' + "".join(f"\\x{byte:02X}" for byte in values[key].encode("utf-8")) + '"'
        lines.append(f"constexpr char {name}[] = {literal};")
    lines += ["}  // namespace BuildSecrets", ""]
    fd, temporary = tempfile.mkstemp(prefix=".build_secrets.", dir=path.parent)
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "w", encoding="ascii") as stream:
            stream.write("\n".join(lines))
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


if __name__ == "__main__":
    try:
        write_header(Path(__file__).resolve().parents[1] / ".pio-local" / "build_secrets.hpp", os.environ)
    except ValueError as error:
        print(error, file=sys.stderr)
        sys.exit(1)
