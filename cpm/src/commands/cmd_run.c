/*
 * cpm — Run Script Command with Nodemon-like Watch Mode
 *
 * Flags:
 *   -w / --watch        Re-run script whenever a watched file changes
 *   --ext=c,h,cpp       Comma-separated list of extensions to watch (default: c,h,cpp,cc,cxx)
 *   --delay=<ms>        Debounce delay in milliseconds after a change (default: 300)
 *   -v / --verbose      Print the file that changed
 *   --clear             Clear the terminal before each re-run
 *   --once              Run once and exit (default without -w)
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <time.h>
#include <errno.h>

#ifdef _WIN32
  #include <windows.h>
  #include <process.h>
#else
  #include <unistd.h>
  #include <sys/wait.h>
#endif

#include "commands/cmd_run.h"
#include "core/manifest.h"
#include "toml.h"

/* ─── ANSI colours ─────────────────────────────────────────────────────── */
#define CLR_RESET  "\033[0m"
#define CLR_GREEN  "\033[1;32m"
#define CLR_YELLOW "\033[1;33m"
#define CLR_CYAN   "\033[1;36m"
#define CLR_RED    "\033[1;31m"
#define CLR_GREY   "\033[0;90m"

/* ─── Cross-platform sleep (milliseconds) ───────────────────────────────── */
static void sleep_ms(long ms) {
#ifdef _WIN32
    Sleep((DWORD)ms);
#else
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
#endif
}

/* ─── Cross-platform child process handle ───────────────────────────────── */
#ifdef _WIN32
  typedef HANDLE child_t;
  #define INVALID_CHILD NULL
#else
  typedef pid_t child_t;
  #define INVALID_CHILD (-1)
#endif

/* ─── Watched-file state ────────────────────────────────────────────────── */
#define MAX_WATCH_FILES 4096

typedef struct {
    char   path[1024];
    time_t mtime;
} watch_entry_t;

static watch_entry_t g_files[MAX_WATCH_FILES];
static int           g_nfiles = 0;

/* Split comma-separated extension list, return array (caller frees) */
static char **parse_extensions(const char *ext_str, int *count) {
    char *copy = strdup(ext_str);
    int n = 0;
    char *tok = strtok(copy, ",");
    while (tok) { n++; tok = strtok(NULL, ","); }
    free(copy);

    char **arr = malloc(sizeof(char *) * (n + 1));
    copy = strdup(ext_str);
    int i = 0;
    tok = strtok(copy, ",");
    while (tok) { arr[i++] = strdup(tok); tok = strtok(NULL, ","); }
    arr[i] = NULL;
    free(copy);
    *count = n;
    return arr;
}

static int ext_matches(const char *filename, char **exts) {
    const char *dot = strrchr(filename, '.');
    if (!dot) return 0;
    dot++;
    for (int i = 0; exts[i]; i++)
        if (strcmp(dot, exts[i]) == 0) return 1;
    return 0;
}

/* Scan directory recursively, collect files whose extension is watched */
static void scan_dir(const char *dir, char **exts) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        if (strcmp(e->d_name, ".cpm") == 0 || strcmp(e->d_name, "bin") == 0) continue;

        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);

        struct stat st;
        if (stat(path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            scan_dir(path, exts);
        } else if (S_ISREG(st.st_mode) && ext_matches(e->d_name, exts)) {
            if (g_nfiles < MAX_WATCH_FILES) {
                snprintf(g_files[g_nfiles].path, sizeof(g_files[g_nfiles].path), "%s", path);
                g_files[g_nfiles].mtime = st.st_mtime;
                g_nfiles++;
            }
        }
    }
    closedir(d);
}

/* Return path of first changed file, or NULL */
static const char *check_changes(void) {
    for (int i = 0; i < g_nfiles; i++) {
        struct stat st;
        if (stat(g_files[i].path, &st) != 0) continue;
        if (st.st_mtime != g_files[i].mtime) {
            g_files[i].mtime = st.st_mtime;
            return g_files[i].path;
        }
    }
    return NULL;
}

static void rescan(char **exts) {
    g_nfiles = 0;
    scan_dir(".", exts);
}

/* ─── Banner helpers ─────────────────────────────────────────────────────── */
static void print_watch_banner(const char *script, int nfiles) {
    printf("\n%s[cpm]%s watching %d files for changes... (script: %s%s%s)\n",
           CLR_CYAN, CLR_RESET, nfiles,
           CLR_GREEN, script, CLR_RESET);
    printf("%s      Press Ctrl+C to quit%s\n\n", CLR_GREY, CLR_RESET);
}

static void print_change_banner(const char *changed_file, const char *script, int verbose) {
    printf("\n%s[cpm]%s change detected", CLR_YELLOW, CLR_RESET);
    if (verbose && changed_file)
        printf(" in %s%s%s", CLR_CYAN, changed_file, CLR_RESET);
    printf(" — restarting '%s%s%s'...\n\n", CLR_GREEN, script, CLR_RESET);
}

static void print_restart_banner(const char *script) {
    printf("\n%s[cpm]%s starting '%s%s%s'\n\n", CLR_CYAN, CLR_RESET, CLR_GREEN, script, CLR_RESET);
}

static void print_exit_banner(int code) {
    if (code == 0)
        printf("\n%s[cpm]%s process exited (code 0)%s\n", CLR_GREEN, CLR_GREY, CLR_RESET);
    else
        printf("\n%s[cpm]%s process exited with code %d%s\n", CLR_RED, CLR_GREY, code, CLR_RESET);
}

/* ─── Child process management ──────────────────────────────────────────── */
static volatile int g_running = 1;

#ifdef _WIN32

static HANDLE g_child_handle = NULL;

static void sig_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_running = 0;
        if (g_child_handle) TerminateProcess(g_child_handle, 1);
    }
}

static HANDLE launch_child(const char *shell_cmd) {
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    /* Build: cmd /C <shell_cmd> */
    char cmd_line[2560];
    snprintf(cmd_line, sizeof(cmd_line), "cmd /C %s", shell_cmd);

    if (!CreateProcessA(NULL, cmd_line, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        fprintf(stderr, "[cpm] failed to launch process (error %lu)\n", GetLastError());
        return NULL;
    }
    CloseHandle(pi.hThread);
    return pi.hProcess;
}

static void kill_child(void) {
    if (g_child_handle) {
        TerminateProcess(g_child_handle, 1);
        WaitForSingleObject(g_child_handle, 500);
        CloseHandle(g_child_handle);
        g_child_handle = NULL;
    }
}

/* Returns exit code, or -1 if still running */
static int poll_child(void) {
    if (!g_child_handle) return -1;
    DWORD code;
    if (GetExitCodeProcess(g_child_handle, &code) && code != STILL_ACTIVE) {
        CloseHandle(g_child_handle);
        g_child_handle = NULL;
        return (int)code;
    }
    return -1;
}

static int run_once(const char *shell_cmd) {
    HANDLE h = launch_child(shell_cmd);
    if (!h) return 1;
    WaitForSingleObject(h, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return (int)code;
}

#else /* POSIX */

static volatile pid_t g_child_pid = -1;

static void sig_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        g_running = 0;
        if (g_child_pid > 0) kill(g_child_pid, SIGTERM);
    }
}

static pid_t launch_child(const char *shell_cmd) {
    pid_t pid = fork();
    if (pid == 0) {
        execl("/bin/sh", "sh", "-c", shell_cmd, (char *)NULL);
        _exit(127);
    }
    return pid;
}

static void kill_child(void) {
    if (g_child_pid > 0) {
        kill(g_child_pid, SIGTERM);
        int status;
        struct timespec ts = { 0, 500000000L };
        nanosleep(&ts, NULL);
        if (waitpid(g_child_pid, &status, WNOHANG) == 0)
            kill(g_child_pid, SIGKILL);
        waitpid(g_child_pid, &status, 0);
        g_child_pid = -1;
    }
}

/* Returns exit code, or -1 if still running */
static int poll_child(void) {
    if (g_child_pid <= 0) return -1;
    int status;
    pid_t result = waitpid(g_child_pid, &status, WNOHANG);
    if (result == g_child_pid) {
        int code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
        g_child_pid = -1;
        return code;
    }
    return -1;
}

static int run_once(const char *shell_cmd) {
    int ret = system(shell_cmd);
    return WIFEXITED(ret) ? WEXITSTATUS(ret) : 1;
}

#endif /* _WIN32 */

/* ─── Main command ──────────────────────────────────────────────────────── */

int cmd_run(const char *script, run_opts_t *opts) {
    toml_table_t *manifest = manifest_load();
    if (!manifest) return 1;

    char key[128];
    snprintf(key, sizeof(key), "scripts.%s", script);
    const char *shell_cmd = toml_get(manifest, key);

    if (!shell_cmd) {
        fprintf(stderr, "error: script '%s' not found in cpm.toml\n", script);
        fprintf(stderr, "hint: add it under [scripts] in cpm.toml\n");
        toml_free(manifest);
        return 1;
    }

    char cmd_copy[2048];
    strncpy(cmd_copy, shell_cmd, sizeof(cmd_copy) - 1);
    cmd_copy[sizeof(cmd_copy) - 1] = '\0';
    toml_free(manifest);

    /* ── Without watch: run once ──────────────────────────────────────── */
    if (!opts || !opts->watch) {
        printf("%s[cpm]%s running '%s%s%s': %s%s%s\n\n",
               CLR_CYAN, CLR_RESET,
               CLR_GREEN, script, CLR_RESET,
               CLR_GREY, cmd_copy, CLR_RESET);
        return run_once(cmd_copy);
    }

    /* ── Watch mode ───────────────────────────────────────────────────── */
    const char *ext_str = (opts->ext && *opts->ext) ? opts->ext : "c,h,cpp,cc,cxx,hpp";
    int   delay_ms = (opts->delay_ms > 0) ? opts->delay_ms : 300;
    int   verbose  = opts->verbose;
    int   do_clear = opts->clear;

    int ext_count;
    char **exts = parse_extensions(ext_str, &ext_count);

    scan_dir(".", exts);

    signal(SIGINT,  sig_handler);
    signal(SIGTERM, sig_handler);

#ifndef _WIN32
    /* Use sigaction on POSIX for reliable delivery */
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
#endif

    printf("%s[cpm watch]%s starting...\n", CLR_CYAN, CLR_RESET);
    print_watch_banner(script, g_nfiles);

    /* First run */
    if (do_clear) system("clear");
    print_restart_banner(script);

#ifdef _WIN32
    g_child_handle = launch_child(cmd_copy);
#else
    g_child_pid = launch_child(cmd_copy);
#endif

    long debounce_ticks   = (delay_ms + 49) / 50;
    long debounce_counter = 0;
    const char *changed_path = NULL;

    while (g_running) {
        sleep_ms(50);

        /* Check if child exited naturally */
        int exit_code = poll_child();
        if (exit_code >= 0) {
            print_exit_banner(exit_code);
            printf("%s[cpm]%s watching for changes...\n", CLR_GREY, CLR_RESET);
        }

        /* Poll for file changes */
        const char *p = check_changes();
        if (p) {
            changed_path     = p;
            debounce_counter = debounce_ticks;
        }

        if (debounce_counter > 0) {
            debounce_counter--;
            if (debounce_counter == 0) {
                kill_child();
                rescan(exts);

                if (do_clear) system("clear");
                print_change_banner(verbose ? changed_path : NULL, script, verbose);

#ifdef _WIN32
                g_child_handle = launch_child(cmd_copy);
#else
                g_child_pid = launch_child(cmd_copy);
#endif
                changed_path = NULL;
            }
        }
    }

    kill_child();

    for (int i = 0; exts[i]; i++) free(exts[i]);
    free(exts);

    printf("\n%s[cpm]%s exiting watch mode%s\n", CLR_CYAN, CLR_RESET, CLR_RESET);
    return 0;
}
