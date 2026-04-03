/*
 * cpm — Manifest and Lockfile Declarations
 */

#ifndef CPM_CORE_MANIFEST_H
#define CPM_CORE_MANIFEST_H

#include "../toml.h"
#include "types.h"

toml_table_t *manifest_load(void);
int           manifest_save(toml_table_t *manifest);
int           manifest_add_dep(const char *name, const char *version, int dev_dep);
int           manifest_remove_dep(const char *name, int dev_dep);

lockfile_t *lockfile_load(void);
int         lockfile_save(lockfile_t *lock);
void        lockfile_free(lockfile_t *lock);

int resolve_deps(toml_table_t *manifest, lockfile_t **result);

int fetch_package(const char *name, const char *version, const char *source);
int build_package(const char *name, const char *version, const char *lang, const char *std);
int install_package(const char *name, const char *version);

int generate_flags_file(lockfile_t *lock);
int generate_compile_commands(lockfile_t *lock);
int compile_project(const char *sources[], int source_count, const char *output);

#endif /* CPM_CORE_MANIFEST_H */
