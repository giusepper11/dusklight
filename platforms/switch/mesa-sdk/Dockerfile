# Isolated Linux toolchain for the unmodified danfromtico Mesa Switch baseline.
# No upstream container-removal, source-copy, or build-directory cleanup script runs.
FROM devkitpro/devkita64@sha256:1fc388c3a0d34bd2045a6dadcb1020e069d5f876a187fd705de14b4440c00282

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git curl ca-certificates \
    python3 python3-venv python3-dev flex bison \
    llvm-15-dev clang-15 libclang-15-dev libclc-15-dev libclc-15 \
    spirv-tools spirv-headers libllvmspirvlib-15-dev
# The pinned base already provides switch-libexpat, switch-libzstd and
# switch-zlib. Upstream explicitly supports Horizon without optional libelf.
# Do not upgrade these packages or fabricate an absent libelf archive.

RUN python3 -m venv /opt/mesa-python && \
    /opt/mesa-python/bin/pip install --no-cache-dir \
    meson==1.9.1 Mako==1.3.10 PyYAML==6.0.3 packaging==25.0
ENV PATH="/opt/mesa-python/bin:/root/.cargo/bin:/usr/lib/llvm-15/bin:${PATH}"
ENV RUSTUP_HOME=/root/.rustup CARGO_HOME=/root/.cargo
RUN curl --fail --show-error --silent --location https://sh.rustup.rs -o /root/rustup-init.sh && \
    sh /root/rustup-init.sh -y --profile minimal --default-toolchain 1.90.0 && \
    rustup target add aarch64-unknown-linux-gnu && \
    cargo install --locked --version 0.72.1 bindgen-cli && \
    cargo install --locked --version 0.29.0 cbindgen
RUN mkdir -p /usr/local/share/mesa-sdk-toolchain && \
    dpkg-query -W > /usr/local/share/mesa-sdk-toolchain/debian-packages.txt && \
    /opt/devkitpro/pacman/bin/pacman -Q > /usr/local/share/mesa-sdk-toolchain/devkitpro-packages.txt && \
    pip freeze > /usr/local/share/mesa-sdk-toolchain/python-packages.txt && \
    rustc -Vv > /usr/local/share/mesa-sdk-toolchain/rustc.txt && \
    cargo install --list > /usr/local/share/mesa-sdk-toolchain/cargo-tools.txt
