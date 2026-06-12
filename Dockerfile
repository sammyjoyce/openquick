# syntax=docker/dockerfile:1.7

ARG SKIP_TESTS=0

FROM oven/bun:1 AS sdk
WORKDIR /work/sdk/js
COPY sdk/js/package.json ./
COPY sdk/js/src ./src
RUN bun install --no-save \
    && bun build src/index.ts --outfile dist/quick.js --format esm

FROM golang:1.25 AS server
ARG SKIP_TESTS=0
WORKDIR /work/server
COPY server/ ./
COPY --from=sdk /work/sdk/js/dist/quick.js ./internal/api/sdk/quick.js
RUN --mount=type=cache,target=/go/pkg/mod \
    --mount=type=cache,target=/root/.cache/go-build \
    if [ "$SKIP_TESTS" != "1" ]; then go vet ./... && go test ./...; fi
RUN --mount=type=cache,target=/go/pkg/mod \
    --mount=type=cache,target=/root/.cache/go-build \
    CGO_ENABLED=0 go build -o /out/quickd ./cmd/quickd

FROM debian:bookworm-slim AS cli
ARG TARGETARCH
ARG SKIP_TESTS=0
ARG ZIG_VERSION=0.16.0
ARG ZIG_SHA256_AMD64=70e49664a74374b48b51e6f3fdfbf437f6395d42509050588bd49abe52ba3d00
ARG ZIG_SHA256_ARM64=ea4b09bfb22ec6f6c6ceac57ab63efb6b46e17ab08d21f69f3a48b38e1534f17
RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates curl xz-utils libncurses-dev libtinfo-dev pkg-config \
    && multiarch="$(case "$(uname -m)" in aarch64) echo aarch64-linux-gnu ;; x86_64) echo x86_64-linux-gnu ;; *) uname -m ;; esac)" \
    && ln -sf "/usr/lib/${multiarch}/libncursesw.so" /usr/lib/libncursesw.so \
    && ln -sf "/usr/lib/${multiarch}/libncursesw.so.6" /usr/lib/libncursesw.so.6 \
    && ln -sf "/usr/lib/${multiarch}/libtinfo.so" /usr/lib/libtinfo.so \
    && ln -sf "/usr/lib/${multiarch}/libtinfo.so.6" /usr/lib/libtinfo.so.6 \
    && rm -rf /var/lib/apt/lists/*
RUN set -eux; \
    case "$TARGETARCH" in \
      amd64) zig_arch=x86_64; zig_sha="$ZIG_SHA256_AMD64" ;; \
      arm64) zig_arch=aarch64; zig_sha="$ZIG_SHA256_ARM64" ;; \
      *) echo "unsupported TARGETARCH=$TARGETARCH" >&2; exit 1 ;; \
    esac; \
    zig_file="zig-${zig_arch}-linux-${ZIG_VERSION}.tar.xz"; \
    zig_url="https://ziglang.org/download/${ZIG_VERSION}/${zig_file}"; \
    curl -fsSLo "/tmp/${zig_file}" "$zig_url"; \
    echo "${zig_sha}  /tmp/${zig_file}" | sha256sum -c -; \
    mkdir -p /opt/zig; \
    tar -xJf "/tmp/${zig_file}" -C /opt/zig --strip-components=1; \
    ln -s /opt/zig/zig /usr/local/bin/zig; \
    rm "/tmp/${zig_file}"; \
    zig version
WORKDIR /work
COPY build.zig build.zig.zon opencli.json ./
COPY src/ ./src/
COPY registry/ ./registry/
COPY tools/ ./tools/
COPY test/ ./test/
# libncurses-dev is installed, but Zig/lld does not reliably resolve Debian
# bookworm's ncurses linker script transitive -ltinfo inside this slim image.
# Build the CLI with the TUI and terminfo styling disabled so Linux container
# builds remain deterministic; the smoke test still covers all non-interactive
# CLI flows. PTY terminal scenarios are disabled because libghostty-vt is not
# part of this image.
RUN zig build -Doptimize=ReleaseSafe -Denable-tui=false -Dterminal-backend=none -Dcli-terminfo=disabled \
    && if [ "$SKIP_TESTS" != "1" ]; then zig build test -Doptimize=ReleaseSafe -Denable-tui=false -Dterminal-backend=none -Dcli-terminfo=disabled; fi \
    && cp zig-out/bin/quick /out-quick

FROM debian:bookworm-slim AS runtime
RUN apt-get update \
    && apt-get install -y --no-install-recommends ca-certificates rsync openssh-client curl zip \
    && rm -rf /var/lib/apt/lists/*
COPY --from=server /out/quickd /usr/local/bin/quickd
COPY --from=cli /out-quick /usr/local/bin/quick
RUN mkdir -p /srv/quick /etc/openquick
# quickd intentionally rejects unauthenticated/dev IAP on non-loopback listeners
# unless --allow-public-unsafe is passed. The container smoke test publishes port
# 9366, so the config listens on 0.0.0.0 and the entrypoint includes that
# explicit test-only override. iap.type=dev plus --identity provides an
# authenticated synthetic user for DB/upload APIs; viewer anonymous access stays
# enabled for static reads without an external IAP.
RUN cat > /etc/openquick/quickd.json <<'EOF'
{
  "listen": "0.0.0.0:9366",
  "remote_root": "/srv/quick",
  "iap": { "type": "dev" },
  "viewer": { "allow_anonymous": true },
  "directory": { "enabled": true }
}
EOF
EXPOSE 9366
ENTRYPOINT ["quickd", "serve", "--config", "/etc/openquick/quickd.json", "--identity", "container-smoke@example.invalid", "--allow-public-unsafe"]
