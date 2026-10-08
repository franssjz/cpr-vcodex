"""Enable the application LTO linker after the framework adds its defaults."""


def configure_linker(env):
    if env.get("ARDUINO_LIB_COMPILE_FLAG") == "Build":
        return
    flags = [flag for flag in env.get("LINKFLAGS", [])
             if flag not in ("-flto", "-fno-lto", "-fuse-linker-plugin")]
    env.Replace(LINKFLAGS=flags + ["-flto", "-fuse-linker-plugin"])


if "Import" in globals():
    Import("env")  # noqa: F821 -- supplied by SCons
    configure_linker(env)  # noqa: F821
