set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER "C:/msys64/clangarm64/bin/clang.exe")
set(CMAKE_CXX_COMPILER "C:/msys64/clangarm64/bin/clang++.exe")

set(CMAKE_C_COMPILER_TARGET x86_64-w64-windows-gnu)
set(CMAKE_CXX_COMPILER_TARGET x86_64-w64-windows-gnu)

set(CMAKE_SYSROOT "C:/msys64/mingw64")

set(CMAKE_C_FLAGS_INIT "-rtlib=libgcc -pthread")
set(CMAKE_CXX_FLAGS_INIT "-stdlib=libstdc++ -rtlib=libgcc -pthread")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-fuse-ld=lld")
