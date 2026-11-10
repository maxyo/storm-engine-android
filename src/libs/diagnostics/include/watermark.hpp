#pragma once
// storm_build_watermark.hpp is generated into an include directory of the build
// (STORM_WATERMARK_FILE in CMakeLists.txt). Included by name, never by an absolute-path
// define: such a define differs per worktree and defeats the shared compiler cache.
#if __has_include("storm_build_watermark.hpp")
#include "storm_build_watermark.hpp"
#define STORM_STRINGIFY_EXPAND_(x) #x
#define STORM_STRINGIFY_(x) STORM_STRINGIFY_EXPAND_(x)
#define STORM_BUILD_WATERMARK_STRING STORM_STRINGIFY_(STORM_BUILD_WATERMARK)
#else
#error "Watermark file is not present. Check build configuration"
#endif
