#ifndef JSON_BUILDER_H_
#define JSON_BUILDER_H_

#include <stddef.h>

typedef struct
{
    char *buf;
    size_t len;
    size_t capacity;
    int first; // tracks whether to prepend a comma
} JsonBuilder;

JsonBuilder *json_new(void);
void json_str(JsonBuilder *j, const char *key, const char *value);
void json_int(JsonBuilder *j, const char *key, long long value);
void json_double(JsonBuilder *j, const char *key, double value);
void json_bool(JsonBuilder *j, const char *key, int value);
void json_null(JsonBuilder *j, const char *key);
char *json_build(JsonBuilder *j); // returns a heap string — caller must free
void json_free(JsonBuilder *j);

#endif