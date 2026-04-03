/*
 * cpm — Add Dependency Command Implementation
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include "commands/cmd_add.h"
#include "commands/cmd_install.h"
#include "core/manifest.h"
#include "registry.h"

int cmd_add(const char *package, const char *version, int dev_mode) {
    printf("Adding %s", package);
    if (version && *version && strcmp(version, "latest") != 0)
        printf("@%s", version);
    printf("%s...\n", dev_mode ? " (dev)" : "");

    if (!registry_exists(package)) {
        fprintf(stderr, "error: package '%s' not found in registry\n", package);
        fprintf(stderr, "hint: try 'cpm add libcurl', 'cpm add zlib', etc.\n");
        return 1;
    }

    if (manifest_add_dep(package, version, dev_mode) != 0) {
        fprintf(stderr, "error: failed to update cpm.toml\n");
        return 1;
    }

    printf("Added %s to %s\n", package, dev_mode ? "dev-dependencies" : "dependencies");
    printf("Running install...\n");
    return cmd_install(0);
}
