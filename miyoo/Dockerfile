FROM debian:trixie
RUN apt-get update -qq && DEBIAN_FRONTEND=noninteractive apt-get install -y -qq --no-install-recommends ca-certificates curl xz-utils python3 cmake ninja-build make qemu-user binutils-arm-linux-gnueabihf && rm -rf /var/lib/apt/lists/*
ARG ZIG=0.17.1
RUN curl -fsSL https://ziglang.org/download/index.json -o /tmp/i.json \
 && python3 -c "import json;d=json.load(open('/tmp/i.json'))['$ZIG']['aarch64-linux'];open('/tmp/u','w').write(d['tarball']);open('/tmp/s','w').write(d['shasum']+'  /tmp/zig.tar.xz\n')" \
 && curl -fsSL "$(cat /tmp/u)" -o /tmp/zig.tar.xz && sha256sum -c /tmp/s \
 && mkdir /opt/zig && tar -xJf /tmp/zig.tar.xz -C /opt/zig --strip-components=1 && rm /tmp/zig.tar.xz
ENV PATH=/opt/zig:$PATH
