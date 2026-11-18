# The tools that build the Linux and Windows files sent to friends.
# tools/build-release.sh builds this image once and reuses it afterwards.
FROM alpine:3.22

# Native compiler with a static libsodium for Linux, MinGW-w64 for Windows.
RUN apk add --no-cache build-base cmake ninja pkgconf linux-headers curl \
        libsodium-dev libsodium-static mingw-w64-gcc

# libsodium for Windows: built from the official source, checked against its known hash.
ARG SODIUM_VERSION
ARG SODIUM_SHA256
RUN cd /tmp \
    && curl -fsSL -o sodium.tar.gz \
        https://github.com/jedisct1/libsodium/releases/download/${SODIUM_VERSION}-RELEASE/libsodium-${SODIUM_VERSION}.tar.gz \
    && echo "${SODIUM_SHA256}  sodium.tar.gz" | sha256sum -c - \
    && tar xzf sodium.tar.gz \
    && cd libsodium-${SODIUM_VERSION} \
    && ./configure --host=x86_64-w64-mingw32 --prefix=/usr/x86_64-w64-mingw32 \
        --disable-shared --enable-static >/dev/null \
    && make -j"$(nproc)" install >/dev/null \
    && cd /tmp && rm -rf sodium.tar.gz libsodium-${SODIUM_VERSION}
