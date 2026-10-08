"""Optimize application objects without propagating LTO into the SDK rebuild."""

from pathlib import Path


def application_lto(build_env, node):
    # ESP-IDF has const and mutable DRAM_ATTR objects that conflict under LTO.
    # Its custom-sdkconfig pass must retain the framework's own compiler flags.
    if build_env.get("ARDUINO_LIB_COMPILE_FLAG") == "Build":
        return node
    if Path(node.name).suffix.lower() not in (".c", ".cc", ".cpp", ".cxx"):
        return node
    return build_env.Object(node, CCFLAGS=list(build_env.get("CCFLAGS", [])) + ["-flto"])


def configure_lto(env):
    env.AddBuildMiddleware(application_lto)


if "Import" in globals():
    Import("env")  # noqa: F821 -- supplied by SCons
    configure_lto(env)  # noqa: F821
