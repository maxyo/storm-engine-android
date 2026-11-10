#!/usr/bin/env python3
"""Check complete JSON after main-thread native allocation failures (synthetic metadata only)."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from run_jvm import encode

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--test', required=True)
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='storm-json-faults-') as temporary:
    root = Path(temporary)
    # Long paths exercise quote() growth; the coherent root exercises integer arrays and maps.
    prefix = [b'x' * 200, b'y' * 200, b'z' * 200]
    files = [{b'length': 1, b'path': prefix + [b'PROGRAM', str(i).encode()]} for i in range(16)]
    files += [{b'length': 1, b'path': prefix + [b'RESOURCE', b'a.tx']}]
    fixture = root / 'fixture.torrent'
    fixture.write_bytes(encode({b'info': {b'name': b'bundle', b'piece length': 16384,
        b'pieces': b'0' * 20, b'files': files}}))
    run = subprocess.run([args.test, str(fixture), str(root / 'staging')], text=True, capture_output=True, timeout=120)
    print(run.stderr, end='')
    if run.returncode:
        raise SystemExit(run.returncode)
    lines = run.stdout.splitlines()
    expected = json.loads(lines[0])
    assert len(lines) >= 2
    for line in lines[1:]:
        poll = json.loads(line)
        assert poll['metadata'] == expected, 'Partial or altered metadata was cached'
        assert poll['state'] in ('awaiting-selection', 'error')
    print(f'PASS {len(lines) - 1} complete poll documents and unchanged metadata cache')
