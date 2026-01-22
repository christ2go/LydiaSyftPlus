FROM ubuntu:24.04
LABEL version="0.1.0"
LABEL authors="Shufang Zhu"
LABEL description="A Docker image to build and run the LydiaSyftPlus project."

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
       g++-multilib              \
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
    apt-get clean &&\
    rm -rf /var/cache

# This adds the 'default' user to sudoers with full privileges:
RUN HOME=/home/default && \
    mkdir -p ${HOME} && \
    GROUP_ID=1000 && \
    USER_ID=1000 && \
    groupadd -r default -f -g "$GROUP_ID" && \
    useradd -u "$USER_ID" -r -g default -d "$HOME" -s /sbin/nologin \
    -c "Default Application User" default && \
    chown -R "$USER_ID:$GROUP_ID" ${HOME} && \
    usermod -a -G sudo default && \
    echo '%sudo ALL=(ALL) NOPASSWD:ALL' >> /etc/sudoers

ENV CC=/usr/bin/gcc
ENV CXX=/usr/bin/g++
ENV CCACHE_DIR=/build/docker_ccache
ENV LD_LIBRARY_PATH=/usr/local/lib

ENV CUDD_VERSION="3.0.0"
ENV MONA_VERSION="1.4-19.dev0"

WORKDIR /build

# Install CUDD
RUN wget https://github.com/whitemech/cudd/releases/download/v${CUDD_VERSION}/cudd_${CUDD_VERSION}_linux-amd64.tar.gz &&\
    tar -xf cudd_${CUDD_VERSION}_linux-amd64.tar.gz &&\
    cd cudd_${CUDD_VERSION}_linux-amd64 &&\
    cp -P lib/* /usr/local/lib/ &&\
    cp -Pr include/* /usr/local/include/ &&\
    cd .. &&\
    rm -rf cudd_${CUDD_VERSION}_linux-amd64*

# Install MONA
RUN wget https://github.com/whitemech/MONA/releases/download/v${MONA_VERSION}/mona_${MONA_VERSION}_linux-amd64.tar.gz &&\
    tar -xf mona_${MONA_VERSION}_linux-amd64.tar.gz &&\
    cd mona_${MONA_VERSION}_linux-amd64 &&\
    cp -P lib/* /usr/local/lib/ &&\
    cp -Pr include/* /usr/local/include &&\
    cd .. &&\
    rm -rf mona_${MONA_VERSION}_linux-amd64*

# Install Syfco (required for TLSF format support)
RUN curl -sSL https://get.haskellstack.org/ | sh &&\
    git clone https://github.com/reactive-systems/syfco.git &&\
    cd syfco &&\
    git checkout 50585e0 &&\
    stack install &&\
    cd .. &&\
    rm -rf syfco /root/.stack

# Make sure syfco is in PATH
ENV PATH="/root/.local/bin:${PATH}"

# Install Z3 (for LydiaSyftPlus dependencies)
RUN wget https://github.com/Z3Prover/z3/releases/download/z3-4.8.12/z3-4.8.12-x64-glibc-2.31.zip &&\
    unzip z3-4.8.12-x64-glibc-2.31.zip &&\
    cd z3-4.8.12-x64-glibc-2.31 &&\
    cp bin/libz3.a /usr/local/lib/ &&\
    cp include/*.h /usr/local/include/ &&\
    cd .. &&\
    rm -rf z3-4.8.12-x64-glibc-2.31*

WORKDIR /build/lydiasyftplus

ARG GIT_REF=main

# Clone and build LydiaSyftPlus (includes Lydia as submodule)
RUN git clone --recursive https://github.com/christ2go/LydiaSyftPlus.git /build/lydiasyftplus

RUN git checkout ${GIT_REF} &&\
    # Build Slugs (optional, for GR(1) synthesis)
    cd submodules/slugs/src &&\
    git checkout a188d83 &&\
    make -j$(nproc) &&\
    cd /build/lydiasyftplus &&\
    # Build LydiaSyftPlus
    mkdir -p build &&\
    cd build &&\
    cmake -DCMAKE_BUILD_TYPE=Release .. &&\
    make -j$(nproc) &&\
    # Install binaries
    cp bin/LydiaSyftEL /usr/local/bin/ &&\
    cp bin/LydiaSyft /usr/local/bin/ 2>/dev/null || true &&\
    cp bin/PPLTLfPlusSynthesisEL /usr/local/bin/ 2>/dev/null || true &&\
    cd /build &&\
    # Clean up build artifacts to reduce image size
    rm -rf /build/lydiasyftplus/build

# Copy examples to user directory
RUN mkdir -p /home/default/examples &&\
    cp -r /build/lydiasyftplus/examples/* /home/default/examples/ &&\
    chown -R default:default /home/default/examples

WORKDIR /home/default

USER default

# Default command shows help
CMD ["/usr/local/bin/LydiaSyftEL", "--help"]
