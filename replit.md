# cpm — A Package Manager for C and C++

## Overview

`cpm` is a CLI tool that brings an npm-style workflow to C and C++ projects. It manages dependencies, resolves versions, fetches packages, and integrates with the build system.

## Project Structure

```
cpm/
  bin/cpm         # Compiled binary
  src/
    main.c        # Main CLI entry point (~1600 lines)
    registry.c    # Package registry client
    resolver.c    # Dependency resolution logic
    toml.c        # TOML manifest parser
  include/
    registry.h
    resolver.h
    toml.h
  cpm.toml        # Project manifest (self-hosted)
cpm.md            # Detailed documentation on design and architecture
Makefile          # Build rules
```

## Language & Build

- **Language**: C (C17 standard)
- **Compiler**: GCC
- **Build command**: `cd cpm && gcc -O2 -Wall -Wextra -std=c17 -Iinclude src/main.c src/registry.c src/resolver.c src/toml.c -o bin/cpm`
- **Run**: `./cpm/bin/cpm --help`

## Important Fix Applied

The `#define _POSIX_C_SOURCE 200809L` was moved to **before** all `#include` directives in `src/main.c` to ensure `strdup` and other POSIX functions are declared correctly.

## CLI Commands

```
cpm init [--cpp]           Initialize a new project
cpm add <package> [ver]    Add a dependency
cpm install [-u]           Install dependencies
cpm build                  Build the project
cpm run <script>           Run a script from cpm.toml
cpm remove <package>       Remove a dependency
cpm update [package]       Update dependencies
cpm publish                Publish package to registry
```

## Workflow

The "Build cpm" workflow compiles the project and shows the help output in the console.
