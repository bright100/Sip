# `cpm` — A Package Manager for C and C++
### *How you'd build an `npm`-style tool for the C/C++ ecosystem*

---

## Table of Contents

1. [Why C Needs This](#1-why-c-needs-this)
2. [What the Tool Does — Bird's Eye View](#2-what-the-tool-does)
3. [The Manifest File — `cpm.toml`](#3-the-manifest-file)
4. [The Lock File — `cpm.lock`](#4-the-lock-file)
5. [The Registry](#5-the-registry)
6. [CLI Commands](#6-cli-commands)
7. [Dependency Resolution](#7-dependency-resolution)
8. [Fetching and Caching Packages](#8-fetching-and-caching-packages)
9. [The Build System Integration](#9-the-build-system-integration)
10. [Package Structure](#10-package-structure)
11. [Versioning](#11-versioning)
12. [The Global Cache](#12-the-global-cache)
13. [Linking — The Hard Part](#13-linking-the-hard-part)
14. [Security Model](#14-security-model)
15. [Internal Architecture](#15-internal-architecture)
16. [C++ Support](#16-c-support)
17. [Why You Can't `cpm install stdio`](#17-why-you-cant-cpm-install-stdio)
18. [What Makes C/C++ Different from Node](#18-what-makes-cc-different-from-node)

---

## 1. Why C Needs This

C and C++ are among the oldest living programming languages, and their package
story has always been a patchwork: system packages via `apt` or `brew`, vendored
source tarballs dropped into a `vendor/` directory, Git submodules, CMake's
`FetchContent`, Conan, vcpkg — none of which feel as natural as typing
`npm install`.

The goal of `cpm` is to replicate the *workflow* that JavaScript developers
take for granted — for both C and C++ projects:

```bash
cpm install             # read manifest, resolve deps, compile and link
cpm add libcurl         # find, fetch, and add a dependency
cpm run build           # execute a named script
```

The *reasons* this is harder in C/C++ than in JavaScript are deep and architectural.
This document explains both what `cpm` does and *why* each design decision exists.

---

## 2. What the Tool Does

At the highest level, `cpm` manages five things:

| Concern | What it means in C |
|---------|--------------------|
| **Discovery** | Find a library by name in a registry |
| **Resolution** | Choose a compatible set of versions with no conflicts |
| **Fetching** | Download source or prebuilt artifacts |
| **Building** | Compile dependencies if they need it |
| **Linking** | Wire the right headers and `.a`/`.so` files into your build |

Each of these is trivial in Node.js (JavaScript is interpreted, linking is
irrelevant, ABI compatibility is not a concern). In C, each one is a genuine
engineering problem.

---

## 3. The Manifest File

Every `cpm`-managed project has a `cpm.toml` at its root. This is the equivalent
of `package.json`. It supports both C and C++ projects — the `lang` and `std`
fields together tell `cpm` which compiler and standard to use.

```toml
[package]
name    = "myapp"
version = "1.0.0"
authors = ["Alice <alice@example.com>"]
license = "MIT"
lang    = "c++"           # "c" or "c++" — defaults to "c"
std     = "c++17"         # C++ standard to compile against

[dependencies]
libcurl   = "^8.0"
nlohmann-json = "~3.11"   # a C++-native package
libuv     = "2.0.0"

[dev-dependencies]
googletest = "^1.14"      # C++ test framework (replaces cmocka for C++ projects)

[build]
# Optional: override compiler and flags
cc      = "g++"           # cpm picks g++ automatically when lang = "c++"
cflags  = ["-O2", "-Wall", "-Wextra"]
ldflags = ["-lpthread"]

[scripts]
build   = "cpm compile src/main.cpp -o bin/myapp"
test    = "cpm compile tests/*.cpp -o bin/tests && ./bin/tests"
clean   = "rm -rf bin/ .cpm/build/"
```

### Key Fields Explained

**`[package].lang`** — Either `"c"` or `"c++"`. This controls which compiler
`cpm` invokes (`gcc`/`clang` for C, `g++`/`clang++` for C++), which standard
library is linked (`libc` vs `libc++ / libstdc++`), and which packages from
the registry are considered compatible. A package tagged as `lang = "c"` is
installable from C++ projects (C is ABI-compatible with C++ when headers use
`extern "C"`). A package tagged as `lang = "c++"` cannot be used from a pure
C project.

**`[package].std`** — The language standard. For C: `c89`, `c99`, `c11`,
`c17`, `c23`. For C++: `c++11`, `c++14`, `c++17`, `c++20`, `c++23`. This
matters because C++20 introduced modules and concepts; a library using
`std::span` or coroutines won't compile if your project is set to `c++14`.
`cpm` passes `-std=c++17` (or whichever you choose) to the compiler.

**`[dependencies]` vs `[dev-dependencies]`** — Dev dependencies get compiled
and linked only when running tests. They are never bundled into a release build.
This is identical in concept to npm's `devDependencies`.

**`[build].ldflags`** — Some libraries require explicit linker flags
(e.g., `-lpthread`, `-lm`, `-ldl`). C++ projects often also need `-lstdc++fs`
for `std::filesystem` on older compilers. npm never has to think about linkers.
`cpm` does.

---

## 4. The Lock File

The lock file is `cpm.lock`. It is generated automatically and should be
committed to version control. It is *never* edited by hand.

```toml
# cpm.lock — auto-generated, do not edit
# Generated: 2024-11-01T12:00:00Z

[[package]]
name     = "libcurl"
version  = "8.4.0"
source   = "registry+https://registry.cpm.dev"
checksum = "sha256:a4b3c2d1e5f6..."
deps     = ["zlib", "openssl"]

[[package]]
name     = "zlib"
version  = "1.3.1"
source   = "registry+https://registry.cpm.dev"
checksum = "sha256:9a8b7c6d5e4f..."
deps     = []

[[package]]
name     = "openssl"
version  = "3.1.4"
source   = "registry+https://registry.cpm.dev"
checksum = "sha256:1f2e3d4c5b6a..."
deps     = []

[[package]]
name     = "cJSON"
version  = "1.7.17"
source   = "registry+https://registry.cpm.dev"
checksum = "sha256:deadbeef1234..."
deps     = []

[[package]]
name     = "libuv"
version  = "2.0.0"
source   = "git+https://github.com/libuv/libuv?tag=v2.0.0"
checksum = "sha256:cafebabe9876..."
deps     = []
```

### Why the Lock File Exists

The lock file ensures *reproducible builds*. Without it:

- `^8.0` for libcurl might resolve to `8.4.0` on your machine and `8.5.0` on
  CI, and the difference might be a breaking API change.
- Transitive dependencies (deps of deps) are invisible in `cpm.toml` but fully
  pinned in the lock file.

When `cpm install` runs:

1. If `cpm.lock` exists → use exact versions from it (no resolution).
2. If `cpm.lock` is absent or `--update` is passed → run the resolver, write a
   new lock file.

This is identical to how `npm ci` (uses lock file) vs `npm install` (may update
lock file) behave.

---

## 5. The Registry

The registry is an HTTPS server with a well-known API. The default is
`https://registry.cpm.dev`, but it is configurable.

### Registry Endpoints

```
GET /v1/packages/{name}
    → Returns all known versions and metadata for a package

GET /v1/packages/{name}/{version}
    → Returns full metadata for a specific version

GET /v1/packages/{name}/{version}/tarball
    → Returns a .tar.gz of the package source

GET /v1/search?q={query}
    → Full-text search over package names and descriptions
```

### A Package Metadata Response

```json
{
  "name": "nlohmann-json",
  "description": "JSON for Modern C++",
  "lang": "c++",
  "latest": "3.11.3",
  "versions": {
    "3.11.3": {
      "tarball": "https://registry.cpm.dev/tarballs/nlohmann-json-3.11.3.tar.gz",
      "checksum": "sha256:deadbeef...",
      "dependencies": {},
      "headers": ["nlohmann/json.hpp"],
      "lib_type": "header-only",
      "std": "c++11"
    }
  }
}
```

Note the `lib_type: "header-only"` — a common C++ pattern where the entire
library lives in `.hpp` files with no `.cpp` to compile. `cpm` handles these
by simply copying the headers into `.cpm/deps/` with no compilation step.

### Self-Hosting

Organizations can run a private registry. The registry protocol is an open
spec, so an internal HTTP server that responds to these routes is a valid
registry. This mirrors npm's private registry support via `--registry` flag or
`.npmrc`.

---

## 6. CLI Commands

### `cpm init`

Creates a new `cpm.toml` with sane defaults, inferring the project name from
the directory name. Pass `--cpp` to initialize a C++ project:

```bash
mkdir myproject && cd myproject
cpm init           # C project (default)
cpm init --cpp     # C++ project — sets lang = "c++", std = "c++17"
# → writes cpm.toml
# → creates src/, include/, tests/ directories
# → writes a starter src/main.c (or src/main.cpp for --cpp)
```

### `cpm add <package> [version]`

Queries the registry, resolves dependencies, updates `cpm.toml` and
`cpm.lock`, and downloads/builds the new package:

```bash
cpm add libcurl          # adds latest compatible version
cpm add libcurl@8.0.0   # adds exact version
cpm add --dev cmocka     # adds to [dev-dependencies]
```

Internally: `add` = resolve + fetch + build + update manifest.

### `cpm install`

Reads `cpm.lock` (or resolves if absent), fetches all packages not already in
the local cache, builds them if needed, and sets up the include and link paths
for your project:

```bash
cpm install
# → .cpm/
#     deps/
#       libcurl-8.4.0/
#         include/curl/curl.h
#         lib/libcurl.a
#       cJSON-1.7.17/
#         include/cJSON.h
#         lib/libcjson.a
```

### `cpm build` / `cpm run <script>`

Executes the script defined in `[scripts]` in `cpm.toml`, with the environment
pre-configured so the compiler knows where to find headers and libraries:

```bash
cpm run build
# equivalent to running:
# gcc -Ic .cpm/deps/libcurl-8.4.0/include \
#        .cpm/deps/cJSON-1.7.17/include    \
#     -Lc .cpm/deps/libcurl-8.4.0/lib      \
#        .cpm/deps/cJSON-1.7.17/lib        \
#     -lcurl -lcjson                        \
#     src/main.c -o bin/myapp
```

### `cpm remove <package>`

Removes a dependency from `cpm.toml`, re-runs resolution, updates the lock
file, and removes now-unused packages from `.cpm/deps/`.

### `cpm update [package]`

Re-runs resolution ignoring the lock file, picking the newest version that
satisfies each constraint. With no argument, updates everything. With a package
name, updates only that package and its subtree.

### `cpm publish`

Packages the current project and uploads it to the registry. Validates that
`cpm.toml` is well-formed, that all declared headers exist, and that the
package builds cleanly before uploading.

---

## 7. Dependency Resolution

This is the most algorithmically complex part of the tool.

### The Problem

Suppose your project depends on:

```
myapp → libcurl ^8.0
myapp → libssl  ^3.0
libcurl → libssl ^3.1    # libcurl also needs libssl, newer minimum
```

The resolver must find a single version of `libssl` that satisfies *both*
`^3.0` (from myapp) and `^3.1` (from libcurl). In this case `3.1.x` works.

If instead:

```
libA → libssl ^2.0
libB → libssl ^3.0       # major version — breaking API changes
```

There is no single version that satisfies both. This is a genuine conflict, and
`cpm` must report it clearly rather than silently picking one.

### The Algorithm: PubGrub

`cpm` uses the **PubGrub** algorithm (the same one used by Dart's `pub` package
manager). It is a SAT-based approach that:

1. Starts with the root package's requirements.
2. Picks a package to assign a version to.
3. Derives constraints from that version's own dependencies.
4. If a contradiction is found (two constraints that can't be satisfied
   simultaneously), it backtracks and explains *why* — producing a human-readable
   error message like:

```
error: dependency conflict
  myapp requires libssl ^2.0
  libcurl 8.4.0 requires libssl ^3.0
  these ranges do not overlap

hint: try upgrading libcurl to a version that supports libssl 2.x,
      or change your libssl constraint to ^3.0
```

This is far better than the classic npm behavior of silently installing two
copies of conflicting packages — something C cannot do because linking would
then find two definitions of the same symbols.

### Why C Can't Have Duplicate Packages

In Node.js, `require('libfoo')` is resolved at runtime per-file. Two packages
can depend on different versions of `libfoo` because each gets its own copy in
its own `node_modules/`.

In C, everything is linked into a single binary. If `libA` and `libB` both
ship a function named `json_parse()`, the linker will complain about a
**multiple definition** error, or worse — silently pick one and break the
other. There is no namespace isolation at the linker level.

This means `cpm`'s resolver is *stricter* than npm's: it must find a single
globally-consistent version set. It cannot punt by installing duplicates.

---

## 8. Fetching and Caching Packages

### Sources

A package can come from multiple sources, declared in the registry metadata:

```
registry+https://registry.cpm.dev  ← official registry tarball
git+https://github.com/user/repo   ← git repo at a tag or commit
path+../mylibrary                  ← local path (for monorepos)
```

### Fetch Process

For a registry tarball:

1. Download `.tar.gz` to a temp file.
2. Verify the SHA-256 checksum against the lock file. Abort if mismatch.
3. Extract to the global cache (see section 12).
4. Symlink or copy into the project's `.cpm/deps/` directory.

For a git source:

1. `git clone --depth=1 --branch <tag>` into the global cache.
2. Record the resolved commit SHA in the lock file (not just the tag, since
   tags can be force-pushed).
3. Symlink into `.cpm/deps/`.

### Integrity Checking

Every package in the lock file has a `checksum` field. `cpm` computes the
SHA-256 of the downloaded content and refuses to proceed if they don't match.
This prevents both accidental corruption and supply-chain attacks where a
registry serves a tampered package.

---

## 9. The Build System Integration

This is where `cpm` diverges most from npm. npm never needs to compile
anything. `cpm` must compile C code.

### Two Modes of Package Distribution

**Mode A: Source packages**

The package ships `.c` and `.h` files. `cpm` compiles them during `install`.
The compiled output (`.a` static library or `.so` shared library) is cached
so it only gets compiled once.

**Mode B: Prebuilt binary packages**

The package ships pre-compiled `.a`/`.so` files for specific platforms
(linux-x86_64, macos-arm64, etc.). `cpm` selects the right binary for the
host platform, skipping compilation entirely. This is faster but requires
the package maintainer to publish builds for every target.

### Compiling a Source Package

When `cpm` builds a source package, it:

1. Reads the package's own `cpm.toml` for build configuration.
2. Gathers all of *its* dependencies' include paths.
3. Runs the compiler to produce object files:
   ```bash
   gcc -c -O2 -std=c11 \
       -I.cpm/deps/dep1/include \
       -I.cpm/deps/dep2/include \
       src/foo.c -o .cpm/build/foo.o
   ```
4. Archives the object files into a static library:
   ```bash
   ar rcs .cpm/build/libfoo.a .cpm/build/foo.o
   ```

### Generated Compiler Flags

After `cpm install`, `cpm` writes a file called `.cpm/flags.env`:

```bash
# .cpm/flags.env — source this in your build scripts
CPM_CFLAGS="-I.cpm/deps/libcurl-8.4.0/include -I.cpm/deps/cJSON-1.7.17/include"
CPM_LDFLAGS="-L.cpm/deps/libcurl-8.4.0/lib -L.cpm/deps/cJSON-1.7.17/lib"
CPM_LIBS="-lcurl -lcjson"
```

It also writes `.cpm/compile_commands.json` — the standard file that editors
like VS Code, Vim (via clangd), and CLion use for code intelligence. This
gives you autocomplete and jump-to-definition for your dependencies
automatically.

---

## 10. Package Structure

A `cpm`-compatible library must follow this layout:

```
mylibrary/
├── cpm.toml          ← package manifest
├── include/
│   └── mylibrary.h   ← public headers (what consumers #include)
├── src/
│   └── mylibrary.c   ← implementation (compiled into libmylibrary.a)
├── tests/
│   └── test_main.c
└── README.md
```

### The Public Header Rule

Only files under `include/` are exposed to consumers of the library.
Files under `src/` are implementation details — their function declarations
won't be visible to users of the package. This is enforced by `cpm`: when
it installs a package, it copies only the `include/` directory into
`.cpm/deps/<name>/include/`.

This mirrors the conventional C practice of separating public API headers
from internal implementation headers, but makes it structural and automatic.

---

## 11. Versioning

`cpm` uses **Semantic Versioning** (semver): `MAJOR.MINOR.PATCH`.

| Part | Meaning in C |
|------|-------------|
| MAJOR | Breaking ABI or API change (removed function, changed struct layout) |
| MINOR | New functions added, backward-compatible |
| PATCH | Bug fix, no API change |

### Version Constraints

The manifest supports npm-style constraint syntax:

| Constraint | Meaning |
|-----------|---------|
| `"8.4.0"` | Exactly this version |
| `"^8.0"` | `>=8.0.0, <9.0.0` — compatible within major |
| `"~1.7.15"` | `>=1.7.15, <1.8.0` — compatible within minor |
| `">=2.0, <3.0"` | Explicit range |
| `"*"` | Any version (not recommended) |

### ABI Compatibility — The C-Specific Wrinkle

In JavaScript, if a function's signature changes, you get a runtime error
or undefined behavior. In C, if a struct grows a new field and you compiled
your code against the old header, you will be passing the wrong amount of
memory around — silently, with no error, causing corruption that surfaces
unpredictably.

This is why major version bumps in C libraries are taken very seriously.
`cpm` enforces that your code and all dependencies are compiled against the
same version of any shared dependency. It also records, in package metadata,
the *minimum* compiler version required and the `sizeof` of key public structs,
so it can warn you if ABI assumptions have changed.

---

## 12. The Global Cache

To avoid re-downloading and re-compiling the same package across multiple
projects, `cpm` maintains a global cache:

```
~/.cpm/
├── registry/
│   └── cache/
│       └── registry.cpm.dev/
│           └── nlohmann-json/
│               └── 3.11.3/
│                   └── nlohmann-json-3.11.3.tar.gz
├── src/
│   └── nlohmann-json-3.11.3/    ← extracted source
│       └── nlohmann/
│           └── json.hpp
└── build/
    └── linux-x86_64-g++-13-c++17/
        └── nlohmann-json-3.11.3/
            └── include/
                └── nlohmann/
                    └── json.hpp  ← header-only: no .a needed
```

The build cache directory is keyed by:

- **OS + architecture** (`linux-x86_64`)
- **Compiler and version** (`gcc-13` or `g++-13`)
- **Language standard** (`c11`, `c++17`, etc.)
- **Package name and version** (`nlohmann-json-3.11.3`)

This means if you switch from `gcc` to `clang`, from `-std=c++17` to
`-std=c++20`, or from a C to a C++ project, affected packages are recompiled.
If nothing changes, compiled `.a` files are reused instantly.

---

## 13. Linking — The Hard Part

Linking is where the difference between C and JavaScript is sharpest.

### Static vs Dynamic Linking

**Static linking** (`.a` files): The library's compiled code is copied
directly into your executable at link time. Your binary is self-contained.
No runtime dependencies. Larger binary size.

**Dynamic linking** (`.so` / `.dylib` files): Your executable records a
reference to the library. The OS loads the library at runtime. Smaller
binary, but the `.so` must be present on the target machine.

`cpm` defaults to **static linking** for packages it manages, because:

- It produces self-contained binaries that are easier to deploy.
- It avoids "DLL hell" — the runtime version of the library not matching
  what was compiled against.
- It makes the package manager's job predictable: no need to manage runtime
  library paths.

You can opt into dynamic linking per-package in `cpm.toml`:

```toml
[dependencies]
libcurl = { version = "^8.0", link = "dynamic" }
```

### System Libraries

Some libraries (`pthread`, `libm`, `libdl`, OpenSSL on some systems) exist
as system packages and should not be vendored by `cpm`. These are declared
as **system dependencies**:

```toml
[system-dependencies]
openssl = ">=1.1"     # resolved via pkg-config, not the cpm registry
pthread = "*"
```

`cpm` resolves system dependencies using `pkg-config`:

```bash
pkg-config --cflags openssl   # → -I/usr/include/openssl
pkg-config --libs openssl     # → -lssl -lcrypto
```

If a system dependency is missing, `cpm` prints an actionable error:

```
error: system dependency 'openssl' not found
  install it with:
    apt:    sudo apt install libssl-dev
    brew:   brew install openssl
    pacman: sudo pacman -S openssl
```

### Circular Dependencies

`cpm` detects and rejects circular dependencies (A depends on B which depends
on A). This is a hard error — circular C library dependencies cannot be linked.

---

## 14. Security Model

### Checksum Verification

Every package in the lock file has a SHA-256 checksum. `cpm` verifies it on
every download and refuses to proceed on mismatch. This catches both
accidental corruption and malicious tampering.

### Signature Verification (Optional)

Package maintainers can sign their releases with an Ed25519 key. The registry
stores the public key; `cpm` verifies the signature before extraction. This is
opt-in but strongly recommended for security-sensitive libraries.

### No Lifecycle Scripts (By Default)

npm packages can declare `postinstall` scripts that run arbitrary code when
the package is installed. This is a significant supply-chain attack vector.

`cpm` does not run arbitrary lifecycle scripts. The only code that runs during
`cpm install` is:

1. The compiler (`gcc`/`clang`), on the package's source files.
2. `ar`, to create the static library archive.

There are no hooks, no `postinstall`, no shell scripts from untrusted packages.
Custom build steps, if a package truly needs them, must be explicitly allowlisted
in your project's `cpm.toml` under `[allowed-build-scripts]`, which shows up
as a security warning and must be confirmed interactively.

---

## 15. Internal Architecture

Here is how `cpm` itself is structured as a C application:

```
cpm/
├── src/
│   ├── main.c              ← CLI entry point, argument parsing
│   ├── manifest.c          ← Parse and write cpm.toml
│   ├── lockfile.c          ← Parse and write cpm.lock
│   ├── registry.c          ← HTTP client for registry API calls
│   ├── resolver.c          ← PubGrub dependency resolution algorithm
│   ├── fetcher.c           ← Download tarballs and git repos
│   ├── cache.c             ← Global cache read/write
│   ├── builder.c           ← Invoke compiler/ar on source packages
│   ├── linker.c            ← Compute -I / -L / -l flag sets
│   ├── pkgconfig.c         ← Resolve system dependencies via pkg-config
│   └── hash.c              ← SHA-256 implementation
├── include/
│   └── cpm.h
└── cpm.toml                ← cpm manages itself
```

### Key Data Structures

```c
// A parsed cpm.toml
typedef struct {
    char *name;
    char *version;
    CStandard std;            // C89, C99, C11, C17, C23
    Dependency *deps;         // array of [dependencies]
    Dependency *dev_deps;     // array of [dev-dependencies]
    SystemDep  *sys_deps;     // array of [system-dependencies]
    BuildConfig build;
    Script     *scripts;
} Manifest;

// A single dependency declaration
typedef struct {
    char *name;
    VersionReq  req;          // parsed constraint e.g. "^8.0"
    LinkMode    link;         // LINK_STATIC or LINK_DYNAMIC
    bool        optional;
} Dependency;

// A fully resolved package (entry in cpm.lock)
typedef struct {
    char *name;
    Version    version;       // exact resolved version
    Source     source;        // registry, git, or path
    uint8_t    checksum[32];  // SHA-256
    char      **deps;         // names of resolved transitive deps
} ResolvedPackage;

// The resolution output
typedef struct {
    ResolvedPackage *packages;
    size_t           count;
} Resolution;
```

### The Resolution to Build Pipeline

```
cpm install
    │
    ├─ read_manifest("cpm.toml")         → Manifest
    ├─ read_lockfile("cpm.lock")         → Resolution (if exists)
    │      or
    │  resolve(manifest, registry)       → Resolution (if no lockfile)
    │
    ├─ write_lockfile(resolution)        → cpm.lock
    │
    ├─ for each package in resolution:
    │   ├─ cache_lookup(pkg)             → hit or miss
    │   ├─ if miss: fetch(pkg)           → downloads tarball/git
    │   ├─ if miss: build(pkg)           → compiles to .a
    │   └─ install_to_project(pkg)       → symlinks into .cpm/deps/
    │
    ├─ compute_flags(resolution)         → CPM_CFLAGS, CPM_LDFLAGS, CPM_LIBS
    └─ write_flags(".cpm/flags.env")
       write_compile_commands(".cpm/compile_commands.json")
```

---

## 16. C++ Support

C++ compatibility is not just a matter of swapping `gcc` for `g++`. There are
several dimensions where C++ projects behave differently, and `cpm` handles
each one explicitly.

### Name Mangling and `extern "C"`

C++ compilers *mangle* symbol names to encode type information. A C function
`int add(int, int)` becomes something like `_Z3addii` in a C++ binary. This
means a C library compiled with `gcc` cannot be linked from a C++ project
unless its headers declare their symbols with `extern "C"`.

`cpm` enforces a rule for packages in the registry: any package with
`lang = "c"` must wrap its public header in an `extern "C"` guard:

```c
/* mylibrary.h */
#ifdef __cplusplus
extern "C" {
#endif

int mylibrary_init(void);
void mylibrary_free(void);

#ifdef __cplusplus
}
#endif
```

`cpm publish` validates this automatically. If a C package's headers lack the
guard, it is tagged as `cpp-unsafe` in the registry and `cpm` will warn you
when adding it to a C++ project.

### Header-Only Libraries

Many modern C++ libraries (nlohmann/json, {fmt}, Catch2, range-v3) ship as
*header-only* — all code lives in `.hpp` files, using templates and `inline`
functions. There is nothing to compile.

`cpm` handles these with `lib_type = "header-only"` in the registry metadata.
During `cpm install`, it skips the compile step entirely and just places the
headers in `.cpm/deps/<name>/include/`. The cache key still includes the
compiler and standard (template instantiation is sensitive to both), but no
`.a` is produced.

### C++ Standard Library — `libstdc++` vs `libc++`

`gcc` links against `libstdc++` by default. `clang` can use either `libstdc++`
or its own `libc++`. This matters when mixing packages:

- If package A was compiled with `libstdc++` and package B with `libc++`, their
  `std::string` types are *different binary types* — passing one to the other
  is undefined behavior.

`cpm` records the standard library in the build cache key:
`linux-x86_64-clang-17-libc++-c++20`. If your project and all your packages
don't agree on the standard library, `cpm` raises an error during resolution
rather than producing a silently broken binary.

You can set your standard library preference in `cpm.toml`:

```toml
[build]
stdlib = "libc++"     # or "libstdc++" — cpm passes -stdlib=libc++
```

### Mixing C and C++ Packages

A C++ project can freely depend on C packages (with proper `extern "C"` guards).
The reverse is not true — a C project cannot include C++ packages, because C
has no concept of classes, templates, or RAII.

`cpm` enforces this at resolution time:

```
error: C project 'myapp' cannot depend on C++ package 'folly'
  folly requires lang = "c++"
  your project is lang = "c"

hint: if you want C++ features, change your manifest to lang = "c++"
```

### ABI Stability in C++

C++ ABI stability is significantly more fragile than C. Changing a class by
adding a private data member, changing the order of virtual functions, or even
changing a default argument can break ABI. `cpm` handles this by:

1. Treating any ABI-breaking change as a **major version bump** (enforced in
   registry submission rules).
2. Recording the compiler's ABI tag (e.g., GCC's `_GLIBCXX_USE_CXX11_ABI`)
   in the build cache key, so old and new ABI variants are stored separately.

---

## 17. Why You Can't `cpm install stdio`

This is a common point of confusion, especially coming from JavaScript where
`require('path')` or `import fs from 'fs'` feel like package imports.

**`stdio.h` is not a package. It is part of the C standard library, which ships
with your compiler.**

When you write `#include <stdio.h>`, the compiler resolves this to a file that
already exists on your system — something like `/usr/include/stdio.h` on Linux,
or a path inside your Xcode toolchain on macOS. There is nothing to download.

```bash
$ cpm install stdio
error: 'stdio' is not a package

'stdio.h' is part of the C standard library, which is provided by your
compiler toolchain. It is already available — no installation needed.

Standard library headers you can use without cpm:
  <stdio.h>    — printf, scanf, fopen, fclose, FILE*
  <stdlib.h>   — malloc, free, exit, atoi
  <string.h>   — memcpy, strlen, strcpy, strcmp
  <math.h>     — sin, cos, sqrt, pow (link with -lm)
  <stdint.h>   — uint8_t, int32_t, uint64_t, etc.
  <stddef.h>   — size_t, ptrdiff_t, NULL
  <stdbool.h>  — bool, true, false  (C99+)
  <assert.h>   — assert()
  <errno.h>    — errno, EINVAL, ENOENT, etc.
  <time.h>     — time_t, clock(), time(), struct tm
  <limits.h>   — INT_MAX, SIZE_MAX, etc.

For C++, the equivalents are:
  <iostream>   — std::cout, std::cin
  <string>     — std::string
  <vector>     — std::vector
  <map>        — std::map
  <memory>     — std::unique_ptr, std::shared_ptr
  <algorithm>  — std::sort, std::find, std::transform
  <filesystem> — std::filesystem::path (C++17)
  <thread>     — std::thread (link with -lpthread)
  <format>     — std::format (C++20)

If you're looking for a higher-level I/O library beyond stdio, try:
  cpm add libuv       — async I/O (like Node's libuv — literally the same lib)
  cpm add spdlog      — fast C++ logging
  cpm add fmt         — {fmt} formatting library (basis for C++20 std::format)
```

### The Distinction in Full

| What it is | How you get it | Example |
|-----------|---------------|---------|
| C/C++ standard library | Ships with the compiler — already there | `stdio.h`, `<vector>` |
| POSIX library | Ships with the OS — already there | `<unistd.h>`, `<pthread.h>` |
| System library | Install via OS package manager | `libssl-dev`, `libz-dev` |
| Third-party library | `cpm add <name>` | `libcurl`, `nlohmann-json` |

`cpm` manages only the last category. The first three are outside its scope —
they are either guaranteed to be present by the compiler or the operating system.

---

## 18. What Makes C/C++ Different from Node

It's worth being explicit about why `cpm` cannot just be "npm but for C/C++":

| Concern | npm / JavaScript | cpm / C and C++ |
|---------|-----------------|---------|
| **Execution model** | Interpreted at runtime | Compiled to native binary |
| **Linking** | Not applicable | Static or dynamic, must be resolved |
| **ABI** | No concept | Struct layout, vtable layout, name mangling — breaks on compiler upgrade |
| **Standard library** | Built-in, always present | Ships with compiler — not a package |
| **Duplicate packages** | Fine (each has its own `node_modules`) | Fatal — linker sees duplicate symbols |
| **Platform** | One JS runtime | Per-platform binaries; must rebuild per OS/arch/compiler/stdlib |
| **Headers** | No concept | Must be distributed alongside the library |
| **Header-only libs** | No concept | Entire library may live in `.hpp` files — nothing to compile |
| **Name mangling** | No concept | C++ symbols are mangled; C libs need `extern "C"` to link from C++ |
| **Build scripts** | `postinstall` runs freely | Restricted for security; no arbitrary code execution |
| **System libs** | No concept | Must interop with OS-provided libraries via pkg-config |
| **Name conflicts** | Namespaced | Two libs exporting `init()` will break the linker |

These differences explain every design decision in `cpm`: the stricter resolver,
the platform-keyed cache, the `lang` and `stdlib` fields, the security model
around build scripts, the `system-dependencies` section, and the generated
compiler flags.

The goal is not to pretend C or C++ is JavaScript. It is to give developers the
same *workflow clarity* — `add`, `install`, `run` — while handling all the
C/C++-specific complexity automatically, out of view, until it becomes relevant
to you.

---

*`cpm` as described here is a design document, not an existing tool. The closest
real-world implementations are [Conan](https://conan.io),
[vcpkg](https://vcpkg.io), and [clib](https://github.com/clibs/clib) — each
of which makes different tradeoffs across the dimensions described above.*
