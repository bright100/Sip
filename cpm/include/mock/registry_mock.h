/*
 * cpm — Mock Registry for Demo/Testing
 * Simulates a real registry without network access
 */

#ifndef CPM_MOCK_REGISTRY_H
#define CPM_MOCK_REGISTRY_H

int         mock_registry_exists(const char *name);
const char *mock_registry_get_version(const char *name, const char *constraint);
const char *mock_registry_get_deps(const char *name, const char *version);
const char *mock_registry_get_lang(const char *name);
const char *mock_registry_get_libtype(const char *name);

#endif /* CPM_MOCK_REGISTRY_H */
