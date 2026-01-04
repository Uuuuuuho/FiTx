FROM debian:bookworm-slim AS intermediate

# Install dependencies
RUN apt-get -qq update && \
    apt-get install -qqy --no-install-recommends \
        gnupg2 wget ca-certificates apt-transport-https \
        autoconf automake cmake dpkg-dev file make patch libc6-dev git \
        fakeroot build-essential ncurses-dev xz-utils libssl-dev bc flex \
        libelf-dev bison python3 python3-pip cpio python3-dev \
        clang-14 clang-tidy-14 clang-format-14 libc++-14-dev libc++abi-14-dev llvm-14-dev && \
    for f in /usr/lib/llvm-14/bin/*; do ln -sf "$f" /usr/bin/; done && \
    ln -sf clang-14 /usr/bin/cc && \
    ln -sf clang-14 /usr/bin/c89 && \
    ln -sf clang-14 /usr/bin/c99 && \
    ln -sf clang++-14 /usr/bin/c++ && \
    ln -sf clang++-14 /usr/bin/g++ && \
    rm -rf /var/lib/apt/lists/*

# Create Util dirs
WORKDIR /tmp/log
ADD tests /tmp/tests
ADD patches /tmp/patches

# Build FiTx
ARG project_path
# default to 1 to avoid empty make -j
ARG nproc=1
ENV LLVM_DIR=/usr/lib/llvm-14/lib/cmake/llvm
ADD src ${project_path}/src
ADD scripts ${project_path}/scripts
WORKDIR ${project_path}/build
RUN cmake -S ${project_path}/src && make -j ${nproc}
RUN pip3 install -r ${project_path}/scripts/requirements.txt --break-system-packages

# Download and configure Linux Kernel
ARG linux_path
ARG linux_tar=linux.tar.gz
ARG SKIP_KERNEL_DOWNLOAD=false
WORKDIR ${linux_path}
RUN if [ "${SKIP_KERNEL_DOWNLOAD}" != "true" ]; then \
            wget https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.15.1.tar.gz -O ${linux_tar} && \
            tar xvf ${linux_tar} && \
            mv linux*/* . && \
            rm ${linux_tar} ; \
            cp /tmp/patches/Makefile.build /linux/scripts/Makefile.build ; \
            cp ${project_path}/scripts/config .config && \
            make CC=clang HOSTCC=clang olddefconfig ; \
        else \
            echo "SKIP_KERNEL_DOWNLOAD=true: skipping linux download and olddefconfig" ; \
        fi

FROM intermediate as final
