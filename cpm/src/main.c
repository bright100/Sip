/*
 * cpm — A Package Manager for C and C++
 * Main entry point — CLI argument parsing and dispatch
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "commands/cmd_init.h"
#include "commands/cmd_add.h"
#include "commands/cmd_install.h"
#include "commands/cmd_build.h"
#include "commands/cmd_run.h"
#include "commands/cmd_remove.h"
#include "commands/cmd_update.h"
#include "commands/cmd_publish.h"
#include "core/manifest.h"
#include "toml.h"

#define CPM_VERSION "0.1.0"

static void print_help(void) {
    printf("cpm \xe2\x80\x94 A Package Manager for C and C++ (v%s)\n\n", CPM_VERSION);
    printf("Usage:\n");
    printf("  cpm init [--cpp]                Initialize a new project\n");
    printf("  cpm add <package> [ver]         Add a dependency\n");
    printf("  cpm add <pkg>@<ver>             Add specific version\n");
    printf("  cpm add --dev <pkg>             Add dev dependency\n");
    printf("  cpm install [-u]                Install dependencies\n");
    printf("  cpm build                       Build the project\n");
    printf("  cpm run <script> [flags]        Run a script from cpm.toml\n");
    printf("  cpm remove <package>            Remove a dependency\n");
    printf("  cpm update [package]            Update dependencies\n");
    printf("  cpm publish                     Publish package to registry\n");
    printf("  cpm help                        Show this help\n");
    printf("\nrun flags (nodemon-style watch mode):\n");
    printf("  -w, --watch           Re-run on file change\n");
    printf("  --ext=c,h,cpp         Extensions to watch (default: c,h,cpp,cc,cxx,hpp)\n");
    printf("  --delay=<ms>          Debounce delay in ms (default: 300)\n");
    printf("  -v, --verbose         Print the file that changed\n");
    printf("  --clear               Clear terminal before each run\n");
    printf("\nExamples:\n");
    printf("  cpm init --cpp        Create a new C++ project\n");
    printf("  cpm add libcurl       Add latest libcurl\n");
    printf("  cpm add zlib@1.3.1    Add specific version\n");
    printf("  cpm install           Install all dependencies\n");
    printf("  cpm run build         Run 'build' script once\n");
    printf("  cpm run build -w      Watch & re-run on change\n");
    printf("  cpm run build --watch --ext=c,h --delay=500 -v --clear\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) { print_help(); return 0; }

    const char *cmd = argv[1];

    /* ── help / version ────────────────────────────────────────────── */
    if (!strcmp(cmd, "help") || !strcmp(cmd, "--help") || !strcmp(cmd, "-h")) {
        print_help();
        return 0;
    }
    if (!strcmp(cmd, "--version") || !strcmp(cmd, "-v")) {
        printf("cpm version %s\n", CPM_VERSION);
        return 0;
    }

    /* ── init ──────────────────────────────────────────────────────── */
    if (!strcmp(cmd, "init")) {
        int cpp = 0;
        for (int i = 2; i < argc; i++)
            if (!strcmp(argv[i], "--cpp")) cpp = 1;
        return cmd_init(cpp);
    }

    /* ── add ───────────────────────────────────────────────────────── */
    if (!strcmp(cmd, "add")) {
        if (argc < 3) {
            fprintf(stderr, "error: package name required\nusage: cpm add <package> [version]\n");
            return 1;
        }
        const char *pkg = argv[2];
        const char *ver = NULL;
        int dev = 0;
        for (int i = 3; i < argc; i++) {
            if (!strcmp(argv[i], "--dev") || !strcmp(argv[i], "-d")) dev = 1;
            else if (argv[i][0] != '-') ver = argv[i];
        }
        /* package@version syntax */
        char pkg_buf[256];
        strncpy(pkg_buf, pkg, sizeof(pkg_buf) - 1);
        char *at = strchr(pkg_buf, '@');
        if (at) { *at = '\0'; ver = at + 1; pkg = pkg_buf; }
        return cmd_add(pkg, ver ? ver : "latest", dev);
    }

    /* ── install ───────────────────────────────────────────────────── */
    if (!strcmp(cmd, "install") || !strcmp(cmd, "i")) {
        int upd = 0;
        for (int i = 2; i < argc; i++)
            if (!strcmp(argv[i], "-u") || !strcmp(argv[i], "--update")) upd = 1;
        return cmd_install(upd);
    }

    /* ── build ─────────────────────────────────────────────────────── */
    if (!strcmp(cmd, "build") || !strcmp(cmd, "b"))
        return cmd_build();

    /* ── run ───────────────────────────────────────────────────────── */
    if (!strcmp(cmd, "run") || !strcmp(cmd, "r")) {
        if (argc < 3) {
            fprintf(stderr, "error: script name required\nusage: cpm run <script> [-w] [--ext=...] [--delay=N] [-v] [--clear]\n");
            return 1;
        }
        const char *script = argv[2];
        run_opts_t opts;
        memset(&opts, 0, sizeof(opts));

        static char ext_buf[256];
        ext_buf[0] = '\0';

        for (int i = 3; i < argc; i++) {
            const char *a = argv[i];
            if (!strcmp(a, "-w") || !strcmp(a, "--watch"))       opts.watch   = 1;
            else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) opts.verbose = 1;
            else if (!strcmp(a, "--clear"))                        opts.clear   = 1;
            else if (!strncmp(a, "--ext=", 6)) {
                strncpy(ext_buf, a + 6, sizeof(ext_buf) - 1);
                opts.ext = ext_buf;
            } else if (!strncmp(a, "--delay=", 8)) {
                opts.delay_ms = atoi(a + 8);
            }
        }
        return cmd_run(script, &opts);
    }

    /* ── remove ────────────────────────────────────────────────────── */
    if (!strcmp(cmd, "remove") || !strcmp(cmd, "rm")) {
        if (argc < 3) {
            fprintf(stderr, "error: package name required\nusage: cpm remove <package>\n");
            return 1;
        }
        return cmd_remove(argv[2]);
    }

    /* ── update ────────────────────────────────────────────────────── */
    if (!strcmp(cmd, "update") || !strcmp(cmd, "up")) {
        const char *pkg = (argc >= 3 && argv[2][0] != '-') ? argv[2] : NULL;
        return cmd_update(pkg);
    }

    /* ── publish ───────────────────────────────────────────────────── */
    if (!strcmp(cmd, "publish") || !strcmp(cmd, "pub"))
        return cmd_publish();

    /* ── compile (internal) ────────────────────────────────────────── */
    if (!strcmp(cmd, "compile")) {
        if (argc < 4) {
            fprintf(stderr, "usage: cpm compile <sources...> -o <output>\n");
            return 1;
        }
        const char *output = NULL;
        for (int i = 2; i < argc; i++)
            if (!strcmp(argv[i], "-o") && i + 1 < argc) { output = argv[i + 1]; break; }
        if (!output) { fprintf(stderr, "error: output file required (-o <file>)\n"); return 1; }
        const char **sources = malloc(sizeof(char *) * (argc - 2));
        int src_count = 0;
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-o")) { i++; continue; }
            sources[src_count++] = argv[i];
        }
        int ret = compile_project(sources, src_count, output);
        free(sources);
        return ret;
    }

    fprintf(stderr, "error: unknown command '%s'\n", cmd);
    fprintf(stderr, "Run 'cpm help' for usage information.\n");
    return 1;
}
