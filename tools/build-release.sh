#!/bin/sh
# Builds the programs the way they are sent to friends: nothing to install.
#
#   tools/build-release.sh linux     static binaries (needs Docker)
#   tools/build-release.sh windows   .exe files, cross-compiled with MinGW-w64 (needs Docker)
#   tools/build-release.sh macos     binaries for both Apple and Intel Macs; run it on a Mac
#
# Results appear in dist/:
#   poher-<system>/          the game: the program and its folder of avatars
#   poher-<system>.zip       the same folder packed for sending (.tar.gz on Linux and macOS)
#   poher-nettest-<system>   the network check, a single file
# The first Linux or Windows build downloads the compilers (about 400 MB)
# into a Docker image; later builds reuse it.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
IMAGE=poher-build

SODIUM_VERSION=1.0.22
SODIUM_SHA256=adbdd8f16149e81ac6078a03aca6fc03b592b89ef7b5ed83841c086191be3349
SODIUM_URL=https://github.com/jedisct1/libsodium/releases/download/$SODIUM_VERSION-RELEASE/libsodium-$SODIUM_VERSION.tar.gz

# Runs this same script inside the build image, with the project at /src and dist/ at /out.
in_container() {
    docker build -t $IMAGE \
        --build-arg SODIUM_VERSION=$SODIUM_VERSION --build-arg SODIUM_SHA256=$SODIUM_SHA256 \
        -f "$ROOT/tools/release.Dockerfile" "$ROOT/tools"
    mkdir -p "$ROOT/dist"
    docker run --rm \
        -v "$ROOT:/src:ro" -v "$ROOT/dist:/out" \
        -e OWNER="$(id -u):$(id -g)" \
        $IMAGE sh /src/tools/build-release.sh "$1"
}

# Puts the game and its avatars into one folder: game_folder <built program> <folder> <name inside>
game_folder() {
    rm -rf "$2"
    mkdir -p "$2"
    cp "$1" "$2/$3"
    cp -R "$SOURCE/avatars" "$2/avatars"
}

case "${1:-}" in
linux)
    in_container inside-linux
    tar -C "$ROOT/dist" -czf "$ROOT/dist/poher-linux.tar.gz" poher-linux
    ;;

windows)
    in_container inside-windows
    rm -f "$ROOT/dist/poher-windows.zip"
    if command -v zip >/dev/null; then
        (cd "$ROOT/dist" && zip -qr poher-windows.zip poher-windows)
    else
        (cd "$ROOT/dist" && python3 -m zipfile -c poher-windows.zip poher-windows)
    fi
    ;;

inside-linux)
    SOURCE=/src
    cmake -S /src -B /work -G Ninja -DPOHER_STATIC=ON
    cmake --build /work --target poher poher-nettest
    strip /work/poher /work/poher-nettest
    cp /work/poher-nettest /out/poher-nettest-linux
    game_folder /work/poher /out/poher-linux poher
    chown -R "$OWNER" /out/poher-nettest-linux /out/poher-linux
    ;;

inside-windows)
    SOURCE=/src
    PKG_CONFIG_LIBDIR=/usr/x86_64-w64-mingw32/lib/pkgconfig \
        cmake -S /src -B /work -G Ninja -DPOHER_STATIC=ON -DCMAKE_TOOLCHAIN_FILE=/src/cmake/mingw-w64.cmake
    cmake --build /work --target poher poher-nettest
    x86_64-w64-mingw32-strip /work/poher.exe /work/poher-nettest.exe
    cp /work/poher-nettest.exe /out/poher-nettest-windows.exe
    game_folder /work/poher.exe /out/poher-windows poher.exe
    chown -R "$OWNER" /out/poher-nettest-windows.exe /out/poher-windows
    ;;

macos)
    WORK=$(mktemp -d)
    MINIMUM=11.0
    cd "$WORK"
    curl -fsSL -o sodium.tar.gz "$SODIUM_URL"
    echo "$SODIUM_SHA256  sodium.tar.gz" | shasum -a 256 -c -
    tar xzf sodium.tar.gz
    # libsodium once per processor type, then both glued into one library.
    for arch in arm64 x86_64; do
        cp -R libsodium-$SODIUM_VERSION sodium-src-$arch
        (
            cd sodium-src-$arch
            host=$arch
            [ "$arch" = arm64 ] && host=aarch64
            ./configure --host=$host-apple-darwin --prefix="$WORK/sodium-$arch" --disable-shared --enable-static \
                CFLAGS="-O2 -arch $arch -mmacosx-version-min=$MINIMUM" LDFLAGS="-arch $arch" >/dev/null
            make -j"$(sysctl -n hw.ncpu)" install >/dev/null
        )
    done
    lipo -create "$WORK/sodium-arm64/lib/libsodium.a" "$WORK/sodium-x86_64/lib/libsodium.a" \
        -output "$WORK/libsodium-universal.a"
    cp "$WORK/libsodium-universal.a" "$WORK/sodium-arm64/lib/libsodium.a"

    PKG_CONFIG_PATH="$WORK/sodium-arm64/lib/pkgconfig" \
        cmake -S "$ROOT" -B "$WORK/build" -DPOHER_STATIC=ON \
        "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=$MINIMUM
    cmake --build "$WORK/build" --target poher poher-nettest -j"$(sysctl -n hw.ncpu)"
    strip "$WORK/build/poher" "$WORK/build/poher-nettest"
    mkdir -p "$ROOT/dist"
    cp "$WORK/build/poher-nettest" "$ROOT/dist/poher-nettest-macos"
    SOURCE=$ROOT
    game_folder "$WORK/build/poher" "$ROOT/dist/poher-macos" poher
    tar -C "$ROOT/dist" -czf "$ROOT/dist/poher-macos.tar.gz" poher-macos
    lipo -info "$ROOT/dist/poher-macos/poher"
    ;;

*)
    sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
    ;;
esac
