#!/usr/bin/env python3
"""Load every scene of the game in parallel headless engines and report what broke.

An on-demand tool for engine and renderer work; not part of the routine checks
(see README.md). Needs the player's game files, a Linux host engine built with
-DSTORM_SCENE_SWEEP=ON, Xvfb and Pillow.

  python3 tools/scene_sweep/run.py --game <game dir> --engine build/host/engine --out <report dir>

  --jobs N            parallel engines (default: from free cores and memory)
  --only PATTERN ...  scenes by shell pattern on "kind:name" or by category, e.g. 'loc:*_town' tavern map:world
  --failed            only the scenes that failed in the previous report of --out
  --changed           only scenes whose scripts or models changed since that report, plus its failures
  --baseline FILE     an earlier report.json: exit non-zero only for scenes that pass there and fail now
  --smoke             one scene of every category (a quick check after an engine change)
  --fail-fast         stop at the first failing scene
  --list              print the scene inventory and exit
"""
import argparse
import collections
import datetime
import fnmatch
import hashlib
import json
import os
from pathlib import Path
import re
import resource
import shutil
import signal
import stat
import struct
import subprocess
import sys
import threading
import time

sys.dont_write_bytecode = True  # no __pycache__ in the source tree
sys.path.insert(0, str(Path(__file__).resolve().parent))
import inventory  # noqa: E402

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit('Pillow is required: python3 -m pip install Pillow')

HERE = Path(__file__).resolve().parent
REPOSITORY = HERE.parents[1]

# Log lines that fail the scene they were written in. (log file pattern, line pattern, reason)
LOG_RULES = [
    (r'error\.log', r'.', 'script error'),
    (r'.', r"Can't load texture .*[^\\/\s]\.tx", 'missing texture'),  # the game also asks for textures with no name
    (r'.', r"Invalid model|Can't load model|can't open file|Can't find model|No geometry", 'missing model'),
    (r'compile\.log', r'Main character <.*> error teleportation', 'teleport failed'),  # characters_login.c
    (r'.', r'\[(critical|error)\]', 'engine error'),
    (r'stdout', r'(?i)\[storm-bgfx\].*(error|fail|trap|unsupported|missing|invalid)', 'renderer error'),
]
FATAL = re.compile(r'Engine stopped: (.*)')
# Defects of the game's own data the engine reports and cannot fix (known_defects.json): reported as KNOWN.
KNOWN_DEFECTS = json.loads((HERE / 'known_defects.json').read_text(encoding='utf-8'))['defects']


def classify(scene, reasons):
    """Splits a scene's reasons into (failing, known): a reason is known when an entry for this scene matches it."""
    failing, known = [], []
    for reason in reasons:
        match = next((defect for defect in KNOWN_DEFECTS
                      if defect['scene'] == scene and defect['error'] in reason), None)
        (known if match else failing).append(f"{reason} [known: {match['file']}]" if match else reason)
    return failing, known


def status_of(scene, reasons):
    failing, known = classify(scene, reasons)
    return 'fail' if failing else 'known' if known else 'pass'


def parse_arguments():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--game', type=Path, required=True, help='game directory (PROGRAM, RESOURCE, engine.ini)')
    parser.add_argument('--engine', type=Path, help='host engine binary built with -DSTORM_SCENE_SWEEP=ON')
    parser.add_argument('--out', type=Path, help='report directory (report.json, summary.txt, shots/, contact/)')
    parser.add_argument('--jobs', type=int)
    parser.add_argument('--only', nargs='+', default=[])
    parser.add_argument('--failed', action='store_true')
    parser.add_argument('--changed', action='store_true')
    parser.add_argument('--baseline', type=Path)
    parser.add_argument('--smoke', action='store_true')
    parser.add_argument('--fail-fast', action='store_true')
    parser.add_argument('--list', action='store_true')
    parser.add_argument('--size', default='1280x720', help='window size (default 1280x720)')
    parser.add_argument('--timeout', type=int, default=240, help='seconds one scene may take before the engine is killed')
    parser.add_argument('--gl-threads', type=int, default=2, help='software rasteriser threads per engine')
    parser.add_argument('--variants', default='',
                        help="graphics option sets separated by ';' (e.g. 'fx_preset=0;fx_preset=2,fx_bloom=0'): every "
                             "game scene is captured once per set (shots/<scene>@v<i>.jpg, report 'variants')")
    parser.add_argument('--settle', type=int, default=300,
                        help='ms each scene renders after its fader before the capture; frame time and the renderer '
                             'counters are means over this window (raise it for performance measurements)')
    parser.add_argument('--techniques', type=Path, default=REPOSITORY / 'src/techniques',
                        help="the engine's techniques, laid over the game's as the APK build does")
    parser.add_argument('--keep-work', action='store_true', help='keep the per-engine game copies and logs')
    arguments = parser.parse_args()
    if not arguments.list and not (arguments.engine and arguments.out):
        parser.error('--engine and --out are required')
    return arguments


def default_jobs():
    """Half of the cores nobody is using, at least a quarter of all cores, within free memory."""
    cores = os.cpu_count()
    idle = cores - os.getloadavg()[0]
    available = next(int(line.split()[1]) for line in open('/proc/meminfo') if line.startswith('MemAvailable')) // 1024
    return max(1, min(int(max(cores / 4, idle / 2)), available // 2048))


def file_digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def game_snapshot(game):
    """Inode, size, modification time and mode of every file of the player's game. The engines run on hard-linked
    copies of it: a write or a chmod through a link would change the player's file and every other sweep's copy."""
    snapshot = {}
    pending = [game]
    while pending:
        with os.scandir(pending.pop()) as entries:
            for entry in entries:
                if entry.is_dir(follow_symlinks=False):
                    pending.append(entry.path)
                else:
                    status = entry.stat(follow_symlinks=False)
                    snapshot[entry.path] = (status.st_ino, status.st_size, status.st_mtime_ns, status.st_mode)
    return snapshot


class LinkError(Exception):
    """The sweep was about to write or delete through a symbolic link, or outside its own copy."""


def refuse_links(root):
    """The game copy must hold no symbolic link at all: cp -al copies a link as a link, and anything the sweep
    deletes or writes through one (RESOURCE/techniques, save, engine.ini) lands in the player's game or another
    shared folder. Fails loudly with every link found."""
    links = [str(root)] if os.path.islink(root) else []
    for directory, directories, files in os.walk(root):
        for name in directories + files:
            if os.path.islink(os.path.join(directory, name)):
                links.append(os.path.join(directory, name))
    if links:
        raise LinkError(f'the game copy {root} holds symbolic links (copy the real folder, not a link to it): '
                        + ', '.join(links[:10]) + (f' and {len(links) - 10} more' if len(links) > 10 else ''))


def inside(root, path):
    """`path` itself and every parent up to `root` are no symbolic links and stay inside `root`; returns `path`.
    Every path the sweep deletes, unlinks or writes in its copy goes through this check."""
    root = Path(os.path.realpath(root))
    path = Path(path)
    current = path
    while True:
        if current.is_symlink():
            raise LinkError(f'refusing to change {path}: {current} is a symbolic link')
        if current == root or current.parent == current:
            break
        current = current.parent
    if Path(os.path.realpath(path)) != root and root not in Path(os.path.realpath(path)).parents:
        raise LinkError(f'refusing to change {path}: it resolves outside {root}')
    return path


def writable_directories(root):
    """cp -a keeps the source's directory modes (a read-only game folder gives a read-only copy). The copy's
    directories are its own inodes, so they are made writable; its files are hard links shared with the
    source and keep their modes (a chmod of one would change the player's file)."""
    for directory, _, _ in os.walk(root):
        make_directory_writable(Path(directory))


def make_directory_writable(path):
    """The sweep's only chmod: a directory of its own copy. A regular file is refused whatever its link count
    (a copy's file is a hard link of the player's file, a chmod changes both)."""
    status = os.lstat(path)
    if not stat.S_ISDIR(status.st_mode):
        raise LinkError(f'refusing to chmod {path}: not a directory (files of the copy keep their modes)')
    if not status.st_mode & 0o200:
        os.chmod(path, status.st_mode | 0o700)


def own_file(root, path):
    """A file of the copy the sweep is about to write: no link on the way, inside the copy, and unlinked first so
    the write creates a new inode. A file still there after the unlink, or a new one with other names (nlink > 1),
    is refused: writing it would change the player's file."""
    path = inside(root, path)
    path.unlink(missing_ok=True)
    if path.exists():
        raise LinkError(f'refusing to write {path}: it is still there after the unlink')
    return path


def check_own_file(path):
    """After the sweep wrote `path`: it must be the copy's own inode."""
    if os.lstat(path).st_nlink != 1:
        raise LinkError(f'{path} was written but is shared with another folder (nlink {os.lstat(path).st_nlink})')


def check_game_unchanged(game, snapshot, when):
    now = game_snapshot(game)
    changed = sorted(set(now) ^ set(snapshot) | {path for path in now.keys() & snapshot.keys() if now[path] != snapshot[path]})
    if changed:
        raise RuntimeError(f'the game directory {game} changed {when} ({len(changed)} files: '
                           f'{", ".join(changed[:5])}): a write through the hard link of a copy, or another program')


def select(scenes, arguments, previous, build):
    chosen = scenes
    if arguments.only:
        chosen = [scene for scene in chosen if any(
            fnmatch.fnmatchcase(scene.id, pattern) or scene.category == pattern for pattern in arguments.only)]
    if arguments.smoke:
        chosen = inventory.first_of_each_category(chosen)
    if arguments.failed or arguments.changed:
        if previous is None:
            sys.exit(f'--failed and --changed need the previous report in {arguments.out}/report.json')
        old = previous['scenes']
        failed = {name for name, result in old.items() if result['status'] not in ('pass', 'known')}
        if arguments.failed:
            chosen = [scene for scene in chosen if scene.id in failed]
        elif previous['build'] != build:
            print('engine or sweep script changed since the previous report: every scene counts as changed')
        else:
            chosen = [scene for scene in chosen if scene.id in failed or scene.id not in old or
                      old[scene.id]['fingerprint'] != scene.fingerprint]
    return chosen


def make_shards(scenes, jobs):
    """Round-robin, with the scenes that are entered from land spread between land scenes."""
    land = [scene for scene in scenes if scene.kind in inventory.LAND_KINDS]
    other = [scene for scene in scenes if scene.kind not in inventory.LAND_KINDS]
    shards = []
    for index in range(jobs):
        mine, extra = land[index::jobs], other[index::jobs]
        for position, scene in enumerate(extra):
            mine.insert((position + 1) * len(mine) // (len(extra) + 1) + position, scene)
        if mine:
            shards.append(mine)
    return shards


class LogTail:
    """New complete lines of the engine's logs since the previous call."""

    def __init__(self, stdout, logs):
        self.stdout, self.logs, self.offsets = stdout, logs, {}

    def collect(self):
        lines = []
        for path in [self.stdout, *sorted(self.logs.glob('*.log'))] if self.logs.is_dir() else [self.stdout]:
            with open(path, 'rb') as stream:
                stream.seek(self.offsets.get(path, 0))
                data = stream.read()
            data = data[:data.rfind(b'\n') + 1]
            self.offsets[path] = self.offsets.get(path, 0) + len(data)
            name = 'stdout' if path == self.stdout else path.name
            lines += [(name, line) for line in data.decode('utf-8', 'replace').splitlines()]
        return lines


def log_reasons(lines):
    reasons = collections.Counter()
    for name, line in lines:
        for files, pattern, reason in LOG_RULES:
            if re.search(files, name) and re.search(pattern, line):
                text = re.sub(r'^\[[^\]]*\] \[\w+\] \[\w+\] ', '', line).strip()
                reasons[f'{reason}: {text[:200]}'] += 1
                break
    return [text if count == 1 else f'{text} (x{count})' for text, count in reasons.items()]


def read_frame(path, size):
    """The Xvfb framebuffer (an XWD file) as an image."""
    width, height = size
    for _ in range(50):
        data = path.read_bytes()
        header = struct.unpack('>25I', data[:100])
        offset, line = header[0] + header[19] * 12, header[12]
        if header[4] == width and header[5] == height and header[11] == 32 and len(data) >= offset + line * height:
            return Image.frombuffer('RGB', size, data[offset:offset + line * height], 'raw', 'BGRX', line, 1).copy()
        time.sleep(0.05)
    raise RuntimeError(f'{path} is not a {width}x{height} 32-bit framebuffer')


def frame_reasons(image, previous_digest):
    """Detects frames that cannot be a rendered scene. Returns (reasons, digest)."""
    small = image.convert('L').resize((160, 90))
    digest = hashlib.sha1(small.tobytes()).hexdigest()
    # The HUD is drawn even when the scene is not: judge the middle of the frame.
    middle = small.crop((16, 18, 144, 72))
    low, high = middle.getextrema()
    histogram = middle.histogram()
    reasons = []
    if sum(histogram[:13]) >= 0.99 * sum(histogram):
        reasons.append('black frame')
    elif high - low <= 8:
        reasons.append('blank frame (a single colour)')
    if digest == previous_digest:
        reasons.append('frame identical to the previous scene (nothing new was drawn)')
    return reasons, digest


def display_in_use(number):
    if Path(f'/tmp/.X11-unix/X{number}').exists() or Path(f'/tmp/.X{number}-lock').exists():
        return True
    return f'@/tmp/.X11-unix/X{number}\n' in Path('/proc/net/unix').read_text()


def start_display(directory, size):
    """A private X server whose screen is a file in `directory`, its messages in xvfb.log.
    Called from one thread: servers started at the same moment with -displayfd alone can
    end up sharing a number with one that is shutting down."""
    for number in range(1200, 1400):
        if display_in_use(number):
            continue
        # The server writes its number to -displayfd again on later server generations and
        # exits when nobody can read it: the read end stays open as long as the server runs.
        read_end, write_end = os.pipe()
        with open(directory / 'xvfb.log', 'ab') as log:
            server = subprocess.Popen(
                ['Xvfb', f':{number}', '-displayfd', str(write_end), '-screen', '0', f'{size[0]}x{size[1]}x24',
                 '-fbdir', str(directory), '-nolisten', 'tcp'],
                pass_fds=[write_end], stdout=log, stderr=subprocess.STDOUT)
        os.close(write_end)
        ready = os.read(read_end, 32)  # empty when the server exits instead of starting
        if ready.strip():
            server.displayfd = read_end
            return number, server
        os.close(read_end)
        server.wait()
    raise RuntimeError('no free X display between :1200 and :1399')


class Shard(threading.Thread):
    def __init__(self, index, scenes, arguments, context):
        super().__init__(name=f'shard-{index}')
        self.index, self.arguments, self.context = index, arguments, context
        self.queue = [scene.id for scene in scenes]
        self.work = arguments.out / 'work' / f'shard-{index:02d}'
        self.size = tuple(int(part) for part in arguments.size.split('x'))
        self.results = {}
        self.work.mkdir(parents=True)
        self.display, self.display_server = start_display(self.work, self.size)
        self.boot_failures = 0
        self.runs = 0

    # --- results -------------------------------------------------------------------------
    def record(self, scene, reasons, **metrics):
        entry = self.results.setdefault(scene, {'reasons': [], 'shard': self.index})
        entry['reasons'] += [reason for reason in reasons if reason not in entry['reasons']]
        entry.update({key: value for key, value in metrics.items() if value is not None})
        entry['status'] = status_of(scene, entry['reasons'])
        if entry['status'] == 'fail':
            self.context['failed'].set()
        self.context['progress'](scene, entry)

    # --- one engine process --------------------------------------------------------------
    def prepare(self, run):
        game = run / 'game'
        # Hard links: no extra disk space, and the engine's resource index does not follow symlinks.
        subprocess.run(['cp', '-al', str(self.arguments.game), str(game)], check=True)
        refuse_links(game)
        writable_directories(game)
        source = self.arguments.game

        def own(relative):
            """A file of the copy the sweep or the game writes: unlinked first, so the write lands in the copy."""
            return own_file(game, game / relative)

        for name in ('engine.ini', 'options'):  # the game rewrites both
            own(name)
        # The player's profiles: a profile named like the sweep's new game stops it at "the profile exists" (and the
        # game would write into the player's hard-linked saves).
        shutil.rmtree(inside(game, inventory.find_case_insensitive(game, 'save') or game / 'save'), ignore_errors=True)
        if (source / 'options').exists():
            shutil.copy(source / 'options', own('options'))
        settings = inventory.read_script(source / 'engine.ini')
        for key, value in (('full_screen', 0), ('screen_x', self.size[0]), ('screen_y', self.size[1]),
                           ('vsync', 0), ('show_fps', 0)):
            settings, count = re.subn(rf'(?mi)^{key}\s*=.*$', f'{key} = {value}', settings)
            if not count:
                settings = f'{key} = {value}\n' + settings
        own('engine.ini').write_text(settings)
        techniques = inside(game, inventory.find_case_insensitive(game, 'RESOURCE/techniques'))
        shutil.rmtree(techniques)
        shutil.copytree(self.arguments.techniques, techniques)
        shutil.copy(HERE / 'scene_sweep.c', own('PROGRAM/scene_sweep.c'))
        os.link(self.context['engine'], own('engine'))  # resources are looked up next to the executable
        for written in ('engine.ini', 'options', 'PROGRAM/scene_sweep.c'):
            if (game / written).exists():
                check_own_file(game / written)
        control = run / 'control'
        control.mkdir()
        (control / 'scenes.txt').write_text(''.join(scene + '\n' for scene in self.queue))
        return game, control

    def run_engine(self):
        """Runs one engine over the queue. Returns when it exits or is killed."""
        self.runs += 1
        run = self.work / f'run-{self.runs}'
        run.mkdir()
        game, control = self.prepare(run)
        environment = dict(os.environ, DISPLAY=f':{self.display}', XDG_DATA_HOME=str(run / 'data'),
                           SDL_VIDEODRIVER='x11', SDL_AUDIODRIVER='dummy', LIBGL_ALWAYS_SOFTWARE='1',
                           LP_NUM_THREADS=str(self.arguments.gl_threads), STORM_SCENE_SWEEP_DIR=str(control),
                           STORM_SCENE_SWEEP_SETTLE_MS=str(self.arguments.settle),
                           STORM_SCENE_SWEEP_VARIANTS=self.arguments.variants)
        stdout = run / 'stdout.log'
        with open(stdout, 'wb') as output:
            engine = subprocess.Popen(
                [str(game / 'engine')], cwd=game, env=environment, stdout=output, stderr=subprocess.STDOUT,
                start_new_session=True, preexec_fn=lambda: resource.setrlimit(resource.RLIMIT_CORE, (0, 0)))
        try:
            self.watch(engine, run, control, stdout)
        finally:
            if engine.poll() is None:
                os.killpg(engine.pid, signal.SIGKILL)
            engine.wait()
            if not self.arguments.keep_work:
                shutil.rmtree(inside(run, game))
        check_game_unchanged(self.arguments.game, self.context['game'], f'during engine run {run}')

    def watch(self, engine, run, control, stdout):
        events = control / 'events.log'
        tail = LogTail(stdout, run / 'data/Akella/Sea Dogs/Logs')
        position = 0
        current = None          # scene between its "begin" and "end"/"fail"
        begun = loaded = time.monotonic()
        booted = False
        finished = False
        pending = []            # log lines not yet attributed to a scene
        previous_frame = None
        metrics = {}
        fatal_checked = 0

        def close(scene, reasons):
            nonlocal current, pending
            reasons = reasons + metrics.pop('capture_reasons', []) + log_reasons(pending)
            self.record(scene, reasons, seconds=round(time.monotonic() - begun, 2), **metrics)
            pending = []
            current = None
            if scene in self.queue:
                self.queue.remove(scene)

        while True:
            exited = engine.poll() is not None
            data = events.read_bytes()[position:] if events.exists() else b''
            data = data[:data.rfind(b'\n') + 1]
            position += len(data)
            for line in data.decode().splitlines():
                stamp, kind, *rest = line.split(' ', 2)
                rest = rest[0] if rest else ''
                if kind == 'begin':
                    current, begun, metrics = rest, time.monotonic(), {}
                elif kind == 'loaded':
                    loaded = time.monotonic()
                    metrics['load_ms'] = round((loaded - begun) * 1000)
                elif kind == 'capture':
                    rest, _, renderer = rest.partition(' renderer: ')
                    values = dict(item.split('=') for item in rest.split())
                    pending += tail.collect()
                    image = read_frame(self.work / 'Xvfb_screen0', self.size)
                    variant = values.get('variant')
                    shot = Path('shots') / (current.replace(':', '/') + (f'@v{variant}' if variant else '') + '.jpg')
                    target = self.arguments.out / shot
                    target.parent.mkdir(parents=True, exist_ok=True)
                    image.save(target, quality=82)
                    status = Path(f'/proc/{engine.pid}/status').read_text()
                    measured = dict(frame_ms=round(float(values['frame_ms']), 1), draws=int(values['draws']),
                                    rss_mb=int(re.search(r'VmRSS:\s+(\d+)', status)[1]) // 1024, shot=str(shot),
                                    # Per-frame means of the renderer counters (storm/render/perf_stats.hpp).
                                    renderer={key: float(value) for key, value in
                                              (item.split('=') for item in renderer.split())})
                    if variant is None or variant == '0':
                        # Later variants show the same scene: only the first is checked against the previous scene.
                        reasons, previous_frame = frame_reasons(image, previous_frame)
                        metrics.update(measured)
                        metrics['capture_reasons'] = reasons
                    if variant is not None:
                        metrics.setdefault('variants', {})[variant] = measured
                    (control / 'ack').touch()
                elif kind == 'end':
                    close(rest, [])
                    booted = booted or rest == inventory.BOOT_SCENES[-1]
                elif kind == 'fail':
                    scene, reason = rest.split(' ', 1)
                    pending += tail.collect()
                    close(scene, [reason])
                elif kind == 'tables':
                    found = {key: int(value) for key, value in (item.split('=') for item in rest.split())}
                    if found != self.context['tables']:
                        self.context['table_mismatch'] = (
                            f'the game built {found} but the inventory parsed {self.context["tables"]} from the scripts')
                elif kind in ('done', 'abort'):
                    finished = True
            if self.display_server.poll() is not None:
                os.killpg(engine.pid, signal.SIGKILL)
                engine.wait()
                pending += tail.collect()
                self.display_died(current)
            if exited:
                break
            now = time.monotonic()
            problem = None
            if now - fatal_checked > 1:
                fatal_checked = now
                fatal = FATAL.search(stdout.read_text(errors='replace')[-4000:])
                if fatal:
                    problem = f'fatal engine error: {fatal[1]}'
            if not problem and now - (begun if current else loaded) > self.arguments.timeout:
                problem = f'timeout: no result within {self.arguments.timeout} s'
            if not problem and self.context['stop'].is_set():
                return
            if problem:
                os.killpg(engine.pid, signal.SIGKILL)
                engine.wait()
                pending += tail.collect()
                self.abnormal(current, problem, booted, close)
                return
            time.sleep(0.02)

        pending += tail.collect()
        code = engine.returncode
        how = f'killed by signal {signal.Signals(-code).name}' if code < 0 else f'exit code {code}'
        if finished and code == 0:
            if pending and log_reasons(pending):
                self.record('exit:shutdown', log_reasons(pending))
        elif finished:
            self.record('exit:shutdown', [f'crash at engine shutdown: {how}'] + log_reasons(pending))
        else:
            self.abnormal(current, f'crash: engine {how}', booted, close)

    def display_died(self, current):
        """The shard's X server exited under the engine: a harness failure, never the scene's."""
        log = (self.work / 'xvfb.log').read_text(errors='replace').strip().splitlines()[-3:]
        raise RuntimeError(f'X server :{self.display} of shard {self.index} exited with code '
                           f'{self.display_server.returncode} during {current or "the boot"}: {" | ".join(log)}')

    def abnormal(self, current, problem, booted, close):
        """The engine died or was killed while `current` (or the boot) was running."""
        if not booted:
            self.boot_failures += 1
        scene = current or (self.queue[0] if booted and self.queue else inventory.BOOT_SCENES[0])
        if current is None:
            problem += ' (between scenes)'
        close(scene, [problem])

    def run(self):
        try:
            self.sweep()
        except Exception as error:  # a harness failure must fail the sweep, not vanish with the thread
            self.context['errors'].append(f'shard {self.index}: {type(error).__name__}: {error}')
            self.context['stop'].set()
        finally:
            self.display_server.terminate()
            self.display_server.wait()
            os.close(self.display_server.displayfd)

    def sweep(self):
        while self.queue and not self.context['stop'].is_set():
            self.run_engine()
            if self.boot_failures >= 2:
                for scene in self.queue:
                    self.record(scene, ['not run: the engine failed twice before a new game was up'])
                self.queue.clear()


def contact_sheets(out, scenes, results):
    """Thumbnails of every captured frame, failures framed in red, 48 per sheet."""
    directory = out / 'contact'
    shutil.rmtree(inside(out, directory), ignore_errors=True)
    directory.mkdir()
    columns, rows, width, height, label = 6, 8, 320, 180, 14
    shown = [scene for scene in scenes if results.get(scene.id, {}).get('shot')]
    groups = [('failures', [scene for scene in shown if results[scene.id]['status'] not in ('pass', 'known')])]
    groups += [('all', sorted(shown, key=lambda scene: (scene.category, scene.id)))]
    for name, members in groups:
        for page in range(0, len(members), columns * rows):
            sheet = Image.new('RGB', (columns * width, rows * (height + label)), (24, 24, 24))
            draw = ImageDraw.Draw(sheet)
            for index, scene in enumerate(members[page:page + columns * rows]):
                x, y = index % columns * width, index // columns * (height + label)
                sheet.paste(Image.open(out / results[scene.id]['shot']).resize((width, height)), (x, y))
                failed = results[scene.id]['status'] not in ('pass', 'known')
                if failed:
                    draw.rectangle((x, y, x + width - 1, y + height - 1), outline=(255, 32, 32), width=3)
                draw.text((x + 3, y + height + 1), scene.id[:52], fill=(255, 96, 96) if failed else (220, 220, 220))
            sheet.save(directory / f'{name}-{page // (columns * rows) + 1:02d}.jpg', quality=80)


def summarize(report, scenes, baseline):
    results = report['scenes']
    lines = []
    ran = [scene for scene in scenes if scene.id in results]
    failed = [scene for scene in ran if results[scene.id]['status'] not in ('pass', 'known')]
    known = [scene for scene in ran if results[scene.id]['status'] == 'known']
    extra = [name for name in results if name not in {scene.id for scene in scenes} and
             results[name]['status'] not in ('pass', 'known')]
    meta = report['meta']
    lines.append(f"Scene sweep {meta['started']}: {len(ran)} scenes, {len(ran) - len(failed) - len(known)} passed, "
                 f"{len(known)} known game data defects, {len(failed)} failed in {meta['wall_seconds']} s with "
                 f"{meta['jobs']} engines")
    lines.append('')
    lines.append(f"{'category':<24}{'scenes':>7}{'failed':>7}")
    categories = collections.Counter(scene.category for scene in ran)
    bad = collections.Counter(scene.category for scene in failed)
    for category, count in sorted(categories.items(), key=lambda item: (-item[1], item[0])):
        lines.append(f'{category:<24}{count:>7}{bad[category]:>7}')
    def failing(name):
        return classify(name, results[name]['reasons'])[0]

    kinds = collections.Counter(reason.split(':')[0] for scene in failed for reason in failing(scene.id))
    kinds.update(reason.split(':')[0] for name in extra for reason in failing(name))
    if kinds:
        lines += ['', 'Failure kinds (scenes can have several):']
        lines += [f'{count:>6}  {kind}' for kind, count in kinds.most_common()]
        texts = collections.Counter(re.sub(r' \(x\d+\)$', '', reason) for scene in failed for reason in failing(scene.id))
        lines += ['', 'Top errors:'] + [f'{count:>6}  {text}' for text, count in texts.most_common(25)]
    slow = sorted((scene for scene in ran if 'load_ms' in results[scene.id]), key=lambda scene: -results[scene.id]['load_ms'])
    if slow:
        loads = sorted(results[scene.id]['load_ms'] for scene in slow)
        lines += ['', f'Load time: median {loads[len(loads) // 2]} ms, slowest:']
        lines += [f"{results[scene.id]['load_ms']:>8} ms  {scene.id}" for scene in slow[:10]]
    regressions = None
    if baseline is not None:
        regressions = [name for name, result in results.items() if result['status'] not in ('pass', 'known') and
                       baseline['scenes'].get(name, {}).get('status') == 'pass']
        lines += ['', f'Against the baseline: {len(regressions)} scenes pass there and fail here']
        lines += [f'  {name}' for name in regressions]
    if known:
        lines += ['', 'KNOWN game data defects (known_defects.json; not failures, not hidden):']
        for scene in known:
            lines.append(f'  {scene.id}')
            lines += [f'      {reason}' for reason in classify(scene.id, results[scene.id]['reasons'])[1][:6]]
    if failed or extra:
        lines += ['', 'Failing scenes:']
        for name in [scene.id for scene in failed] + extra:
            lines.append(f'  {name}')
            lines += [f'      {reason}' for reason in failing(name)[:6]]
    return '\n'.join(lines) + '\n', (regressions if baseline is not None else [scene.id for scene in failed] + extra)


def main():
    arguments = parse_arguments()
    arguments.game = arguments.game.resolve()
    scenes = inventory.build(arguments.game)
    if arguments.list:
        chosen = select(scenes, arguments, None, None)
        for category, count in sorted(collections.Counter(scene.category for scene in chosen).items(), key=lambda item: -item[1]):
            print(f'{count:>6}  {category}')
        print(f'{len(chosen):>6}  total (+ {", ".join(inventory.BOOT_SCENES)} in every engine)')
        if arguments.only:
            print('\n'.join(scene.id for scene in chosen))
        return 0
    arguments.out = arguments.out.resolve()
    arguments.engine = arguments.engine.resolve()
    for tool in ('Xvfb', 'cp'):
        if not shutil.which(tool):
            sys.exit(f'{tool} not found')
    arguments.out.mkdir(parents=True, exist_ok=True)
    if os.stat(arguments.out).st_dev != os.stat(arguments.game).st_dev:
        sys.exit('--out must be on the same filesystem as --game: the per-engine game copies are hard links')
    report_path = arguments.out / 'report.json'
    previous = json.loads(report_path.read_text()) if report_path.exists() else None
    build = {'engine': file_digest(arguments.engine), 'script': file_digest(HERE / 'scene_sweep.c')}
    chosen = select(scenes, arguments, previous, build)
    if not chosen:
        print('no scenes selected')
        return 0
    jobs = min(arguments.jobs or default_jobs(), len(chosen))
    shutil.rmtree(inside(arguments.out, arguments.out / 'work'), ignore_errors=True)
    (arguments.out / 'work').mkdir()
    # One copy of the engine per sweep, hard-linked into every game copy: the build directory
    # can be rebuilt incrementally while the sweep runs.
    engine = shutil.copy(arguments.engine, arguments.out / 'work' / 'engine')

    done = collections.Counter()
    lock = threading.Lock()
    context = {'stop': threading.Event(), 'failed': threading.Event(), 'errors': [], 'table_mismatch': None,
               'tables': inventory.tables(arguments.game), 'engine': engine, 'game': game_snapshot(arguments.game)}

    def progress(scene, entry):
        with lock:
            done[entry['status']] += 1
            if entry['status'] == 'known':
                print(f"KNOWN {scene}: {'; '.join(classify(scene, entry['reasons'])[1][:3])}", flush=True)
            elif entry['status'] != 'pass':
                print(f"FAIL {scene}: {'; '.join(entry['reasons'][:3])}", flush=True)
                if arguments.fail_fast:
                    context['stop'].set()
            elif sum(done.values()) % 50 == 0:
                print(f"{sum(done.values())} results, {done['fail']} failed", flush=True)

    context['progress'] = progress
    print(f'{len(chosen)} scenes on {jobs} engines', flush=True)
    started = time.monotonic()
    shards = [Shard(index, queue, arguments, context) for index, queue in enumerate(make_shards(chosen, jobs))]
    for shard in shards:
        shard.start()
    try:
        for shard in shards:
            shard.join()
    except KeyboardInterrupt:
        context['stop'].set()
        for shard in shards:
            shard.join()
        raise
    if context['errors']:
        sys.exit('scene sweep harness error: ' + '; '.join(context['errors']))
    if not arguments.keep_work:
        shutil.rmtree(inside(arguments.out, arguments.out / 'work'), ignore_errors=True)

    # Merge: scenes are unique to a shard; the boot scenes and exit ran in every engine.
    results = {}
    for shard in shards:
        for name, entry in shard.results.items():
            if name in results:
                results[name]['reasons'] += [reason for reason in entry['reasons'] if reason not in results[name]['reasons']]
                results[name]['status'] = status_of(name, results[name]['reasons'])
            else:
                results[name] = entry
    known = {scene.id: scene for scene in scenes}
    for name, entry in results.items():
        entry['category'] = known[name].category if name in known else 'engine'
        entry['fingerprint'] = known[name].fingerprint if name in known else ''
    for scene in chosen:
        if scene.id not in results:
            results[scene.id] = {'status': 'skipped', 'reasons': ['not run: the sweep stopped early'],
                                 'category': scene.category, 'fingerprint': scene.fingerprint}
    if context['table_mismatch']:
        results['inventory:tables'] = {'status': 'fail', 'reasons': [context['table_mismatch']], 'category': 'engine',
                                       'fingerprint': ''}
    if (arguments.failed or arguments.changed) and previous and previous['build'] == build:
        results = {**previous['scenes'], **results}  # keep the report complete after a partial run
    report = {
        'meta': {'started': datetime.datetime.now().astimezone().isoformat(timespec='seconds'),
                 'wall_seconds': round(time.monotonic() - started), 'jobs': jobs, 'size': arguments.size,
                 'cores': os.cpu_count(), 'load_average': round(os.getloadavg()[0], 1),
                 'engine': str(arguments.engine), 'game': str(arguments.game)},
        'build': build,
        'scenes': dict(sorted(results.items())),
    }
    report_path.write_text(json.dumps(report, indent=1, ensure_ascii=False) + '\n')
    baseline = json.loads(arguments.baseline.read_text()) if arguments.baseline else None
    boot = [inventory.Scene(name, 'engine') for name in inventory.BOOT_SCENES]
    summary, failing = summarize(report, boot + [scene for scene in scenes if scene.id in results], baseline)
    (arguments.out / 'summary.txt').write_text(summary)
    contact_sheets(arguments.out, boot + scenes, results)
    print(summary)
    print(f'report: {report_path}')
    return 1 if failing else 0


if __name__ == '__main__':
    sys.exit(main())
