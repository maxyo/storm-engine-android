#!/usr/bin/env python3
"""Exercise actual patched SDL Android surface callbacks with host stub services.

Usage: python3 tools/android/test_sdl_surface_lifecycle.py --sdl-source PATH
PATH must be an isolated SDL checkout with sdl2-android-ndk30.patch applied.
This checks callback branching/ownership, not Android Vulkan presentation.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def callback(source: str, name: str) -> str:
    start = source.rindex("JNIEXPORT void JNICALL SDL_JAVA_INTERFACE(" + name + ")")
    brace = source.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        if source[end] == "{":
            depth += 1
        elif source[end] == "}":
            depth -= 1
        end += 1
    return source[start:end]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdl-source", type=Path, required=True)
    parser.add_argument("--cc", default="cc")
    args = parser.parse_args()
    source = (args.sdl_source / "src/core/android/SDL_android.c").read_text()
    callbacks = "\n".join(callback(source, name) for name in
                          ("onNativeSurfaceChanged", "onNativeSurfaceDestroyed"))
    harness = r'''
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#define JNIEXPORT
#define JNICALL
#define SDL_JAVA_INTERFACE(name) name
#define SDL_WINDOW_OPENGL 2u
#define SDL_WINDOW_VULKAN 0x10000000u
#define EGL_NO_SURFACE NULL
typedef void JNIEnv;
typedef void *jclass;
typedef void *NativeWindowType;
typedef struct { void *native_window, *egl_surface; int backup_done; } SDL_WindowData;
typedef struct { unsigned flags; void *driverdata; } SDL_Window;
typedef struct { int unused; } SDL_VideoDevice;
static SDL_Window *Android_Window;
static int Android_ActivityMutex;
static int creates, destroys, releases, waits, errors, locks;
static SDL_VideoDevice device;
static void SDL_LockMutex(int unused) { (void)unused; ++locks; }
static void SDL_UnlockMutex(int unused) { (void)unused; --locks; }
static SDL_VideoDevice *SDL_GetVideoDevice(void) { return &device; }
static void *SDL_EGL_CreateSurface(SDL_VideoDevice *d, NativeWindowType w) {
    assert(d == &device && w); ++creates; return (void *)2;
}
static void SDL_EGL_DestroySurface(SDL_VideoDevice *d, void *s) {
    assert(d == &device && s); ++destroys;
}
static void ANativeWindow_release(void *w) { assert(w); ++releases; }
static void SDL_Delay(int ms) { assert(ms == 10 && locks == 0); ++waits; }
static void SDL_SetError(const char *error) { assert(error); ++errors; }
'''
    harness += callbacks
    harness += r'''
static void reset(void) { creates=destroys=releases=waits=errors=locks=0; }
int main(void) {
    SDL_WindowData data = {(void *)1, EGL_NO_SURFACE, 0};
    SDL_Window window = {SDL_WINDOW_VULKAN, &data};
    Android_Window = &window;
    onNativeSurfaceChanged(NULL, NULL);
    assert(creates == 0 && locks == 0);
    onNativeSurfaceDestroyed(NULL, NULL);
    assert(waits == 0 && errors == 0 && destroys == 0);
    assert(releases == 1 && !data.native_window && locks == 0);
    onNativeSurfaceDestroyed(NULL, NULL);
    assert(releases == 1); /* no double native-window release */

    reset(); window.flags = SDL_WINDOW_OPENGL;
    data.native_window = (void *)1; data.backup_done = 1;
    onNativeSurfaceChanged(NULL, NULL);
    onNativeSurfaceChanged(NULL, NULL);
    assert(creates == 1 && data.egl_surface);
    onNativeSurfaceDestroyed(NULL, NULL);
    assert(destroys == 1 && releases == 1 && waits == 0);
    assert(!data.egl_surface && !data.native_window && locks == 0);

    reset(); data.native_window = (void *)1;
    data.egl_surface = (void *)2; data.backup_done = 0;
    onNativeSurfaceDestroyed(NULL, NULL);
    assert(waits == 49 && errors == 1); /* existing GL timeout preserved */
    assert(destroys == 1 && releases == 1 && locks == 0);

    reset(); window.flags = 0; data.native_window = (void *)1;
    onNativeSurfaceChanged(NULL, NULL);
    onNativeSurfaceDestroyed(NULL, NULL);
    assert(creates == 0 && waits == 0 && releases == 1 && locks == 0);

    reset(); Android_Window = NULL;
    onNativeSurfaceChanged(NULL, NULL);
    onNativeSurfaceDestroyed(NULL, NULL);
    assert(creates == 0 && destroys == 0 && releases == 0 && locks == 0);
    puts("SDL actual surface callbacks: Vulkan/GL/non-GL/no-window lifecycle PASS");
    return 0;
}
'''
    with tempfile.TemporaryDirectory(prefix="storm-sdl-surface-") as directory:
        cfile = Path(directory) / "surface.c"
        binary = Path(directory) / "surface-check"
        cfile.write_text(harness)
        subprocess.run([args.cc, "-std=c99", "-Wall", "-Wextra", "-Werror",
                        "-Wno-unused-parameter", str(cfile), "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
