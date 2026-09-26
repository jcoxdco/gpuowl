
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
  i18n.cpp
  )

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

# Translation of the help and the status messages (see po/).  MSVC builds load libintl at run time, so they need
# nothing at build time; elsewhere libintl is linked (it is part of glibc).  Without it the build is English-only.
set(PRPLL_LOCALEDIR "/usr/share/locale" CACHE STRING
    "Where the message catalogs are looked for when there is no locale directory next to the executable")
if (MSVC)
  target_compile_definitions(prpll PRIVATE PRPLL_NLS=1)
else()
  find_package(Intl)
  if (Intl_FOUND)
    target_compile_definitions(prpll PRIVATE PRPLL_NLS=1 "PRPLL_LOCALEDIR=\"${PRPLL_LOCALEDIR}\"")
    target_include_directories(prpll PRIVATE ${Intl_INCLUDE_DIRS})
    target_link_libraries(prpll ${Intl_LIBRARIES})
  else()
    message(STATUS "libintl not found: building without translations (English only)")
  endif()
endif()

# "pot" and "locale" targets, as in the Makefile, when the gettext tools are installed.  "locale" puts the
# compiled catalogs in a locale directory next to the executable, where PRPLL looks for them first.
find_package(Gettext QUIET)
find_program(PRPLL_XGETTEXT xgettext)
file(GLOB PO_FILES ${CMAKE_SOURCE_DIR}/po/*.po)
if (GETTEXT_FOUND)
  set(LOCALE_COMMANDS)
  foreach(po ${PO_FILES})
    get_filename_component(lang ${po} NAME_WE)
    list(APPEND LOCALE_COMMANDS
      COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:prpll>/locale/${lang}/LC_MESSAGES
      COMMAND ${GETTEXT_MSGFMT_EXECUTABLE} --check -o $<TARGET_FILE_DIR:prpll>/locale/${lang}/LC_MESSAGES/prpll.mo ${po})
  endforeach()
  add_custom_target(locale ${LOCALE_COMMANDS} VERBATIM)
endif()
if (GETTEXT_FOUND AND PRPLL_XGETTEXT)
  file(GLOB I18N_SOURCES RELATIVE ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
  list(REMOVE_ITEM I18N_SOURCES src/bundle.cpp)
  set(POT_COMMANDS)
  foreach(po ${PO_FILES})
    list(APPEND POT_COMMANDS COMMAND ${GETTEXT_MSGMERGE_EXECUTABLE} --quiet --update --backup=none ${po} po/prpll.pot)
  endforeach()
  add_custom_target(pot
    COMMAND ${PRPLL_XGETTEXT} -C --from-code=UTF-8 --keyword=_ --flag=_:1:c-format --package-name=PRPLL
            --msgid-bugs-address=https://github.com/gwoltman/gpuowl/issues -o po/prpll.pot ${I18N_SOURCES}
    ${POT_COMMANDS}
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    VERBATIM)
endif()

# Regenerated on every build (a commit changes the version without touching any source file) by version.cmake,
# which only rewrites version.inc when the string changes.  -DPRPLL_VERSION=... sets it, as make VERSION=... does.
add_custom_target(prpll_version
  COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/version.inc
          -DPRPLL_VERSION=${PRPLL_VERSION} -P ${CMAKE_CURRENT_SOURCE_DIR}/version.cmake
  BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/version.inc
  )
add_dependencies(prpll prpll_version)

file(
  GLOB CL_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cl/*.cl
  )

# The CUDA compatibility header must come first in the bundle, as it does in the
# Makefile: KernelCompiler passes the bundle's files to the OpenCL compiler as
# headers and skips the first one, which it expects to be opencl_compat.cuh.
# Without it the first *.cl file is dropped instead and no kernel can compile.
file(
  GLOB CUH_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/cuda/*.cuh
  )
set(CL_SOURCES ${CUH_SOURCES} ${CL_SOURCES})

add_custom_command(
  OUTPUT bundle.cpp
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMAND ${CMAKE_SOURCE_DIR}/genbundle.sh ${CL_SOURCES} > ${CMAKE_CURRENT_BINARY_DIR}/bundle.cpp
  DEPENDS ${CL_SOURCES}
  )
