# 📦 cpm — A Package Manager for C and C++

> *The npm you always wanted, but for C.*

[![Version](https://img.shields.io/badge/version-0.1.0-blue)](https://github.com/cpm-pkg/cpm)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-lightgrey)](#-installation)

---

## 🚀 What is cpm?

**cpm** brings the frictionless workflow that JavaScript developers take for granted to the C and C++ ecosystem.

```bash
cpm install             # 📥 Read manifest, resolve deps, compile and link
cpm add libcurl         # 🔍 Find, fetch, and add a dependency
cpm run build           # ▶️  Execute a named script
cpm run build -w        # 👁️  Watch files and re-run on change (nodemon-style)
```

No more vendored tarballs. No more Git submodules. No more CMake nightmares. Just type the name of the library you want and go.

---

## 📋 Table of Contents

1. [Installation](#-installation)
2. [Quick Start](#-quick-start)
3. [CLI Reference](#-cli-reference)
4. [Watch Mode (nodemon)](#-watch-mode-nodemon-style)
5. [The Manifest File](#-the-manifest-file-cpmtoml)
6. [The Lock File](#-the-lock-file-cpmlock)
7. [Dependency Resolution](#-dependency-resolution)
8. [Supported Packages](#-supported-packages)
9. [Project Structure](#-project-structure)
10. [Building from Source](#-building-from-source)
11. [Architecture](#-architecture)
12. [Why C Needs This](#-why-c-needs-this)
13. [FAQ](#-faq)

---

## 💾 Installation

### 🪟 Windows — winget

```powershell
winget install cpm-pkg.cpm
```

### 🍎 macOS — Homebrew

```bash
brew install cpm-pkg
```

### 🐧 Linux / macOS — curl installer

```bash
curl -fsSL https://cpm.dev/install.sh | sh
```

Or, if you prefer to inspect the script first (always a good idea ✅):

```bash
curl -fsSL https://cpm.dev/install.sh -o install.sh
less install.sh
sh install.sh
```

### 🔧 Building from Source

```bash
git clone https://github.com/cpm-pkg/cpm
cd cpm
make
# Binary lands at cpm/bin/cpm — add it to your PATH
```

---

## ⚡ Quick Start

```bash
# 1️⃣  Create a new C project
cpm init

# 1️⃣  ...or a C++ project
cpm init --cpp

# 2️⃣  Add a dependency
cpm add libcurl
cpm add zlib@1.3.1
cpm add --dev cmocka       # dev-only dependency

# 3️⃣  Install everything
cpm install

# 4️⃣  Build
cpm run build

# 5️⃣  Watch & live-reload on save
cpm run build -w
```

---

## 📖 CLI Reference

| Command | Alias | Description |
|---------|-------|-------------|
| `cpm init` | | Initialize a new C project |
| `cpm init --cpp` | | Initialize a new C++ project |
| `cpm add <pkg>` | | Add latest version of a package |
| `cpm add <pkg>@<ver>` | | Add a specific version |
| `cpm add --dev <pkg>` | `-d` | Add a dev-only dependency |
| `cpm install` | `i` | Install all dependencies from manifest |
| `cpm install -u` | `--update` | Re-resolve and update the lockfile |
| `cpm build` | `b` | Build the project |
| `cpm run <script>` | `r` | Run a named script from `cpm.toml` |
| `cpm run <script> -w` | `--watch` | Run with nodemon-style file watching 👇 |
| `cpm remove <pkg>` | `rm` | Remove a dependency |
| `cpm update` | `up` | Update all packages to latest compatible |
| `cpm update <pkg>` | | Update one package |
| `cpm publish` | `pub` | Publish your package to the registry |
| `cpm help` | `-h` | Show help |
| `cpm --version` | `-v` | Show version |

---

## 👁️ Watch Mode (nodemon-style)

`cpm run` supports a built-in file watcher that re-runs your script whenever source files change — just like **nodemon** does for Node.js.

### Basic Usage

```bash
# Run 'build' script and restart whenever any .c/.h/.cpp file changes
cpm run build -w

# Same with long flag
cpm run build --watch
```

### All Watch Flags

| Flag | Default | Description |
|------|---------|-------------|
| `-w` / `--watch` | off | Enable watch mode |
| `--ext=c,h,cpp` | `c,h,cpp,cc,cxx,hpp` | Comma-separated file extensions to watch |
| `--delay=<ms>` | `300` | Debounce delay — wait this long after the last change before restarting |
| `-v` / `--verbose` | off | Print the exact file that triggered the restart |
| `--clear` | off | Clear the terminal before each run |

### Examples

```bash
# Watch .c and .h files, clear screen, print changed file
cpm run build --watch --ext=c,h --clear -v

# Custom debounce — wait 1 second after changes stop
cpm run build -w --delay=1000

# Watch test runner
cpm run test -w --ext=c,h --clear

# Full options
cpm run build --watch --ext=c,h,cpp --delay=500 -v --clear
```

### How it works 🔬

1. On startup, cpm recursively scans your project for all files matching `--ext`
2. It launches your script as a **child process**
3. Every **50 ms**, it polls each tracked file's modification time (`mtime`)
4. When a change is detected, a **debounce timer** starts (default 300 ms)
5. If no further changes happen within the debounce window, the child process is **gracefully killed** (SIGTERM → SIGKILL after 500 ms) and the script is **restarted**
6. New files created during a session are picked up after each restart

Press `Ctrl+C` to exit watch mode cleanly.

---

## 📄 The Manifest File (`cpm.toml`)

Every cpm project has a `cpm.toml` at its root. This is the equivalent of `package.json`.

```toml
[package]
name    = "myapp"
version = "1.0.0"
authors = ["Alice <alice@example.com>"]
license = "MIT"
lang    = "c++"           # "c" or "c++"
std     = "c++17"         # C/C++ standard

[dependencies]
libcurl       = "^8.0"
cJSON         = "~1.7"
libuv         = "2.0.0"

[dev-dependencies]
cmocka        = "^1.1"    # test framework

[build]
cc      = "g++"
cflags  = ["-O2", "-Wall", "-Wextra"]
ldflags = ["-lpthread"]

[scripts]
build   = "cpm compile src/main.cpp -o bin/myapp"
test    = "cpm compile tests/*.cpp -o bin/tests && ./bin/tests"
clean   = "rm -rf bin/ .cpm/build/"
```

### Version Constraints

| Syntax | Meaning |
|--------|---------|
| `"8.4.0"` | Exact version |
| `"^8.0"` | Compatible within major (`>=8.0 <9.0`) |
| `"~1.7"` | Compatible within minor (`>=1.7 <1.8`) |
| `">=2.0"` | Greater than or equal to |
| `"*"` / `"latest"` | Any version |

---

## 🔒 The Lock File (`cpm.lock`)

`cpm.lock` is auto-generated and **should be committed to version control**. It pins exact versions of every direct and transitive dependency, ensuring reproducible builds across machines.

```toml
# cpm.lock — auto-generated, do not edit
# Generated: 2024-01-15T12:34:56Z

[[package]]
name     = "libcurl"
version  = "8.4.0"
source   = "registry+https://registry.cpm.dev"
checksum = "sha256:abc123..."
deps     = ["zlib", "openssl"]

[[package]]
name     = "zlib"
version  = "1.3.1"
source   = "registry+https://registry.cpm.dev"
checksum = "sha256:def456..."
deps     = []
```

---

## 🧠 Dependency Resolution

cpm uses a **PubGrub-inspired** constraint solver:

- Resolves **direct** and **transitive** dependencies
- Detects **version conflicts** and reports them clearly
- Enforces **language compatibility** — a C project cannot pull in C++ packages unless you opt in with `lang = "c++"`
- Respects **semver constraints** (`^`, `~`, `>=`, exact)

---

## 📦 Supported Packages

The built-in registry includes these packages for demo/testing:

| Package | Language | Type | Description |
|---------|----------|------|-------------|
| `libcurl` | C | source | HTTP client library |
| `zlib` | C | source | Compression library |
| `openssl` | C | system | TLS/SSL toolkit |
| `libuv` | C | source | Async I/O |
| `cJSON` | C | source | Lightweight JSON parser |
| `cmocka` | C | source | Unit testing framework |
| `nlohmann-json` | C++ | header-only | Modern JSON for C++ |
| `googletest` | C++ | source | Google Test framework |
| `fmt` | C++ | header-only | Formatting library |
| `spdlog` | C++ | header-only | Fast logging |
| `catch2` | C++ | header-only | Test framework |

---

## 🗂️ Project Structure

```
cpm/
├── include/
│   ├── core/
│   │   ├── types.h          # Core types (lock_package_t, lockfile_t, etc.)
│   │   ├── utils.h          # Utility function declarations
│   │   └── manifest.h       # Manifest & lockfile declarations
│   ├── commands/
│   │   ├── cmd_init.h
│   │   ├── cmd_add.h
│   │   ├── cmd_install.h
│   │   ├── cmd_build.h
│   │   ├── cmd_run.h        # Includes run_opts_t for watch mode
│   │   ├── cmd_remove.h
│   │   ├── cmd_update.h
│   │   └── cmd_publish.h
│   ├── mock/
│   │   └── registry_mock.h  # Mock registry for demos
│   ├── toml.h               # TOML parser header
│   ├── registry.h           # Registry client header
│   └── resolver.h           # Dependency resolver header
│
├── src/
│   ├── main.c               # CLI argument parsing & dispatch
│   ├── core/
│   │   ├── utils.c          # File I/O, version parsing, hashing
│   │   └── manifest.c       # Manifest, lockfile, fetch, build, install
│   ├── commands/
│   │   ├── cmd_init.c
│   │   ├── cmd_add.c
│   │   ├── cmd_install.c
│   │   ├── cmd_build.c
│   │   ├── cmd_run.c        # ⭐ Nodemon-style watch engine
│   │   ├── cmd_remove.c
│   │   ├── cmd_update.c
│   │   └── cmd_publish.c
│   ├── mock/
│   │   └── registry_mock.c
│   ├── toml.c
│   ├── registry.c
│   └── resolver.c
│
├── bin/
│   └── cpm                  # Compiled binary
└── cpm.toml                 # Self-hosted manifest
```

---

## 🔨 Building from Source

```bash
# Clone the repo
git clone https://github.com/cpm-pkg/cpm
cd cpm

# Build (requires gcc or clang, C17 support)
make

# Run it
./cpm/bin/cpm --help

# Add to PATH
export PATH="$PWD/cpm/bin:$PATH"
```

**Requirements:** `gcc` ≥ 7 (or `clang` ≥ 5) with C17 support. No other dependencies.

---

## 🏗️ Architecture

```
main.c              CLI parsing → dispatch
│
├── cmd_init        Creates cpm.toml + project skeleton
├── cmd_add         Validates package → manifest_add_dep → cmd_install
├── cmd_install     Resolve → lockfile_save → fetch → build → install
├── cmd_build       Reads scripts.build → compile_project
├── cmd_run ⭐      Reads scripts.<name> → run (+ optional watch loop)
├── cmd_remove      manifest_remove_dep → re-resolve → prune deps
├── cmd_update      Re-resolve → fetch updated packages
└── cmd_publish     Validate manifest → upload (mock)

core/utils.c        File I/O, version semver, SHA-256, path helpers
core/manifest.c     TOML manifest CRUD, lockfile I/O, dep resolution,
                    package fetch/build/install, compile_project
mock/registry_mock.c  In-memory package database (no network)
toml.c              Lightweight TOML parser
registry.c          HTTP registry stubs (future)
resolver.c          PubGrub resolver stubs (future)
```

---

## ❓ Why C Needs This

C and C++ are among the oldest living languages, and their package story has always been a patchwork:

- **`apt` / `brew`** — system-wide, hard to pin versions
- **Vendored tarballs** — manual, unversioned, bloats repo
- **Git submodules** — painful, no version constraints
- **CMake `FetchContent`** — complex, CMake-specific
- **Conan / vcpkg** — powerful but heavyweight

cpm aims to be to C what npm is to JavaScript: simple, fast, opinionated, and just works.

---

## ❓ FAQ

**Q: Can I use cpm with an existing project?**
> Yes — run `cpm init` in your project root (it warns if `cpm.toml` already exists, but won't overwrite your code), then add dependencies with `cpm add`.

**Q: Does watch mode work on Windows?**
> Watch mode uses `fork()`, `nanosleep()`, and POSIX signals — it requires a POSIX shell environment on Windows (WSL, Git Bash, MSYS2, Cygwin).

**Q: What's the difference between `--delay` and polling interval?**
> cpm polls every 50 ms. `--delay` is the *debounce* — how long after the last change before restarting. A higher value avoids restarts mid-save in editors that write files in multiple steps.

**Q: Why can't I `cpm install stdio`?**
> `stdio.h` is part of the C standard library — it's built into your toolchain. cpm manages *third-party* libraries that aren't part of the standard.

---

## 📜 License

MIT © cpm contributors
