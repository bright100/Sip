#!/usr/bin/env sh
# ============================================================
#  cpm — Install Script
#  Supports: Linux, macOS, Windows (via WSL / Git Bash / MSYS2)
#
#  Usage:
#    curl -fsSL https://cpm.dev/install.sh | sh
#
#  Manual install methods:
#    Windows : winget install cpm-pkg.cpm
#    macOS   : brew install cpm-pkg
#    Linux   : curl -fsSL https://cpm.dev/install.sh | sh
# ============================================================
set -e

CPM_VERSION="0.1.0"
CPM_REPO="https://github.com/cpm-pkg/cpm"
CPM_RELEASE_BASE="https://github.com/cpm-pkg/cpm/releases/download/v${CPM_VERSION}"
INSTALL_DIR="${CPM_INSTALL_DIR:-$HOME/.local/bin}"

# ── Colour helpers ───────────────────────────────────────────
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
CYAN='\033[1;36m'
GREY='\033[0;90m'
RESET='\033[0m'

info()    { printf "${CYAN}[cpm]${RESET} %s\n" "$*"; }
success() { printf "${GREEN}[cpm]${RESET} %s\n" "$*"; }
warn()    { printf "${YELLOW}[cpm]${RESET} %s\n" "$*"; }
error()   { printf "${RED}[cpm] error:${RESET} %s\n" "$*" >&2; exit 1; }

# ── Detect OS and architecture ───────────────────────────────
detect_os() {
    case "$(uname -s 2>/dev/null)" in
        Linux*)   echo "linux" ;;
        Darwin*)  echo "macos" ;;
        MINGW*|MSYS*|CYGWIN*) echo "windows" ;;
        *)
            # Fallback: check for Windows env vars
            if [ -n "${WINDIR}" ] || [ -n "${SystemRoot}" ]; then
                echo "windows"
            else
                echo "unknown"
            fi
            ;;
    esac
}

detect_arch() {
    case "$(uname -m 2>/dev/null)" in
        x86_64|amd64) echo "x86_64" ;;
        aarch64|arm64) echo "aarch64" ;;
        armv7l)        echo "armv7" ;;
        *)             echo "x86_64" ;;   # safe default
    esac
}

# ── Check for required tools ─────────────────────────────────
need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        error "required command not found: $1"
    fi
}

# ── Download binary ──────────────────────────────────────────
download() {
    local url="$1"
    local dest="$2"
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --progress-bar "$url" -o "$dest"
    elif command -v wget >/dev/null 2>&1; then
        wget -q --show-progress "$url" -O "$dest"
    else
        error "neither curl nor wget found — please install one and retry"
    fi
}

# ── Build from source ────────────────────────────────────────
build_from_source() {
    info "Cloning repository..."
    need_cmd git
    need_cmd gcc
    need_cmd make

    TMP_DIR=$(mktemp -d)
    trap 'rm -rf "$TMP_DIR"' EXIT

    git clone --depth=1 "$CPM_REPO" "$TMP_DIR/cpm" 2>/dev/null \
        || error "Failed to clone $CPM_REPO"

    info "Building cpm from source..."
    (
        cd "$TMP_DIR/cpm"
        make >/dev/null 2>&1 || make 2>&1 | tail -5
    ) || error "Build failed — check that gcc and make are installed"

    mkdir -p "$INSTALL_DIR"
    cp "$TMP_DIR/cpm/cpm/bin/cpm" "$INSTALL_DIR/cpm"
    chmod +x "$INSTALL_DIR/cpm"
}

# ── Install prebuilt binary ──────────────────────────────────
install_prebuilt() {
    local os="$1"
    local arch="$2"
    local ext=""
    local bin_name="cpm"

    if [ "$os" = "windows" ]; then
        ext=".exe"
        bin_name="cpm.exe"
    fi

    local url="${CPM_RELEASE_BASE}/cpm-${os}-${arch}${ext}"
    local tmp_bin
    tmp_bin=$(mktemp)

    info "Downloading cpm v${CPM_VERSION} for ${os}/${arch}..."
    if ! download "$url" "$tmp_bin" 2>/dev/null; then
        warn "Prebuilt binary not available for ${os}/${arch}, building from source..."
        rm -f "$tmp_bin"
        build_from_source
        return
    fi

    mkdir -p "$INSTALL_DIR"
    mv "$tmp_bin" "$INSTALL_DIR/$bin_name"
    chmod +x "$INSTALL_DIR/$bin_name"
}

# ── PATH check / update ──────────────────────────────────────
ensure_path() {
    local dir="$1"
    case ":${PATH}:" in
        *":${dir}:"*) return ;;  # already in PATH
    esac

    warn "${dir} is not in your PATH"
    info "Adding it now for this session..."
    export PATH="${dir}:${PATH}"

    # Detect shell config file
    local shell_rc=""
    case "${SHELL}" in
        */zsh)  shell_rc="${HOME}/.zshrc" ;;
        */bash) shell_rc="${HOME}/.bashrc" ;;
        */fish) shell_rc="${HOME}/.config/fish/config.fish" ;;
        *)      shell_rc="${HOME}/.profile" ;;
    esac

    if [ -n "$shell_rc" ]; then
        printf '\n# cpm package manager\nexport PATH="%s:$PATH"\n' "$dir" >> "$shell_rc"
        info "Added to ${shell_rc} — restart your shell or run:"
        info "  export PATH=\"${dir}:\$PATH\""
    fi
}

# ── Windows-specific guidance ────────────────────────────────
windows_guide() {
    printf "\n"
    info "Windows installation options:"
    printf "\n"
    printf "  ${CYAN}Option 1${RESET} — winget (recommended):\n"
    printf "    ${GREY}winget install cpm-pkg.cpm${RESET}\n\n"
    printf "  ${CYAN}Option 2${RESET} — Build from source (requires Git + MinGW/MSYS2):\n"
    printf "    ${GREY}git clone %s${RESET}\n" "$CPM_REPO"
    printf "    ${GREY}cd cpm${RESET}\n"
    printf "    ${GREY}make${RESET}\n"
    printf "    ${GREY}# Add cpm/bin to your PATH${RESET}\n\n"
    printf "  ${CYAN}Option 3${RESET} — WSL (Windows Subsystem for Linux):\n"
    printf "    ${GREY}wsl -- curl -fsSL https://cpm.dev/install.sh | sh${RESET}\n\n"
}

# ── macOS Homebrew path ──────────────────────────────────────
macos_brew_hint() {
    if command -v brew >/dev/null 2>&1; then
        printf "\n"
        info "Tip: you can also install via Homebrew:"
        printf "  ${GREY}brew install cpm-pkg${RESET}\n\n"
    fi
}

# ── Main ─────────────────────────────────────────────────────
main() {
    printf "\n"
    info "Installing cpm v${CPM_VERSION}..."
    printf "\n"

    OS=$(detect_os)
    ARCH=$(detect_arch)

    info "Detected: ${OS}/${ARCH}"

    case "$OS" in
        windows)
            windows_guide
            # Try to install anyway (WSL / MSYS2 / Cygwin environment)
            if command -v gcc >/dev/null 2>&1; then
                info "gcc found — building from source..."
                build_from_source
            else
                exit 0
            fi
            ;;
        macos)
            macos_brew_hint
            install_prebuilt "$OS" "$ARCH"
            ;;
        linux)
            install_prebuilt "$OS" "$ARCH"
            ;;
        *)
            warn "Unrecognised OS — attempting build from source..."
            build_from_source
            ;;
    esac

    ensure_path "$INSTALL_DIR"

    printf "\n"
    success "cpm installed successfully! 🎉"
    printf "\n"
    info "Try it out:"
    printf "  ${GREY}cpm --version${RESET}\n"
    printf "  ${GREY}cpm init${RESET}\n"
    printf "  ${GREY}cpm add libcurl${RESET}\n"
    printf "  ${GREY}cpm run build -w   # nodemon-style watch${RESET}\n"
    printf "\n"
    info "Full documentation: ${CYAN}https://cpm.dev/docs${RESET}"
    printf "\n"
}

main "$@"
