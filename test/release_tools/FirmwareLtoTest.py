import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("enable_lto", ROOT / "scripts" / "enable_lto.py")
LTO = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(LTO)


class FakeEnvironment(dict):
    def Object(self, node, **flags):
        return node, flags

    def AddBuildMiddleware(self, callback):
        self["middleware"] = callback

    def AppendUnique(self, **values):
        for key, flags in values.items():
            current = self.setdefault(key, [])
            current.extend(flag for flag in flags if flag not in current)


class FirmwareLtoTest(unittest.TestCase):
    def test_application_and_library_objects_use_lto_without_mutating_shared_flags(self):
        env = FakeEnvironment(CCFLAGS=["-Os"], ARDUINO_LIB_COMPILE_FLAG="Inactive")
        for filename in ("main.cpp", "font.cc", "parser.cxx", "decoder.c"):
            node = Path(filename)
            result, flags = LTO.application_lto(env, node)
            self.assertIs(result, node)
            self.assertEqual(flags["CCFLAGS"], ["-Os", "-flto"])
        self.assertEqual(env["CCFLAGS"], ["-Os"])

    def test_custom_sdk_objects_are_not_modified(self):
        env = FakeEnvironment(CCFLAGS=["-Os"], ARDUINO_LIB_COMPILE_FLAG="Build")
        for filename in ("port.c", "flash_ops.c", "esp32-hal.cpp"):
            node = Path(filename)
            self.assertIs(LTO.application_lto(env, node), node)
        self.assertEqual(env["CCFLAGS"], ["-Os"])

    def test_assembly_and_prebuilt_objects_are_not_modified(self):
        env = FakeEnvironment(CCFLAGS=["-Os"])
        for filename in ("vectors.S", "startup.s", "library.o", "library.a"):
            node = Path(filename)
            self.assertIs(LTO.application_lto(env, node), node)

    def test_linker_support_is_enabled_without_global_compiler_flags(self):
        env = FakeEnvironment(CCFLAGS=["-Os"], LINKFLAGS=["-Wl,--gc-sections", "-flto"])
        LTO.configure_lto(env)
        self.assertIs(env["middleware"], LTO.application_lto)
        self.assertEqual(env["CCFLAGS"], ["-Os"])
        self.assertEqual(env["LINKFLAGS"], ["-Wl,--gc-sections", "-flto", "-fuse-linker-plugin"])

    def test_global_build_flags_do_not_leak_lto_into_sdk(self):
        import configparser

        config = configparser.ConfigParser(interpolation=None)
        config.read(ROOT / "platformio.ini", encoding="utf-8")
        flags = [line.strip() for line in config["base"]["build_flags"].splitlines()]
        self.assertFalse(any(flag.startswith("-flto") for flag in flags))
        self.assertIn("pre:scripts/enable_lto.py", config["base"]["extra_scripts"])


if __name__ == "__main__":
    unittest.main()
