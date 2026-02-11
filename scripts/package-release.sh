#!/usr/bin/env bash
##
## NeuronOS — Release Packaging Script
##
## Builds a self-contained release tarball for the current platform.
## The binary is statically linked (BUILD_SHARED_LIBS=OFF), so no .so files
## are needed — only system libraries (libc, libm, libstdc++, libgomp).
##
## Usage: ./scripts/package-release.sh [version] [build_dir]
##   version:   e.g. "0.9.0" (default: reads from neuronos.h)
##   build_dir: path to cmake build dir (default: auto-detect)
##
## Output: neuronos-v${VERSION}-${OS}-${ARCH}.tar.gz
##
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ── Colors ──
RED='\033[0;31m'; GREEN='\033[0;32m'; NC='\033[0m'
info()  { echo "[info]  $*"; }
ok()    { echo -e "${GREEN}[ok]${NC}    $*"; }
die()   { echo -e "${RED}[error]${NC} $*" >&2; exit 1; }

# ── Detect version from header if not provided ──
if [ -n "${1:-}" ]; then
    VERSION="$1"
else
    VERSION=$(grep -oP '#define NEURONOS_VERSION\s+"\K[^"]+' \
        "$REPO_ROOT/neuronos/include/neuronos/neuronos.h" 2>/dev/null || echo "0.0.0")
fi

# ── Build dir — find it ──
if [ -n "${2:-}" ]; then
    BUILD_DIR="$2"
elif [ -d "$REPO_ROOT/build" ]; then
    BUILD_DIR="$REPO_ROOT/build"
elif [ -d "$REPO_ROOT/../build" ]; then
    BUILD_DIR="$(cd "$REPO_ROOT/../build" && pwd)"
else
    die "Build directory not found. Pass it as second argument."
fi

# ── Detect platform ──
OS="$(uname -s | tr '[:upper:]' '[:lower:]')"
ARCH="$(uname -m)"

case "$OS" in
    darwin) OS="macos" ;;
    linux)  OS="linux" ;;
    *)      die "Unsupported OS: $OS" ;;
esac

case "$ARCH" in
    x86_64|amd64)   ARCH="x86_64" ;;
    aarch64|arm64)   ARCH="arm64" ;;
    *)               die "Unsupported arch: $ARCH" ;;
esac

RELEASE_NAME="neuronos-v${VERSION}-${OS}-${ARCH}"
STAGING_DIR="/tmp/${RELEASE_NAME}"
OUTPUT_FILE="${REPO_ROOT}/${RELEASE_NAME}.tar.gz"

echo "════════════════════════════════════════════"
echo "  NeuronOS Release Packager"
echo "════════════════════════════════════════════"
echo "  Version:  v${VERSION}"
echo "  Platform: ${OS}-${ARCH}"
echo "  Build:    ${BUILD_DIR}"
echo "  Output:   ${OUTPUT_FILE}"
echo "════════════════════════════════════════════"
echo ""

# ── Verify binary exists ──
NEURONOS_CLI="$BUILD_DIR/bin/neuronos-cli"
if [ ! -f "$NEURONOS_CLI" ]; then
    die "Binary not found: $NEURONOS_CLI
    Build first: cmake -B build -S neuronos -DBUILD_SHARED_LIBS=OFF && cmake --build build -j\$(nproc)"
fi

# ── Verify it is statically linked (no libllama.so dependency) ──
if [ "$OS" = "linux" ] && command -v ldd &>/dev/null; then
    if ldd "$NEURONOS_CLI" 2>&1 | grep -q "libllama"; then
        die "Binary is dynamically linked to libllama.so!
    Rebuild with: cmake -B build -S neuronos -DBUILD_SHARED_LIBS=OFF
    The release binary MUST be statically linked."
    fi
    ok "Binary is statically linked (no libllama/libggml deps)"
fi

# ── Create staging directory ──
rm -rf "$STAGING_DIR"
mkdir -p "$STAGING_DIR"/{bin,share/neuronos/grammars}

# ── Copy binary ──
info "Copying neuronos-cli..."
cp "$NEURONOS_CLI" "$STAGING_DIR/bin/"
strip "$STAGING_DIR/bin/neuronos-cli" 2>/dev/null || true
ok "Binary: $(du -h "$STAGING_DIR/bin/neuronos-cli" | cut -f1) (stripped)"

# ── Copy grammars ──
GRAMMAR_DIR=""
for d in "$REPO_ROOT/grammars" "$REPO_ROOT/neuronos/grammars"; do
    if [ -d "$d" ]; then GRAMMAR_DIR="$d"; break; fi
done

if [ -n "$GRAMMAR_DIR" ]; then
    info "Copying grammars from $GRAMMAR_DIR..."
    cp "$GRAMMAR_DIR"/*.gbnf "$STAGING_DIR/share/neuronos/grammars/" 2>/dev/null || true
    count=$(ls -1 "$STAGING_DIR/share/neuronos/grammars/"*.gbnf 2>/dev/null | wc -l)
    ok "Grammars: $count files"
else
    info "No grammar directory found, skipping"
fi

# ── Create README ──
cat > "$STAGING_DIR/README.md" << 'README_EOF'
# NeuronOS

Universal AI agent engine for edge devices — "The Android of AI"

## Quick Start

    # Run with a BitNet model
    ./bin/neuronos-cli <model.gguf> run "Hello"

    # Agent mode (with tools)
    ./bin/neuronos-cli <model.gguf> agent "List files in /tmp"

    # Interactive REPL
    ./bin/neuronos-cli <model.gguf> chat

    # HTTP server (OpenAI-compatible API)
    ./bin/neuronos-cli <model.gguf> serve --port 8080

    # MCP server (for Claude Desktop, VS Code, etc.)
    ./bin/neuronos-cli <model.gguf> mcp

## Download a Model

    # BitNet b1.58 2B — recommended for edge (1.7 GB)
    curl -L -o model.gguf \
      https://huggingface.co/microsoft/BitNet-b1.58-2B-4T-gguf/resolve/main/ggml-model-i2_s.gguf

## System Requirements

  - Linux x86_64 or ARM64 / macOS ARM64
  - ~2 GB RAM for BitNet 2B model
  - No GPU required (CPU-optimized ternary inference)

## More Info

  https://github.com/Neuron-OS/neuronos
README_EOF

# ── Create tarball ──
info "Creating tarball..."
cd /tmp
tar czf "$OUTPUT_FILE" "$RELEASE_NAME"
rm -rf "$STAGING_DIR"

SIZE=$(du -h "$OUTPUT_FILE" | cut -f1)
echo ""
echo "════════════════════════════════════════════"
echo -e "  ${GREEN}Done:${NC} ${OUTPUT_FILE}"
echo "  Size: ${SIZE}"
echo ""
echo "  Contents:"
tar tzf "$OUTPUT_FILE" | sed 's/^/    /'
echo "════════════════════════════════════════════"
