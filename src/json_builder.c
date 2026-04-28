#include "json_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INITIAL_CAP 256

JsonBuilder *json_new(void)
{
    JsonBuilder *j = malloc(sizeof(JsonBuilder));
    if (!j)
        return NULL;

    j->buf = malloc(INITIAL_CAP);
    if (!j->buf)
    {
        free(j);
        return NULL;
    }

    j->capacity = INITIAL_CAP;
    j->len = 0;
    j->first = 1;

    j->buf[j->len++] = '{';
    j->buf[j->len] = '\0';

    return j;
}

static int ensure(JsonBuilder *j, size_t needed)
{
    if (j->len + needed < j->capacity)
        return 1;

    size_t new_cap = j->capacity;
    while (new_cap <= j->len + needed)
        new_cap *= 2;

    char *tmp = realloc(j->buf, new_cap);
    if (!tmp)
        return 0;

    j->buf = tmp;
    j->capacity = new_cap;
    return 1;
}

static void append(JsonBuilder *j, const char *s, size_t len)
{
    if (!ensure(j, len + 1))
        return;
    memcpy(j->buf + j->len, s, len);
    j->len += len;
    j->buf[j->len] = '\0';
}

static void append_char(JsonBuilder *j, char c)
{
    if (!ensure(j, 2))
        return;
    j->buf[j->len++] = c;
    j->buf[j->len] = '\0';
}

static void append_key(JsonBuilder *j, const char *key)
{
    if (!j->first)
        append_char(j, ',');
    j->first = 0;

    append_char(j, '"');
    append(j, key, strlen(key));
    append(j, "\":", 2);
}

static void append_escaped_string(JsonBuilder *j, const char *s)
{
    append_char(j, '"');

    for (const char *p = s; *p; p++)
    {
        unsigned char c = (unsigned char)*p;

        switch (c)
        {
        case '"':
            append(j, "\\\"", 2);
            break;
        case '\\':
            append(j, "\\\\", 2);
            break;
        case '\n':
            append(j, "\\n", 2);
            break;
        case '\r':
            append(j, "\\r", 2);
            break;
        case '\t':
            append(j, "\\t", 2);
            break;
        default:
            if (c < 0x20)
            {
                char esc[7];
                snprintf(esc, sizeof(esc), "\\u%04x", c);
                append(j, esc, 6);
            }
            else
                append_char(j, (char)c);
            break;
        }
    }

    append_char(j, '"');
}

void json_str(JsonBuilder *j, const char *key, const char *value)
{
    if (!j || !key || !value)
        return;
    append_key(j, key);
    append_escaped_string(j, value);
}

void json_int(JsonBuilder *j, const char *key, long long value)
{
    if (!j || !key)
        return;
    append_key(j, key);

    char buf[32];
    int len = snprintf(buf, sizeof(buf), "%lld", value);
    if (len > 0)
        append(j, buf, (size_t)len);
}

void json_double(JsonBuilder *j, const char *key, double value)
{
    if (!j || !key)
        return;
    append_key(j, key);

    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%g", value);
    if (len > 0)
        append(j, buf, (size_t)len);
}

void json_bool(JsonBuilder *j, const char *key, int value)
{
    if (!j || !key)
        return;
    append_key(j, key);
    append(j, value ? "true" : "false", value ? 4 : 5);
}

void json_null(JsonBuilder *j, const char *key)
{
    if (!j || !key)
        return;
    append_key(j, key);
    append(j, "null", 4);
}

char *json_build(JsonBuilder *j)
{
    if (!j)
        return NULL;
    append_char(j, '}');
    return strdup(j->buf);
}

void json_free(JsonBuilder *j)
{
    if (!j)
        return;
    free(j->buf);
    free(j);
}