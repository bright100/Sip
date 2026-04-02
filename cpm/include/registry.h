/*
 * Registry Client for cpm
 * Handles communication with the package registry
 */

#ifndef CPM_REGISTRY_H
#define CPM_REGISTRY_H

#include <stddef.h>

typedef struct {
    char *name;
    char *description;
    char *latest_version;
    char **versions;
    size_t version_count;
} package_info_t;

package_info_t *registry_search(const char *registry_url, const char *package_name);
int registry_download(const char *registry_url, const char *package_name, 
                      const char *version, const char *dest_path);
void package_info_free(package_info_t *info);

#endif /* CPM_REGISTRY_H */
