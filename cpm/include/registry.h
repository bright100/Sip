/*
 * Registry Client for cpm
 * Handles communication with the package registry
 */

#ifndef CPM_REGISTRY_H
#define CPM_REGISTRY_H

#include <stddef.h>

#ifndef CPM_REGISTRY_URL
#define CPM_REGISTRY_URL "https://registry.cpm.dev"
#endif

typedef struct {
    char *name;
    char *description;
    char *latest_version;
    char **versions;
    size_t version_count;
} package_info_t;

/* Low-level registry API */
int         registry_exists(const char *name);
const char *registry_get_version(const char *name, const char *constraint);
const char *registry_get_deps(const char *name, const char *version);
const char *registry_get_lang(const char *name);
const char *registry_get_libtype(const char *name);

/* High-level registry API */
package_info_t *registry_search(const char *registry_url, const char *package_name);
int registry_download(const char *registry_url, const char *package_name, 
                      const char *version, const char *dest_path);
void package_info_free(package_info_t *info);

#endif /* CPM_REGISTRY_H */
