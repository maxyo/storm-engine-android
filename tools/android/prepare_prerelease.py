#!/usr/bin/env python3
"""Fail closed before publishing a game-port prerelease; probes cannot pass."""
import argparse
import hashlib
import json
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--directory', type=Path, required=True)
p.add_argument('--commit', required=True)
a = p.parse_args()
d = a.directory
manifest = json.loads((d / 'build-manifest.json').read_text())
if manifest.get('kind') != 'game-port' or manifest.get('game_port_ready') is not True:
    raise SystemExit('Refusing prerelease: this artifact is not a ready game-port build (integration probes stay CI artifacts).')
if manifest.get('commit') != a.commit:
    raise SystemExit('Artifact commit does not match the release commit')
if 'full engine ARM64 linked' not in manifest.get('verified', []):
    raise SystemExit('Full-engine link verification is required')
files = list(d.glob('*.apk'))
if len(files) != 1 or 'probe' in files[0].name.lower():
    raise SystemExit('Expected exactly one game-port APK, not a probe')
sha = hashlib.sha256(files[0].read_bytes()).hexdigest()
if sha != manifest.get('apk_sha256'):
    raise SystemExit('APK SHA256 does not match manifest')
if (d / 'SHA256SUMS').read_text().strip() != sha + '  ' + files[0].name:
    raise SystemExit('SHA256SUMS does not match APK')
notes = '# Android test prerelease\n\nCommit: `' + a.commit + '`\n\n'
notes += 'APK SHA256: `' + sha + '`\n\n'
notes += '## Build evidence and limitations\n\n```json\n' + json.dumps(manifest, indent=2) + '\n```\n\n'
notes += (d / 'TESTING.md').read_text()
(d / 'RELEASE_NOTES.md').write_text(notes)
print('Game-port artifact checks passed; release notes prepared')
