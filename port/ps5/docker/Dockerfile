# SpeedBreaker PS5: the Linux tools PS5_Vulkan's RADV, payload SDK and
# clean-room libc builds need (port/ps5/tools/setup-ps5-vulkan.sh runs them in
# it; nothing of this image ends up in the app).
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates git curl wget unzip zip xz-utils rsync build-essential cmake ninja-build \
        pkg-config python3 python3-pip python3-venv python3-mako python3-yaml python3-ply python3-packaging \
        clang-18 lld-18 llvm-18 llvm-18-dev libclang-18-dev libllvmspirvlib-18-dev libclc-18-dev libclc-18 \
        libzstd-dev zlib1g-dev libexpat1-dev bison flex spirv-tools glslang-tools \
    && rm -rf /var/lib/apt/lists/*
RUN python3 -m venv --system-site-packages /opt/meson && /opt/meson/bin/pip install --no-cache-dir meson==1.9.0
ENV PATH=/opt/meson/bin:/usr/lib/llvm-18/bin:$PATH
ENV LLVM_CONFIG=/usr/bin/llvm-config-18 CC=clang-18 CXX=clang++-18
