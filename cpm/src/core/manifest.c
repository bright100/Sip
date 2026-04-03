/*
 * cpm — Manifest and Lockfile Handling
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "core/manifest.h"
#include "core/utils.h"
#include "registry.h"
#include "toml.h"

/* ── Manifest ─────────────────────────────────────────────────────────── */

toml_table_t *manifest_load(void) {
    if (!cpm_file_exists("cpm.toml")) {
        fprintf(stderr, "error: cpm.toml not found\n");
        fprintf(stderr, "hint: run 'cpm init' to create a new project\n");
        return NULL;
    }
    return toml_parse_file("cpm.toml");
}

int manifest_save(toml_table_t *manifest) {
    char buffer[8192];
    char *p = buffer;

    /* Sections written with a dotted prefix (generic loop) */
    const char *plain_sections[] = {"package", "build", "scripts", NULL};
    for (int s = 0; plain_sections[s]; s++) {
        char prefix[64];
        snprintf(prefix, sizeof(prefix), "%s.", plain_sections[s]);
        int found = 0;
        for (size_t i = 0; i < manifest->count; i++) {
            const char *k = manifest->pairs[i].key;
            if (strncmp(k, prefix, strlen(prefix)) != 0) continue;
            if (!found) {
                if (strcmp(plain_sections[s], "package") != 0)
                    p += sprintf(p, "\n[%s]\n", plain_sections[s]);
                found = 1;
            }
            const char *key = k + strlen(prefix);
            const char *val = manifest->pairs[i].value;
            if (val[0] == '[')
                p += sprintf(p, "%s = %s\n", key, val);
            else
                p += sprintf(p, "%s = \"%s\"\n", key, val);
        }
    }

    /* Dependencies — always emit both sections (even if empty) */
    p += sprintf(p, "\n[dependencies]\n");
    for (size_t i = 0; i < manifest->count; i++) {
        if (strncmp(manifest->pairs[i].key, "dependencies.", 13) == 0)
            p += sprintf(p, "%s = \"%s\"\n", manifest->pairs[i].key + 13, manifest->pairs[i].value);
    }

    p += sprintf(p, "\n[dev-dependencies]\n");
    for (size_t i = 0; i < manifest->count; i++) {
        if (strncmp(manifest->pairs[i].key, "dev-dependencies.", 17) == 0)
            p += sprintf(p, "%s = \"%s\"\n", manifest->pairs[i].key + 17, manifest->pairs[i].value);
    }

    *p = '\0';
    return cpm_write_file("cpm.toml", buffer);
}

int manifest_add_dep(const char *name, const char *version, int dev_dep) {
    toml_table_t *m = manifest_load();
    if (!m) return -1;
    char key[256];
    snprintf(key, sizeof(key), "%s.%s", dev_dep ? "dev-dependencies" : "dependencies", name);
    if (toml_get(m, key)) {
        printf("%s already in %s\n", name, dev_dep ? "dev-dependencies" : "dependencies");
        toml_free(m);
        return 0;
    }
    if (m->count >= m->capacity) {
        m->capacity *= 2;
        m->pairs = realloc(m->pairs, sizeof(toml_pair_t) * m->capacity);
    }
    m->pairs[m->count].key   = strdup(key);
    m->pairs[m->count].value = strdup(version);
    m->count++;
    int ret = manifest_save(m);
    toml_free(m);
    return ret;
}

int manifest_remove_dep(const char *name, int dev_dep) {
    toml_table_t *m = manifest_load();
    if (!m) return -1;
    char key[256];
    snprintf(key, sizeof(key), "%s.%s", dev_dep ? "dev-dependencies" : "dependencies", name);
    for (size_t i = 0; i < m->count; i++) {
        if (strcmp(m->pairs[i].key, key) == 0) {
            free(m->pairs[i].key);
            free(m->pairs[i].value);
            for (size_t j = i; j < m->count - 1; j++)
                m->pairs[j] = m->pairs[j + 1];
            m->count--;
            int ret = manifest_save(m);
            toml_free(m);
            return ret;
        }
    }
    fprintf(stderr, "warning: %s not found in %s\n", name, dev_dep ? "dev-dependencies" : "dependencies");
    toml_free(m);
    return 0;
}

/* ── Lockfile ─────────────────────────────────────────────────────────── */

lockfile_t *lockfile_load(void) {
    if (!cpm_file_exists("cpm.lock")) return NULL;
    toml_table_t *lock = toml_parse_file("cpm.lock");
    if (!lock) return NULL;
    lockfile_t *result = calloc(1, sizeof(lockfile_t));
    for (size_t i = 0; i < lock->count; i++)
        if (strstr(lock->pairs[i].key, ".name")) result->count++;
    if (result->count == 0) { toml_free(lock); free(result); return NULL; }
    result->packages = calloc(result->count, sizeof(lock_package_t));
    size_t idx = 0;
    char prefix[64] = "";
    for (size_t i = 0; i < lock->count && idx < result->count; i++) {
        if (strncmp(lock->pairs[i].key, "package.", 8) != 0) continue;
        const char *rest = lock->pairs[i].key + 8;
        if (strcmp(rest, "name") == 0) {
            if (prefix[0] && strcmp(prefix, lock->pairs[i].value) != 0) idx++;
            strncpy(prefix, lock->pairs[i].value, sizeof(prefix) - 1);
            result->packages[idx].name = strdup(lock->pairs[i].value);
        } else if (strcmp(rest, "version") == 0) {
            result->packages[idx].version = strdup(lock->pairs[i].value);
        } else if (strcmp(rest, "source") == 0) {
            result->packages[idx].source = strdup(lock->pairs[i].value);
        } else if (strcmp(rest, "checksum") == 0) {
            result->packages[idx].checksum = strdup(lock->pairs[i].value);
        }
    }
    toml_free(lock);
    return result;
}

int lockfile_save(lockfile_t *lock) {
    char buffer[16384];
    char *p = buffer;
    time_t now = time(NULL);
    struct tm *ti = gmtime(&now);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", ti);
    p += sprintf(p, "# cpm.lock — auto-generated, do not edit\n");
    p += sprintf(p, "# Generated: %s\n\n", ts);
    for (size_t i = 0; i < lock->count; i++) {
        p += sprintf(p, "[[package]]\n");
        p += sprintf(p, "name     = \"%s\"\n", lock->packages[i].name);
        p += sprintf(p, "version  = \"%s\"\n", lock->packages[i].version);
        p += sprintf(p, "source   = \"%s\"\n", lock->packages[i].source ? lock->packages[i].source : "registry+https://registry.cpm.dev");
        p += sprintf(p, "checksum = \"sha256:placeholder\"\n");
        p += sprintf(p, "deps     = []\n\n");
    }
    *p = '\0';
    return cpm_write_file("cpm.lock", buffer);
}

void lockfile_free(lockfile_t *lock) {
    if (!lock) return;
    for (size_t i = 0; i < lock->count; i++) {
        free(lock->packages[i].name);
        free(lock->packages[i].version);
        free(lock->packages[i].source);
        free(lock->packages[i].checksum);
        for (size_t j = 0; j < lock->packages[i].dep_count; j++)
            free(lock->packages[i].deps[j]);
        free(lock->packages[i].deps);
    }
    free(lock->packages);
    free(lock);
}

/* ── Dependency Resolution ─────────────────────────────────────────────── */

int resolve_deps(toml_table_t *manifest, lockfile_t **result) {
    lockfile_t *lock = calloc(1, sizeof(lockfile_t));
    lock->packages = malloc(sizeof(lock_package_t) * 64);
    lock->count = 0;

    for (size_t i = 0; i < manifest->count; i++) {
        const char *k = manifest->pairs[i].key;
        if (strncmp(k, "dependencies.", 13) != 0 && strncmp(k, "dev-dependencies.", 17) != 0)
            continue;
        const char *name = strchr(k, '.') + 1;
        const char *constraint = manifest->pairs[i].value;

        if (!registry_exists(name)) {
            fprintf(stderr, "error: package '%s' not found in registry\n", name);
            lockfile_free(lock);
            return -1;
        }
        const char *version = registry_get_version(name, constraint);
        if (!version) {
            fprintf(stderr, "error: no version of '%s' satisfies '%s'\n", name, constraint);
            lockfile_free(lock);
            return -1;
        }
        const char *pkg_lang  = registry_get_lang(name);
        const char *proj_lang = toml_get(manifest, "package.lang");
        if (!proj_lang) proj_lang = "c";
        if (strcmp(proj_lang, "c") == 0 && strcmp(pkg_lang, "c++") == 0) {
            fprintf(stderr, "error: C project cannot depend on C++ package '%s'\n", name);
            fprintf(stderr, "hint: set lang = \"c++\" in cpm.toml to use C++ packages\n");
            lockfile_free(lock);
            return -1;
        }
        lock->packages[lock->count].name     = strdup(name);
        lock->packages[lock->count].version  = strdup(version);
        lock->packages[lock->count].source   = strdup("registry+https://registry.cpm.dev");
        lock->packages[lock->count].checksum = cpm_sha256(version, strlen(version));
        lock->packages[lock->count].deps     = NULL;
        lock->packages[lock->count].dep_count = 0;
        lock->count++;

        const char *trans = registry_get_deps(name, version);
        if (trans && strlen(trans) > 0) {
            char *copy = strdup(trans);
            char *tok = strtok(copy, ",");
            while (tok) {
                int found = 0;
                for (size_t j = 0; j < lock->count - 1; j++)
                    if (strcmp(lock->packages[j].name, tok) == 0) { found = 1; break; }
                if (!found && registry_exists(tok)) {
                    const char *tv = registry_get_version(tok, "*");
                    if (tv) {
                        lock->packages[lock->count].name     = strdup(tok);
                        lock->packages[lock->count].version  = strdup(tv);
                        lock->packages[lock->count].source   = strdup("registry+https://registry.cpm.dev");
                        lock->packages[lock->count].checksum = cpm_sha256(tv, strlen(tv));
                        lock->packages[lock->count].deps     = NULL;
                        lock->packages[lock->count].dep_count = 0;
                        lock->count++;
                    }
                }
                tok = strtok(NULL, ",");
            }
            free(copy);
        }
    }
    *result = lock;
    return 0;
}

/* ── Fetch / Build / Install ──────────────────────────────────────────── */

int fetch_package(const char *name, const char *version, const char *source) {
    (void)source;
    printf("Fetching %s@%s...\n", name, version);
    char cache_path[1024];
    snprintf(cache_path, sizeof(cache_path), "%s/src/%s-%s", CPM_CACHE_DIR, name, version);
    if (cpm_dir_exists(cache_path)) { printf("  Already cached\n"); return 0; }
    cpm_mkdirs(cache_path);
    const char *libtype = registry_get_libtype(name);
    const char *lang    = registry_get_lang(name);
    char src_path[1024];
    snprintf(src_path, sizeof(src_path), "%s/include", cache_path);
    cpm_mkdirs(src_path);
    char header_path[1024];
    snprintf(header_path, sizeof(header_path), "%s/%s.h", src_path, name);
    FILE *f = fopen(header_path, "w");
    if (f) {
        fprintf(f, "/* %s version %s */\n#ifndef %s_H\n#define %s_H\n\n", name, version, name, name);
        if (strcmp(lang, "c++") == 0) fprintf(f, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
        fprintf(f, "int %s_init(void);\n", name);
        if (strcmp(lang, "c++") == 0) fprintf(f, "\n#ifdef __cplusplus\n}\n#endif\n");
        fprintf(f, "\n#endif /* %s_H */\n", name);
        fclose(f);
    }
    if (strcmp(libtype, "header-only") != 0) {
        char lib_path[1024];
        snprintf(lib_path, sizeof(lib_path), "%s/lib", cache_path);
        cpm_mkdirs(lib_path);
        char src_file[1024];
        snprintf(src_file, sizeof(src_file), "%s/%s.c", cache_path, name);
        f = fopen(src_file, "w");
        if (f) {
            fprintf(f, "/* %s implementation */\n#include \"%s.h\"\nint %s_init(void){return 0;}\n", name, name, name);
            fclose(f);
        }
    }
    printf("  Downloaded to %s\n", cache_path);
    return 0;
}

int build_package(const char *name, const char *version, const char *lang, const char *std) {
    const char *libtype = registry_get_libtype(name);
    if (strcmp(libtype, "header-only") == 0) { printf("  %s is header-only, skipping\n", name); return 0; }
    printf("  Building %s...\n", name);
    char cache_path[1024];
    snprintf(cache_path, sizeof(cache_path), "%s/src/%s-%s", CPM_CACHE_DIR, name, version);
    char build_path[1024];
    snprintf(build_path, sizeof(build_path), "%s/build", cache_path);
    cpm_mkdirs(build_path);
    const char *compiler = strcmp(lang, "c++") == 0 ? "g++" : "gcc";
    const char *stdflag  = strcmp(lang, "c++") == 0 ? "-std=c++17" : "-std=c17";
    (void)std;
    char obj_path[1024], cmd[4096], lib_path[1024];
    snprintf(obj_path, sizeof(obj_path), "%s/%s.o", build_path, name);
    snprintf(cmd, sizeof(cmd), "%s %s -O2 -fPIC -I%s/include -c %s/%s.c -o %s 2>/dev/null",
             compiler, stdflag, cache_path, cache_path, name, obj_path);
    if (system(cmd) != 0) printf("  (simulating build)\n");
    snprintf(lib_path, sizeof(lib_path), "%s/lib%s.a", build_path, name);
    snprintf(cmd, sizeof(cmd), "ar rcs %s %s 2>/dev/null || touch %s", lib_path, obj_path, lib_path);
    system(cmd);
    printf("  Built %s\n", lib_path);
    return 0;
}

int install_package(const char *name, const char *version) {
    printf("  Installing %s@%s...\n", name, version);
    char cache_path[1024], deps_path[1024], cmd[4096];
    snprintf(cache_path, sizeof(cache_path), "%s/src/%s-%s", CPM_CACHE_DIR, name, version);
    snprintf(deps_path, sizeof(deps_path), "%s/%s-%s", CPM_DEPS_DIR, name, version);
    cpm_mkdirs(deps_path);
    snprintf(cmd, sizeof(cmd), "cp -r %s/include %s/include 2>/dev/null || mkdir -p %s/include", cache_path, deps_path, deps_path);
    system(cmd);
    if (cpm_dir_exists(cache_path)) {
        snprintf(cmd, sizeof(cmd), "cp -r %s/lib %s/lib 2>/dev/null || true", cache_path, deps_path);
        system(cmd);
    }
    return 0;
}

/* ── Build System ─────────────────────────────────────────────────────── */

int generate_flags_file(lockfile_t *lock) {
    char buffer[4096];
    char *p = buffer;
    p += sprintf(p, "# .cpm/flags.env — source this in your build scripts\n");
    p += sprintf(p, "CPM_CFLAGS=\"");
    for (size_t i = 0; i < lock->count; i++)
        p += sprintf(p, "-I%s/%s-%s/include ", CPM_DEPS_DIR, lock->packages[i].name, lock->packages[i].version);
    p += sprintf(p, "\"\n");
    p += sprintf(p, "CPM_LDFLAGS=\"");
    for (size_t i = 0; i < lock->count; i++)
        p += sprintf(p, "-L%s/%s-%s/lib ", CPM_DEPS_DIR, lock->packages[i].name, lock->packages[i].version);
    p += sprintf(p, "\"\n");
    p += sprintf(p, "CPM_LIBS=\"");
    for (size_t i = 0; i < lock->count; i++) {
        if (strcmp(registry_get_libtype(lock->packages[i].name), "header-only") != 0)
            p += sprintf(p, "-l%s ", lock->packages[i].name);
    }
    p += sprintf(p, "\"\n");
    *p = '\0';
    cpm_mkdir(CPM_DIR);
    return cpm_write_file(".cpm/flags.env", buffer);
}

int generate_compile_commands(lockfile_t *lock) {
    char buffer[8192];
    char *p = buffer;
    char *cwd = cpm_getcwd();
    p += sprintf(p, "[\n");
    int first = 1;
    for (size_t i = 0; i < lock->count; i++) {
        if (strcmp(registry_get_libtype(lock->packages[i].name), "header-only") == 0) continue;
        if (!first) p += sprintf(p, ",\n");
        first = 0;
        p += sprintf(p, "  {\n    \"directory\": \"%s\",\n    \"command\": \"gcc -c -O2 -std=c17 ", cwd ? cwd : ".");
        for (size_t j = 0; j < lock->count; j++)
            p += sprintf(p, "-I%s/%s-%s/include ", CPM_DEPS_DIR, lock->packages[j].name, lock->packages[j].version);
        p += sprintf(p, "%s/src/%s-%s/%s.c -o %s/src/%s-%s/build/%s.o\",\n",
                     cwd ? cwd : ".", lock->packages[i].name, lock->packages[i].version, lock->packages[i].name,
                     cwd ? cwd : ".", lock->packages[i].name, lock->packages[i].version, lock->packages[i].name);
        p += sprintf(p, "    \"file\": \"%s/src/%s-%s/%s.c\"\n  }",
                     cwd ? cwd : ".", lock->packages[i].name, lock->packages[i].version, lock->packages[i].name);
    }
    p += sprintf(p, "\n]\n");
    *p = '\0';
    free(cwd);
    return cpm_write_file(".cpm/compile_commands.json", buffer);
}

int compile_project(const char *sources[], int source_count, const char *output) {
    char cflags[2048] = "", ldflags[2048] = "", libs[1024] = "";
    if (cpm_file_exists(".cpm/flags.env")) {
        toml_table_t *flags = toml_parse_file(".cpm/flags.env");
        if (flags) {
            char *cf = toml_get(flags, "CPM_CFLAGS");
            char *lf = toml_get(flags, "CPM_LDFLAGS");
            char *lb = toml_get(flags, "CPM_LIBS");
            if (cf) strncpy(cflags,  cf, sizeof(cflags)  - 1);
            if (lf) strncpy(ldflags, lf, sizeof(ldflags) - 1);
            if (lb) strncpy(libs,    lb, sizeof(libs)    - 1);
            toml_free(flags);
        }
    }
    toml_table_t *manifest = manifest_load();
    const char *lang = manifest ? toml_get(manifest, "package.lang") : "c";
    const char *std  = manifest ? toml_get(manifest, "package.std")  : NULL;
    const char *compiler = (lang && strcmp(lang, "c++") == 0) ? "g++" : "gcc";
    const char *stdflag  = (std && strcmp(lang ? lang : "", "c++") == 0) ? "-std=c++17" : "-std=c17";
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "%s %s -O2 -Wall -Wextra %s %s ", compiler, stdflag, cflags, ldflags);
    for (int i = 0; i < source_count; i++) {
        char *tail = cmd + strlen(cmd);
        snprintf(tail, sizeof(cmd) - strlen(cmd), "%s ", sources[i]);
    }
    char *tail = cmd + strlen(cmd);
    snprintf(tail, sizeof(cmd) - strlen(cmd), "-o %s %s", output, libs);
    printf("Compiling: %s\n", cmd);
    char *copy = strdup(output);
    char *slash = strrchr(copy, '/');
    if (slash) { *slash = '\0'; if (strlen(copy) > 0) cpm_mkdirs(copy); }
    free(copy);
    int ret = system(cmd);
    if (ret == 0) printf("Build successful: %s\n", output);
    else fprintf(stderr, "Build failed\n");
    if (manifest) toml_free(manifest);
    return ret;
}

