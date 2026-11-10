# Injected into the SDL2 configure (CMAKE_PROJECT_INCLUDE, tools/android/build_dependencies.py).
# SDL compiles the NDK's cpu-features.c into libSDL2 with -Werror=declaration-after-statement;
# NDK r30's x86/x86_64 block of that file declares after statements (its ARM blocks do not), so
# only the x86_64 build failed. The NDK file is not ours to patch: relax that one warning for it.
set_source_files_properties("${ANDROID_NDK}/sources/android/cpufeatures/cpu-features.c"
    PROPERTIES COMPILE_OPTIONS -Wno-declaration-after-statement)
