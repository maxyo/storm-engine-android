# Sourced by the Android build scripts. The shared sources directory ($work/sources: SDL, every
# worktree's builds) has one writer at a time and no checkout changes under a running build:
# writers (ci_dependencies.sh checkout_source, engine_sources.py --directory) take an exclusive lock
# on $work/sources/.storm-sources.lock, builds compiling from it hold a shared one on the same file
# until they exit. The engine's other sources are immutable per-pin checkouts (engine_sources.py
# --shared) and need no lock. A lock not had within STORM_SOURCES_LOCK_TIMEOUT seconds (default 900) is an
# error naming the holder.
sources_lock() { # sources_lock <shared|exclusive> <sources dir>
    local mode=$1 dir=$2 timeout=${STORM_SOURCES_LOCK_TIMEOUT:-900}
    mkdir -p "$dir"
    exec {storm_sources_lock_fd}>>"$dir/.storm-sources.lock"
    if ! flock -n "-${mode:0:1}" "$storm_sources_lock_fd"; then
        echo "waiting for $dir ($mode lock; held by: $(cat "$dir/.storm-sources.lock" 2>/dev/null || true))" >&2
        if ! flock -w "$timeout" "-${mode:0:1}" "$storm_sources_lock_fd"; then
            echo "ERROR: $dir stayed locked for $timeout s (held by: $(cat "$dir/.storm-sources.lock" 2>/dev/null || true));" \
                "a checkout there must not change under a running build. Retry when it is done (STORM_SOURCES_LOCK_TIMEOUT sets the wait)." >&2
            exit 1
        fi
    fi
}
