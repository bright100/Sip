/*
 * cpm — Build Command Implementation
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "commands/cmd_build.h"
#include "commands/cmd_install.h"
#include "core/manifest.h"
#include "toml.h"

int cmd_build(void) {
    printf("Building project...\n");

    toml_table_t *manifest = manifest_load();
    if (!manifest) {
        cmd_install(0);
        manifest = manifest_load();
        if (!manifest) return 1;
    }

    const char *build_cmd = toml_get(manifest, "scripts.build");
    if (!build_cmd) {
        const char *name = toml_get(manifest, "package.name");
        char output[256];
        snprintf(output, sizeof(output), "bin/%s", name ? name : "app");
        const char *sources[] = {"src/main.c"};
        compile_project(sources, 1, output);
    } else {
        printf("Running: %s\n", build_cmd);
        system(build_cmd);
    }

    toml_free(manifest);
    return 0;
}
