"""Locate the pinned GLM checkout for tests that compile engine fragments directly.

The fragments use glm::vec3 / glm::mat4 and storm_glm.h, so the stubs include the
real math headers instead of redefining vector types.
"""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ENVIRONMENT = 'STORM_GLM_SOURCE'


def glm_source(explicit=None):
    """--glm-source value, else $STORM_GLM_SOURCE, else a FetchContent copy in a build tree."""
    candidates = [explicit, os.environ.get(ENVIRONMENT)]
    for pattern in ('build/*/_deps/storm_glm-src', 'build/*/*/_deps/storm_glm-src',
                    'build/*/*/*/_deps/storm_glm-src'):
        candidates += sorted(ROOT.glob(pattern))
    # The copy shared by all build directories (cmake/StormBuildCache.cmake), newest first.
    shared = Path(os.environ.get('STORM_DEPS_CACHE', Path.home() / '.cache/storm-deps')) / 'fetchcontent'
    candidates += sorted(shared.glob('storm_glm-*'), key=lambda path: path.stat().st_mtime, reverse=True)
    for candidate in candidates:
        if candidate and (Path(candidate) / 'glm/glm.hpp').is_file():
            return Path(candidate).resolve()
    raise SystemExit('Pinned GLM 1.0.3 sources not found: pass --glm-source or set ' + ENVIRONMENT +
                     ' to an extracted checkout (e.g. <build>/_deps/storm_glm-src)')


def math_include_flags(explicit=None):
    """Compiler flags giving a translation unit storm_glm.h and GLM itself."""
    return ['-I' + str(ROOT / 'src/libs/math/include'), '-isystem', str(glm_source(explicit))]
