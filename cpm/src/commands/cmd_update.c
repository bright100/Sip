/*
 * cpm — Update Dependencies Command Implementation
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include "commands/cmd_update.h"
#include "core/manifest.h"
#include "toml.h"

int cmd_update(const char *package) {
    printf("Updating %s...\n", package ? package : "all dependencies");

    toml_table_t *manifest = manifest_load();
    if (!manifest) return 1;

    lockfile_t *lock = NULL;
    if (resolve_deps(manifest, &lock) == 0) {
        lockfile_save(lock);
        for (size_t i = 0; i < lock->count; i++) {
            if (!package || strcmp(lock->packages[i].name, package) == 0)
                fetch_package(lock->packages[i].name, lock->packages[i].version, lock->packages[i].source);
        }
        printf("Updated to latest versions\n");
        lockfile_free(lock);
    }

    toml_free(manifest);
    return 0;
}
