#!/usr/bin/env bash
# Build native dependencies for the Espressif QEMU fork from source.
# Sources are fetched from GitHub (the only reachable host).
# Installed under work/deps (prefix). No autotools on this host: pcre2 and
# libffi are built manually with canned config headers (x86_64 linux).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
WORK="$ROOT/work"
SRC="$WORK/src"
PREFIX="$WORK/deps"
NPROC="$(nproc)"
export PATH="$HOME/.local/bin:$PREFIX/bin:$PATH"
export PKG_CONFIG_SYSTEM_INCLUDE_PATH=""
export PKG_CONFIG_SYSTEM_LIBRARY_PATH=""
export PKG_CONFIG_PATH="$PREFIX/lib/pkgconfig:${PKG_CONFIG_PATH:-}"
export CFLAGS="-O2 -fPIC"
export CXXFLAGS="-O2 -fPIC"

mkdir -p "$SRC" "$PREFIX/lib/pkgconfig" "$PREFIX/include"

fetch() { # name url
  local name="$1" url="$2"
  if [ ! -e "$SRC/$name/.fetched" ]; then
    echo ">>> fetching $name"
    rm -rf "$SRC/$name"; mkdir -p "$SRC/$name"
    curl -sSL "$url" -o "$SRC/$name.tar.gz"
    tar -xf "$SRC/$name.tar.gz" -C "$SRC/$name" --strip-components=1
    rm -f "$SRC/$name.tar.gz"
    touch "$SRC/$name/.fetched"
  fi
}

fetch pkgconf https://codeload.github.com/pkgconf/pkgconf/tar.gz/refs/tags/pkgconf-3.0.11
fetch zlib    https://codeload.github.com/madler/zlib/tar.gz/refs/tags/v1.3.1
fetch libffi  https://codeload.github.com/libffi/libffi/tar.gz/refs/tags/v3.4.6
fetch pcre2   https://codeload.github.com/PCRE2Project/pcre2/tar.gz/refs/tags/pcre2-10.43
fetch glib    https://codeload.github.com/GNOME/glib/tar.gz/refs/tags/2.78.6
fetch gpg-error https://codeload.github.com/Distrotech/libgpg-error/tar.gz/refs/heads/master
fetch gcrypt    https://codeload.github.com/Distrotech/libgcrypt/tar.gz/refs/heads/distrotech-libgcrypt

# glib's gvdb subproject normally clones from gitlab.gnome.org (unreachable);
# pre-seed it from the GNOME GitHub mirror at the pinned revision.
if [ ! -e "$SRC/glib/subprojects/gvdb/meson.build" ]; then
  rm -rf "$SRC/gvdb" "$SRC/glib/subprojects/gvdb"
  git clone --depth 1 https://github.com/GNOME/gvdb.git "$SRC/gvdb" >/dev/null 2>&1
  (cd "$SRC/gvdb" && git fetch --depth 1 origin 0854af0fdb6d527a8d1999835ac2c5059976c210 >/dev/null 2>&1 && git checkout -q 0854af0fdb6d527a8d1999835ac2c5059976c210)
  cp -r "$SRC/gvdb" "$SRC/glib/subprojects/gvdb"
  rm -rf "$SRC/glib/subprojects/gvdb/.git"
fi

# ---------------------------------------------------------------- pkgconf
echo ">>> building pkgconf"
if [ ! -x "$PREFIX/bin/pkgconf" ]; then
  (cd "$SRC/pkgconf" && meson setup _b --prefix="$PREFIX" --libdir=lib --buildtype=release -Ddefault_library=static >/dev/null \
    && ninja -C _b >/dev/null && ninja -C _b install >/dev/null)
  ln -sf pkgconf "$PREFIX/bin/pkg-config"
fi

# ---------------------------------------------------------------- zlib
echo ">>> building zlib"
if ! ls "$PREFIX"/lib/libz.a >/dev/null 2>&1; then
  (cd "$SRC/zlib" && ./configure --prefix="$PREFIX" >/dev/null && make -j"$NPROC" >/dev/null && make install >/dev/null)
fi

# ---------------------------------------------------------------- pcre2 (manual)
echo ">>> building pcre2"
if ! ls "$PREFIX"/lib/libpcre2-8.* >/dev/null 2>&1; then
  P="$SRC/pcre2"
  mkdir -p "$P/build"
  cp "$P/src/config.h.generic" "$P/build/config.h"
  echo "#define SUPPORT_JIT 1" >> "$P/build/config.h"
  cp "$P/src/pcre2.h.generic" "$P/build/pcre2.h"
  cp "$P/src/pcre2_chartables.c.dist" "$P/build/pcre2_chartables.c"
  SRCS="pcre2_auto_possess.c pcre2_chkdint.c pcre2_compile.c pcre2_config.c pcre2_context.c
        pcre2_convert.c pcre2_dfa_match.c pcre2_error.c pcre2_extuni.c pcre2_find_bracket.c
        pcre2_fold.c pcre2_fullinfo.c pcre2_get_error_message.c pcre2_jit_misc.c
        pcre2_maketables.c pcre2_match.c pcre2_match_data.c pcre2_newline.c pcre2_ord2utf.c
        pcre2_pattern_info.c pcre2_script_run.c pcre2_serialize.c pcre2_string_utils.c
        pcre2_study.c pcre2_substitute.c pcre2_substring.c pcre2_tables.c pcre2_ucd.c
        pcre2_valid_utf.c pcre2_xclass.c pcre2_jit_compile.c"
  (cd "$P/build" && for f in $SRCS; do gcc $CFLAGS -DHAVE_CONFIG_H -DPCRE2_CODE_UNIT_WIDTH=8 -I. -I"$P/src" -c "$P/src/$f" -o "${f%.c}.o"; done \
    && gcc $CFLAGS -DHAVE_CONFIG_H -DPCRE2_CODE_UNIT_WIDTH=8 -I. -I"$P/src" -c pcre2_chartables.c -o pcre2_chartables.o \
    && ar rcs libpcre2-8.a *.o \
    && gcc $CFLAGS -shared -o libpcre2-8.so.0.12.0 -Wl,-soname,libpcre2-8.so.0 *.o)
  cp "$P/build/libpcre2-8.a" "$PREFIX/lib/"
  cp "$P/build/libpcre2-8.so.0.12.0" "$PREFIX/lib/"
  ln -sf libpcre2-8.so.0.12.0 "$PREFIX/lib/libpcre2-8.so.0"
  ln -sf libpcre2-8.so.0 "$PREFIX/lib/libpcre2-8.so"
  cp "$P/build/pcre2.h" "$PREFIX/include/"
  cat > "$PREFIX/lib/pkgconfig/libpcre2-8.pc" <<EOF
prefix=$PREFIX
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: libpcre2-8
Description: PCRE2 - Perl compatible regular expressions (8 bit)
Version: 10.43
Libs: -L\${libdir} -lpcre2-8
Cflags: -I/home/user/ESP32-Playground/work/deps/include
EOF
fi

# ---------------------------------------------------------------- libffi (manual, x86_64)
echo ">>> building libffi"
if ! ls "$PREFIX"/lib/libffi.* >/dev/null 2>&1; then
  F="$SRC/libffi"
  mkdir -p "$F/build"
  sed -e 's/@VERSION@/3.4.6/g' -e 's/@TARGET@/X86_64/g' -e 's/@HAVE_LONG_DOUBLE@/1/g' -e 's/@FFI_EXEC_TRAMPOLINE_TABLE@/0/g' "$F/include/ffi.h.in" > "$F/build/ffi.h"
  cp "$F/src/x86/ffitarget.h" "$F/build/"
  cat > "$F/build/fficonfig.h" <<'EOF'
#ifndef FFICONFIG_H
#define FFICONFIG_H
#define HAVE_ALLOCA 1
#define HAVE_ALLOCA_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_MEMORY_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_UNISTD_H 1
#define HAVE_DLFCN_H 1
#define HAVE_MEMCPY 1
#define HAVE_MEMMOVE 1
#define HAVE_LONG_DOUBLE 1
#define HAVE_LONG_DOUBLE_VARIANT 0
#define HAVE_INT128_T 1
#define SIZEOF_DOUBLE 8
#define SIZEOF_LONG_DOUBLE 16
#define SIZEOF_SIZE_T 8
#define SYMBOL_UNDERSCORE 0
#define FFI_EXEC_TRAMPOLINE_TABLE 0
#define FFI_MMAP_EXEC_WRIT 0
#define FFI_CLOSURES 1
#define EH_FRAME_FLAGS "a"
#define HAVE_AS_X86_PCREL 1
#define HAVE_AS_X86_UTF8 1
#define HAVE_RO_EH_FRAME 1
#define HAVE_HIDDEN_VISIBILITY_ATTRIBUTE 1

#ifdef HAVE_HIDDEN_VISIBILITY_ATTRIBUTE
#ifdef LIBFFI_ASM
#ifdef __APPLE__
#define FFI_HIDDEN(name) .private_extern name
#else
#define FFI_HIDDEN(name) .hidden name
#endif
#else
#define FFI_HIDDEN __attribute__ ((visibility ("hidden")))
#endif
#else
#ifdef LIBFFI_ASM
#define FFI_HIDDEN(name)
#else
#define FFI_HIDDEN
#endif
#endif

#endif
EOF
  (cd "$F/build" && \
    gcc $CFLAGS -I. -I"$F/include" -I"$F/src/x86" -DHAVE_FFICONFIG_H -DFFI_BUILDING -DX86_64 -c "$F/src/prep_cif.c" -o prep_cif.o && \
    gcc $CFLAGS -I. -I"$F/include" -I"$F/src/x86" -DHAVE_FFICONFIG_H -DFFI_BUILDING -DX86_64 -c "$F/src/types.c" -o types.o && \
    gcc $CFLAGS -I. -I"$F/include" -I"$F/src/x86" -DHAVE_FFICONFIG_H -DFFI_BUILDING -DX86_64 -c "$F/src/raw_api.c" -o raw_api.o && \
    gcc $CFLAGS -I. -I"$F/include" -I"$F/src/x86" -DHAVE_FFICONFIG_H -DFFI_BUILDING -DX86_64 -c "$F/src/java_raw_api.c" -o java_raw_api.o && \
    gcc $CFLAGS -I. -I"$F/include" -I"$F/src/x86" -DHAVE_FFICONFIG_H -DFFI_BUILDING -DX86_64 -c "$F/src/closures.c" -o closures.o && \
    gcc $CFLAGS -I. -I"$F/include" -I"$F/src/x86" -DHAVE_FFICONFIG_H -DFFI_BUILDING -DX86_64 -c "$F/src/tramp.c" -o tramp.o && \
    gcc $CFLAGS -I. -I"$F/include" -I"$F/src/x86" -DHAVE_FFICONFIG_H -DFFI_BUILDING -DX86_64 -c "$F/src/x86/ffi64.c" -o ffi64.o && \
    gcc $CFLAGS -I. -I"$F/include" -I"$F/src/x86" -DHAVE_FFICONFIG_H -DFFI_BUILDING -DX86_64 -c "$F/src/x86/ffi.c" -o ffi32.o && \
    gcc $CFLAGS -I. -I"$F/include" -I"$F/src/x86" -DHAVE_FFICONFIG_H -DFFI_BUILDING -DX86_64 -c "$F/src/x86/ffiw64.c" -o ffiw64.o && \
    gcc $CFLAGS -I. -I"$F/include" -I"$F/src/x86" -DHAVE_FFICONFIG_H -DFFI_BUILDING -DLIBFFI_ASM -DX86_64 -c "$F/src/x86/win64.S" -o win64.o && \
    gcc $CFLAGS -I. -I"$F/include" -I"$F/src/x86" -DHAVE_FFICONFIG_H -DFFI_BUILDING -DLIBFFI_ASM -DX86_64 -c "$F/src/x86/unix64.S" -o unix64.o && \
    gcc $CFLAGS -I. -I"$F/include" -I"$F/src/x86" -DHAVE_FFICONFIG_H -DFFI_BUILDING -DLIBFFI_ASM -DX86_64 -c "$F/src/x86/sysv.S" -o sysv.o && \
    ar rcs libffi.a *.o && \
    gcc $CFLAGS -shared -o libffi.so.8.1.4 -Wl,-soname,libffi.so.8 *.o)
  cp "$F/build/libffi.a" "$PREFIX/lib/"
  cp "$F/build/libffi.so.8.1.4" "$PREFIX/lib/"
  ln -sf libffi.so.8.1.4 "$PREFIX/lib/libffi.so.8"
  ln -sf libffi.so.8 "$PREFIX/lib/libffi.so"
  cp "$F/build/ffi.h" "$F/build/ffitarget.h" "$PREFIX/include/"
  cat > "$PREFIX/lib/pkgconfig/libffi.pc" <<EOF
prefix=$PREFIX
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: libffi
Description: Library supporting Foreign Function Interfaces
Version: 3.4.6
Libs: -L\${libdir} -lffi
Cflags: -I/home/user/ESP32-Playground/work/deps/include
EOF
fi

# ---------------------------------------------------------------- glib
echo ">>> building glib"
if ! pkg-config --exists glib-2.0 2>/dev/null; then
  (cd "$SRC/glib" && meson setup _b --prefix="$PREFIX" --libdir=lib --buildtype=release \
      -Dtests=false -Dnls=disabled -Dglib_debug=disabled -Dlibelf=disabled \
      -Dman=false -Dgtk_doc=false \
    && ninja -C _b && ninja -C _b install) || { echo "GLIB BUILD FAILED"; exit 1; }
fi

echo ">>> building gpg-error"
if ! ls "$PREFIX"/lib/libgpg-error.so* >/dev/null 2>&1; then
  (cd "$SRC/gpg-error" && ./configure --prefix="$PREFIX" --enable-shared --disable-static --disable-doc --disable-tests >/dev/null && make -j"$NPROC" >/dev/null && make install >/dev/null) || { echo "GPG-ERROR BUILD FAILED"; exit 1; }
fi

echo ">>> building libgcrypt"
if ! ls "$PREFIX"/lib/libgcrypt.so* >/dev/null 2>&1; then
  (cd "$SRC/gcrypt" && ./configure --prefix="$PREFIX" --enable-shared --disable-static --disable-doc --with-libgpg-error-prefix="$PREFIX" >/dev/null && make -j"$NPROC" >/dev/null && make install >/dev/null) || { echo "LIBGCRYPT BUILD FAILED"; exit 1; }
fi

echo ">>> deps done"
pkg-config --modversion glib-2.0
ls "$PREFIX/lib" | head -40
