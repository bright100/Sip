/*
 * TOML Parser Implementation for cpm
 */

#include "toml.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static char *trim_whitespace(char *str) {
    if (!str) return NULL;
    
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str;
    
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    
    return str;
}

static char *duplicate_string(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    char *dup = malloc(len + 1);
    if (dup) {
        memcpy(dup, s, len + 1);
    }
    return dup;
}

toml_table_t *toml_parse_file(const char *filename) {
    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "Error: Cannot open file '%s'\n", filename);
        return NULL;
    }
    
    toml_table_t *table = malloc(sizeof(toml_table_t));
    if (!table) {
        fclose(f);
        return NULL;
    }
    
    table->capacity = 64;
    table->count = 0;
    table->pairs = malloc(sizeof(toml_pair_t) * table->capacity);
    if (!table->pairs) {
        free(table);
        fclose(f);
        return NULL;
    }
    
    char line[1024];
    char current_section[256] = "";
    
    while (fgets(line, sizeof(line), f)) {
        char *trimmed = trim_whitespace(line);
        
        /* Skip empty lines and comments */
        if (trimmed[0] == '\0' || trimmed[0] == '#') {
            continue;
        }
        
        /* Section header [section] or [section.subsection] */
        if (trimmed[0] == '[') {
            char *end = strchr(trimmed, ']');
            if (end) {
                *end = '\0';
                strncpy(current_section, trimmed + 1, sizeof(current_section) - 1);
                current_section[sizeof(current_section) - 1] = '\0';
            }
            continue;
        }
        
        /* Key-value pair */
        char *eq = strchr(trimmed, '=');
        if (eq) {
            *eq = '\0';
            char *key = trim_whitespace(trimmed);
            char *value = trim_whitespace(eq + 1);
            
            /* Remove quotes from string values */
            size_t vlen = strlen(value);
            if (vlen >= 2 && value[0] == '"' && value[vlen-1] == '"') {
                value[vlen-1] = '\0';
                value++;
            }
            
            /* Build full key with section prefix */
            char full_key[512];
            if (current_section[0]) {
                snprintf(full_key, sizeof(full_key), "%s.%s", current_section, key);
            } else {
                strncpy(full_key, key, sizeof(full_key) - 1);
                full_key[sizeof(full_key) - 1] = '\0';
            }
            
            /* Add to table */
            if (table->count >= table->capacity) {
                table->capacity *= 2;
                table->pairs = realloc(table->pairs, sizeof(toml_pair_t) * table->capacity);
            }
            
            table->pairs[table->count].key = duplicate_string(full_key);
            table->pairs[table->count].value = duplicate_string(value);
            table->count++;
        }
    }
    
    fclose(f);
    return table;
}

char *toml_get(toml_table_t *table, const char *key) {
    if (!table || !key) return NULL;
    
    for (size_t i = 0; i < table->count; i++) {
        if (strcmp(table->pairs[i].key, key) == 0) {
            return table->pairs[i].value;
        }
    }
    return NULL;
}

void toml_free(toml_table_t *table) {
    if (!table) return;
    
    for (size_t i = 0; i < table->count; i++) {
        free(table->pairs[i].key);
        free(table->pairs[i].value);
    }
    free(table->pairs);
    free(table);
}
