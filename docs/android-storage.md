# Android resource location contract

The host SDL Activity must expose the public Java method
`String getGameDataDirectory()`. It returns the absolute filesystem path of an
existing user-selected installation, or throws if that location is unavailable.
The engine reads this method through JNI before changing its working directory.
UTF-16 conversion preserves Unicode folder names. JNI errors and relative or
empty paths fail explicitly; the engine does not silently choose another game.

The application owns permission requests, folder selection and optional ZIP
extraction. These are outside the engine repository. This supports both an
app-specific imported installation and direct shared-storage access. The
application must check the selected location and any required Android storage
permission again before starting native code. The native runtime checks that
`engine.ini` exists and does not create the selected game directory.

Saves, screenshots and logs still use the private internal `files/storm` root.
Changing the game resource location never changes that user-data root.

Host and Android compile checks:

```sh
python3 tests/android_runtime/run.py --sdl-source /path/to/SDL
CXX=/path/to/aarch64-linux-android24-clang++ python3 tests/android_runtime/run.py --sdl-source /path/to/SDL --compile-only
```

Application device checks must verify direct folder access, denied/revoked
permission and ZIP fallback using the complete APK. A running native main
function alone does not establish working Vulkan presentation or gameplay.

Android diagnostics mirror engine loggers and DXVK messages to logcat using
`StormEngine` and `StormDXVK` tags. Engine log files flush on informational or
higher messages, preserving initialization errors if native startup fails.
Geometry initialization rejects an unavailable renderer instead of dereferencing
it. None of these diagnostics substitutes for a working renderer.
