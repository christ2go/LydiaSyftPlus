FROM debian:bookworm
LABEL version="0.1.0"
LABEL authors="Shufang Zhu"
LABEL description="A Docker image to build and run the LydiaSyftPlusEL project."

ENV DEBIAN_FRONTEND=noninteractive
ENV LC_ALL=C.UTF-8
ENV LANG=C.UTF-8

# Install system dependencies
RUN apt-get update && \
    apt-get upgrade -y && \
    apt-get install -y           \
       automake                  \
       build-essential           \
       clang                     \
       gcc                       \
       g++                       \
       gdb                       \
       clang-tidy                \
       clang-format              \
       gcovr                     \
       llvm                      \
       sudo                      \
       make                      \
       cmake                     \
       git                       \
       less                      \
       wget                      \
       curl                      \
       unzip                     \
       flex                      \
       bison                     \
       libtool                   \
       graphviz                  \
       libgraphviz-dev           \
       libboost-all-dev &&       \
       mona
    apt-get clean &&\
    rm -rf /var/cache

ENV CC=/usr/bin/gcc
ENV CXX=/usr/bin/g++
ENV CCACHE_DIR=/build/docker_ccache
ENV LD_LIBRARY_PATH=/usr/local/lib

ENV CUDD_VERSION="3.0.0"
ENV MONA_VERSION="1.4-19.dev0"

WORKDIR /build

# Build CUDD from source
RUN wget https://github.com/whitemech/cudd/archive/refs/tags/v${CUDD_VERSION}.zip -O cudd.zip &&\
    unzip cudd.zip &&\
    cd cudd-${CUDD_VERSION} &&\
    ./configure --enable-obj --enable-shared --enable-static --prefix=/usr/local &&\
    make -j$(nproc) &&\
    make install &&\
    cd .. &&\
    rm -rf cudd-${CUDD_VERSION} cudd.zip

# Build MONA from source
RUN git clone https://github.com/whitemech/MONA.git &&\
    cd MONA &&\
    git checkout v${MONA_VERSION} &&\
    autoreconf -i &&\
    ./configure --prefix=/usr/local &&\
    make -j$(nproc) &&\
    make install &&\
    # Manually ensure all headers are installed
    mkdir -p /usr/local/include/mona &&\
    cp Mem/*.h /usr/local/include/mona/ &&\
    cp BDD/*.h /usr/local/include/mona/ &&\
    cp DFA/*.h /usr/local/include/mona/ &&\
    cp GTA/*.h /usr/local/include/mona/ &&\
    # Fix pointer type issue in ALL hash.h files - replace 'pointer' with 'void *'
    find /usr/local/include/mona -name "hash.h" -exec sed -i 's/pointer data;/void * data;/g' {} \; &&\
    find /usr/local/include/mona -name "hash.h" -exec sed -i 's/, pointer);/, void *);/g' {} \; &&\
    find /usr/local/include/mona -name "hash.h" -exec sed -i 's/^pointer /void * /g' {} \; &&\
    # Verify the fix was applied
    grep -n "void \*" /usr/local/include/mona/hash.h | head -3 &&\
    # Verify critical headers are present
    ls -la /usr/local/include/mona/mem.h &&\
    ls -la /usr/local/include/mona/hash.h &&\
    cd .. &&\
    rm -rf MONA

# Build Z3 from source (for ARM64 compatibility)

WORKDIR /build/lydiasyftplus

ARG GIT_REF=main

# Clone and build LydiaSyftPlus (includes Lydia as submodule)
RUN git clone --recursive https://github.com/christ2go/LydiaSyftPlus.git /build/lydiasyftplus

RUN git checkout ${GIT_REF} &&\
    cd /build/lydiasyftplus &&\
    # Fix Lydia's CMakeLists.txt - the LYDIA_ENABLE_TESTS logic is inverted!
    sed -i 's/if (NOT LYDIA_ENABLE_TESTS)/if (LYDIA_ENABLE_TESTS)/' submodules/lydia/CMakeLists.txt &&\
    # Fix MONA header include order - mem.h must come before hash.h
    sed -i '/#include <mona\/hash.h>/d' submodules/lydia/lib/include/lydia/mona_ext/mona_ext_base.hpp &&\
    sed -i '/#include <mona\/mem.h>/a #include <mona/hash.h>' submodules/lydia/lib/include/lydia/mona_ext/mona_ext_base.hpp &&\
    # Build LydiaSyftPlus
    mkdir -p build &&\
    cd build &&\
    cmake -DCMAKE_BUILD_TYPE=Release \
          -DBUILD_TESTING=OFF \
          -DLYDIA_ENABLE_TESTS=OFF \
          -DLYDIA_ENABLE_BENCHMARK=OFF .. &&\
    make -j$(nproc) &&\
    # Verify binaries exist before installing
    ls -la bin/ &&\
    # Install binaries (fail if main binary doesn't exist)
    cp bin/LydiaSyftEL /usr/local/bin/LydiaSyftEL &&\
    chmod +x /usr/local/bin/LydiaSyftEL &&\
    # Copy optional binaries if they exist
    (cp bin/LydiaSyft /usr/local/bin/ 2>/dev/null || true) &&\
    (cp bin/PPLTLfPlusSynthesisEL /usr/local/bin/ 2>/dev/null || true) &&\
    # Verify installation
    ls -la /usr/local/bin/LydiaSyftEL &&\
    # Copy examples for later use
    mkdir -p /examples &&\
    cp -r /build/lydiasyftplus/examples/* /examples/ &&\
    # Clean up build artifacts to reduce image size
    cd /build &&\
    rm -rf /build/lydiasyftplus/build

# Copy examples to user directory
RUN mkdir -p /root/examples &&\
    cp -r /examples/* /root/examples/

WORKDIR /root

# Default command shows help
CMD ["/usr/local/bin/LydiaSyftEL", "--help"]
