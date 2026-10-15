#!/usr/bin/env python3
"""Offline dependency/import selection tests using the real Conan 1 recipe."""
import io
import sys
from pathlib import Path
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from conanfile import StormEngine
from conans.client.output import ConanOutput
from conans.model.env_info import EnvValues
from conans.model.settings import Settings

class CrashReportDependencies(unittest.TestCase):
    def test_optional_dependencies_and_imports(self):
        for os_name in ("Windows", "Linux"):
            for enabled in (False, True):
                for steam in (False, True):
                    for sdl in (False, True):
                        with self.subTest(os=os_name, crash_reports=enabled, steam=steam, sdl=sdl):
                            recipe = StormEngine(ConanOutput(io.StringIO()), None)
                            settings = Settings({"os": ["Windows", "Linux"], "arch": ["x86_64"],
                                                 "compiler": ["gcc"], "build_type": ["Release", "Debug"]})
                            recipe.initialize(settings, EnvValues())
                            recipe.settings.os = os_name
                            recipe.settings.build_type = "Release"
                            recipe.options.crash_reports = enabled
                            recipe.options.steam = steam
                            recipe.options.conan_sdl = sdl
                            recipe.options.output_directory = "unused-test-output"
                            recipe.requirements()
                            requirements = {r.ref.name for r in recipe.requires.values()}
                            self.assertEqual("sentry-native" in requirements, enabled)
                            self.assertEqual("7zip" in requirements, enabled and os_name == "Windows")
                            self.assertEqual("openssl" in requirements, enabled and os_name == "Linux")
                            self.assertEqual("steamworks" in requirements, steam)
                            self.assertEqual("sdl" in requirements, sdl)
                            self.assertEqual("directx" in requirements, os_name == "Windows")
                            self.assertTrue({"fmod", "spdlog", "zlib"} <= requirements)
                            copied = []
                            recipe.copy = lambda name, **kwargs: copied.append(name)
                            recipe._StormEngine__install_folder = lambda *args: None
                            recipe._StormEngine__write_watermark = lambda: None
                            recipe.imports()
                            self.assertEqual(any("crashpad_handler" in p for p in copied), enabled)
                            self.assertEqual("7za.exe" in copied, enabled and os_name == "Windows")

if __name__ == "__main__":
    unittest.main()
