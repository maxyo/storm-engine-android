#!/usr/bin/env python3
"""Compile the real diagnostics translation unit; no SDK/library stubs.

Provide existing official SDL 2.0.18, spdlog 1.9.2 source trees and the
sentry-native 0.6.5 include directory. This is a host compile-only check,
not a link, logging-runtime, crash-handler or full-engine test.
"""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--sdl-source", type=Path, required=True)
parser.add_argument("--spdlog-source", type=Path, required=True)
parser.add_argument("--fmt-source", type=Path)
parser.add_argument("--sentry-include", type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="storm-diagnostics-") as directory:
    build = Path(directory)
    (build / "SDL2").symlink_to(args.sdl_source.resolve() / "include", target_is_directory=True)
    watermark = build / "generated_revision.hpp"
    watermark.write_text("#pragma once\n#define STORM_BUILD_WATERMARK diagnostics_compile_check\n")
    for enabled in (False, True):
        output = build / ("enabled.o" if enabled else "disabled.o")
        command = [os.environ.get("CXX", "c++"), "-std=c++20", "-pthread", "-c",
                   str(root / "src/libs/diagnostics/src/lifecycle_diagnostics_service.cpp"),
                   "-o", str(output), f'-DSTORM_WATERMARK_FILE="{watermark}"',
                   "-I" + str(build), "-I" + str(args.spdlog_source.resolve() / "include")]
        if args.fmt_source:
            command += ["-DSPDLOG_FMT_EXTERNAL", "-DFMT_HEADER_ONLY",
                        "-I" + str(args.fmt_source.resolve() / "include")]
        for library in ("diagnostics", "core", "util"):
            command.append("-I" + str(root / "src/libs" / library / "include"))
        if enabled:
            command += ["-DSTORM_ENABLE_CRASH_REPORTS=1", "-I" + str(args.sentry_include.resolve())]
        subprocess.run(command, check=True)
        symbols = subprocess.check_output(["nm", "-u", str(output)], text=True)
        has_sentry = "sentry_" in symbols
        if has_sentry != enabled:
            raise RuntimeError(f"Unexpected Sentry references with reporting={enabled}")
        print(f"PASS: real diagnostics object, crash_reports={enabled}, Sentry symbols={has_sentry}")
