from pathlib import Path
import subprocess
import sys

for script in ("rain_batch.py", "drawrects_restore.py"):
    subprocess.run([sys.executable, str(Path(__file__).with_name(script))], check=True)
