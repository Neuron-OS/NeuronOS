#!/usr/bin/env bash
##
## NeuronOS — One-Command Installer
##
## Detects hardware → downloads binary → selects best model → configures → runs.
##
## Usage:
##   curl -fsSL https://raw.githubusercontent.com/Neuron-OS/neuronos/main/scripts/install.sh | bash
##
## Private repo:
##   gh auth login
##   bash <(gh api repos/Neuron-OS/neuronos/contents/scripts/install.sh --jq '.content' | base64 -d)
##
## Options (env vars):
##   NEURONOS_VERSION=0.8.1     Pin specific version (default: latest)
##   NEURONOS_INSTALL_DIR=PATH  Install location (default: ~/.neuronos)
##   NEURONOS_NO_MODEL=1        Skip model download
##   NEURONOS_NO_WELCOME=1      Skip first-run welcome prompt
##   GITHUB_TOKEN=ghp_xxx       Auth token for private repo
##
set -euo pipefail

# ── Config ──
GITHUB_REPO="Neuron-OS/neuronos"
DEFAULT_INSTALL_DIR="$HOME/.neuronos"

# ── Colors ──
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
BLUE='\033[0;34m'; CYAN='\033[0;36m'; BOLD='\033[1m'; DIM='\033[2m'; NC='\033[0m'
info()    { echo -e "${BLUE}▸${NC}  $*"; }
ok()      { echo -e "${GREEN}✓${NC}  $*"; }
warn()    { echo -e "${YELLOW}!${NC}  $*"; }
die()     { echo -e "${RED}✗${NC}  $*" >&2; exit 1; }
section() { echo -e "\n${CYAN}${BOLD}── $* ──${NC}"; }

# ══════════════════════════════════════════════════════════════
#  MODEL CATALOG
#
#  Pre-converted GGUF models, sorted by quality (best first).
#  The installer picks the biggest model that fits in RAM.
#
#  Format: name|params|size_mb|min_ram_mb|hf_url|description
# ══════════════════════════════════════════════════════════════
MODEL_CATALOG=(
    "Falcon3-10B-1.58bit|10B|3500|7168|https://huggingface.co/Neuron-OS/models/resolve/main/falcon3-10b-instruct-1.58bit-i2_s.gguf|Falcon3 10B Instruct — maximum agent intelligence"
    "Falcon3-7B-1.58bit|7B|2500|5120|https://huggingface.co/Neuron-OS/models/resolve/main/falcon3-7b-instruct-1.58bit-i2_s.gguf|Falcon3 7B Instruct — excellent reasoning"
    "Falcon3-3B-1.58bit|3B|1100|3072|https://huggingface.co/Neuron-OS/models/resolve/main/falcon3-3b-instruct-1.58bit-i2_s.gguf|Falcon3 3B Instruct — balanced quality/speed"
    "BitNet-2B|2B|780|2048|https://huggingface.co/microsoft/bitnet-b1.58-2B-4T-gguf/resolve/main/ggml-model-i2_s.gguf|BitNet b1.58 2B — fast edge inference"
    "Falcon3-1B-1.58bit|1B|400|1024|https://huggingface.co/Neuron-OS/models/resolve/main/falcon3-1b-instruct-1.58bit-i2_s.gguf|Falcon3 1B Instruct — ultralight IoT"
)

# Fallback: always available, no conversion needed
FALLBACK_MODEL_URL="https://huggingface.co/microsoft/bitnet-b1.58-2B-4T-gguf/resolve/main/ggml-model-i2_s.gguf"
FALLBACK_MODEL_FILE="ggml-model-i2_s.gguf"

# ── GitHub API helper ──
gh_api() {
    local url="$1"; shift
    local auth_header=""
    if [ -n "${GITHUB_TOKEN:-}" ]; then
        auth_header="Authorization: token ${GITHUB_TOKEN}"
    elif command -v gh &>/dev/null 2>&1; then
        local token
        token=$(gh auth token 2>/dev/null) || true
        [ -n "$token" ] && auth_header="Authorization: token ${token}"
    fi
    if [ -n "$auth_header" ]; then
        curl -fsSL -H "$auth_header" -H "Accept: application/vnd.github.v3+json" "$url" "$@"
    else
        curl -fsSL -H "Accept: application/vnd.github.v3+json" "$url" "$@"
    fi
}

gh_download() {
    local url="$1"; local dest="$2"
    local auth_header=""
    if [ -n "${GITHUB_TOKEN:-}" ]; then
        auth_header="Authorization: token ${GITHUB_TOKEN}"
    elif command -v gh &>/dev/null 2>&1; then
        local token
        token=$(gh auth token 2>/dev/null) || true
        [ -n "$token" ] && auth_header="Authorization: token ${token}"
    fi
    if [ -n "$auth_header" ]; then
        curl -fSL -H "$auth_header" -H "Accept: application/octet-stream" \
            --progress-bar -o "$dest" "$url"
    else
        curl -fSL --progress-bar -o "$dest" "$url"
    fi
}

# ══════════════════════════════════════════════════════════════
#  HARDWARE DETECTION (pure bash — no Python, no external deps)
# ══════════════════════════════════════════════════════════════
detect_hardware() {
    section "Hardware Detection"

    # ── CPU ──
    HW_CPU="Unknown"
    if [ -f /proc/cpuinfo ]; then
        HW_CPU=$(grep -m1 "model name" /proc/cpuinfo | cut -d: -f2 | sed 's/^ //' || echo "Unknown")
    elif command -v sysctl &>/dev/null; then
        HW_CPU=$(sysctl -n machdep.cpu.brand_string 2>/dev/null || echo "Apple Silicon")
    fi

    # ── Architecture ──
    HW_ARCH=$(uname -m)
    case "$HW_ARCH" in
        x86_64|amd64)    HW_ARCH="x86_64" ;;
        aarch64|arm64)   HW_ARCH="arm64" ;;
        *)               HW_ARCH="$HW_ARCH" ;;
    esac

    # ── Cores ──
    HW_CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

    # ── RAM (MB) ──
    HW_RAM_TOTAL=0
    HW_RAM_AVAIL=0
    if [ -f /proc/meminfo ]; then
        HW_RAM_TOTAL=$(awk '/^MemTotal:/ {printf "%.0f", $2/1024}' /proc/meminfo)
        HW_RAM_AVAIL=$(awk '/^MemAvailable:/ {printf "%.0f", $2/1024}' /proc/meminfo)
        if [ "$HW_RAM_AVAIL" = "0" ] || [ -z "$HW_RAM_AVAIL" ]; then
            HW_RAM_AVAIL=$((HW_RAM_TOTAL * 60 / 100))
        fi
    elif command -v sysctl &>/dev/null; then
        local memsize
        memsize=$(sysctl -n hw.memsize 2>/dev/null || echo 0)
        HW_RAM_TOTAL=$((memsize / 1024 / 1024))
        HW_RAM_AVAIL=$((HW_RAM_TOTAL * 60 / 100))
    fi

    # RAM budget: available - 500MB safety margin
    HW_RAM_BUDGET=$((HW_RAM_AVAIL - 500))
    [ "$HW_RAM_BUDGET" -lt 512 ] && HW_RAM_BUDGET=512

    # ── GPU ──
    HW_GPU="None"
    HW_GPU_VRAM=0
    if command -v nvidia-smi &>/dev/null; then
        local gpu_info
        gpu_info=$(nvidia-smi --query-gpu=name,memory.total --format=csv,noheader,nounits 2>/dev/null || true)
        if [ -n "$gpu_info" ]; then
            HW_GPU=$(echo "$gpu_info" | cut -d, -f1 | sed 's/^ //;s/ $//')
            HW_GPU_VRAM=$(echo "$gpu_info" | cut -d, -f2 | sed 's/^ //;s/ $//')
        fi
    fi

    # ── ISA Features ──
    HW_FEATURES=""
    if [ "$HW_ARCH" = "x86_64" ] && [ -f /proc/cpuinfo ]; then
        local flags
        flags=$(grep -m1 "flags" /proc/cpuinfo | cut -d: -f2 || true)
        [[ "$flags" == *"avx2"* ]]    && HW_FEATURES="${HW_FEATURES}AVX2 "
        [[ "$flags" == *"avx512"* ]]   && HW_FEATURES="${HW_FEATURES}AVX512 "
        [[ "$flags" == *"avx_vnni"* ]] && HW_FEATURES="${HW_FEATURES}AVX-VNNI "
    elif [ "$HW_ARCH" = "arm64" ]; then
        HW_FEATURES="NEON "
    fi
    HW_FEATURES="${HW_FEATURES:-scalar}"

    # ── Display ──
    echo -e "  ${BOLD}CPU:${NC}      $HW_CPU"
    echo -e "  ${BOLD}Arch:${NC}     $HW_ARCH"
    echo -e "  ${BOLD}Cores:${NC}    $HW_CORES"
    echo -e "  ${BOLD}RAM:${NC}      ${HW_RAM_TOTAL} MB total / ${HW_RAM_AVAIL} MB available"
    echo -e "  ${BOLD}Budget:${NC}   ${HW_RAM_BUDGET} MB for models"
    if [ "$HW_GPU_VRAM" -gt 0 ]; then
        echo -e "  ${BOLD}GPU:${NC}      $HW_GPU (${HW_GPU_VRAM} MB)"
    else
        echo -e "  ${BOLD}GPU:${NC}      None detected (CPU-only — perfect for BitNet)"
    fi
    echo -e "  ${BOLD}ISA:${NC}      ${HW_FEATURES}"
}

# ══════════════════════════════════════════════════════════════
#  MODEL SELECTION — pick the best model for this hardware
# ══════════════════════════════════════════════════════════════
select_model() {
    section "Model Selection"

    SELECTED_NAME=""
    SELECTED_URL=""
    SELECTED_FILE=""
    SELECTED_SIZE=0
    SELECTED_DESC=""
    SELECTED_PARAMS=""

    # Try each model from the catalog (ordered best → smallest)
    for entry in "${MODEL_CATALOG[@]}"; do
        IFS='|' read -r name params size_mb min_ram_mb url desc <<< "$entry"

        if [ "$HW_RAM_BUDGET" -ge "$min_ram_mb" ]; then
            # Check if URL is reachable (fast HEAD request, 5s timeout)
            local status_code
            status_code=$(curl -sI -o /dev/null -w "%{http_code}" --connect-timeout 5 "$url" 2>/dev/null || echo "000")

            if [ "$status_code" = "200" ] || [ "$status_code" = "302" ] || [ "$status_code" = "301" ]; then
                SELECTED_NAME="$name"
                SELECTED_PARAMS="$params"
                SELECTED_URL="$url"
                SELECTED_FILE="$(basename "$url")"
                SELECTED_SIZE="$size_mb"
                SELECTED_DESC="$desc"
                ok "Best fit: ${BOLD}${name}${NC} (${params} params, ~${size_mb} MB)"
                info "$desc"
                return
            fi
        fi
    done

    # Fallback: always-available 2B model (pre-converted by Microsoft)
    SELECTED_NAME="BitNet-2B"
    SELECTED_PARAMS="2B"
    SELECTED_URL="$FALLBACK_MODEL_URL"
    SELECTED_FILE="$FALLBACK_MODEL_FILE"
    SELECTED_SIZE=780
    SELECTED_DESC="BitNet b1.58 2B — fast edge inference"
    ok "Selected: ${BOLD}${SELECTED_NAME}${NC} (~${SELECTED_SIZE} MB)"
    info "$SELECTED_DESC"
}

# ── Check prerequisites ──
check_deps() {
    for cmd in curl tar; do
        command -v "$cmd" &>/dev/null || die "Required: $cmd — please install it first."
    done
}

# ── Detect platform for binary download ──
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
}

# ── Get latest version ──
get_latest_version() {
    if [ -n "${NEURONOS_VERSION:-}" ]; then
        VERSION="$NEURONOS_VERSION"
        return
    fi

    local api_url="https://api.github.com/repos/${GITHUB_REPO}/releases/latest"
    local body
    body=$(gh_api "$api_url" 2>/dev/null) || true

    if [ -n "$body" ]; then
        VERSION=$(echo "$body" | grep -oP '"tag_name":\s*"v?\K[^"]+' | head -1)
    fi

    if [ -z "${VERSION:-}" ]; then
        local tags_url="https://api.github.com/repos/${GITHUB_REPO}/tags?per_page=1"
        body=$(gh_api "$tags_url" 2>/dev/null) || true
        VERSION=$(echo "$body" | grep -oP '"name":\s*"v?\K[^"]+' | head -1)
    fi

    if [ -z "${VERSION:-}" ]; then
        die "Cannot determine version. Set NEURONOS_VERSION=x.y.z or check auth."
    fi
}

# ── Download and install the NeuronOS binary ──
install_binary() {
    section "Installing NeuronOS"

    local install_dir="${NEURONOS_INSTALL_DIR:-$DEFAULT_INSTALL_DIR}"
    local tarball_name="neuronos-v${VERSION}-${PLATFORM}.tar.gz"

    info "Version: ${BOLD}v${VERSION}${NC}  Platform: ${BOLD}${PLATFORM}${NC}"

    # Find the release asset URL
    local api_url="https://api.github.com/repos/${GITHUB_REPO}/releases/tags/v${VERSION}"
    local release_json
    release_json=$(gh_api "$api_url" 2>/dev/null) || die "Release v${VERSION} not found."

    local download_url=""
    local download_is_browser=0

    # Try parsing with python3 for reliable JSON
    if command -v python3 &>/dev/null; then
        download_url=$(echo "$release_json" | python3 -c "
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

    # Fallback: grep
    if [ -z "$download_url" ]; then
        download_url=$(echo "$release_json" | \
            grep -oP '"browser_download_url":\s*"\K[^"]+' | \
            grep "${tarball_name}" | head -1)
        download_is_browser=1
    fi

    if [ -z "$download_url" ]; then
        die "No binary for ${tarball_name}. Check: https://github.com/${GITHUB_REPO}/releases"
    fi

    # Download
    local tmp_dir
    tmp_dir=$(mktemp -d)
    trap "rm -rf '$tmp_dir'" EXIT

    info "Downloading binary..."
    if [ "$download_is_browser" = "1" ]; then
        curl -fSL --progress-bar -o "$tmp_dir/$tarball_name" "$download_url" || \
            die "Download failed. For private repos: GITHUB_TOKEN=ghp_xxx bash install.sh"
    else
        gh_download "$download_url" "$tmp_dir/$tarball_name" || \
            die "Download failed."
    fi

    local size
    size=$(du -h "$tmp_dir/$tarball_name" | cut -f1)
    ok "Binary: ${size}"

    # Extract and install
    mkdir -p "$install_dir"
    tar xzf "$tmp_dir/$tarball_name" -C "$tmp_dir"
    local extracted_dir
    extracted_dir=$(find "$tmp_dir" -maxdepth 1 -type d -name "neuronos-*" | head -1)
    [ -z "$extracted_dir" ] && die "Extraction failed."

    cp -r "$extracted_dir"/* "$install_dir/"
    chmod +x "$install_dir/bin/neuronos-cli"

    # Create `neuronos` wrapper → delegates to neuronos-cli
    cat > "$install_dir/bin/neuronos" << 'WRAPPER_EOF'
#!/bin/sh
## NeuronOS — Universal AI Agent Engine
## Wrapper that delegates to neuronos-cli with auto-configuration.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
exec "$SCRIPT_DIR/neuronos-cli" "$@"
WRAPPER_EOF
    chmod +x "$install_dir/bin/neuronos"

    ok "Installed to ${BOLD}${install_dir}${NC}"
}

# ── Download the selected model ──
download_model() {
    if [ "${NEURONOS_NO_MODEL:-}" = "1" ]; then
        return
    fi

    section "Downloading AI Model"

    local install_dir="${NEURONOS_INSTALL_DIR:-$DEFAULT_INSTALL_DIR}"
    local models_dir="$install_dir/models"
    local model_path="$models_dir/$SELECTED_FILE"

    if [ -f "$model_path" ]; then
        ok "Model already exists: ${BOLD}$(basename "$model_path")${NC}"
        return
    fi

    mkdir -p "$models_dir"

    info "Downloading ${BOLD}${SELECTED_NAME}${NC} (~${SELECTED_SIZE} MB)..."
    info "Source: $(echo "$SELECTED_URL" | sed 's|https://huggingface.co/||')"
    echo ""

    if curl -fL --progress-bar -o "$model_path" "$SELECTED_URL"; then
        local msize
        msize=$(du -h "$model_path" | cut -f1)
        echo ""
        ok "Model: ${BOLD}$(basename "$model_path")${NC} (${msize})"
    else
        warn "Model download failed. You can download manually later:"
        warn "  curl -L -o $model_path $SELECTED_URL"
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
        echo "# NeuronOS — AI Agent Engine"
        echo "$path_line"
    } >> "$rc_file"

    ok "Added to PATH in ${rc_file}"
    export PATH="${bin_dir}:$PATH"
}

# ── Create default configuration ──
setup_config() {
    local install_dir="${NEURONOS_INSTALL_DIR:-$DEFAULT_INSTALL_DIR}"

    mkdir -p "$install_dir/models"

    # Default MCP config
    if [ ! -f "$install_dir/mcp.json" ]; then
        echo '{ "servers": [] }' > "$install_dir/mcp.json"
    fi

    # Hardware profile
    cat > "$install_dir/hw_profile.json" << HW_EOF
{
  "cpu": "$HW_CPU",
  "arch": "$HW_ARCH",
  "cores": $HW_CORES,
  "ram_total_mb": $HW_RAM_TOTAL,
  "ram_available_mb": $HW_RAM_AVAIL,
  "ram_budget_mb": $HW_RAM_BUDGET,
  "gpu": "$HW_GPU",
  "gpu_vram_mb": $HW_GPU_VRAM,
  "features": "$HW_FEATURES",
  "selected_model": "$SELECTED_NAME",
  "installed_at": "$(date -Iseconds 2>/dev/null || date)"
}
HW_EOF
}

# ── First-run: demonstrate NeuronOS capabilities ──
run_welcome() {
    if [ "${NEURONOS_NO_WELCOME:-}" = "1" ]; then
        return
    fi

    local install_dir="${NEURONOS_INSTALL_DIR:-$DEFAULT_INSTALL_DIR}"
    local cli="$install_dir/bin/neuronos-cli"
    local model_path
    model_path=$(find "$install_dir/models" -name "*.gguf" -type f 2>/dev/null | head -1)

    if [ ! -x "$cli" ] || [ -z "$model_path" ]; then
        return
    fi

    section "NeuronOS Agent — First Run"
    echo ""

    local welcome_prompt
    welcome_prompt="You just got installed on a new device. Introduce yourself in 3-4 sentences. \
State your name (NeuronOS), that you are running 100% locally with zero cloud dependency, \
and list your key powers: persistent memory, tool use (filesystem, shell, web), \
agent reasoning, MCP protocol support, and 1.58-bit ternary model efficiency. \
End with an invitation to chat or give you a task. Be confident and concise."

    # Run with the installed binary
    "$cli" "$model_path" run "$welcome_prompt" -n 256 --temp 0.7 2>/dev/null || true

    echo ""
}

# ══════════════════════════════════════════════════════════════
#  MAIN
# ══════════════════════════════════════════════════════════════
main() {
    echo ""
    echo -e "${BOLD}${CYAN}"
    cat << 'BANNER'
    _   __                            ____  _____
   / | / /__  __  ___________  ____  / __ \/ ___/
  /  |/ / _ \/ / / / ___/ __ \/ __ \/ / / /\__ \
 / /|  /  __/ /_/ / /  / /_/ / / / / /_/ /___/ /
/_/ |_/\___/\__,_/_/   \____/_/ /_/\____//____/
BANNER
    echo -e "${NC}"
    echo -e "${BOLD}  Universal AI Agent Engine — \"The Android of AI\"${NC}"
    echo -e "  ${DIM}One command: detect hardware → select model → install → run${NC}"
    echo ""

    check_deps
    detect_platform
    detect_hardware
    select_model
    get_latest_version
    install_binary
    download_model
    setup_config
    setup_path

    local install_dir="${NEURONOS_INSTALL_DIR:-$DEFAULT_INSTALL_DIR}"

    echo ""
    echo -e "${GREEN}${BOLD}════════════════════════════════════════════════════════════${NC}"
    echo -e "${GREEN}${BOLD}  NeuronOS v${VERSION} — Ready${NC}"
    echo -e "${GREEN}${BOLD}════════════════════════════════════════════════════════════${NC}"
    echo ""
    echo -e "  ${BOLD}Binary:${NC}   $install_dir/bin/neuronos"
    echo -e "  ${BOLD}Model:${NC}    $SELECTED_NAME ($SELECTED_PARAMS params, ~${SELECTED_SIZE} MB)"
    echo -e "  ${BOLD}Hardware:${NC} $HW_CORES cores, ${HW_RAM_TOTAL} MB RAM, ${HW_FEATURES}"
    echo ""
    echo -e "  ${BOLD}Commands:${NC}"
    echo -e "    ${CYAN}neuronos${NC}                   Interactive REPL (auto-config)"
    echo -e "    ${CYAN}neuronos run${NC} \"question\"     One-shot generation"
    echo -e "    ${CYAN}neuronos agent${NC} \"task\"       Agent with tools"
    echo -e "    ${CYAN}neuronos serve${NC}              HTTP server (OpenAI API)"
    echo -e "    ${CYAN}neuronos mcp${NC}                MCP server (Claude/VS Code)"
    echo -e "    ${CYAN}neuronos hwinfo${NC}             Hardware capabilities"
    echo ""

    # Run the welcome demo
    run_welcome

    echo -e "  ${DIM}Tip: open a new terminal or run: source ~/.$(basename "${SHELL:-bash}")rc${NC}"
    echo ""
}

main "$@"
