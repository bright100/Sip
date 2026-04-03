/*
 * cpm — Init Command Implementation
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "commands/cmd_init.h"
#include "core/utils.h"
int cmd_init(int cpp_mode) {
    printf("Initializing %s project...\n", cpp_mode ? "C++" : "C");
    if (cpm_file_exists("cpm.toml"))
        fprintf(stderr, "warning: cpm.toml already exists\n");
    char *cwd = cpm_getcwd();
    /* Normalize backslashes to forward slashes (Windows compat) */
    for (char *p = cwd; *p; p++) if (*p == '\\') *p = '/';
    char *proj = strrchr(cwd, '/');
    proj = proj ? proj + 1 : cwd;
    const char *tmpl =
        "[package]\n"
        "name    = \"%s\"\n"
        "version = \"0.1.0\"\n"
        "authors = [\"Developer <dev@example.com>\"]\n"
        "license = \"MIT\"\n"
        "lang    = \"%s\"\n"
        "std     = \"%s\"\n"
        "\n[dependencies]\n"
        "\n[dev-dependencies]\n"
        "\n[build]\n"
        "cc      = \"%s\"\n"
        "cflags  = [\"-O2\", \"-Wall\", \"-Wextra\"]\n"
        "ldflags = []\n"
        "\n[scripts]\n"
        "build   = \"cpm compile src/main.%s -o bin/%s\"\n"
        "test    = \"cpm compile tests/*.%s -o bin/tests && ./bin/tests\"\n"
        "clean   = \"rm -rf bin/ .cpm/build/\"\n";
    char manifest[2048];
    snprintf(manifest, sizeof(manifest), tmpl,
             proj,
             cpp_mode ? "c++" : "c",
             cpp_mode ? "c++17" : "c17",
             cpp_mode ? "g++" : "gcc",
             cpp_mode ? "cpp" : "c",
             proj,
             cpp_mode ? "cpp" : "c");
    if (cpm_write_file("cpm.toml", manifest) != 0) {
        fprintf(stderr, "error: failed to create cpm.toml\n");
        free(cwd);
        return 1;
    }
    cpm_mkdir("src");
    cpm_mkdir("include");
    cpm_mkdir("tests");
    cpm_mkdir("bin");
    char main_file[64];
    snprintf(main_file, sizeof(main_file), "src/main.%s", cpp_mode ? "cpp" : "c");
    if (!cpm_file_exists(main_file)) {
        FILE *f = fopen(main_file, "w");
        if (f) {
            if (cpp_mode) {
                fprintf(f, "#include <iostream>\n\nint main() {\n");
                fprintf(f, "    std::cout << \"Hello from %s!\" << std::endl;\n    return 0;\n}\n", proj);
            } else {
                fprintf(f, "#include <stdio.h>\n\nint main() {\n");
                fprintf(f, "    printf(\"Hello from %s!\\n\");\n    return 0;\n}\n", proj);
            }
            fclose(f);
        }
    }
    printf("Created cpm.toml\n");
    printf("Created src/, include/, tests/, bin/ directories\n");
    printf("Created %s\n", main_file);
    printf("\nNext steps:\n");
    printf("  cpm add <package>     # Add a dependency\n");
    printf("  cpm install           # Install dependencies\n");
    
    free(cwd);
    return 0;
}
