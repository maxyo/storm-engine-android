#!/usr/bin/env python3
"""storm_patched_view (cmake/StormBuildCache.cmake) applies a patch whose directory is the source root (".", the
RmlUi patches) in a build directory inside a Git work tree, and a patch that changes nothing is a configure error."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

here = Path(__file__).resolve().parent
root = here.parents[1]
patch = """diff --git a/src/a.txt b/src/a.txt
--- a/src/a.txt
+++ b/src/a.txt
@@ -1 +1 @@
-old
+new
"""
# Inside the repository's ignored build/ directory, like every real build directory: Git finds the work tree
# above it unless the patching stops it.
(root / 'build').mkdir(exist_ok=True)
with tempfile.TemporaryDirectory(prefix='patch-view-', dir=root / 'build') as directory:
    work = Path(directory)
    (work / 'pristine/src').mkdir(parents=True)
    (work / 'pristine/src/a.txt').write_text('old\n')
    (work / 'fix.patch').write_text(patch)

    def configure():
        return subprocess.run(['cmake', f'-DROOT={root}', f'-DWORK={work}', '-P', str(here / 'view.cmake')], cwd=work,
                              capture_output=True, text=True)

    result = configure()
    viewed = work / '_deps/lib-patched-0/src/a.txt'
    if result.returncode != 0 or not viewed.exists() or viewed.read_text() != 'new\n':
        sys.exit(f'the patch is not applied in the view (exit {result.returncode}): {result.stdout}{result.stderr}')
    # A patch Git skips or that leaves the file as it was must stop the configure.
    (work / 'pristine/src/a.txt').write_text('new\n')
    (work / 'fix.patch').write_text(patch.replace('-old\n+new', '-new\n+new'))
    shutil.rmtree(work / '_deps')
    result = configure()
    if result.returncode == 0 or 'applied nothing' not in result.stdout + result.stderr:
        sys.exit(f'a patch that changes nothing passed: {result.stdout}{result.stderr}')
print('patched view: applied at the source root, an empty patch refused')
