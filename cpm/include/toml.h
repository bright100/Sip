/*
 * TOML Parser for cpm
 * Simple TOML parsing for cpm.toml files
 */

#ifndef CPM_TOML_H
#define CPM_TOML_H

#include <stddef.h>

typedef struct {
    char *key;
    char *value;
} toml_pair_t;

typedef struct {
    toml_pair_t *pairs;
    size_t count;
    size_t capacity;
} toml_table_t;

toml_table_t *toml_parse_file(const char *filename);
char *toml_get(toml_table_t *table, const char *key);
void toml_free(toml_table_t *table);

#endif /* CPM_TOML_H */
