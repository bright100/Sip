/*
 * registry.c — CPM Registry Client
 *
 * Replaces mock/registry_mock.c with real HTTP calls to the CPM Registry API.
 * Uses libcurl for HTTP and a minimal hand-rolled JSON parser (no extra deps).
 *
 * Flow:
 *   1. First call to any registry_* function triggers registry_load().
 *   2. registry_load() hits GET /api/packages and caches the response in
 *      .cpm/registry-cache.json (TTL: 1 hour).
 *   3. Individual package detail is fetched on demand and cached per-package
 *      in .cpm/pkg-cache/<name>.json.
 *   4. Falls back to stale cache if the network is unavailable.
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
#  include <windows.h>
#  define mkdir_compat(p) mkdir(p)
#else
#  include <unistd.h>
#  define mkdir_compat(p) mkdir((p), 0755)
#endif

#include <curl/curl.h>
#include "registry.h"
#include "core/utils.h"

/* ── Config ─────────────────────────────────────────────────────────────── */
#define CACHE_DIR          ".cpm/registry"
#define CACHE_LIST         CACHE_DIR "/packages.json"
#define CACHE_PKG_DIR      CACHE_DIR "/pkg"
#define CACHE_TTL          3600          /* seconds — 1 hour                */
#define MAX_PACKAGES       512
#define MAX_VERSIONS       64
#define MAX_DEPS           32
#define NAME_LEN           128
#define VER_LEN            32
#define URL_LEN            1024
#define DESC_LEN           512

/* ── In-memory store ────────────────────────────────────────────────────── */
typedef struct {
    char name       [NAME_LEN];
    char description[DESC_LEN];
    char lang       [16];
    char libtype    [32];
    char latest     [VER_LEN];
    /* Detailed fields — populated after per-package fetch */
    char versions   [MAX_VERSIONS][VER_LEN];
    int  version_count;
    char deps       [MAX_VERSIONS][NAME_LEN * 4]; /* comma-sep dep names   */
    char dep_constraints[MAX_VERSIONS][NAME_LEN * 4];
    char download_url[URL_LEN];
    int  detail_loaded;
} reg_entry_t;

static reg_entry_t g_pkgs[MAX_PACKAGES];
static int         g_count       = 0;
static int         g_list_loaded = 0;
static char        g_token[256]  = "";
static char        g_base_url[URL_LEN] = CPM_REGISTRY_URL;

/* ── Auth token ─────────────────────────────────────────────────────────── */
void registry_set_token(const char *token)
{
    snprintf(g_token, sizeof(g_token), "%s", token ? token : "");
}

/* ── curl write buffer ──────────────────────────────────────────────────── */
typedef struct { char *data; size_t size; } cbuf_t;

static size_t write_cb(void *ptr, size_t size, size_t n, void *ud)
{
    size_t real = size * n;
    cbuf_t *b   = ud;
    char   *tmp = realloc(b->data, b->size + real + 1);
    if (!tmp) return 0;
    b->data = tmp;
    memcpy(b->data + b->size, ptr, real);
    b->size += real;
    b->data[b->size] = '\0';
    return real;
}

/* ── HTTP GET ────────────────────────────────────────────────────────────── */
static char *http_get(const char *url)
{
    CURL   *curl = curl_easy_init();
    if (!curl) return NULL;

    cbuf_t buf = { malloc(1), 0 };
    buf.data[0] = '\0';

    struct curl_slist *headers = NULL;
    if (g_token[0]) {
        char hdr[320];
        snprintf(hdr, sizeof(hdr), "X-Cpm-Token: %s", g_token);
        headers = curl_slist_append(headers, hdr);
    }
    headers = curl_slist_append(headers, "Accept: application/json");

    curl_easy_setopt(curl, CURLOPT_URL,            url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER,      headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,   write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,       &buf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION,  1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,         15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,       "cpm/0.1");
#ifdef _WIN32
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER,  0L);
#endif

    CURLcode rc = curl_easy_perform(curl);
    curl_slist_free_all(headers);

    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK || status < 200 || status >= 300) {
        if (rc != CURLE_OK)
            fprintf(stderr, "[cpm] network error: %s\n", curl_easy_strerror(rc));
        else
            fprintf(stderr, "[cpm] registry returned HTTP %ld for %s\n", status, url);
        free(buf.data);
        return NULL;
    }
    return buf.data;
}

/* ── HTTP POST (for download tracking) ──────────────────────────────────── */
static void http_post_empty(const char *url)
{
    CURL *curl = curl_easy_init();
    if (!curl) return;
    curl_easy_setopt(curl, CURLOPT_URL,           url);
    curl_easy_setopt(curl, CURLOPT_POST,          1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, 0L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,       10L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,     "cpm/0.1");
#ifdef _WIN32
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
#endif
    curl_easy_perform(curl);
    curl_easy_cleanup(curl);
}

/* ── Minimal JSON helpers ───────────────────────────────────────────────── */

/* Return a malloc'd string value for the given key, or NULL. */
static char *json_str(const char *json, const char *key)
{
    char needle[NAME_LEN + 4];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return NULL;
    p += strlen(needle);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p != '"') return NULL;
    p++;
    const char *e = p;
    while (*e && *e != '"') { if (*e == '\\') e++; e++; }
    size_t len = (size_t)(e - p);
    char  *out = malloc(len + 1);
    memcpy(out, p, len);
    out[len] = '\0';
    return out;
}

/* Fill arr[] with string values from the JSON array at key. Returns count. */
static int json_str_arr(const char *json, const char *key,
                         char arr[][VER_LEN], int max)
{
    char needle[NAME_LEN + 4];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return 0;
    p += strlen(needle);
    while (*p && *p != '[') p++;
    if (*p != '[') return 0;
    p++;
    int n = 0;
    while (*p && *p != ']' && n < max) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (*p == ']' || !*p) break;
        if (*p == '"') {
            p++;
            const char *e = p;
            while (*e && *e != '"') e++;
            size_t len = (size_t)(e - p);
            if (len >= VER_LEN) len = VER_LEN - 1;
            memcpy(arr[n], p, len);
            arr[n][len] = '\0';
            n++;
            p = e + 1;
        } else p++;
    }
    return n;
}

/* Find the JSON object/array that is the value of key.
   Returns pointer to the opening { or [, or NULL. */
static const char *json_obj(const char *json, const char *key)
{
    char needle[NAME_LEN + 4];
    snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char *p = strstr(json, needle);
    if (!p) return NULL;
    p += strlen(needle);
    while (*p && *p != '{' && *p != '[' && *p != '\n') p++;
    return (*p == '{' || *p == '[') ? p : NULL;
}

/* Skip a JSON value (string, number, object, array). Returns pointer after it. */
static const char *json_skip(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p == '"') {
        p++;
        while (*p && *p != '"') { if (*p == '\\') p++; p++; }
        return *p ? p + 1 : p;
    }
    if (*p == '{' || *p == '[') {
        char open = *p, close = (*p == '{') ? '}' : ']';
        int depth = 0;
        while (*p) {
            if (*p == open)  depth++;
            if (*p == close) { depth--; if (!depth) return p + 1; }
            if (*p == '"') { p++; while (*p && *p != '"') { if (*p=='\\') p++; p++; } }
            p++;
        }
        return p;
    }
    /* number / bool / null */
    while (*p && *p != ',' && *p != '}' && *p != ']' && *p != '\n') p++;
    return p;
}

/* ── Cache helpers ──────────────────────────────────────────────────────── */
static void ensure_cache_dirs(void)
{
    mkdir_compat(CACHE_DIR);
    mkdir_compat(CACHE_PKG_DIR);
}

static int cache_fresh(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return (time(NULL) - st.st_mtime) < CACHE_TTL;
}

static char *cache_read(const char *path)
{
    return cpm_read_file(path);
}

static void cache_write(const char *path, const char *json)
{
    ensure_cache_dirs();
    cpm_write_file(path, json);
}

/* ── Parse package list (GET /api/packages) ─────────────────────────────── */
static int parse_package_list(const char *json)
{
    /*
     * Expected shape (array of PackageSummaryDto):
     * [
     *   { "name":"cJSON", "description":"...", "lang":"c",
     *     "libType":"source", "latestVersion":"1.7.17", "updatedAt":"..." },
     *   ...
     * ]
     */
    g_count = 0;
    const char *p = json;
    while (*p && *p != '[') p++;
    if (*p != '[') return -1;
    p++;

    while (*p && g_count < MAX_PACKAGES) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (*p == ']' || !*p) break;
        if (*p != '{') { p++; continue; }

        /* Find matching } */
        const char *obj_start = p;
        const char *obj_end   = json_skip(p);
        size_t obj_len = (size_t)(obj_end - obj_start);

        char obj[2048];
        if (obj_len >= sizeof(obj)) obj_len = sizeof(obj) - 1;
        memcpy(obj, obj_start, obj_len);
        obj[obj_len] = '\0';

        reg_entry_t *e = &g_pkgs[g_count];
        memset(e, 0, sizeof(*e));

        char *v;
        if ((v = json_str(obj, "name")))          { snprintf(e->name,        sizeof(e->name),        "%s", v); free(v); } else { p = obj_end; continue; }
        if ((v = json_str(obj, "description")))    { snprintf(e->description, sizeof(e->description), "%s", v); free(v); }
        if ((v = json_str(obj, "lang")))           { snprintf(e->lang,        sizeof(e->lang),        "%s", v); free(v); }
        else snprintf(e->lang, sizeof(e->lang), "c");
        if ((v = json_str(obj, "libType")))        { snprintf(e->libtype,     sizeof(e->libtype),     "%s", v); free(v); }
        else snprintf(e->libtype, sizeof(e->libtype), "source");
        if ((v = json_str(obj, "latestVersion")))  { snprintf(e->latest,      sizeof(e->latest),      "%s", v); free(v); }

        g_count++;
        p = obj_end;
    }
    return 0;
}

/* ── Parse single package detail (GET /api/packages/{name}) ─────────────── */
static void parse_package_detail(reg_entry_t *e, const char *json)
{
    /*
     * Expected shape (PackageDetailDto):
     * {
     *   "name":"cJSON", "description":"...", "lang":"c", "libType":"source",
     *   "versions": [
     *     { "version":"1.7.17", "downloadUrl":"...", "checksum":"...",
     *       "dependencies": [ { "dependencyName":"zlib", "versionConstraint":"^1.3" } ]
     *     }
     *   ]
     * }
     */
    char *v;
    if ((v = json_str(json, "lang")))    { snprintf(e->lang,    sizeof(e->lang),    "%s", v); free(v); }
    if ((v = json_str(json, "libType"))) { snprintf(e->libtype, sizeof(e->libtype), "%s", v); free(v); }

    const char *vers_arr = json_obj(json, "versions");
    if (!vers_arr || *vers_arr != '[') { e->detail_loaded = 1; return; }

    const char *p = vers_arr + 1;
    e->version_count = 0;

    while (*p && e->version_count < MAX_VERSIONS) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r' || *p == '\t') p++;
        if (*p == ']' || !*p) break;
        if (*p != '{') { p++; continue; }

        const char *obj_start = p;
        const char *obj_end   = json_skip(p);
        size_t obj_len = (size_t)(obj_end - obj_start);

        char obj[4096];
        if (obj_len >= sizeof(obj)) obj_len = sizeof(obj) - 1;
        memcpy(obj, obj_start, obj_len);
        obj[obj_len] = '\0';

        int vi = e->version_count;

        if ((v = json_str(obj, "version"))) {
            snprintf(e->versions[vi], sizeof(e->versions[vi]), "%s", v);
            free(v);
        } else { p = obj_end; continue; }

        if ((v = json_str(obj, "downloadUrl"))) {
            /* Store on first version only — used by registry_download */
            if (vi == 0) snprintf(e->download_url, sizeof(e->download_url), "%s", v);
            free(v);
        }

        /* Parse dependencies array */
        const char *deps_arr = json_obj(obj, "dependencies");
        char deps_buf[NAME_LEN * 4]  = "";
        char cons_buf[NAME_LEN * 4]  = "";

        if (deps_arr && *deps_arr == '[') {
            const char *dp = deps_arr + 1;
            while (*dp && *dp != ']') {
                while (*dp == ' ' || *dp == ',' || *dp == '\n' || *dp == '\r' || *dp == '\t') dp++;
                if (*dp == ']' || !*dp) break;
                if (*dp != '{') { dp++; continue; }

                const char *de = json_skip(dp);
                size_t dlen = (size_t)(de - dp);
                char dobj[512];
                if (dlen >= sizeof(dobj)) dlen = sizeof(dobj) - 1;
                memcpy(dobj, dp, dlen);
                dobj[dlen] = '\0';

                char *dn = json_str(dobj, "dependencyName");
                char *dc = json_str(dobj, "versionConstraint");
                if (dn) {
                    if (deps_buf[0]) strncat(deps_buf, ",", sizeof(deps_buf) - strlen(deps_buf) - 1);
                    strncat(deps_buf, dn, sizeof(deps_buf) - strlen(deps_buf) - 1);
                    free(dn);
                }
                if (dc) {
                    if (cons_buf[0]) strncat(cons_buf, ",", sizeof(cons_buf) - strlen(cons_buf) - 1);
                    strncat(cons_buf, dc, sizeof(cons_buf) - strlen(cons_buf) - 1);
                    free(dc);
                }
                dp = de;
            }
        }

        snprintf(e->deps[vi],            sizeof(e->deps[vi]),            "%s", deps_buf);
        snprintf(e->dep_constraints[vi], sizeof(e->dep_constraints[vi]), "%s", cons_buf);

        e->version_count++;
        p = obj_end;
    }

    e->detail_loaded = 1;
}

/* ── Load package list ───────────────────────────────────────────────────── */
static int registry_load_list(void)
{
    if (g_list_loaded) return 0;

    char *json = NULL;

    if (cache_fresh(CACHE_LIST)) {
        json = cache_read(CACHE_LIST);
    }

    if (!json) {
        char url[URL_LEN];
        snprintf(url, sizeof(url), "%s/api/packages", g_base_url);
        printf("[cpm] fetching package list from registry...\n");
        json = http_get(url);
        if (json) cache_write(CACHE_LIST, json);
    }

    if (!json) {
        /* Try stale cache */
        json = cache_read(CACHE_LIST);
        if (json) fprintf(stderr, "[cpm] warning: using stale registry cache (offline?)\n");
    }

    if (!json) {
        fprintf(stderr, "[cpm] error: could not reach registry\n");
        return -1;
    }

    int ret = parse_package_list(json);
    free(json);
    if (ret == 0) g_list_loaded = 1;
    return ret;
}

/* ── Load per-package detail ────────────────────────────────────────────── */
static reg_entry_t *load_detail(const char *name)
{
    if (registry_load_list() != 0) return NULL;

    reg_entry_t *e = NULL;
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_pkgs[i].name, name) == 0) { e = &g_pkgs[i]; break; }
    if (!e) return NULL;
    if (e->detail_loaded) return e;

    /* Check per-package cache */
    char cache_path[512];
    snprintf(cache_path, sizeof(cache_path), "%s/%s.json", CACHE_PKG_DIR, name);

    char *json = NULL;
    if (cache_fresh(cache_path)) {
        json = cache_read(cache_path);
    }

    if (!json) {
        char url[URL_LEN];
        snprintf(url, sizeof(url), "%s/api/packages/%s", g_base_url, name);
        json = http_get(url);
        if (json) cache_write(cache_path, json);
    }

    if (!json) {
        json = cache_read(cache_path);
        if (json) fprintf(stderr, "[cpm] warning: using stale cache for %s\n", name);
    }

    if (json) {
        parse_package_detail(e, json);
        free(json);
    }

    return e;
}

/* ══ Public low-level API (same signatures as mock/registry_mock.h) ═══════ */

int registry_exists(const char *name)
{
    if (registry_load_list() != 0) return 0;
    for (int i = 0; i < g_count; i++)
        if (strcmp(g_pkgs[i].name, name) == 0) return 1;
    return 0;
}

const char *registry_get_version(const char *name, const char *constraint)
{
    reg_entry_t *e = load_detail(name);
    if (!e) return NULL;

    static char result[VER_LEN];
    result[0] = '\0';

    for (int i = 0; i < e->version_count; i++) {
        if (cpm_version_satisfies(e->versions[i], constraint)) {
            if (result[0] == '\0' || cpm_compare_versions(e->versions[i], result) > 0)
                snprintf(result, sizeof(result), "%s", e->versions[i]);
        }
    }
    return result[0] ? result : NULL;
}

const char *registry_get_deps(const char *name, const char *version)
{
    reg_entry_t *e = load_detail(name);
    if (!e) return "";
    for (int i = 0; i < e->version_count; i++)
        if (strcmp(e->versions[i], version) == 0)
            return e->deps[i];
    return "";
}

const char *registry_get_lang(const char *name)
{
    reg_entry_t *e = load_detail(name);
    return e ? e->lang : "c";
}

const char *registry_get_libtype(const char *name)
{
    reg_entry_t *e = load_detail(name);
    return e ? e->libtype : "source";
}

/* ══ Public high-level API ═════════════════════════════════════════════════ */

package_info_t *registry_search(const char *registry_url, const char *package_name)
{
    if (registry_url && *registry_url)
        snprintf(g_base_url, sizeof(g_base_url), "%s", registry_url);

    reg_entry_t *e = load_detail(package_name);
    if (!e) return NULL;

    package_info_t *info      = calloc(1, sizeof(package_info_t));
    info->name                = strdup(e->name);
    info->description         = strdup(e->description);
    info->latest_version      = strdup(e->latest[0] ? e->latest : (e->version_count ? e->versions[0] : "unknown"));
    info->version_count       = (size_t)e->version_count;
    info->versions            = calloc(info->version_count, sizeof(char *));
    for (size_t i = 0; i < info->version_count; i++)
        info->versions[i] = strdup(e->versions[i]);
    return info;
}

int registry_download(const char *registry_url, const char *package_name,
                      const char *version, const char *dest_path)
{
    if (registry_url && *registry_url)
        snprintf(g_base_url, sizeof(g_base_url), "%s", registry_url);

    reg_entry_t *e = load_detail(package_name);
    if (!e) {
        fprintf(stderr, "[cpm] unknown package: %s\n", package_name);
        return -1;
    }

    /* Fetch version-specific detail for the download URL */
    char url[URL_LEN];
    snprintf(url, sizeof(url), "%s/api/packages/%s/%s", g_base_url, package_name, version);
    char *detail_json = http_get(url);

    char download_url[URL_LEN] = "";
    if (detail_json) {
        char *du = json_str(detail_json, "downloadUrl");
        if (du) { snprintf(download_url, sizeof(download_url), "%s", du); free(du); }
        free(detail_json);
    }

    if (!download_url[0]) {
        fprintf(stderr, "[cpm] no download URL for %s@%s\n", package_name, version);
        return -1;
    }

    printf("[cpm] downloading %s@%s...\n", package_name, version);
    cpm_mkdirs(dest_path);

    /* Download tarball */
    char tarball[512];
    snprintf(tarball, sizeof(tarball), "%s/%s-%s.tar.gz", dest_path, package_name, version);

    CURL *curl = curl_easy_init();
    if (!curl) return -1;

    FILE *f = fopen(tarball, "wb");
    if (!f) { curl_easy_cleanup(curl); return -1; }

    curl_easy_setopt(curl, CURLOPT_URL,            download_url);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,  NULL);   /* default fwrite */
    curl_easy_setopt(curl, CURLOPT_WRITEDATA,      f);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT,        120L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT,      "cpm/0.1");
#ifdef _WIN32
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
#endif

    CURLcode rc = curl_easy_perform(curl);
    fclose(f);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        fprintf(stderr, "[cpm] download failed: %s\n", curl_easy_strerror(rc));
        return -1;
    }

    /* Extract */
    char cmd[1024];
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "tar -xzf \"%s\" -C \"%s\" 2>nul", tarball, dest_path);
#else
    snprintf(cmd, sizeof(cmd), "tar -xzf \"%s\" -C \"%s\" 2>/dev/null", tarball, dest_path);
#endif
    system(cmd);

    /* Track the download on the registry */
    char track_url[URL_LEN];
    snprintf(track_url, sizeof(track_url), "%s/api/packages/%s/%s/download",
             g_base_url, package_name, version);
    http_post_empty(track_url);

    printf("[cpm] installed %s@%s to %s\n", package_name, version, dest_path);
    return 0;
}

void package_info_free(package_info_t *info)
{
    if (!info) return;
    free(info->name);
    free(info->description);
    free(info->latest_version);
    for (size_t i = 0; i < info->version_count; i++) free(info->versions[i]);
    free(info->versions);
    free(info);
}
