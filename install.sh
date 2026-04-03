#!/usr/bin/env sh
set -e

CPM_VERSION="0.1.0"
CPM_REPO="https://github.com/bright100/Sip.git"
INSTALL_DIR="${CPM_INSTALL_DIR:-$HOME/.local/bin}"

# ── Colour helpers ───────────────────────────────────────────
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
CYAN='\033[1;36m'
RESET='\033[0m'

info() { printf "${CYAN}[cpm]${RESET} %s\n" "$*"; }
success() { printf "${GREEN}[cpm]${RESET} %s\n" "$*"; }
warn() { printf "${YELLOW}[cpm]${RESET} %s\n" "$*"; }
error() {
  printf "${RED}[cpm] error:${RESET} %s\n" "$*" >&2
  exit 1
}

# ── Detect OS ────────────────────────────────────────────────
detect_os() {
  case "$(uname -s 2>/dev/null)" in
    Linux*) echo "linux" ;;
    Darwin*) echo "macos" ;;
    MINGW* | MSYS* | CYGWIN*) echo "windows" ;;
    *)
      if [ -n "${WINDIR}" ] || [ -n "${SystemRoot}" ]; then
        echo "windows"
      else
        echo "unknown"
      fi
      ;;
  esac
}

OS="$(detect_os)"

# ── Install dependencies ─────────────────────────────────────
install_deps() {
  info "Checking dependencies..."

  if command -v curl >/dev/null 2>&1 && [ -f /usr/include/curl/curl.h ]; then
    success "libcurl already installed"
    return
  fi

  case "$OS" in
    linux)
      if command -v apt >/dev/null 2>&1; then
        info "Installing libcurl (apt)..."
        sudo apt update
        sudo apt install -y libcurl4-openssl-dev build-essential
      elif command -v dnf >/dev/null 2>&1; then
        info "Installing libcurl (dnf)..."
        sudo dnf install -y libcurl-devel gcc make
      elif command -v yum >/dev/null 2>&1; then
        info "Installing libcurl (yum)..."
        sudo yum install -y libcurl-devel gcc make
      else
        warn "Unknown package manager. Please install libcurl dev manually."
      fi
      ;;
    macos)
      if command -v brew >/dev/null 2>&1; then
        info "Installing libcurl via brew..."
        brew install curl
      else
        warn "Homebrew not found. Install curl manually."
      fi
      ;;
    windows)
      warn "On Windows, use MSYS2 and run:"
      warn "  pacman -S mingw-w64-x86_64-curl"
      ;;
    *)
      warn "Unsupported OS. Please install libcurl manually."
      ;;
  esac
}

# ── Build CPM ────────────────────────────────────────────────
build() {
  info "Cloning repository..."
  git clone "$CPM_REPO" cpm-src
  cd cpm-src

  install_deps

  info "Building CPM..."

  # macOS brew path fix
  if [ "$OS" = "macos" ] && command -v brew >/dev/null 2>&1; then
    CURL_FLAGS="-I$(brew --prefix curl)/include -L$(brew --prefix curl)/lib"
  else
    CURL_FLAGS=""
  fi

  gcc -O2 -Wall -Wextra -std=c17 -Wno-format-truncation \
    -Icpm/include \
    cpm/src/main.c \
    cpm/src/toml.c \
    cpm/src/registry.c \
    cpm/src/resolver.c \
    cpm/src/core/utils.c \
    cpm/src/core/manifest.c \
    cpm/src/commands/cmd_init.c \
    cpm/src/commands/cmd_add.c \
    cpm/src/commands/cmd_install.c \
    cpm/src/commands/cmd_build.c \
    cpm/src/commands/cmd_run.c \
    cpm/src/commands/cmd_remove.c \
    cpm/src/commands/cmd_update.c \
    cpm/src/commands/cmd_publish.c \
    $CURL_FLAGS \
    -lcurl \
    -o cpm

  mkdir -p "$INSTALL_DIR"
  mv cpm "$INSTALL_DIR/cpm"

  success "Installed cpm to $INSTALL_DIR/cpm"

  if ! echo "$PATH" | grep -q "$INSTALL_DIR"; then
    warn "Add this to your PATH:"
    echo "export PATH=\"$INSTALL_DIR:\$PATH\""
  fi
}

build
