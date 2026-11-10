#!/usr/bin/env python3
"""Run the host engine on a game directory (Caribbean Legend or TEHO) with a hard timeout.

The game directory is used in place and read-only: it is the engine's working directory, and
everything the engine writes (logs, saves, options) goes to <out>/data. The run fails if any
file in the game directory changed. See README.md.
"""
import argparse
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--game', type=Path, required=True, help='game directory (engine.ini, Program, Resource)')
parser.add_argument('--engine', type=Path, required=True, help='host engine binary (build dir with engine-shaders/)')
parser.add_argument('--out', type=Path, required=True, help='writable directory for logs, saves and options')
parser.add_argument('--timeout', type=float, default=60, help='seconds before the engine is killed')
parser.add_argument('--display', help='X display to use; default: a private Xvfb')
args = parser.parse_args()
game, engine, out = args.game.resolve(), args.engine.resolve(), args.out.resolve()
if out.is_relative_to(game):
    sys.exit('--out must be outside the game directory')
data = out / 'data'
shutil.rmtree(data, ignore_errors=True)
data.mkdir(parents=True)
logs = data / 'Akella/Sea Dogs/Logs'
stamp = out / 'started'
stamp.write_text('')
time.sleep(0.01)  # mtimes after the stamp are changes made by this run

xvfb = None
display = args.display
if not display:
    display = ':' + str(5800 + os.getpid() % 100)
    xvfb = subprocess.Popen(['Xvfb', display, '-screen', '0', '1920x1080x24', '-nolisten', 'tcp'],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
environment = dict(os.environ, DISPLAY=display, XDG_DATA_HOME=str(data), SDL_AUDIODRIVER='dummy',
                   SDL_VIDEODRIVER='x11', LIBGL_ALWAYS_SOFTWARE='1')
started = time.monotonic()
with open(out / 'stdout.log', 'wb') as output:
    process = subprocess.Popen([str(engine)], cwd=game, env=environment, stdout=output, stderr=subprocess.STDOUT,
                               start_new_session=True)
    # A fatal engine error shows a message box that waits forever: stop at the [critical] line.
    reason = None
    while reason is None:
        code = process.poll()
        system = logs / 'system.log'
        if code is not None:
            reason = f'engine exited with code {code}' if code >= 0 else f'engine killed by signal {-code}'
        elif system.exists() and '] [critical] ' in system.read_text(errors='replace'):
            reason = 'fatal engine error (see system.log)'
        elif time.monotonic() - started > args.timeout:
            reason = f'timeout after {args.timeout:.0f} s'
        else:
            time.sleep(0.2)
    if process.poll() is None:
        # The frame the engine shows when it is stopped (local evidence only: never commit it).
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'x11grab', '-video_size', '1920x1080',
                        '-i', display, '-frames:v', '1', str(out / 'screen.png')])
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()
elapsed = time.monotonic() - started
if xvfb:
    xvfb.terminate()
    xvfb.wait()

changed = subprocess.run(['find', str(game), '-newer', str(stamp)], capture_output=True, text=True).stdout.split()
print(f'{reason}; {elapsed:.1f} s; logs in {logs}; last frame {out / "screen.png"}')
for name in ('system', 'error', 'compile'):
    path = logs / f'{name}.log'
    if path.exists():
        lines = path.read_text(errors='replace').splitlines()
        print(f'--- {name}.log: {len(lines)} lines, last 15:')
        print('\n'.join(lines[-15:]))
if changed:
    sys.exit('The game directory was modified: ' + ', '.join(changed[:20]))
