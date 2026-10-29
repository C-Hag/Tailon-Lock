# Cross-compile the Windows .exe from Linux with MinGW-w64.
# dnf-based distributions call the packages mingw64-gcc, mingw64-gcc-c++
# (SDL3 needs C++ on Windows) and mingw64-winpthreads-static.
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(MINGW_PREFIX x86_64-w64-mingw32)
set(CMAKE_C_COMPILER   ${MINGW_PREFIX}-gcc)
set(CMAKE_CXX_COMPILER ${MINGW_PREFIX}-g++)
set(CMAKE_RC_COMPILER  ${MINGW_PREFIX}-windres)

# dnf-based distributions install the sysroot here; Debian-based ones use /usr/x86_64-w64-mingw32.
if(EXISTS /usr/${MINGW_PREFIX}/sys-root/mingw)
  set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_PREFIX}/sys-root/mingw)
else()
  set(CMAKE_FIND_ROOT_PATH /usr/${MINGW_PREFIX})
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Lets `ctest` run the .exe through Wine when it is installed.
find_program(WINE_EXECUTABLE wine)
if(WINE_EXECUTABLE)
  set(CMAKE_CROSSCOMPILING_EMULATOR ${WINE_EXECUTABLE})
endif()
