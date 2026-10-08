"""Keep PlatformIO's compiler and linker in agreement for firmware LTO."""

Import("env")

# build_flags reaches compilation, but this platform constructs LINKFLAGS
# separately. GCC must load its LTO plugin when consuming the resulting objects.
env.Append(LINKFLAGS=["-flto", "-fuse-linker-plugin"])
