# Third-party code

None. The repository holds no third-party source, and the build downloads
nothing: the game links only the operating system's own libraries (on macOS
its system frameworks; on Windows the system DLLs; on Linux libc, libm and
pthread, and it opens the system's Wayland or X11, ALSA and Vulkan libraries
when it starts). Every file is original code written for this project.

One tool, for developers only: after changing the HLSL shaders, `tools/build_shaders.sh` downloads a pinned
release of Microsoft's shader compiler (dxc) into `build/_tools/` to make `src/gpu_shaders.h`. It is not part of the
game, and a normal build does not need it.
