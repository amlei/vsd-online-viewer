#!/usr/bin/env bash
# Build the patched libvisio fork and the vsd2svg converter.
#
#   tools/vsd2svg/build.sh [--clean]
#
# The result is renderer/build/vsd2svg; host applications call it either through
# $VSD2SVG_BIN or with the default path (see README.md).
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIBVISIO_VERSION="${LIBVISIO_VERSION:-0.1.11}"
TARBALL="libvisio-${LIBVISIO_VERSION}.tar.xz"
SRC_URL="https://dev-www.libreoffice.org/src/libvisio/${TARBALL}"
BUILD="$HERE/build"
SRC="$BUILD/libvisio-${LIBVISIO_VERSION}"

if [[ "${1:-}" == "--clean" ]]; then
  rm -rf "$BUILD"
fi

mkdir -p "$BUILD"

# ---------------------------------------------------------------- toolchain
if command -v brew >/dev/null 2>&1; then
  BREW_PREFIX="$(brew --prefix)"
  ICU_PREFIX="$(brew --prefix icu4c@78 2>/dev/null || brew --prefix icu4c 2>/dev/null || echo)"
  XML2_PREFIX="$(brew --prefix libxml2 2>/dev/null || echo)"
  BOOST_PREFIX="$(brew --prefix boost 2>/dev/null || echo)"
else
  BREW_PREFIX=""
  ICU_PREFIX=""
  XML2_PREFIX=""
  BOOST_PREFIX=""
fi

export PKG_CONFIG_PATH="${ICU_PREFIX:+$ICU_PREFIX/lib/pkgconfig:}${XML2_PREFIX:+$XML2_PREFIX/lib/pkgconfig:}${PKG_CONFIG_PATH:-}"
export CPPFLAGS="${BOOST_PREFIX:+-I$BOOST_PREFIX/include} ${CPPFLAGS:-}"
export LDFLAGS="${BOOST_PREFIX:+-L$BOOST_PREFIX/lib} ${LDFLAGS:-}"

# --------------------------------------------------------------- fetch+patch
if [[ ! -d "$SRC" ]]; then
  if [[ ! -f "$BUILD/$TARBALL" ]]; then
    echo "== fetching libvisio $LIBVISIO_VERSION"
    curl -fsSL -o "$BUILD/$TARBALL" "$SRC_URL"
  fi
  tar -xf "$BUILD/$TARBALL" -C "$BUILD"
  echo "== applying patches"
  for patch in "$HERE"/patches/*.patch; do
    [[ -e "$patch" ]] || continue
    echo "   $(basename "$patch")"
    ( cd "$SRC" && patch -p1 --forward --silent < "$patch" )
  done
fi

# ------------------------------------------------------------- build libvisio
if [[ ! -f "$SRC/src/lib/.libs/libvisio-0.1.dylib" && ! -f "$SRC/src/lib/.libs/libvisio-0.1.so" ]]; then
  echo "== configuring libvisio"
  ( cd "$SRC" && ./configure --prefix="$BUILD/prefix" --disable-tests >"$BUILD/configure.log" 2>&1 )
  echo "== building libvisio"
  ( cd "$SRC" && make -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" >"$BUILD/make.log" 2>&1 )
fi

LIBDIR="$SRC/src/lib/.libs"
if [[ -f "$LIBDIR/libvisio-0.1.dylib" ]]; then
  VISIO_LIB="$LIBDIR/libvisio-0.1.dylib"
  # the dylib is built with an absolute install name pointing at the throw away
  # configure prefix; rewrite it so the converter can find it through rpath
  real="$(readlink "$VISIO_LIB")"
  install_name_tool -id "@rpath/${real:-$(basename "$VISIO_LIB")}" "$LIBDIR/$(basename "$VISIO_LIB")" 2>/dev/null || true
else
  VISIO_LIB="$LIBDIR/libvisio-0.1.so"
fi

# --------------------------------------------------------------- build vsd2svg
echo "== building vsd2svg"
CXX="${CXX:-c++}"
$CXX -std=c++17 -O2 -Wall -Wno-unused-parameter \
  -I"$SRC/inc" \
  -I"$HERE/src" \
  $(pkg-config --cflags librevenge-0.0 librevenge-stream-0.0) \
  "$HERE/src/"*.cpp \
  -o "$BUILD/vsd2svg" \
  "$VISIO_LIB" \
  $(pkg-config --libs librevenge-0.0 librevenge-stream-0.0) \
  -Wl,-rpath,"$LIBDIR"

echo "== done: $BUILD/vsd2svg"
"$BUILD/vsd2svg" --help
