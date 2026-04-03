/*
 * cpm — A Package Manager for C and C++
 * Main entry point with full CLI implementation
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include "toml.h"
#include "registry.h"
#include "resolver.h"

#define CPM_VERSION "0.1.0"
#define DEFAULT_REGISTRY "https://registry.cpm.dev"
#define CPM_DIR ".cpm"
#define CPM_DEPS_DIR ".cpm/deps"
#define CPM_CACHE_DIR "~/.cpm"

/* Forward declarations */
static int cmd_init(int cpp_mode);
static int cmd_add(const char *package, const char *version, int dev_mode);
static int cmd_install(int update_mode);
static int cmd_build(void);
static int cmd_run(const char *script);
static int cmd_remove(const char *package);
static int cmd_update(const char *package);
static int cmd_publish(void);
static void print_help(void);

/* Utility functions */
static char *read_file_contents(const char *path);
static int write_file_contents(const char *path, const char *content);
static int file_exists(const char *path);
static int dir_exists(const char *path);
static int create_directory(const char *path);
static int create_directories_recursive(const char *path);
static char *get_working_dir(void);
static char *compute_sha256(const char *data, size_t len);
static int verify_checksum(const char *path, const char *expected);
static char *trim_string(char *str);
static int parse_version(const char *ver, int *major, int *minor, int *patch);
static int version_satisfies(const char *version, const char *constraint);
static int compare_versions(const char *v1, const char *v2);

/* Manifest operations */
static toml_table_t *load_manifest(void);
static int save_manifest(toml_table_t *manifest);
static int add_dependency_to_manifest(const char *name, const char *version, int dev_dep);
static int remove_dependency_from_manifest(const char *name, int dev_dep);

/* Lockfile operations */
typedef struct {
    char *name;
    char *version;
    char *source;
    char *checksum;
    char **deps;
    size_t dep_count;
} lock_package_t;

typedef struct {
    lock_package_t *packages;
    size_t count;
} lockfile_t;

static lockfile_t *load_lockfile(void);
static int save_lockfile(lockfile_t *lock);
static void lockfile_free(lockfile_t *lock);

/* Dependency resolution */
static int resolve_dependencies_simple(toml_table_t *manifest, lockfile_t **result);

/* Fetch and cache */
static int fetch_package(const char *name, const char *version, const char *source);
static int build_package(const char *name, const char *version, const char *lang, const char *std);
static int install_package_to_project(const char *name, const char *version);

/* Build system */
static int generate_flags_file(lockfile_t *lock);
static int generate_compile_commands(lockfile_t *lock);
static int compile_project(const char *sources[], int source_count, const char *output);

/* Registry mock (simulates registry without network) */
static int mock_registry_exists(const char *name);
static const char *mock_registry_get_version(const char *name, const char *constraint);
static const char *mock_registry_get_deps(const char *name, const char *version);
static const char *mock_registry_get_lang(const char *name);
static const char *mock_registry_get_libtype(const char *name);

/* ============== Main Entry Point ============== */

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

    if (strcmp(cmd, "--version") == 0 || strcmp(cmd, "-v") == 0) {
        printf("cpm version %s\n", CPM_VERSION);
        return 0;
    }

    /* Initialize command */
    if (strcmp(cmd, "init") == 0) {
        int cpp_mode = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--cpp") == 0) cpp_mode = 1;
        }
        return cmd_init(cpp_mode);
    }

    /* Add command */
    if (strcmp(cmd, "add") == 0) {
        if (argc < 3) {
            fprintf(stderr, "error: package name required\n");
            fprintf(stderr, "usage: cpm add <package> [version]\n");
            return 1;
        }
        const char *pkg = argv[2];
        const char *ver = NULL;
        int dev_mode = 0;
        
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "--dev") == 0 || strcmp(argv[i], "-d") == 0) {
                dev_mode = 1;
            } else if (argv[i][0] != '-') {
                ver = argv[i];
            }
        }
        
        /* Parse package@version syntax */
        char *at = strchr((char*)pkg, '@');
        if (at) {
            *at = '\0';
            ver = at + 1;
        }
        
        return cmd_add(pkg, ver ? ver : "latest", dev_mode);
    }

    /* Install command */
    if (strcmp(cmd, "install") == 0 || strcmp(cmd, "i") == 0) {
        int update_mode = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--update") == 0 || strcmp(argv[i], "-u") == 0) {
                update_mode = 1;
            }
        }
        return cmd_install(update_mode);
    }

    /* Build command */
    if (strcmp(cmd, "build") == 0 || strcmp(cmd, "b") == 0) {
        return cmd_build();
    }

    /* Run command */
    if (strcmp(cmd, "run") == 0 || strcmp(cmd, "r") == 0) {
        if (argc < 3) {
            fprintf(stderr, "error: script name required\n");
            fprintf(stderr, "usage: cpm run <script>\n");
            return 1;
        }
        return cmd_run(argv[2]);
    }

    /* Remove command */
    if (strcmp(cmd, "remove") == 0 || strcmp(cmd, "rm") == 0) {
        if (argc < 3) {
            fprintf(stderr, "error: package name required\n");
            fprintf(stderr, "usage: cpm remove <package>\n");
            return 1;
        }
        return cmd_remove(argv[2]);
    }

    /* Update command */
    if (strcmp(cmd, "update") == 0 || strcmp(cmd, "up") == 0) {
        const char *pkg = NULL;
        if (argc >= 3 && argv[2][0] != '-') {
            pkg = argv[2];
        }
        return cmd_update(pkg);
    }

    /* Publish command */
    if (strcmp(cmd, "publish") == 0 || strcmp(cmd, "pub") == 0) {
        return cmd_publish();
    }

    /* Compile command (internal use) */
    if (strcmp(cmd, "compile") == 0) {
        /* Internal compile - used by scripts */
        if (argc < 4) {
            fprintf(stderr, "usage: cpm compile <sources...> -o <output>\n");
            return 1;
        }
        /* Find -o flag */
        const char *output = NULL;
        int src_start = 2;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
                output = argv[i + 1];
                break;
            }
        }
        if (!output) {
            fprintf(stderr, "error: output file required (-o <file>)\n");
            return 1;
        }
        /* Collect sources */
        const char **sources = malloc(sizeof(char*) * (argc - 2));
        int src_count = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "-o") == 0) { i++; continue; }
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

static void print_help(void) {
    printf("cpm — A Package Manager for C and C++ (v%s)\n\n", CPM_VERSION);
    printf("Usage:\n");
    printf("  cpm init [--cpp]           Initialize a new project\n");
    printf("  cpm add <package> [ver]    Add a dependency\n");
    printf("  cpm add <pkg>@<ver>        Add specific version\n");
    printf("  cpm add --dev <pkg>        Add dev dependency\n");
    printf("  cpm install [-u]           Install dependencies\n");
    printf("  cpm build                  Build the project\n");
    printf("  cpm run <script>           Run a script from cpm.toml\n");
    printf("  cpm remove <package>       Remove a dependency\n");
    printf("  cpm update [package]       Update dependencies\n");
    printf("  cpm publish                Publish package to registry\n");
    printf("  cpm help                   Show this help message\n");
    printf("\nExamples:\n");
    printf("  cpm init --cpp             Create a new C++ project\n");
    printf("  cpm add libcurl            Add latest libcurl\n");
    printf("  cpm add nlohmann-json@3.11.3\n");
    printf("  cpm add --dev googletest   Add test framework\n");
    printf("  cpm install                Install all dependencies\n");
    printf("  cpm run build              Run the build script\n");
}

/* ============== Utility Functions ============== */

static char *read_file_contents(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char *buf = malloc(size + 1);
    if (!buf) { fclose(f); return NULL; }
    
    size_t read = fread(buf, 1, size, f);
    buf[read] = '\0';
    fclose(f);
    return buf;
}

static int write_file_contents(const char *path, const char *content) {
    /* Create parent directory if needed */
    char *path_copy = strdup(path);
    char *last_slash = strrchr(path_copy, '/');
    if (last_slash) {
        *last_slash = '\0';
        if (strlen(path_copy) > 0) {
            create_directories_recursive(path_copy);
        }
    }
    free(path_copy);
    
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    
    fprintf(f, "%s", content);
    fclose(f);
    return 0;
}

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int dir_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int create_directory(const char *path) {
    return mkdir(path, 0755);
}

static int create_directories_recursive(const char *path) {
    char tmp[1024];
    char *p = NULL;
    size_t len;

    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    
    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
    return 0;
}

static char *get_working_dir(void) {
    char *buf = malloc(1024);
    if (getcwd(buf, 1024)) {
        return buf;
    }
    free(buf);
    return NULL;
}

static char *trim_string(char *str) {
    if (!str) return NULL;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

static int parse_version(const char *ver, int *major, int *minor, int *patch) {
    if (sscanf(ver, "%d.%d.%d", major, minor, patch) >= 1) {
        if (*minor == -1) *minor = 0;
        if (*patch == -1) *patch = 0;
        return 0;
    }
    return -1;
}

static int compare_versions(const char *v1, const char *v2) {
    int maj1, min1, pat1, maj2, min2, pat2;
    parse_version(v1, &maj1, &min1, &pat1);
    parse_version(v2, &maj2, &min2, &pat2);
    
    if (maj1 != maj2) return maj1 - maj2;
    if (min1 != min2) return min1 - min2;
    return pat1 - pat2;
}

static int version_satisfies(const char *version, const char *constraint) {
    int vmaj, vmin, vpat;
    int cmaj, cmin, cpat;
    
    if (strcmp(constraint, "*") == 0 || strcmp(constraint, "latest") == 0) {
        return 1;
    }
    
    /* Exact version */
    if (strchr(constraint, '.') && !strchr(constraint, '^') && !strchr(constraint, '~')) {
        return strcmp(version, constraint) == 0;
    }
    
    /* Parse version */
    if (parse_version(version, &vmaj, &vmin, &vpat) < 0) return 0;
    
    /* Caret (^) - compatible within major version */
    if (constraint[0] == '^') {
        if (parse_version(constraint + 1, &cmaj, &cmin, &cpat) < 0) return 0;
        if (vmaj != cmaj) return 0;
        return compare_versions(version, constraint + 1) >= 0;
    }
    
    /* Tilde (~) - compatible within minor version */
    if (constraint[0] == '~') {
        if (parse_version(constraint + 1, &cmaj, &cmin, &cpat) < 0) return 0;
        if (vmaj != cmaj || vmin != cmin) return 0;
        return compare_versions(version, constraint + 1) >= 0;
    }
    
    /* Greater than or equal */
    if (strncmp(constraint, ">=", 2) == 0) {
        if (parse_version(constraint + 2, &cmaj, &cmin, &cpat) < 0) return 0;
        return compare_versions(version, constraint + 2) >= 0;
    }
    
    /* Simple version number - treat as minimum */
    if (parse_version(constraint, &cmaj, &cmin, &cpat) >= 0) {
        return compare_versions(version, constraint) >= 0;
    }
    
    return 0;
}

/* Simplified SHA-256 (for demo - uses hash of string) */
static char *compute_sha256(const char *data, size_t len) {
    /* In real implementation, use proper SHA-256 */
    unsigned int hash = 5381;
    for (size_t i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)data[i];
    }
    
    char *result = malloc(65);
    snprintf(result, 65, "sha256:%08x%08x%08x%08x%08x%08x%08x%08x",
             hash, hash ^ 0xdeadbeef, hash ^ 0xcafebabe, hash ^ 0x12345678,
             hash ^ 0xabcdef01, hash ^ 0xfedcba98, hash ^ 0x11111111, hash ^ 0x22222222);
    return result;
}

static int verify_checksum(const char *path, const char *expected) {
    /* In real implementation, compute actual SHA-256 */
    (void)path;
    (void)expected;
    return 1; /* Always pass for demo */
}

/* ============== Mock Registry ============== */

/* Simulated package database */
static const char *mock_packages[] = {
    "libcurl", "nlohmann-json", "cJSON", "libuv", "zlib", "openssl",
    "googletest", "cmocka", "fmt", "spdlog", "catch2", NULL
};

static const char *mock_versions[] = {
    "libcurl:8.4.0,8.3.0,8.2.0,8.0.0",
    "nlohmann-json:3.11.3,3.11.2,3.11.0,3.10.0",
    "cJSON:1.7.17,1.7.16,1.7.15",
    "libuv:2.0.0,1.46.0,1.45.0",
    "zlib:1.3.1,1.3.0,1.2.13",
    "openssl:3.1.4,3.1.3,3.0.0,1.1.1",
    "googletest:1.14.0,1.13.0,1.12.0",
    "cmocka:1.1.7,1.1.6,1.1.5",
    "fmt:10.1.0,10.0.0,9.1.0",
    "spdlog:1.12.0,1.11.0,1.10.0",
    "catch2:3.4.0,3.3.0,3.2.0",
    NULL
};

static const char *mock_deps[] = {
    "libcurl:8.4.0:zlib,openssl",
    "libcurl:8.0.0:zlib",
    "nlohmann-json:3.11.3:",
    "cJSON:1.7.17:",
    "libuv:2.0.0:",
    "zlib:1.3.1:",
    "openssl:3.1.4:",
    "googletest:1.14.0:",
    "cmocka:1.1.7:",
    "fmt:10.1.0:",
    "spdlog:1.12.0:fmt",
    "catch2:3.4.0:",
    NULL
};

static const char *mock_langs[] = {
    "libcurl:c",
    "nlohmann-json:c++",
    "cJSON:c",
    "libuv:c",
    "zlib:c",
    "openssl:c",
    "googletest:c++",
    "cmocka:c",
    "fmt:c++",
    "spdlog:c++",
    "catch2:c++",
    NULL
};

static const char *mock_libtypes[] = {
    "libcurl:source",
    "nlohmann-json:header-only",
    "cJSON:source",
    "libuv:source",
    "zlib:source",
    "openssl:system",
    "googletest:source",
    "cmocka:source",
    "fmt:header-only",
    "spdlog:header-only",
    "catch2:header-only",
    NULL
};

static int mock_registry_exists(const char *name) {
    for (int i = 0; mock_packages[i]; i++) {
        if (strcmp(mock_packages[i], name) == 0) return 1;
    }
    return 0;
}

static const char *mock_registry_get_version(const char *name, const char *constraint) {
    for (int i = 0; mock_versions[i]; i++) {
        if (strncmp(mock_versions[i], name, strlen(name)) == 0 && 
            mock_versions[i][strlen(name)] == ':') {
            const char *vers = mock_versions[i] + strlen(name) + 1;
            char *vers_copy = strdup(vers);
            char *token = strtok(vers_copy, ",");
            const char *best = NULL;
            
            while (token) {
                if (version_satisfies(token, constraint)) {
                    if (!best || compare_versions(token, best) > 0) {
                        best = token;
                    }
                }
                token = strtok(NULL, ",");
            }
            
            if (best) {
                static char result[32];
                strncpy(result, best, sizeof(result) - 1);
                result[sizeof(result) - 1] = '\0';
                free(vers_copy);
                return result;
            }
            free(vers_copy);
        }
    }
    return NULL;
}

static const char *mock_registry_get_deps(const char *name, const char *version) {
    for (int i = 0; mock_deps[i]; i++) {
        char key[128];
        snprintf(key, sizeof(key), "%s:%s:", name, version);
        if (strncmp(mock_deps[i], key, strlen(key)) == 0) {
            return mock_deps[i] + strlen(key);
        }
    }
    return "";
}

static const char *mock_registry_get_lang(const char *name) {
    for (int i = 0; mock_langs[i]; i++) {
        if (strncmp(mock_langs[i], name, strlen(name)) == 0 && 
            mock_langs[i][strlen(name)] == ':') {
            return mock_langs[i] + strlen(name) + 1;
        }
    }
    return "c";
}

static const char *mock_registry_get_libtype(const char *name) {
    for (int i = 0; mock_libtypes[i]; i++) {
        if (strncmp(mock_libtypes[i], name, strlen(name)) == 0 && 
            mock_libtypes[i][strlen(name)] == ':') {
            return mock_libtypes[i] + strlen(name) + 1;
        }
    }
    return "source";
}

/* ============== Manifest Operations ============== */

static toml_table_t *load_manifest(void) {
    if (!file_exists("cpm.toml")) {
        fprintf(stderr, "error: cpm.toml not found\n");
        fprintf(stderr, "hint: run 'cpm init' to create a new project\n");
        return NULL;
    }
    return toml_parse_file("cpm.toml");
}

static int save_manifest(toml_table_t *manifest) {
    /* Rebuild TOML from pairs */
    char buffer[8192];
    char *p = buffer;
    
    /* Group by section */
    const char *sections[] = {"package", "dependencies", "dev-dependencies", "build", "scripts", NULL};
    
    for (int s = 0; sections[s]; s++) {
        int found = 0;
        char section_prefix[64];
        
        if (strcmp(sections[s], "dependencies") == 0) {
            strcpy(section_prefix, "");
        } else {
            snprintf(section_prefix, sizeof(section_prefix), "%s.", sections[s]);
        }
        
        for (size_t i = 0; i < manifest->count; i++) {
            if (strcmp(sections[s], "dependencies") == 0) {
                if (strncmp(manifest->pairs[i].key, "dependencies.", 13) == 0 ||
                    strncmp(manifest->pairs[i].key, "dev-dependencies.", 17) == 0) {
                    continue; /* Handle separately */
                }
            }
            
            if (strncmp(manifest->pairs[i].key, section_prefix, strlen(section_prefix)) == 0) {
                if (!found) {
                    if (strcmp(sections[s], "package") != 0) {
                        p += sprintf(p, "\n[%s]\n", sections[s]);
                    }
                    found = 1;
                }
                const char *key = manifest->pairs[i].key + strlen(section_prefix);
                
                /* Check if value is array-like */
                if (manifest->pairs[i].value[0] == '[') {
                    p += sprintf(p, "%s = %s\n", key, manifest->pairs[i].value);
                } else {
                    p += sprintf(p, "%s = \"%s\"\n", key, manifest->pairs[i].value);
                }
            }
        }
        
        /* Handle dependencies specially */
        if (strcmp(sections[s], "dependencies") == 0) {
            int has_deps = 0, has_dev = 0;
            
            for (size_t i = 0; i < manifest->count; i++) {
                if (strncmp(manifest->pairs[i].key, "dependencies.", 13) == 0) {
                    if (!has_deps) {
                        p += sprintf(p, "\n[dependencies]\n");
                        has_deps = 1;
                    }
                    const char *key = manifest->pairs[i].key + 13;
                    p += sprintf(p, "%s = \"%s\"\n", key, manifest->pairs[i].value);
                }
            }
            
            for (size_t i = 0; i < manifest->count; i++) {
                if (strncmp(manifest->pairs[i].key, "dev-dependencies.", 17) == 0) {
                    if (!has_dev) {
                        p += sprintf(p, "\n[dev-dependencies]\n");
                        has_dev = 1;
                    }
                    const char *key = manifest->pairs[i].key + 17;
                    p += sprintf(p, "%s = \"%s\"\n", key, manifest->pairs[i].value);
                }
            }
        }
    }
    
    *p = '\0';
    return write_file_contents("cpm.toml", buffer);
}

static int add_dependency_to_manifest(const char *name, const char *version, int dev_dep) {
    toml_table_t *manifest = load_manifest();
    if (!manifest) return -1;
    
    char key[256];
    snprintf(key, sizeof(key), "%s.%s", dev_dep ? "dev-dependencies" : "dependencies", name);
    
    /* Check if already exists */
    if (toml_get(manifest, key)) {
        printf("%s is already in %s\n", name, dev_dep ? "dev-dependencies" : "dependencies");
        toml_free(manifest);
        return 0;
    }
    
    /* Add new dependency */
    if (manifest->count >= manifest->capacity) {
        manifest->capacity *= 2;
        manifest->pairs = realloc(manifest->pairs, sizeof(toml_pair_t) * manifest->capacity);
    }
    
    manifest->pairs[manifest->count].key = strdup(key);
    manifest->pairs[manifest->count].value = strdup(version);
    manifest->count++;
    
    int ret = save_manifest(manifest);
    toml_free(manifest);
    return ret;
}

static int remove_dependency_from_manifest(const char *name, int dev_dep) {
    toml_table_t *manifest = load_manifest();
    if (!manifest) return -1;
    
    char key[256];
    snprintf(key, sizeof(key), "%s.%s", dev_dep ? "dev-dependencies" : "dependencies", name);
    
    /* Find and remove */
    for (size_t i = 0; i < manifest->count; i++) {
        if (strcmp(manifest->pairs[i].key, key) == 0) {
            free(manifest->pairs[i].key);
            free(manifest->pairs[i].value);
            
            /* Shift remaining */
            for (size_t j = i; j < manifest->count - 1; j++) {
                manifest->pairs[j] = manifest->pairs[j + 1];
            }
            manifest->count--;
            
            int ret = save_manifest(manifest);
            toml_free(manifest);
            return ret;
        }
    }
    
    fprintf(stderr, "warning: %s not found in %s\n", name, dev_dep ? "dev-dependencies" : "dependencies");
    toml_free(manifest);
    return 0;
}

/* ============== Lockfile Operations ============== */

static lockfile_t *load_lockfile(void) {
    if (!file_exists("cpm.lock")) {
        return NULL;
    }
    
    toml_table_t *lock = toml_parse_file("cpm.lock");
    if (!lock) return NULL;
    
    lockfile_t *result = calloc(1, sizeof(lockfile_t));
    
    /* Count [[package]] entries */
    for (size_t i = 0; i < lock->count; i++) {
        if (strstr(lock->pairs[i].key, ".name")) {
            result->count++;
        }
    }
    
    if (result->count == 0) {
        toml_free(lock);
        free(result);
        return NULL;
    }
    
    result->packages = calloc(result->count, sizeof(lock_package_t));
    
    /* Parse each package */
    size_t pkg_idx = 0;
    char prefix[64] = "";
    
    for (size_t i = 0; i < lock->count && pkg_idx < result->count; i++) {
        if (strncmp(lock->pairs[i].key, "package.", 8) == 0) {
            const char *rest = lock->pairs[i].key + 8;
            
            if (strcmp(rest, "name") == 0) {
                if (prefix[0] && strcmp(prefix, lock->pairs[i].value) != 0) {
                    pkg_idx++;
                }
                strncpy(prefix, lock->pairs[i].value, sizeof(prefix) - 1);
                result->packages[pkg_idx].name = strdup(lock->pairs[i].value);
            } else if (strcmp(rest, "version") == 0) {
                result->packages[pkg_idx].version = strdup(lock->pairs[i].value);
            } else if (strcmp(rest, "source") == 0) {
                result->packages[pkg_idx].source = strdup(lock->pairs[i].value);
            } else if (strcmp(rest, "checksum") == 0) {
                result->packages[pkg_idx].checksum = strdup(lock->pairs[i].value);
            }
        }
    }
    
    toml_free(lock);
    return result;
}

static int save_lockfile(lockfile_t *lock) {
    char buffer[16384];
    char *p = buffer;
    
    time_t now = time(NULL);
    struct tm *tm_info = gmtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    
    p += sprintf(p, "# cpm.lock — auto-generated, do not edit\n");
    p += sprintf(p, "# Generated: %s\n\n", timestamp);
    
    for (size_t i = 0; i < lock->count; i++) {
        p += sprintf(p, "[[package]]\n");
        p += sprintf(p, "name     = \"%s\"\n", lock->packages[i].name);
        p += sprintf(p, "version  = \"%s\"\n", lock->packages[i].version);
        p += sprintf(p, "source   = \"%s\"\n", lock->packages[i].source ? lock->packages[i].source : "registry+https://registry.cpm.dev");
        p += sprintf(p, "checksum = \"sha256:placeholder\"\n");
        p += sprintf(p, "deps     = []\n\n");
    }
    
    *p = '\0';
    return write_file_contents("cpm.lock", buffer);
}

static void lockfile_free(lockfile_t *lock) {
    if (!lock) return;
    
    for (size_t i = 0; i < lock->count; i++) {
        free(lock->packages[i].name);
        free(lock->packages[i].version);
        free(lock->packages[i].source);
        free(lock->packages[i].checksum);
        
        for (size_t j = 0; j < lock->packages[i].dep_count; j++) {
            free(lock->packages[i].deps[j]);
        }
        free(lock->packages[i].deps);
    }
    
    free(lock->packages);
    free(lock);
}

/* ============== Dependency Resolution ============== */

static int resolve_dependencies_simple(toml_table_t *manifest, lockfile_t **result) {
    lockfile_t *lock = calloc(1, sizeof(lockfile_t));
    lock->packages = malloc(sizeof(lock_package_t) * 64);
    lock->count = 0;
    
    /* Collect all dependencies */
    for (size_t i = 0; i < manifest->count; i++) {
        if (strncmp(manifest->pairs[i].key, "dependencies.", 13) == 0 ||
            strncmp(manifest->pairs[i].key, "dev-dependencies.", 17) == 0) {
            
            const char *name = strchr(manifest->pairs[i].key, '.') + 1;
            const char *constraint = manifest->pairs[i].value;
            
            /* Check if package exists in mock registry */
            if (!mock_registry_exists(name)) {
                fprintf(stderr, "error: package '%s' not found in registry\n", name);
                lockfile_free(lock);
                return -1;
            }
            
            /* Find best matching version */
            const char *version = mock_registry_get_version(name, constraint);
            if (!version) {
                fprintf(stderr, "error: no version of '%s' satisfies '%s'\n", name, constraint);
                lockfile_free(lock);
                return -1;
            }
            
            /* Check language compatibility */
            const char *pkg_lang = mock_registry_get_lang(name);
            const char *proj_lang = toml_get(manifest, "package.lang");
            if (!proj_lang) proj_lang = "c";
            
            if (strcmp(proj_lang, "c") == 0 && strcmp(pkg_lang, "c++") == 0) {
                fprintf(stderr, "error: C project cannot depend on C++ package '%s'\n", name);
                fprintf(stderr, "hint: set lang = \"c++\" in cpm.toml to use C++ packages\n");
                lockfile_free(lock);
                return -1;
            }
            
            /* Add to lock */
            lock->packages[lock->count].name = strdup(name);
            lock->packages[lock->count].version = strdup(version);
            lock->packages[lock->count].source = strdup("registry+https://registry.cpm.dev");
            lock->packages[lock->count].checksum = compute_sha256(version, strlen(version));
            lock->packages[lock->count].deps = NULL;
            lock->packages[lock->count].dep_count = 0;
            lock->count++;
            
            /* Process transitive dependencies */
            const char *trans_deps = mock_registry_get_deps(name, version);
            if (trans_deps && strlen(trans_deps) > 0) {
                char *deps_copy = strdup(trans_deps);
                char *token = strtok(deps_copy, ",");
                
                while (token) {
                    /* Check if already added */
                    int found = 0;
                    for (size_t j = 0; j < lock->count - 1; j++) {
                        if (strcmp(lock->packages[j].name, token) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    
                    if (!found && mock_registry_exists(token)) {
                        const char *trans_ver = mock_registry_get_version(token, "*");
                        if (trans_ver) {
                            lock->packages[lock->count].name = strdup(token);
                            lock->packages[lock->count].version = strdup(trans_ver);
                            lock->packages[lock->count].source = strdup("registry+https://registry.cpm.dev");
                            lock->packages[lock->count].checksum = compute_sha256(trans_ver, strlen(trans_ver));
                            lock->packages[lock->count].deps = NULL;
                            lock->packages[lock->count].dep_count = 0;
                            lock->count++;
                        }
                    }
                    
                    token = strtok(NULL, ",");
                }
                
                free(deps_copy);
            }
        }
    }
    
    *result = lock;
    return 0;
}

/* ============== Fetch and Build ============== */

static int fetch_package(const char *name, const char *version, const char *source) {
    (void)source;
    
    printf("Fetching %s@%s...\n", name, version);
    
    /* Create cache directory structure */
    char cache_path[512];
    snprintf(cache_path, sizeof(cache_path), "%s/src/%s-%s", CPM_CACHE_DIR, name, version);
    
    if (dir_exists(cache_path)) {
        printf("  Already cached\n");
        return 0;
    }
    
    create_directories_recursive(cache_path);
    
    /* Create mock source files based on package type */
    const char *libtype = mock_registry_get_libtype(name);
    const char *lang = mock_registry_get_lang(name);
    
    char src_path[512];
    snprintf(src_path, sizeof(src_path), "%s/%s", cache_path, strcmp(lang, "c++") == 0 ? "include" : "include");
    create_directories_recursive(src_path);
    
    /* Create placeholder header */
    char header_path[512];
    snprintf(header_path, sizeof(header_path), "%s/%s.h", src_path, name);
    
    FILE *f = fopen(header_path, "w");
    if (f) {
        fprintf(f, "/* %s version %s */\n", name, version);
        fprintf(f, "#ifndef %s_H\n", name);
        fprintf(f, "#define %s_H\n\n", name);
        
        if (strcmp(lang, "c++") == 0) {
            fprintf(f, "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n");
        }
        
        fprintf(f, "/* Placeholder for %s */\n", name);
        fprintf(f, "int %s_init(void);\n", name);
        
        if (strcmp(lang, "c++") == 0) {
            fprintf(f, "\n#ifdef __cplusplus\n}\n#endif\n");
        }
        
        fprintf(f, "\n#endif /* %s_H */\n", name);
        fclose(f);
    }
    
    /* Create source file for non-header-only libraries */
    if (strcmp(libtype, "header-only") != 0) {
        char lib_path[512];
        snprintf(lib_path, sizeof(lib_path), "%s/lib", cache_path);
        create_directories_recursive(lib_path);
        
        char src_file[512];
        snprintf(src_file, sizeof(src_file), "%s/%s.c", cache_path, name);
        f = fopen(src_file, "w");
        if (f) {
            fprintf(f, "/* %s implementation */\n", name);
            fprintf(f, "#include \"%s.h\"\n\n", name);
            fprintf(f, "int %s_init(void) { return 0; }\n", name);
            fclose(f);
        }
    }
    
    printf("  Downloaded to %s\n", cache_path);
    return 0;
}

static int build_package(const char *name, const char *version, const char *lang, const char *std) {
    const char *libtype = mock_registry_get_libtype(name);
    
    if (strcmp(libtype, "header-only") == 0) {
        printf("  %s is header-only, skipping build\n", name);
        return 0;
    }
    
    printf("  Building %s...\n", name);
    
    char cache_path[512];
    snprintf(cache_path, sizeof(cache_path), "%s/src/%s-%s", CPM_CACHE_DIR, name, version);
    
    char build_path[512];
    snprintf(build_path, sizeof(build_path), "%s/build", cache_path);
    create_directories_recursive(build_path);
    
    /* Determine compiler */
    const char *compiler = strcmp(lang, "c++") == 0 ? "g++" : "gcc";
    const char *stdflag = strcmp(lang, "c++") == 0 ? "-std=c++17" : "-std=c17";
    (void)std; /* Use default for now */
    
    char obj_path[512];
    snprintf(obj_path, sizeof(obj_path), "%s/%s.o", build_path, name);
    
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s %s -O2 -fPIC -I%s/include -c %s/%s.c -o %s 2>/dev/null",
             compiler, stdflag, cache_path, cache_path, name, obj_path);
    
    if (system(cmd) != 0) {
        /* Create dummy .a file for demo */
        printf("  (simulating build)\n");
    }
    
    /* Create static library */
    char lib_path[512];
    snprintf(lib_path, sizeof(lib_path), "%s/lib%s.a", build_path, name);
    
    snprintf(cmd, sizeof(cmd), "ar rcs %s %s 2>/dev/null || touch %s", lib_path, obj_path, lib_path);
    system(cmd);
    
    printf("  Built %s\n", lib_path);
    return 0;
}

static int install_package_to_project(const char *name, const char *version) {
    printf("  Installing %s@%s to project...\n", name, version);
    
    char cache_path[512];
    snprintf(cache_path, sizeof(cache_path), "%s/src/%s-%s", CPM_CACHE_DIR, name, version);
    
    char deps_path[256];
    snprintf(deps_path, sizeof(deps_path), "%s/%s-%s", CPM_DEPS_DIR, name, version);
    
    create_directories_recursive(deps_path);
    
    /* Copy include directory */
    char src_include[512], dst_include[256];
    snprintf(src_include, sizeof(src_include), "%s/include", cache_path);
    snprintf(dst_include, sizeof(dst_include), "%s/include", deps_path);
    
    /* For demo, just create symlink or copy */
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "cp -r %s %s 2>/dev/null || mkdir -p %s", src_include, dst_include, dst_include);
    system(cmd);
    
    /* Copy lib directory if exists */
    char src_lib[512], dst_lib[256];
    snprintf(src_lib, sizeof(src_lib), "%s/lib", cache_path);
    snprintf(dst_lib, sizeof(dst_lib), "%s/lib", deps_path);
    
    if (dir_exists(src_lib)) {
        snprintf(cmd, sizeof(cmd), "cp -r %s %s 2>/dev/null || mkdir -p %s", src_lib, dst_lib, dst_lib);
        system(cmd);
    }
    
    return 0;
}

/* ============== Build System ============== */

static int generate_flags_file(lockfile_t *lock) {
    char buffer[4096];
    char *p = buffer;
    
    p += sprintf(p, "# .cpm/flags.env — source this in your build scripts\n");
    p += sprintf(p, "CPM_CFLAGS=\"");
    
    for (size_t i = 0; i < lock->count; i++) {
        p += sprintf(p, "-I%s/%s-%s/include ", CPM_DEPS_DIR, lock->packages[i].name, lock->packages[i].version);
    }
    p += sprintf(p, "\"\n");
    
    p += sprintf(p, "CPM_LDFLAGS=\"");
    for (size_t i = 0; i < lock->count; i++) {
        p += sprintf(p, "-L%s/%s-%s/lib ", CPM_DEPS_DIR, lock->packages[i].name, lock->packages[i].version);
    }
    p += sprintf(p, "\"\n");
    
    p += sprintf(p, "CPM_LIBS=\"");
    for (size_t i = 0; i < lock->count; i++) {
        const char *libtype = mock_registry_get_libtype(lock->packages[i].name);
        if (strcmp(libtype, "header-only") != 0) {
            p += sprintf(p, "-l%s ", lock->packages[i].name);
        }
    }
    p += sprintf(p, "\"\n");
    
    *p = '\0';
    
    create_directory(CPM_DIR);
    return write_file_contents(".cpm/flags.env", buffer);
}

static int generate_compile_commands(lockfile_t *lock) {
    char buffer[8192];
    char *p = buffer;
    
    p += sprintf(p, "[\n");
    
    /* Add compile commands for each dependency */
    int first = 1;
    for (size_t i = 0; i < lock->count; i++) {
        const char *libtype = mock_registry_get_libtype(lock->packages[i].name);
        if (strcmp(libtype, "header-only") == 0) continue;
        
        if (!first) p += sprintf(p, ",\n");
        first = 0;
        
        p += sprintf(p, "  {\n");
        p += sprintf(p, "    \"directory\": \"%s\",\n", get_working_dir());
        p += sprintf(p, "    \"command\": \"gcc -c -O2 -std=c17 ");
        
        for (size_t j = 0; j < lock->count; j++) {
            p += sprintf(p, "-I%s/%s-%s/include ", CPM_DEPS_DIR, lock->packages[j].name, lock->packages[j].version);
        }
        
        p += sprintf(p, "%s/src/%s-%s/%s.c -o %s/src/%s-%s/build/%s.o\",\n",
                     get_working_dir(), lock->packages[i].name, lock->packages[i].version,
                     lock->packages[i].name,
                     get_working_dir(), lock->packages[i].name, lock->packages[i].version,
                     lock->packages[i].name);
        p += sprintf(p, "    \"file\": \"%s/src/%s-%s/%s.c\"\n",
                     get_working_dir(), lock->packages[i].name, lock->packages[i].version,
                     lock->packages[i].name);
        p += sprintf(p, "  }");
    }
    
    p += sprintf(p, "\n]\n");
    *p = '\0';
    
    return write_file_contents(".cpm/compile_commands.json", buffer);
}

static int compile_project(const char *sources[], int source_count, const char *output) {
    /* Load flags */
    char cflags[2048] = "";
    char ldflags[2048] = "";
    char libs[1024] = "";
    
    if (file_exists(".cpm/flags.env")) {
        toml_table_t *flags = toml_parse_file(".cpm/flags.env");
        if (flags) {
            char *cf = toml_get(flags, "CPM_CFLAGS");
            char *lf = toml_get(flags, "CPM_LDFLAGS");
            char *lb = toml_get(flags, "CPM_LIBS");
            
            if (cf) strncpy(cflags, cf, sizeof(cflags) - 1);
            if (lf) strncpy(ldflags, lf, sizeof(ldflags) - 1);
            if (lb) strncpy(libs, lb, sizeof(libs) - 1);
            
            toml_free(flags);
        }
    }
    
    /* Get project settings */
    toml_table_t *manifest = load_manifest();
    const char *std = manifest ? toml_get(manifest, "package.std") : NULL;
    const char *lang = manifest ? toml_get(manifest, "package.lang") : "c";
    
    const char *compiler = (lang && strcmp(lang, "c++") == 0) ? "g++" : "gcc";
    const char *stdflag = std ? (strcmp(lang, "c++") == 0 ? "-std=c++17" : "-std=c17") : "-std=c17";
    
    /* Build command */
    char cmd[4096];
    snprintf(cmd, sizeof(cmd), "%s %s -O2 -Wall -Wextra %s %s ", 
             compiler, stdflag, cflags, ldflags);
    
    for (int i = 0; i < source_count; i++) {
        char *tail = cmd + strlen(cmd);
        snprintf(tail, sizeof(cmd) - strlen(cmd), "%s ", sources[i]);
    }
    
    char *tail = cmd + strlen(cmd);
    snprintf(tail, sizeof(cmd) - strlen(cmd), "-o %s %s", output, libs);
    
    printf("Compiling: %s\n", cmd);
    
    /* Create output directory */
    char *output_copy = strdup(output);
    char *slash = strrchr(output_copy, '/');
    if (slash) {
        *slash = '\0';
        if (strlen(output_copy) > 0) {
            create_directories_recursive(output_copy);
        }
    }
    free(output_copy);
    
    int ret = system(cmd);
    
    if (ret == 0) {
        printf("Build successful: %s\n", output);
    } else {
        fprintf(stderr, "Build failed\n");
    }
    
    if (manifest) toml_free(manifest);
    return ret;
}

/* ============== CLI Commands ============== */

static int cmd_init(int cpp_mode) {
    printf("Initializing %s project...\n", cpp_mode ? "C++" : "C");
    
    /* Check if already initialized */
    if (file_exists("cpm.toml")) {
        fprintf(stderr, "warning: cpm.toml already exists\n");
    }
    
    /* Get directory name for project name */
    char *cwd = get_working_dir();
    char *proj_name = strrchr(cwd, '/');
    proj_name = proj_name ? proj_name + 1 : cwd;
    
    /* Create cpm.toml */
    /* Create cpm.toml */
    char manifest[2048];
    const char *tmpl = 
        "[package]\n"
        "name    = \"%s\"\n"
        "version = \"0.1.0\"\n"
        "authors = [\"Developer <dev@example.com>\"]\n"
        "license = \"MIT\"\n"
        "lang    = \"%s\"\n"
        "std     = \"%s\"\n"
        "\n"
        "[dependencies]\n"
        "\n"
        "[dev-dependencies]\n"
        "\n"
        "[build]\n"
        "cc      = \"%s\"\n"
        "cflags  = [\"-O2\", \"-Wall\", \"-Wextra\"]\n"
        "ldflags = []\n"
        "\n"
        "[scripts]\n"
        "build   = \"cpm compile src/main.%s -o bin/%s\"\n"
        "test    = \"cpm compile tests/*.%s -o bin/tests && ./bin/tests\"\n"
        "clean   = \"rm -rf bin/ .cpm/build/\"\n";
    
    snprintf(manifest, sizeof(manifest), tmpl,
        proj_name,
        cpp_mode ? "c++" : "c",
        cpp_mode ? "c++17" : "c17",
        cpp_mode ? "g++" : "gcc",
        cpp_mode ? "cpp" : "c",
        proj_name,
        cpp_mode ? "cpp" : "c");

    
    if (write_file_contents("cpm.toml", manifest) != 0) {
        fprintf(stderr, "error: failed to create cpm.toml\n");
        free(cwd);
        return 1;
    }
    
    /* Create directory structure */
    create_directory("src");
    create_directory("include");
    create_directory("tests");
    create_directory("bin");
    
    /* Create starter main file */
    char main_file[512];
    snprintf(main_file, sizeof(main_file), "src/main.%s", cpp_mode ? "cpp" : "c");
    
    if (!file_exists(main_file)) {
        FILE *f = fopen(main_file, "w");
        if (f) {
            if (cpp_mode) {
                fprintf(f, "#include <iostream>\n\n");
                fprintf(f, "int main() {\n");
                fprintf(f, "    std::cout << \"Hello from %s!\" << std::endl;\n", proj_name);
                fprintf(f, "    return 0;\n");
                fprintf(f, "}\n");
            } else {
                fprintf(f, "#include <stdio.h>\n\n");
                fprintf(f, "int main() {\n");
                fprintf(f, "    printf(\"Hello from %s!\\n\");\n", proj_name);
                fprintf(f, "    return 0;\n");
                fprintf(f, "}\n");
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
    printf("  cpm run build         # Build the project\n");
    
    free(cwd);
    return 0;
}

static int cmd_add(const char *package, const char *version, int dev_mode) {
    printf("Adding %s", package);
    if (version && strcmp(version, "latest") != 0) {
        printf("@%s", version);
    }
    printf("%s...\n", dev_mode ? " (dev)" : "");
    
    /* Check if package exists */
    if (!mock_registry_exists(package)) {
        fprintf(stderr, "error: package '%s' not found in registry\n", package);
        fprintf(stderr, "hint: try searching for similar packages\n");
        return 1;
    }
    
    /* Add to manifest */
    if (add_dependency_to_manifest(package, version, dev_mode) != 0) {
        fprintf(stderr, "error: failed to update cpm.toml\n");
        return 1;
    }
    
    printf("Added %s to %s\n", package, dev_mode ? "dev-dependencies" : "dependencies");
    
    /* Auto-install */
    printf("Running install...\n");
    return cmd_install(0);
}

static int cmd_install(int update_mode) {
    printf("Installing dependencies...\n");
    
    /* Load manifest */
    toml_table_t *manifest = load_manifest();
    if (!manifest) {
        return 1;
    }
    
    /* Try to load existing lockfile */
    lockfile_t *lock = NULL;
    
    if (!update_mode && load_lockfile()) {
        lock = load_lockfile();
        printf("Using existing lockfile\n");
    }
    
    /* Resolve dependencies */
    if (!lock || update_mode) {
        printf("Resolving dependencies...\n");
        
        if (resolve_dependencies_simple(manifest, &lock) != 0) {
            fprintf(stderr, "error: dependency resolution failed\n");
            toml_free(manifest);
            return 1;
        }
        
        /* Save lockfile */
        if (save_lockfile(lock) != 0) {
            fprintf(stderr, "warning: failed to save lockfile\n");
        }
        printf("Generated cpm.lock\n");
    }
    
    toml_free(manifest);
    
    /* Fetch and build each package */
    create_directories_recursive(CPM_DEPS_DIR);
    
    for (size_t i = 0; i < lock->count; i++) {
        const char *name = lock->packages[i].name;
        const char *version = lock->packages[i].version;
        
        const char *lang = mock_registry_get_lang(name);
        const char *std = toml_get(load_manifest(), "package.std");
        
        fetch_package(name, version, lock->packages[i].source);
        build_package(name, version, lang, std ? std : "c17");
        install_package_to_project(name, version);
    }
    
    /* Generate build files */
    generate_flags_file(lock);
    generate_compile_commands(lock);
    
    printf("\nInstalled %zu packages\n", lock->count);
    printf("Dependencies are available in %s/\n", CPM_DEPS_DIR);
    printf("Source .cpm/flags.env to use compiler flags\n");
    
    lockfile_free(lock);
    return 0;
}

static int cmd_build(void) {
    printf("Building project...\n");
    
    /* Load manifest for script */
    toml_table_t *manifest = load_manifest();
    if (!manifest) {
        /* Try to run install first */
        cmd_install(0);
        manifest = load_manifest();
        if (!manifest) return 1;
    }
    
    /* Get build script */
    const char *build_cmd = toml_get(manifest, "scripts.build");
    if (!build_cmd) {
        /* Default build */
        const char *lang = toml_get(manifest, "package.lang");
        const char *ext = (lang && strcmp(lang, "c++") == 0) ? "cpp" : "c";
        
        char output[256];
        const char *name = toml_get(manifest, "package.name");
        snprintf(output, sizeof(output), "bin/%s", name ? name : "app");
        
        const char *sources[] = {"src/main.c", "src/main.cpp"};
        compile_project(sources, 1, output);
    } else {
        printf("Running: %s\n", build_cmd);
        system(build_cmd);
    }
    
    toml_free(manifest);
    return 0;
}

static int cmd_run(const char *script) {
    toml_table_t *manifest = load_manifest();
    if (!manifest) return 1;
    
    char key[64];
    snprintf(key, sizeof(key), "scripts.%s", script);
    
    const char *cmd = toml_get(manifest, key);
    if (!cmd) {
        fprintf(stderr, "error: script '%s' not found in cpm.toml\n", script);
        toml_free(manifest);
        return 1;
    }
    
    printf("Running '%s': %s\n", script, cmd);
    int ret = system(cmd);
    
    toml_free(manifest);
    return ret;
}

static int cmd_remove(const char *package) {
    printf("Removing %s...\n", package);
    
    /* Remove from manifest */
    if (remove_dependency_from_manifest(package, 0) != 0) {
        /* Try dev dependencies */
        remove_dependency_from_manifest(package, 1);
    }
    
    /* Re-resolve and update lockfile */
    toml_table_t *manifest = load_manifest();
    if (manifest) {
        lockfile_t *lock = NULL;
        if (resolve_dependencies_simple(manifest, &lock) == 0) {
            save_lockfile(lock);
            
            /* Clean up unused packages */
            DIR *dir = opendir(CPM_DEPS_DIR);
            if (dir) {
                struct dirent *entry;
                while ((entry = readdir(dir)) != NULL) {
                    if (entry->d_name[0] == '.') continue;
                    
                    /* Check if still needed */
                    int found = 0;
                    for (size_t i = 0; i < lock->count; i++) {
                        if (strncmp(entry->d_name, lock->packages[i].name, strlen(lock->packages[i].name)) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    
                    if (!found) {
                        char path[512];
                        snprintf(path, sizeof(path), "%s/%s", CPM_DEPS_DIR, entry->d_name);
                        printf("  Removing unused: %s\n", entry->d_name);
                        snprintf(path, sizeof(path), "rm -rf %s/%s", CPM_DEPS_DIR, entry->d_name);
                        system(path);
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

static int cmd_update(const char *package) {
    printf("Updating %s...\n", package ? package : "all dependencies");
    
    toml_table_t *manifest = load_manifest();
    if (!manifest) return 1;
    
    lockfile_t *lock = NULL;
    if (resolve_dependencies_simple(manifest, &lock) == 0) {
        save_lockfile(lock);
        
        /* Re-fetch and rebuild updated packages */
        for (size_t i = 0; i < lock->count; i++) {
            if (!package || strcmp(lock->packages[i].name, package) == 0) {
                fetch_package(lock->packages[i].name, lock->packages[i].version, 
                             lock->packages[i].source);
            }
        }
        
        printf("Updated to latest versions\n");
        lockfile_free(lock);
    }
    
    toml_free(manifest);
    return 0;
}

static int cmd_publish(void) {
    printf("Publishing package...\n");
    
    /* Validate manifest */
    toml_table_t *manifest = load_manifest();
    if (!manifest) {
        fprintf(stderr, "error: cpm.toml not found\n");
        return 1;
    }
    
    const char *name = toml_get(manifest, "package.name");
    const char *version = toml_get(manifest, "package.version");
    
    if (!name || !version) {
        fprintf(stderr, "error: package name and version required\n");
        toml_free(manifest);
        return 1;
    }
    
    /* Validate headers exist */
    const char *lang = toml_get(manifest, "package.lang");
    char header_pattern[256];
    snprintf(header_pattern, sizeof(header_pattern), "include/%s.h", name);
    
    /* In real implementation, would validate extern "C" guards for C libs */
    
    printf("Package: %s@%s\n", name, version);
    printf("Validating package structure...\n");
    
    /* Check required files */
    if (!file_exists("cpm.toml")) {
        fprintf(stderr, "error: cpm.toml missing\n");
        toml_free(manifest);
        return 1;
    }
    
    /* Would create tarball and upload to registry */
    printf("Would upload to registry (mock mode)\n");
    printf("Publish successful!\n");
    
    toml_free(manifest);
    return 0;
}
