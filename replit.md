# cpm — A Package Manager for C and C++

## Overview

`cpm` brings npm-style dependency management to C and C++ projects. It manages packages, resolves version constraints, fetches/builds dependencies, and provides a nodemon-style file watcher for `cpm run`.

## Project Structure

```
cpm/
  bin/cpm                     # Compiled binary
  include/
    core/
      types.h                 # Core types (lock_package_t, lockfile_t)
      utils.h                 # Utility declarations
      manifest.h              # Manifest, lockfile, fetch/build/install
    commands/
      cmd_init.h / cmd_add.h / cmd_install.h / cmd_build.h
      cmd_run.h               # Includes run_opts_t for watch mode
      cmd_remove.h / cmd_update.h / cmd_publish.h
    mock/
      registry_mock.h         # Mock registry (no network)
    toml.h / registry.h / resolver.h
  src/
    main.c                    # CLI dispatch
    core/
      utils.c                 # I/O, semver, hashing, path helpers
      manifest.c              # Manifest CRUD, lockfile, fetch/build/install
    commands/
      cmd_init.c / cmd_add.c / cmd_install.c / cmd_build.c
      cmd_run.c               # Nodemon-style watch engine (poll + fork/kill)
      cmd_remove.c / cmd_update.c / cmd_publish.c
    mock/
      registry_mock.c         # In-memory package database
    toml.c / registry.c / resolver.c
  cpm.toml                    # Self-hosted manifest

Makefile                      # Build rules (all sources listed)
README.md                     # Full docs with emojis and install instructions
install.sh                    # Cross-platform installer (Linux/macOS/Windows)
cpm.md                        # Original architecture documentation
```

## Language & Build

- **Language**: C (C17 standard)
- **Compiler**: GCC
- **Build**: `make` from repo root, or use the workflow
- **Binary**: `cpm/bin/cpm`

## Key Features

- `cpm init [--cpp]` — scaffold C or C++ project
- `cpm add <pkg>[@ver]` — add dependency to manifest + install
- `cpm install [-u]` — resolve, fetch, build, and link dependencies
- `cpm run <script> [-w]` — run scripts with optional nodemon-style watch
- `cpm remove <pkg>` — remove dependency and prune unused packages

## Watch Mode (cmd_run.c)

The watch engine uses POSIX `fork()` + `poll()` (50ms interval) + `mtime` comparison. Flags:
- `-w / --watch` — enable
- `--ext=c,h,cpp` — extensions to track (default: c,h,cpp,cc,cxx,hpp)
- `--delay=<ms>` — debounce window (default 300ms)
- `-v / --verbose` — print changed file
- `--clear` — clear terminal before each run

## Installation

- **Windows**: `winget install cpm-pkg.cpm`
- **macOS**: `brew install cpm-pkg`
- **Linux/macOS**: `curl -fsSL https://cpm.dev/install.sh | sh`

## Important Fix

`#define _POSIX_C_SOURCE 200809L` is placed before all `#include` directives in every `.c` file to ensure `strdup`, `nanosleep`, and POSIX-only APIs are available under strict C17.
