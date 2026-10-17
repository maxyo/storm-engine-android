#!/usr/bin/env python3
"""Run real production JNI under the JDK checker; no network or game data."""
import argparse
from pathlib import Path
import subprocess
import tempfile


def encode(value):
    if isinstance(value, int):
        return b"i" + str(value).encode() + b"e"
    if isinstance(value, dict):
        return b"d" + b"".join(encode(k) + encode(v) for k, v in sorted(value.items())) + b"e"
    if isinstance(value, list):
        return b"l" + b"".join(encode(v) for v in value) + b"e"
    return str(len(value)).encode() + b":" + value


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", type=Path, required=True)
    parser.add_argument("--java", required=True)
    parser.add_argument("--javac", required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="storm-torrent-jni-") as directory:
        work = Path(directory)
        torrent = work / "тест-😀.torrent"
        torrent.write_bytes(encode({b"info": {
            b"name": "Корсары 🏴‍☠️".encode(), b"piece length": 16384,
            b"pieces": b"0" * 20,
            b"files": [{b"length": 10, b"path": [b"setup.exe"]}],
        }}))
        malformed = work / "malformed.torrent"
        malformed.write_bytes(b"le")
        subprocess.run([args.javac, "-encoding", "UTF-8", "-d", str(work),
                        str(Path(__file__).with_name("NativeTorrents.java"))], check=True)
        result = subprocess.run([args.java, "-Xcheck:jni", "-cp", str(work),
                                 "org.stormengine.port.NativeTorrents", str(args.library.resolve()),
                                 str(torrent), str(malformed)], text=True, capture_output=True)
        print(result.stdout, end="")
        print(result.stderr, end="")
        if result.returncode or result.stderr or "WARNING" in result.stdout or "FATAL" in result.stdout:
            raise SystemExit("Production JNI failed the JVM checker")


if __name__ == "__main__":
    main()
