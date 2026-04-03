/*
 * cpm — Remove Dependency Command Implementation
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "commands/cmd_remove.h"
#include "core/manifest.h"
#include "core/utils.h"
#include "toml.h"

int cmd_remove(const char *package) {
    printf("Removing %s...\n", package);

    if (manifest_remove_dep(package, 0) != 0)
        manifest_remove_dep(package, 1);

    toml_table_t *manifest = manifest_load();
    if (manifest) {
        lockfile_t *lock = NULL;
        if (resolve_deps(manifest, &lock) == 0) {
            lockfile_save(lock);

            DIR *dir = opendir(CPM_DEPS_DIR);
            if (dir) {
                struct dirent *entry;
                while ((entry = readdir(dir)) != NULL) {
                    if (entry->d_name[0] == '.') continue;
                    int found = 0;
                    for (size_t i = 0; i < lock->count; i++) {
                        if (strncmp(entry->d_name, lock->packages[i].name, strlen(lock->packages[i].name)) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        printf("  Removing unused: %s\n", entry->d_name);
                        char cmd[512];
                        snprintf(cmd, sizeof(cmd), "rm -rf %s/%s", CPM_DEPS_DIR, entry->d_name);
                        system(cmd);
                    }
                }
                closedir(dir);
            }
            lockfile_free(lock);
        }
        toml_free(manifest);
    }

    printf("Removed %s\n", package);
    return 0;
}
