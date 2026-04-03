/*
 * cpm — Install Dependencies Command Implementation
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include "commands/cmd_install.h"
#include "core/manifest.h"
#include "core/utils.h"
#include "registry.h"
#include "toml.h"

int cmd_install(int update_mode) {
    printf("Installing dependencies...\n");

    toml_table_t *manifest = manifest_load();
    if (!manifest) return 1;

    lockfile_t *lock = NULL;

    if (!update_mode) {
        lock = lockfile_load();
        if (lock) printf("Using existing lockfile\n");
    }

    if (!lock || update_mode) {
        printf("Resolving dependencies...\n");
        if (resolve_deps(manifest, &lock) != 0) {
            fprintf(stderr, "error: dependency resolution failed\n");
            toml_free(manifest);
            return 1;
        }
        if (lockfile_save(lock) != 0)
            fprintf(stderr, "warning: failed to save lockfile\n");
        printf("Generated cpm.lock\n");
    }

    toml_free(manifest);
    cpm_mkdirs(CPM_DEPS_DIR);

    for (size_t i = 0; i < lock->count; i++) {
        const char *name    = lock->packages[i].name;
        const char *version = lock->packages[i].version;
        const char *lang    = registry_get_lang(name);

        toml_table_t *m = manifest_load();
        const char *std = m ? toml_get(m, "package.std") : NULL;

        fetch_package(name, version, lock->packages[i].source);
        build_package(name, version, lang, std ? std : "c17");
        install_package(name, version);

        if (m) toml_free(m);
    }

    generate_flags_file(lock);
    generate_compile_commands(lock);

    printf("\nInstalled %zu package(s)\n", lock->count);
    printf("Dependencies available in %s/\n", CPM_DEPS_DIR);
    printf("Source .cpm/flags.env to use compiler flags\n");

    lockfile_free(lock);
    return 0;
}
