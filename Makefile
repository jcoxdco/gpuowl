# Use "make CUDA=1" for a CUDA build, use "make DEBUG=1" for a debug build

# The build artifacts are put in the "build-release" subfolder (or "build-debug" for a debug build).

# On Windows invoke with "make exe" or "make all"

DEBUG = 0
CUDA = 0
STATIC_RUNTIME = 0
STATIC_CUDA = 0

# Uncomment below as desired to set a particular compiler or force a debug build:
# CXX = g++-12
# DEBUG = 1
# or export those into environment, or pass on the command line e.g.
# make all DEBUG=1 CXX=g++-12

HOST_OS = $(shell uname -s)
# MSYS2's make reports MSYS_NT when the Windows CI runs it from PowerShell. The compiler is still
# MinGW, so that host has to take the same Windows path as a MINGW_NT uname.
HOST_WIN = $(findstring MINGW,$(HOST_OS))$(findstring MSYS,$(HOST_OS))

CXX ?= g++

ifeq ($(CUDA), 1)
 BIN=build-cuda
 CUDASRCS1 = clwrap_cuda.cpp cudawrap.cpp
 CUDAFLAGS = -DCUDA_BACKEND -Isrc/cuda -I/usr/local/cuda/include
 CUDAOBJS = $(CUDASRCS1:%.cpp=$(BIN)/%.o)
 ifeq ($(STATIC_CUDA), 1)
  OPENCL_LIBS = -L/usr/local/cuda/lib64 -Wl,--start-group -lnvrtc_static -lnvrtc-builtins_static -lnvptxcompiler_static -Wl,--end-group -lcuda -lpthread -ldl
 else
  OPENCL_LIBS = -L/usr/local/cuda/lib64 -Wl,-rpath,'$$ORIGIN' -lnvrtc -lcuda -lpthread
 endif
else
 BIN=build-release
 CUDAFLAGS =
 CUDAOBJS =
 ifeq ($(HOST_OS), Darwin)
  OPENCL_LIBS = -framework OpenCL
 else
  OPENCL_LIBS = -lOpenCL -lpthread
 endif
endif

COMMON_FLAGS = -Wall -Wextra $(CUDAFLAGS) -std=c++20

ifeq ($(STATIC_RUNTIME),1)
 LDFLAGS += -static-libstdc++ -static-libgcc

 ifneq ($(HOST_WIN),)
# For mingw-64 use this:
  LDFLAGS += -static
 endif
endif

ifneq ($(HOST_WIN),)
 CPPFLAGS += -DWINVER=0x0601 -D_WIN32_WINNT=0x0601
 LDFLAGS += -Wl,--subsystem,console:6.01
endif

# NLS=1 translates the help and the status messages through GNU gettext (catalogs in po/, see "make locale");
# NLS=0 builds an English-only binary.  gettext is part of glibc; MinGW needs libintl (MSYS2's gettext package)
# and is built with it when libintl.h is found; macOS has no system libintl.
# LOCALEDIR is where the catalogs are looked for when there is no "locale" directory next to the executable.
ifeq ($(HOST_OS), Darwin)
 NLS ?= 0
else ifneq ($(HOST_WIN),)
 NLS ?= $(if $(shell $(CXX) $(CPPFLAGS) -include libintl.h -fsyntax-only -x c++ /dev/null 2>/dev/null && echo 1),1,0)
else
 NLS ?= 1
endif
LOCALEDIR ?= /usr/share/locale

ifeq ($(NLS), 1)
 CPPFLAGS += -DPRPLL_NLS=1 -DPRPLL_LOCALEDIR='"$(LOCALEDIR)"'
 ifneq ($(HOST_WIN),)
  NLS_LIBS = -lintl -liconv
 endif
endif
# -fext-numeric-literals

ifeq ($(DEBUG), 1)

BIN=build-debug
CXXFLAGS = -g -Og $(COMMON_FLAGS)

else

CXXFLAGS = -O3 -flto -DNDEBUG $(COMMON_FLAGS)

endif

SRCS1 = fs.cpp Trig.cpp TuneEntry.cpp Primes.cpp tune.cpp CycleFile.cpp TrigBufCache.cpp Event.cpp Queue.cpp TimeInfo.cpp Profile.cpp bundle.cpp Saver.cpp KernelCompiler.cpp Kernel.cpp gpuid.cpp File.cpp Proof.cpp log.cpp Worktodo.cpp common.cpp main.cpp Gpu.cpp clwrap.cpp Task.cpp timeutil.cpp Args.cpp state.cpp Signal.cpp FFTConfig.cpp AllocTrac.cpp sha3.cpp md5.cpp version.cpp i18n.cpp

SRCS2 = test.cpp

# SRCS=$(addprefix src/, $(SRCS1))

OBJS = $(CUDAOBJS) $(SRCS1:%.cpp=$(BIN)/%.o)
DEPDIR := $(BIN)/.d
$(shell mkdir -p $(DEPDIR) >/dev/null)
DEPFLAGS = -MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
COMPILE.cc = $(CXX) $(DEPFLAGS) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c
POSTCOMPILE = @mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d && touch $@

all: prpll

prpll: $(BIN)/prpll

amd: $(BIN)/prpll-amd

#$(BIN)/test: $(BIN)/test.o
#	$(CXX) $(CXXFLAGS) -o $@ $< $(LIBPATH)

$(BIN)/prpll: ${OBJS}
	$(CXX) $(LDFLAGS) $(CXXFLAGS) -o $@ ${OBJS} $(LIBPATH) $(OPENCL_LIBS) $(NLS_LIBS)

# Instead of linking with libOpenCL, link with libamdocl64
$(BIN)/prpll-amd: ${OBJS}
	$(CXX) $(LDFLAGS) $(CXXFLAGS) -o $@ ${OBJS} $(LIBPATH) -lamdocl64 -L/opt/rocm/lib $(NLS_LIBS)

# Translation catalogs.  These need the gettext tools and are not part of the default build.
# "make pot" re-extracts the marked messages into po/prpll.pot and merges them into each po/<lang>.po;
# "make locale" compiles each po/<lang>.po into $(BIN)/locale/<lang>/LC_MESSAGES/prpll.mo, and fails
# on a translation whose printf conversions differ from the English.
PO_FILES = $(wildcard po/*.po)
MO_FILES = $(patsubst po/%.po,$(BIN)/locale/%/LC_MESSAGES/prpll.mo,$(PO_FILES))

pot:
	xgettext -C --from-code=UTF-8 --keyword=_ --flag=_:1:c-format --package-name=PRPLL \
	  --msgid-bugs-address=https://github.com/gwoltman/gpuowl/issues -o po/prpll.pot \
	  $(filter-out src/bundle.cpp,$(wildcard src/*.cpp)) $(wildcard src/*.h)
	for po in $(PO_FILES); do msgmerge --quiet --update --backup=none $$po po/prpll.pot || exit 1; done

locale: $(MO_FILES)

$(BIN)/locale/%/LC_MESSAGES/prpll.mo: po/%.po
	mkdir -p $(dir $@)
	msgfmt --check -o $@ $<

.PHONY: pot locale

clean:
	rm -rf build-debug build-release build-cuda

$(BIN)/%.o : src/%.cpp $(DEPDIR)/%.d
	$(COMPILE.cc) $(OUTPUT_OPTION) $<
	$(POSTCOMPILE)

$(BIN)/%.o : src/cuda/%.cpp $(DEPDIR)/%.d
	$(COMPILE.cc) $(OUTPUT_OPTION) $<
	$(POSTCOMPILE)

# src/bundle.cpp is just a wrapping of the OpenCL sources (*.cl) as a C string (as well as the CUDA OpenCL translation code)

src/bundle.cpp: genbundle.sh src/cuda/*.cuh src/cl/*.cl
	bash genbundle.sh $^ > src/bundle.cpp

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d

src/version.cpp : src/version.inc

# The version string compiled into the binary and reported to PrimeNet in
# every result. Defaults to `git describe` of the checkout; a build from an
# exported tree (no .git) or a packager that wants the upstream string passes
# it explicitly: make VERSION=v8.0-57-g6cb4c12
VERSION ?= $(notdir $(shell git describe --tags --long --dirty --always 2>/dev/null))
ifeq ($(strip $(VERSION)),)
$(warning No git checkout to take the version from, building as "unknown"; pass VERSION=... to set it)
override VERSION := unknown
endif

src/version.inc: FORCE
	echo \"$(VERSION)\" > $(BIN)/version.new
	diff -q -N $(BIN)/version.new $@ >/dev/null || mv $(BIN)/version.new $@
	echo Version: `cat $@`

FORCE:

include $(wildcard $(patsubst %,$(DEPDIR)/%.d,$(basename $(SRCS1))))
# include $(wildcard $(patsubst %,$(DEPDIR)/%.d,$(basename $(SRCS2))))
