# PlatformIO pre-script for env:host. The windows_x86 toolchain (GCC 5)
# defaults to C++98; the library needs C++11. Applied to C++ only, so the
# Unity C sources do not get a C++ flag.
Import("env")

env.Append(CXXFLAGS=["-std=gnu++11"])
