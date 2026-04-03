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

# ── Windows: Install build tools ─────────────────────────────
windows_install_build_tools() {
  info "Checking for required build tools on Windows..."
  
  local need_gcc=0
  local need_make=0
  local need_curl_dev=0
  
  if ! command -v gcc >/dev/null 2>&1; then
    warn "gcc not found"
    need_gcc=1
  fi
  
  if ! command -v make >/dev/null 2>&1; then
    warn "make not found"
    need_make=1
  fi
  
  # Check for curl development headers by trying to compile a test file
  if ! echo '#include <curl/curl.h>' | gcc -E - >/dev/null 2>&1; then
    warn "curl development headers not found (curl/curl.h)"
    need_curl_dev=1
  fi
  
  if [ $need_gcc -eq 0 ] && [ $need_make -eq 0 ] && [ $need_curl_dev -eq 0 ]; then
    success "All required build tools are installed"
    return 0
  fi
  
  # Try winget first (recommended)
  if command -v winget >/dev/null 2>&1; then
    info "Using winget to install missing tools..."
    
    if [ $need_gcc -eq 1 ]; then
      info "Installing MinGW-w64 (gcc) via winget..."
      winget install --id MSYS2.MSYS2 --silent || \
      winget install --id Brechtjer.MinGW-w64 --silent || \
      warn "Failed to install gcc via winget"
    fi
    
    if [ $need_make -eq 1 ]; then
      info "Installing make via winget..."
      winget install --id MSYS2.MSYS2 --silent || \
      winget install --id GnuWin32.Make --silent || \
      warn "Failed to install make via winget"
    fi
    
    if [ $need_curl_dev -eq 1 ]; then
      info "Installing curl development headers via winget..."
      winget install --id MSYS2.MSYS2 --silent || \
      warn "Failed to install MSYS2 via winget for curl headers"
    fi
    
    # Refresh PATH after winget install
    export PATH="/c/msys64/mingw64/bin:/c/Program Files (x86)/GnuWin32/bin:$PATH"
    
    # Verify installation
    if command -v gcc >/dev/null 2>&1 && command -v make >/dev/null 2>&1 && echo '#include <curl/curl.h>' | gcc -E - >/dev/null 2>&1; then
      success "Build tools installed successfully via winget"
      return 0
    fi
  fi
  
  # Try chocolatey
  if command -v choco >/dev/null 2>&1; then
    info "Using chocolatey to install missing tools..."
    
    if [ $need_gcc -eq 1 ]; then
      info "Installing mingw via chocolatey..."
      choco install mingw -y || warn "Failed to install gcc via chocolatey"
    fi
    
    if [ $need_make -eq 1 ]; then
      info "Installing make via chocolatey..."
      choco install make -y || warn "Failed to install make via chocolatey"
    fi
    
    if [ $need_curl_dev -eq 1 ]; then
      info "Installing curl via chocolatey..."
      choco install curl -y || warn "Failed to install curl via chocolatey"
    fi
    
    # Refresh PATH
    export PATH="/c/tools/mingw64/bin:$PATH"
    
    if command -v gcc >/dev/null 2>&1 && command -v make >/dev/null 2>&1 && echo '#include <curl/curl.h>' | gcc -E - >/dev/null 2>&1; then
      success "Build tools installed successfully via chocolatey"
      return 0
    fi
  fi
  
  # Try scoop
  if command -v scoop >/dev/null 2>&1; then
    info "Using scoop to install missing tools..."
    
    if [ $need_gcc -eq 1 ]; then
      info "Installing mingw via scoop..."
      scoop install mingw || warn "Failed to install gcc via scoop"
    fi
    
    if [ $need_make -eq 1 ]; then
      info "Installing make via scoop..."
      scoop install make || warn "Failed to install make via scoop"
    fi
    
    if [ $need_curl_dev -eq 1 ]; then
      info "Installing curl via scoop..."
      scoop install curl || warn "Failed to install curl via scoop"
    fi
    
    if command -v gcc >/dev/null 2>&1 && command -v make >/dev/null 2>&1 && echo '#include <curl/curl.h>' | gcc -E - >/dev/null 2>&1; then
      success "Build tools installed successfully via scoop"
      return 0
    fi
  fi
  
  # If we reach here, automatic installation failed
  if [ $need_gcc -eq 1 ] || [ $need_make -eq 1 ] || [ $need_curl_dev -eq 1 ]; then
    error "Could not automatically install build tools. Please install manually:
  - Option 1: Install MSYS2 from https://www.msys2.org/, then run:
      pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-make mingw-w64-x86_64-curl
  - Option 2: Install MinGW-w64 from https://winlibs.com/
  - Option 3: Use WSL (Windows Subsystem for Linux)
  
Then run the installer again."
    return 1
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
