/*
 * Registry Client Implementation for cpm
 */

#include "registry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

package_info_t *registry_search(const char *registry_url, const char *package_name) {
    /* Stub implementation - would make HTTP request to registry in full version */
    (void)registry_url;
    (void)package_name;
    
    fprintf(stderr, "Registry search not yet implemented\n");
    return NULL;
}

int registry_download(const char *registry_url, const char *package_name,
                      const char *version, const char *dest_path) {
    /* Stub implementation - would download tarball from registry */
    (void)registry_url;
    (void)package_name;
    (void)version;
    (void)dest_path;
    
    fprintf(stderr, "Registry download not yet implemented\n");
    return -1;
}

void package_info_free(package_info_t *info) {
    if (!info) return;
    
    free(info->name);
    free(info->description);
    free(info->latest_version);
    
    if (info->versions) {
        for (size_t i = 0; i < info->version_count; i++) {
            free(info->versions[i]);
        }
        free(info->versions);
    }
    
    free(info);
}
