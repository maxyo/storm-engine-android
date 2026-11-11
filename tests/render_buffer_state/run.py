"""Renderer buffer lock/restore regressions over production source blocks."""
import argparse
import os
from pathlib import Path
import subprocess
import sys

sys.dont_write_bytecode = True  # keep the source tree clean
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'glm_math'))
from glm_source import ENVIRONMENT, glm_source

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--glm-source', type=Path, help='Offline extracted pinned GLM source')
args = parser.parse_args()
# rain_batch.py compiles a block that uses the real GLM-based math headers.
os.environ[ENVIRONMENT] = str(glm_source(args.glm_source))

for script in ("rain_batch.py", "drawrects_restore.py", "buffer_handles.py"):
    subprocess.run([sys.executable, str(Path(__file__).with_name(script))], check=True)
