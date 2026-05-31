
add_executable(prpll
  Trig.cpp
  Primes.cpp
  bundle.cpp
  Proof.cpp
  log.cpp md5.cpp sha3.cpp AllocTrac.cpp FFTConfig.cpp Worktodo.cpp common.cpp main.cpp Gpu.cpp clwrap.cpp Task.cpp timeutil.cpp Args.cpp state.cpp Signal.cpp
  File.cpp
  gpuid.cpp
  version.cpp
  KernelCompiler.cpp
  Kernel.cpp
  Saver.cpp
  Queue.cpp
  TimeInfo.cpp
  Profile.cpp
  Event.cpp
  TrigBufCache.cpp
  CycleFile.cpp
  tune.cpp
  TuneEntry.cpp
  fs.cpp
  version.inc
  )

if(MSVC)
  # Silence MSVC-only C4996 "unsafe CRT" deprecation warnings (sscanf/fopen/sprintf/localtime/gmtime).
  # These are a Windows-only security nudge with no codegen or runtime impact; the *_s replacements are
  # not portable to the Linux/GCC build. Inputs here are own savefiles/config, not untrusted data.
  target_compile_definitions(prpll PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
  # Real GCC (not clang), needed for 128-bit floats and std::filesystem::path
  set(CMAKE_CXX_COMPILER g++-14)
endif()

find_package(OpenCL)

if (${OpenCL_FOUND})
  # Use OpenCL library found by cmake
  target_link_libraries(prpll OpenCL::OpenCL)
else()
  # Pass -lOpenCL to the linker and hope for the best
  target_link_libraries(prpll OpenCL)
endif()

target_include_directories(prpll PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

if(WIN32)
  find_program(BASH bash HINTS "$ENV{ProgramFiles}/Git/bin")
  if(NOT BASH)
    message(FATAL_ERROR "Git Bash is required to build on Windows (bash not found)")
  endif()
  file(TO_CMAKE_PATH "${CMAKE_CURRENT_BINARY_DIR}/version.inc" VERSION_INC_OUT)
  file(TO_CMAKE_PATH "${CMAKE_CURRENT_BINARY_DIR}/bundle.cpp" BUNDLE_CPP_OUT)
endif()

if(WIN32)
  add_custom_command(
    OUTPUT version.inc
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND "${BASH}" -c "printf '\"%s\"\\n' \"$$(git describe --tags --long --dirty --always)\" > ${VERSION_INC_OUT}"
    DEPENDS ${CMAKE_SOURCE_DIR}
    VERBATIM
    )
else()
  add_custom_command(
    OUTPUT version.inc
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND git describe --tags --long --dirty --always --match ${CMAKE_SOURCE_DIR} | sed 's/.*/"&"/' > ${CMAKE_CURRENT_BINARY_DIR}/version.inc
    DEPENDS ${CMAKE_SOURCE_DIR}
    )
endif()

file(
  GLOB CL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cl/*.cl
  )

# The CUDA compat header must be the FIRST file in the bundle: KernelCompiler::compile()
# skips the first embedded file (the CUDA-only opencl_compat shim) for OpenCL builds.
# If it is omitted, base.cl ends up first and gets skipped, breaking all kernel compiles.
file(
  GLOB CUDA_COMPAT_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cuda/*.cuh
  )

if(WIN32)
  add_custom_command(
    OUTPUT bundle.cpp
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND "${BASH}" -c "./genbundle.sh src/cuda/*.cuh src/cl/*.cl > ${BUNDLE_CPP_OUT}"
    DEPENDS ${CUDA_COMPAT_SOURCES} ${CL_SOURCES} ${CMAKE_SOURCE_DIR}/genbundle.sh
    VERBATIM
    )
else()
  add_custom_command(
    OUTPUT bundle.cpp
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    COMMAND ${CMAKE_SOURCE_DIR}/genbundle.sh ${CUDA_COMPAT_SOURCES} ${CL_SOURCES} > ${CMAKE_CURRENT_BINARY_DIR}/bundle.cpp
    DEPENDS ${CUDA_COMPAT_SOURCES} ${CL_SOURCES}
    )
endif()
