#!/usr/bin/env bash
##
## NeuronOS Installer
##
## Install a pre-built NeuronOS binary for your platform.
##
## Quick install (public repo or with gh CLI authenticated):
##   curl -fsSL https://raw.githubusercontent.com/Neuron-OS/neuronos/main/scripts/install.sh | bash
##
## Private repo (needs authentication — pick one):
##   # Option A: gh CLI (recommended)
##   gh auth login
##   bash <(gh api repos/Neuron-OS/neuronos/contents/scripts/install.sh --jq '.content' | base64 -d)
##
##   # Option B: GITHUB_TOKEN
##   GITHUB_TOKEN=ghp_xxx curl -fsSL -H "Authorization: token ghp_xxx" \
##     https://raw.githubusercontent.com/Neuron-OS/neuronos/main/scripts/install.sh | bash
##
## Options (env vars):
##   NEURONOS_VERSION=0.8.0     Pin specific version (default: latest release)
##   NEURONOS_INSTALL_DIR=PATH  Install location (default: ~/.neuronos)
##   NEURONOS_NO_MODEL=1        Skip model download prompt
##   GITHUB_TOKEN=ghp_xxx       Auth token for private repo access
##
set -euo pipefail

# ── Config ──
GITHUB_REPO="Neuron-OS/neuronos"
DEFAULT_INSTALL_DIR="$HOME/.neuronos"
MODEL_HF_REPO="microsoft/BitNet-b1.58-2B-4T-gguf"
MODEL_FILE="ggml-model-i2_s.gguf"

# ── Colors ──
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; BOLD='\033[1m'; NC='\033[0m'
info()  { echo -e "${BLUE}[info]${NC}  $*"; }
ok()    { echo -e "${GREEN}[ok]${NC}    $*"; }
warn()  { echo -e "${YELLOW}[warn]${NC}  $*"; }
die()   { echo -e "${RED}[error]${NC} $*" >&2; exit 1; }

# ── GitHub API helper ──
# Uses GITHUB_TOKEN, or gh CLI token, or unauthenticated
gh_api() {
    local url="$1"; shift
    local auth_header=""

    if [ -n "${GITHUB_TOKEN:-}" ]; then
        auth_header="Authorization: token ${GITHUB_TOKEN}"
    elif command -v gh &>/dev/null 2>&1; then
        local token
        token=$(gh auth token 2>/dev/null) || true
        if [ -n "$token" ]; then
            auth_header="Authorization: token ${token}"
        fi
    fi

    if [ -n "$auth_header" ]; then
        curl -fsSL -H "$auth_header" -H "Accept: application/vnd.github.v3+json" "$url" "$@"
    else
        curl -fsSL -H "Accept: application/vnd.github.v3+json" "$url" "$@"
    fi
}

# Download a release asset (binary file) — needs different Accept header for private repos
gh_download() {
    local url="$1"; local dest="$2"
    local auth_header=""

    if [ -n "${GITHUB_TOKEN:-}" ]; then
        auth_header="Authorization: token ${GITHUB_TOKEN}"
    elif command -v gh &>/dev/null 2>&1; then
        local token
        token=$(gh auth token 2>/dev/null) || true
        if [ -n "$token" ]; then
            auth_header="Authorization: token ${token}"
        fi
    fi

    if [ -n "$auth_header" ]; then
        curl -fSL -H "$auth_header" -H "Accept: application/octet-stream" \
            --progress-bar -o "$dest" "$url"
    else
        curl -fSL --progress-bar -o "$dest" "$url"
    fi
}

# ── Check prerequisites ──
check_deps() {
    for cmd in curl tar; do
        command -v "$cmd" &>/dev/null || die "Required: $cmd — please install it first."
    done
}

# ── Detect platform ──
detect_platform() {
    local os arch
    os="$(uname -s)"; arch="$(uname -m)"

    case "$os" in
        Linux)  os="linux" ;;
        Darwin) os="macos" ;;
        *)      die "Unsupported OS: $os (Linux and macOS supported)" ;;
    esac

    case "$arch" in
        x86_64|amd64)  arch="x86_64" ;;
        aarch64|arm64) arch="arm64" ;;
        *)             die "Unsupported arch: $arch (x86_64 and arm64 supported)" ;;
    esac

    PLATFORM="${os}-${arch}"
    info "Platform: ${BOLD}${PLATFORM}${NC}"
}

# ── Get latest version ──
get_latest_version() {
    if [ -n "${NEURONOS_VERSION:-}" ]; then
        VERSION="$NEURONOS_VERSION"
        info "Pinned version: v${VERSION}"
        return
    fi

    info "Checking latest release..."

    # Try GitHub Releases API
    local api_url="https://api.github.com/repos/${GITHUB_REPO}/releases/latest"
    local body
    body=$(gh_api "$api_url" 2>/dev/null) || true

    if [ -n "$body" ]; then
        VERSION=$(echo "$body" | grep -oP '"tag_name":\s*"v?\K[^"]+' | head -1)
        if [ -n "$VERSION" ]; then
            info "Latest release: ${BOLD}v${VERSION}${NC}"
            return
        fi
    fi

    # Fallback: list tags
    local tags_url="https://api.github.com/repos/${GITHUB_REPO}/tags?per_page=1"
    body=$(gh_api "$tags_url" 2>/dev/null) || true
    VERSION=$(echo "$body" | grep -oP '"name":\s*"v?\K[^"]+' | head -1)

    if [ -z "$VERSION" ]; then
        die "Cannot determine version. Set NEURONOS_VERSION=x.y.z or check auth:
  - For private repos: GITHUB_TOKEN=ghp_xxx bash install.sh
  - Or install gh CLI: https://cli.github.com"
    fi
    info "Latest tag: ${BOLD}v${VERSION}${NC}"
}

# ── Find the release asset URL for our platform ──
get_asset_url() {
    local tarball_name="neuronos-v${VERSION}-${PLATFORM}.tar.gz"
    local api_url="https://api.github.com/repos/${GITHUB_REPO}/releases/tags/v${VERSION}"

    info "Looking for ${BOLD}${tarball_name}${NC}..."

    local release_json
    release_json=$(gh_api "$api_url" 2>/dev/null) || die "Release v${VERSION} not found.
Check: https://github.com/${GITHUB_REPO}/releases"

    # Parse asset URL: use python3 for reliable JSON parsing, grep as fallback
    # We need the asset API URL (works for private repos with Accept: octet-stream)
    DOWNLOAD_URL=""
    if command -v python3 &>/dev/null; then
        DOWNLOAD_URL=$(echo "$release_json" | python3 -c "
import json, sys
try:
    data = json.load(sys.stdin)
    for a in data.get('assets', []):
        if a.get('name') == '${tarball_name}':
            print(a['url'])
            break
except: pass
" 2>/dev/null)
    fi

    # Fallback: grep for browser_download_url (works for public repos)
    if [ -z "$DOWNLOAD_URL" ]; then
        DOWNLOAD_URL=$(echo "$release_json" | \
            grep -oP '"browser_download_url":\s*"\K[^"]+' | \
            grep "${tarball_name}" | head -1)
        DOWNLOAD_IS_BROWSER=1
    else
        DOWNLOAD_IS_BROWSER=0
    fi

    if [ -z "$DOWNLOAD_URL" ]; then
        local available
        available=$(echo "$release_json" | grep -oP '"name":\s*"\K[^"]+\.tar\.gz' || echo "(none)")
        die "No binary for ${tarball_name}. Available: ${available}"
    fi

    ok "Found: ${tarball_name}"
}

# ── Download and install ──
install_neuronos() {
    local install_dir="${NEURONOS_INSTALL_DIR:-$DEFAULT_INSTALL_DIR}"
    local tarball_name="neuronos-v${VERSION}-${PLATFORM}.tar.gz"
    local tmp_dir
    tmp_dir=$(mktemp -d)
    trap "rm -rf '$tmp_dir'" EXIT

    info "Downloading ${BOLD}${tarball_name}${NC}..."

    if [ "${DOWNLOAD_IS_BROWSER:-0}" = "1" ]; then
        # Browser URL: simpler download (public repos)
        if ! curl -fSL --progress-bar -o "$tmp_dir/$tarball_name" "$DOWNLOAD_URL"; then
            die "Download failed. For private repos: GITHUB_TOKEN=ghp_xxx bash install.sh"
        fi
    else
        # Asset API URL: needs Accept: octet-stream header (works for private repos)
        if ! gh_download "$DOWNLOAD_URL" "$tmp_dir/$tarball_name"; then
            die "Download failed: $DOWNLOAD_URL\nCheck GITHUB_TOKEN or gh auth."
        fi
    fi

    local size
    size=$(du -h "$tmp_dir/$tarball_name" | cut -f1)
    ok "Downloaded ${size}"

    # ── Extract ──
    info "Installing to ${BOLD}${install_dir}${NC}..."
    mkdir -p "$install_dir"

    tar xzf "$tmp_dir/$tarball_name" -C "$tmp_dir"
    local extracted_dir
    extracted_dir=$(find "$tmp_dir" -maxdepth 1 -type d -name "neuronos-*" | head -1)

    if [ -z "$extracted_dir" ]; then
        die "Extraction failed — tarball may be corrupt."
    fi

    # Copy into install dir (bin/, share/, README.md)
    cp -r "$extracted_dir"/* "$install_dir/"
    chmod +x "$install_dir/bin/neuronos-cli"

    ok "Installed to ${install_dir}"
}

# ── Offer model download ──
download_model() {
    if [ "${NEURONOS_NO_MODEL:-}" = "1" ]; then
        return
    fi

    local install_dir="${NEURONOS_INSTALL_DIR:-$DEFAULT_INSTALL_DIR}"
    local models_dir="$install_dir/models"
    local model_path="$models_dir/$MODEL_FILE"

    if [ -f "$model_path" ]; then
        ok "Model already exists: $model_path"
        return
    fi

    echo ""
    info "Would you like to download the BitNet b1.58 2B model? (~1.7 GB)"
    info "This is required to run NeuronOS. You can skip and download later."
    echo -n "  Download now? [Y/n] "

    # If stdin is not a terminal (piped), default to yes
    if [ -t 0 ]; then
        read -r answer
    else
        answer="y"
        echo "y (auto)"
    fi

    case "$answer" in
        [nN]*)
            info "Skipping. Download later with:"
            info "  curl -L -o $model_path \\"
            info "    https://huggingface.co/${MODEL_HF_REPO}/resolve/main/${MODEL_FILE}"
            return
            ;;
    esac

    mkdir -p "$models_dir"
    info "Downloading model from HuggingFace..."
    local model_url="https://huggingface.co/${MODEL_HF_REPO}/resolve/main/${MODEL_FILE}"

    if curl -fL --progress-bar -o "$model_path" "$model_url"; then
        ok "Model: $model_path ($(du -h "$model_path" | cut -f1))"
    else
        warn "Download failed. Try manually:"
        warn "  curl -L -o $model_path $model_url"
        rm -f "$model_path"
    fi
}

# ── Add to PATH ──
setup_path() {
    local install_dir="${NEURONOS_INSTALL_DIR:-$DEFAULT_INSTALL_DIR}"
    local bin_dir="$install_dir/bin"

    # Already in PATH?
    if echo "$PATH" | tr ':' '\n' | grep -qx "$bin_dir"; then
        return
    fi

    local shell_name rc_file path_line
    shell_name=$(basename "${SHELL:-/bin/bash}")

    case "$shell_name" in
        zsh)  rc_file="$HOME/.zshrc" ;;
        fish) rc_file="$HOME/.config/fish/config.fish" ;;
        bash)
            for f in "$HOME/.bashrc" "$HOME/.bash_profile" "$HOME/.profile"; do
                if [ -f "$f" ]; then rc_file="$f"; break; fi
            done
            rc_file="${rc_file:-$HOME/.bashrc}"
            ;;
        *)    rc_file="$HOME/.profile" ;;
    esac

    if [ "$shell_name" = "fish" ]; then
        path_line="set -gx PATH ${bin_dir} \$PATH"
    else
        path_line="export PATH=\"${bin_dir}:\$PATH\""
    fi

    # Don't duplicate
    if [ -f "$rc_file" ] && grep -q "$bin_dir" "$rc_file" 2>/dev/null; then
        return
    fi

    {
        echo ""
        echo "# NeuronOS"
        echo "$path_line"
    } >> "$rc_file"

    ok "Added to PATH in ${rc_file}"
    warn "Run: ${BOLD}source ${rc_file}${NC} or open a new terminal"
}

# ══════════════════════════════════════════
#  Main
# ══════════════════════════════════════════
main() {
    echo ""
    echo -e "${BOLD}════════════════════════════════════════════${NC}"
    echo -e "${BOLD}  NeuronOS Installer${NC}"
    echo -e "${BOLD}  The Android of AI — Edge Agent Engine${NC}"
    echo -e "${BOLD}════════════════════════════════════════════${NC}"
    echo ""

    check_deps
    detect_platform
    get_latest_version
    get_asset_url
    install_neuronos
    download_model
    setup_path

    local install_dir="${NEURONOS_INSTALL_DIR:-$DEFAULT_INSTALL_DIR}"
    echo ""
    echo -e "${BOLD}════════════════════════════════════════════${NC}"
    echo -e "${GREEN}  NeuronOS v${VERSION} installed${NC}"
    echo -e "${BOLD}════════════════════════════════════════════${NC}"
    echo ""
    if [ -f "$install_dir/models/$MODEL_FILE" ]; then
        echo "  Quick start:"
        echo "    neuronos-cli $install_dir/models/$MODEL_FILE run \"Hello\""
        echo "    neuronos-cli $install_dir/models/$MODEL_FILE agent \"List files\""
        echo "    neuronos-cli $install_dir/models/$MODEL_FILE chat"
    else
        echo "  Next: download a model"
        echo "    curl -L -o model.gguf \\"
        echo "      https://huggingface.co/${MODEL_HF_REPO}/resolve/main/${MODEL_FILE}"
        echo ""
        echo "  Then:"
        echo "    neuronos-cli model.gguf run \"Hello\""
    fi
    echo ""
    echo "  Docs: https://github.com/${GITHUB_REPO}"
    echo ""
}

main "$@"
