/*
 * Resolver for cpm
 * Dependency resolution using PubGrub algorithm
 */

#ifndef CPM_RESOLVER_H
#define CPM_RESOLVER_H

#include <stddef.h>

typedef struct {
    char *package_name;
    char *version_constraint;
} dependency_t;

typedef struct {
    dependency_t *deps;
    size_t count;
    size_t capacity;
} dependency_list_t;

int resolve_dependencies(dependency_list_t *requirements, char **resolved_versions);
void dependency_list_free(dependency_list_t *list);

#endif /* CPM_RESOLVER_H */
