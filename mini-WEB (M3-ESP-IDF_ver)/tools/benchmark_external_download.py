"""Measure actual public HTTP file downloads. MB/s uses decimal bytes; no firmware build."""
from __future__ import annotations

import argparse
import hashlib
import json
import random
import time
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import urlsplit, urlunsplit
from urllib.request import Request, urlopen


def download(url: str, timeout: float = 120, max_bytes: int = 128 * 1024 * 1024,
             expected_sha256: str | None = None) -> dict:
    parts = urlsplit(url)
    if parts.scheme not in ("http", "https") or not parts.hostname or parts.username or parts.password:
        raise ValueError("Use a public HTTP(S) URL without embedded credentials")
    # Request identity so compressed byte counts do not inflate the result.
    request = Request(url, headers={"Accept-Encoding": "identity", "Cache-Control": "no-cache",
                                    "User-Agent": "M3-download-benchmark/1"})
    digest, count = hashlib.sha256(), 0
    started = time.perf_counter()
    with urlopen(request, timeout=timeout) as response:
        if response.status != 200:
            raise ValueError(f"Expected complete file HTTP 200, got {response.status}")
        if response.headers.get("Content-Encoding", "identity").lower() not in ("", "identity"):
            raise ValueError("Compressed response: choose an uncompressed binary test file")
        first = response.read(1)
        ttfb = time.perf_counter() - started
        if first:
            digest.update(first)
            count = 1
        while True:
            if time.perf_counter() - started > timeout:
                raise TimeoutError("Overall download deadline exceeded")
            data = response.read(65536)
            if not data:
                break
            count += len(data)
            if count > max_bytes:
                raise ValueError("Download exceeded the configured byte limit")
            digest.update(data)
        elapsed = time.perf_counter() - started
        advertised = response.headers.get("Content-Length")
        if not count or (advertised is not None and count != int(advertised)):
            raise ValueError("Empty or incomplete response")
        sha256 = digest.hexdigest()
        if expected_sha256 and sha256 != expected_sha256.lower():
            raise ValueError("SHA-256 mismatch")
        return {"bytes": count, "seconds": elapsed, "ttfb_seconds": ttfb,
                "MBps": count / elapsed / 1_000_000,
                "body_MBps": count / max(elapsed - ttfb, 1e-9) / 1_000_000,
                "sha256": sha256, "transport": response.headers.get("X-Relay-Transport", "unknown"),
                "cloudflare_cache": response.headers.get("CF-Cache-Status", "unknown"),
                "cache_control": response.headers.get("Cache-Control", "unknown")}


def make_fixture(path: Path, size_mib: int) -> dict:
    if not 1 <= size_mib <= 64:
        raise ValueError("Fixture must be 1..64 MiB")
    rng, digest = random.Random(20261016), hashlib.sha256()
    # Exclusive creation preserves any existing file. Data contains no user information.
    with path.open("xb") as output:
        for _ in range(size_mib):
            data = rng.randbytes(1024 * 1024)
            output.write(data)
            digest.update(data)
    return {"path": str(path.resolve()), "bytes": size_mib * 1024 * 1024, "sha256": digest.hexdigest()}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--url", help="Public file URL; use a .txt fixture for the existing public path policy")
    parser.add_argument("--repeat", type=int, default=5)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--expected-sha256")
    parser.add_argument("--target-MBps", type=float, default=5)
    parser.add_argument("--output", type=Path, help="New JSON report path (existing files are preserved)")
    parser.add_argument("--make-fixture", type=Path)
    parser.add_argument("--size-mib", type=int, default=64)
    args = parser.parse_args()
    if args.make_fixture:
        print(json.dumps(make_fixture(args.make_fixture, args.size_mib), ensure_ascii=False, indent=2))
        return 0
    if not args.url or not 1 <= args.repeat <= 100 or args.timeout <= 0:
        parser.error("Provide --url, repeat 1..100 and a positive timeout")
    parts = urlsplit(args.url)
    report = {"url": urlunsplit((parts.scheme, parts.hostname or "", parts.path, "", "")),
              "created_utc": datetime.now(timezone.utc).isoformat(), "target_MBps": args.target_MBps,
              "evidence": "real_http_download_from_this_computer", "runs": []}
    for number in range(1, args.repeat + 1):
        try:
            result = download(args.url, args.timeout, expected_sha256=args.expected_sha256)
            result["ok"] = result["MBps"] >= args.target_MBps
        except Exception as error:
            # URLs, query strings, credentials and response bodies never enter error reports.
            result = {"ok": False, "error_type": type(error).__name__}
        report["runs"].append(result)
        print(json.dumps({"run": number, **result}, ensure_ascii=False), flush=True)
    hashes = {run["sha256"] for run in report["runs"] if "sha256" in run}
    report["passed"] = all(run["ok"] for run in report["runs"]) and len(hashes) == 1
    if args.output:
        with args.output.open("x", encoding="utf-8") as output:
            json.dump(report, output, ensure_ascii=False, indent=2)
            output.write("\n")
    print("PASS" if report["passed"] else "TARGET NOT MET")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
