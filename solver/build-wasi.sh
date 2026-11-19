#!/bin/bash

# Needs only make and a wasi-sdk release (https://github.com/WebAssembly/wasi-sdk/releases), which provides clang,
# wasm-ld, llvm-ar, the wasi-libc sysroot and the wasm32 compiler runtime.  Unpack it anywhere and either
#   WASI_SDK=/path/to/wasi-sdk-NN.0-x86_64-linux solver/build-wasi.sh
# or unpack it into the top of this source tree (any directory named wasi-sdk-*) and just run solver/build-wasi.sh.
[ -n "$BASH_VERSION" ] || exec bash "$0" "$@"
set -eu
cd "$(dirname "$0")/.."
TOP=$PWD

if [ -z "${WASI_SDK:-}" ]; then
    for d in "$TOP"/wasi-sdk-*/; do [ -x "$d/bin/clang" ] && WASI_SDK=${d%/} && break; done
fi
[ -x "${WASI_SDK:-}/bin/clang" ] || { echo "wasi-sdk not found: set WASI_SDK=/path/to/wasi-sdk-NN.0-<arch>-<os>
(download from https://github.com/WebAssembly/wasi-sdk/releases) or unpack it into $TOP" >&2; exit 1; }
CC=$WASI_SDK/bin/wasm32-wasip1-clang        # the SDK's clang preconfigured for wasm32-wasip1: sysroot, crt, compiler-rt
AR=$WASI_SDK/bin/llvm-ar
RANLIB=$WASI_SDK/bin/llvm-ranlib
for t in "$CC" "$AR" "$RANLIB"; do [ -x "$t" ] || { echo "$t missing: is $WASI_SDK a wasi-sdk >= 25?" >&2; exit 1; }; done

OPT=${AN_WASM_CFLAGS:--O3 -g0 -msimd128 -flto}
EMU="-D_WASI_EMULATED_MMAN -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS -D_WASI_EMULATED_GETPID"
CFLAGS="$OPT -DNDEBUG -D_GNU_SOURCE $EMU -isystem $TOP/solver/wasi/include -include $TOP/solver/wasi/compat.h"
[ -n "${AN_VERBOSE:-}" ] || QUIET=-s
echo "wasi-sdk: $WASI_SDK"
echo "flags:    $OPT"

cp gsl-an/config.h.in gsl-an/config.h
for n in HAVE_IEEE_COMPARISONS HAVE_DECL_{FINITE,ISNAN,ISINF,HYPOT,EXPM1,LOG1P,ACOSH,ASINH,ATANH,LDEXP,FREXP} STDC_HEADERS; do
    sed -i "s/^#undef $n\$/#define $n 1/" gsl-an/config.h
done
echo '#define HAVE_NETPBM 0' > include/astrometry/os-features-config.h
echo '# wasi: no optional deps' > util/makefile.os-features

for d in qfits-an gsl-an libkd util catalogs solver; do
    find $d -maxdepth 1 \( -name '*.o' -o -name 'lib*.a' \) -delete
    : > $d/deps
done
export SYSTEM_GSL=no
lib() { local dir=$1; shift; make $QUIET -j"$(nproc)" -o deps -C "$dir" CC="$CC" AR="$AR" RANLIB="$RANLIB" FLAGS="$CFLAGS" "$@"; }
lib qfits-an libqfits.a
CFLAGS=-DHAVE_CONFIG_H lib gsl-an libgsl-an.a
lib libkd libkd.a
lib util libanbase.a libanutils.a libanfiles.a
lib catalogs libcatalogs.a
lib solver libastrometry.a

INC="-I include -I include/astrometry -I gsl-an -I util -I qfits-an -I libkd -I catalogs -I solver"
LIBS="solver/libastrometry.a catalogs/libcatalogs.a util/libanfiles.a libkd/libkd.a util/libanutils.a util/libanbase.a gsl-an/libgsl-an.a qfits-an/libqfits.a"
WL="-lwasi-emulated-mman -lwasi-emulated-signal -lwasi-emulated-process-clocks -lwasi-emulated-getpid"
HOSTMMAP="solver/wasi/hostmmap.c -Wl,--wrap=fopen -Wl,--wrap=fclose -Wl,--wrap=mmap -Wl,--wrap=munmap"
ENGINE="solver/engine-main.c solver/wasi/compat.c"
mkdir -p solver/wasm
link() { local out=$1; shift; [ -z "${AN_VERBOSE:-}" ] || echo "[link] $out" >&2
    "$CC" $INC $CFLAGS "$@" $LIBS $WL -o "solver/wasm/$out.wasm"; }

link astrometry-engine                     $ENGINE
link astrometry-engine-hostmmap            $ENGINE $HOSTMMAP # needs the host to provide an.map / an.unmap
link augment-xylist                        solver/augment-xylist-main.c solver/augment-xylist.c solver/wasi/compat.c
link kd-cone                               solver/kd-cone-main.c solver/wasi/compat.c -mexec-model=reactor #todo hostmap?
# Both engine variants import an.next_job (used only when AN_HOST_JOBS is set); the host must define it.
for f in astrometry-engine astrometry-engine-hostmmap; do   # import names are stored as plain text in the module
    grep -q next_job solver/wasm/$f.wasm || { echo "$f.wasm is missing the an.next_job import" >&2; exit 1; }
done
echo "built in solver/wasm:"; ls -l solver/wasm/*.wasm | awk '{printf "  %-48s %9d bytes\n", $9, $5}'
