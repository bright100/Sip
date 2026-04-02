/*
 * cpm — A Package Manager for C and C++
 * Main entry point
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void print_help(void) {
    printf("cpm — A Package Manager for C and C++\n\n");
    printf("Usage:\n");
    printf("  cpm init           Initialize a new project\n");
    printf("  cpm add <package>  Add a dependency\n");
    printf("  cpm install        Install dependencies\n");
    printf("  cpm build          Build the project\n");
    printf("  cpm run <script>   Run a script from cpm.toml\n");
    printf("  cpm remove <pkg>   Remove a dependency\n");
    printf("  cpm update [pkg]   Update dependencies\n");
    printf("  cpm publish        Publish package to registry\n");
    printf("  cpm help           Show this help message\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        print_help();
        return 0;
    }

    const char *cmd = argv[1];

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "--help") == 0 || strcmp(cmd, "-h") == 0) {
        print_help();
        return 0;
    }

    printf("cpm: command '%s' not yet implemented\n", cmd);
    printf("This is a stub implementation. Full functionality coming soon.\n");
    
    return 1;
}
