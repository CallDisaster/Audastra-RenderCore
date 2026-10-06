# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 CallDisaster
"""纯CPU真实夹具门禁；只读取作者显式提供的本地副本，不提取/执行/分发地图。"""
from __future__ import annotations

import hashlib
import os
from pathlib import Path
import subprocess
import sys
import time

IDENTITIES = (
    ("FORGE_SOURCE_ORDER_MODEL", 293672,
     "2cb39727b36e830151ee0e23099fce2fb7a1feb71ec8f5025ddb104607fca696"),
    ("FORGE_SOURCE_ORDER_OWNED_MAP", 341259558,
     "172e3b1b170b6d4d4665efa2a99cfcab3ca41f598e990b17664448a6a3c8880a"),
)


def verified(path: Path, size: int, expected: str) -> tuple[int, int, int, int]:
    before = path.stat()
    if before.st_size != size:
        raise RuntimeError(f"{path.name}: source size mismatch")
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    after = path.stat()
    identity = lambda s: (s.st_dev, s.st_ino, s.st_size, s.st_mtime_ns)
    if identity(before) != identity(after) or digest.hexdigest() != expected:
        raise RuntimeError(f"{path.name}: SHA/source changed during read")
    return identity(after)


def main() -> int:
    if len(sys.argv) != 2:
        raise RuntimeError("expected CPU test executable")
    if any(not os.environ.get(name) for name, _, _ in IDENTITIES):
        print("SKIP: set FORGE_SOURCE_ORDER_MODEL and FORGE_SOURCE_ORDER_OWNED_MAP to owned local copies")
        return 77
    started = time.monotonic()
    paths = [Path(os.environ[name]).resolve(strict=True) for name, _, _ in IDENTITIES]
    before = [verified(path, size, sha) for path, (_, size, sha) in zip(paths, IDENTITIES)]
    process = subprocess.run([sys.argv[1], "real-model"], check=False, timeout=90,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    after = [verified(path, size, sha) for path, (_, size, sha) in zip(paths, IDENTITIES)]
    if before != after:
        raise RuntimeError("bound source identity changed during CPU test")
    # MSVC本地控制台可能采用CP936；中文仅为诊断，编码失败不等于模型失败。
    for data, stream in ((process.stdout, sys.stdout), (process.stderr, sys.stderr)):
        try:
            text = data.decode("utf-8", errors="strict")
        except UnicodeDecodeError:
            text = data.decode("gb18030", errors="strict")
        stream.write(text)
    print(f"SHA_BOUND_CPU: model={IDENTITIES[0][2]} map={IDENTITIES[1][2]} "
          f"elapsed={time.monotonic() - started:.3f}s; no game/runtime equivalence claim")
    return process.returncode


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, RuntimeError, subprocess.TimeoutExpired, UnicodeError) as error:
        print(f"REAL_FIXTURE_FAILED: {error}", file=sys.stderr)
        sys.exit(1)
