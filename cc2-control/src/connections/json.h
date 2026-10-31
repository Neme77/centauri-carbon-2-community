/* Bounded JSON decoder for local API messages. No pointers into secret storage
 * are returned by status; caller erases command documents after processing. */
#ifndef CC2_CONNECTIONS_JSON_H
#define CC2_CONNECTIONS_JSON_H
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NG_JSON_TOKENS 640
#define NG_JSON_STRINGS 16384
typedef struct {
  char type;
  int next, count;
  const char *text;
  double number;
} ng_token;
typedef struct {
  const char *p, *end;
  ng_token v[NG_JSON_TOKENS];
  int used;
  char strings[NG_JSON_STRINGS];
  size_t chars;
} ng_json;
static inline int ng_jchar(ng_json *j, unsigned c) {
  if (!c || j->chars + 1 >= sizeof(j->strings))
    return 0;
  j->strings[j->chars++] = (char)c;
  return 1;
}
static inline int ng_jhex(const char *p, unsigned *v) {
  *v = 0;
  for (int i = 0; i < 4; i++) {
    unsigned c = (unsigned char)p[i];
    unsigned d = c >= '0' && c <= '9'   ? c - '0'
                 : c >= 'a' && c <= 'f' ? c - 'a' + 10
                 : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                        : 99;
    if (d > 15)
      return 0;
    *v = (*v << 4) | d;
  }
  return 1;
}
static inline int ng_junicode(ng_json *j, unsigned c) {
  if (c < 0x80)
    return ng_jchar(j, c);
  if (c < 0x800)
    return ng_jchar(j, 0xc0 | (c >> 6)) && ng_jchar(j, 0x80 | (c & 63));
  if (c < 0x10000)
    return ng_jchar(j, 0xe0 | (c >> 12)) &&
           ng_jchar(j, 0x80 | ((c >> 6) & 63)) && ng_jchar(j, 0x80 | (c & 63));
  return ng_jchar(j, 0xf0 | (c >> 18)) &&
         ng_jchar(j, 0x80 | ((c >> 12) & 63)) &&
         ng_jchar(j, 0x80 | ((c >> 6) & 63)) && ng_jchar(j, 0x80 | (c & 63));
}
static inline const char *ng_jstring(ng_json *j) {
  if (j->p == j->end || *j->p++ != '"')
    return NULL;
  size_t begin = j->chars;
  while (j->p < j->end) {
    unsigned c = (unsigned char)*j->p++;
    if (c == '"') {
      if (j->chars >= sizeof(j->strings))
        return NULL;
      j->strings[j->chars++] = 0;
      return j->strings + begin;
    }
    if (c < 32)
      return NULL;
    if (c == '\\') {
      if (j->p == j->end)
        return NULL;
      c = (unsigned char)*j->p++;
      if (c == 'u') {
        unsigned value;
        if (j->end - j->p < 4 || !ng_jhex(j->p, &value))
          return NULL;
        j->p += 4;
        if (value >= 0xd800 && value <= 0xdbff) {
          unsigned low;
          if (j->end - j->p < 6 || j->p[0] != '\\' || j->p[1] != 'u' ||
              !ng_jhex(j->p + 2, &low) || low < 0xdc00 || low > 0xdfff)
            return NULL;
          j->p += 6;
          value = 0x10000 + ((value - 0xd800) << 10) + (low - 0xdc00);
        } else if (value >= 0xdc00 && value <= 0xdfff)
          return NULL;
        if (!ng_junicode(j, value))
          return NULL;
        continue;
      }
      if (c == 'b')
        c = '\b';
      else if (c == 'f')
        c = '\f';
      else if (c == 'n')
        c = '\n';
      else if (c == 'r')
        c = '\r';
      else if (c == 't')
        c = '\t';
      else if (c != '"' && c != '\\' && c != '/')
        return NULL;
    } else if (c >= 128) {
      unsigned value = c;
      int extra = 0;
      unsigned minimum = 0;
      if (c >= 0xc2 && c <= 0xdf) {
        extra = 1;
        value = c & 31;
        minimum = 0x80;
      } else if (c >= 0xe0 && c <= 0xef) {
        extra = 2;
        value = c & 15;
        minimum = 0x800;
      } else if (c >= 0xf0 && c <= 0xf4) {
        extra = 3;
        value = c & 7;
        minimum = 0x10000;
      } else
        return NULL;
      if (j->end - j->p < extra)
        return NULL;
      for (int k = 0; k < extra; k++) {
        unsigned d = (unsigned char)*j->p++;
        if ((d & 0xc0) != 0x80)
          return NULL;
        value = (value << 6) | (d & 63);
      }
      if (value < minimum || value > 0x10ffff ||
          (value >= 0xd800 && value <= 0xdfff) || !ng_junicode(j, value))
        return NULL;
      continue;
    }
    if (!ng_jchar(j, c))
      return NULL;
  }
  return NULL;
}
static inline void ng_jspace(ng_json *j) {
  while (j->p < j->end &&
         (*j->p == ' ' || *j->p == '\t' || *j->p == '\r' || *j->p == '\n'))
    j->p++;
}
static inline int ng_jvalue(ng_json *j, int depth) {
  ng_jspace(j);
  if (depth > 16 || j->p == j->end || j->used >= NG_JSON_TOKENS)
    return -1;
  int at = j->used++;
  ng_token *v = &j->v[at];
  v->type = *j->p;
  if (*j->p == '{' || *j->p == '[') {
    char close = *j->p++ == '{' ? '}' : ']';
    ng_jspace(j);
    if (j->p < j->end && *j->p == close) {
      j->p++;
      v->next = j->used;
      return at;
    }
    while (j->p < j->end) {
      if (close == '}') {
        if (j->used >= NG_JSON_TOKENS || *j->p != '"')
          return -1;
        int key = j->used++;
        j->v[key].type = '"';
        j->v[key].text = ng_jstring(j);
        j->v[key].next = key + 1;
        if (!j->v[key].text)
          return -1;
        for (int previous = at + 1; previous < key;
             previous = j->v[previous + 1].next)
          if (!strcmp(j->v[previous].text, j->v[key].text))
            return -1;
        ng_jspace(j);
        if (j->p == j->end || *j->p++ != ':')
          return -1;
      }
      if (ng_jvalue(j, depth + 1) < 0)
        return -1;
      v->count++;
      ng_jspace(j);
      if (j->p == j->end)
        return -1;
      if (*j->p == close) {
        j->p++;
        v->next = j->used;
        return at;
      }
      if (*j->p++ != ',')
        return -1;
      ng_jspace(j);
    }
    return -1;
  }
  if (*j->p == '"') {
    v->text = ng_jstring(j);
    if (!v->text)
      return -1;
  } else if (*j->p == 't' || *j->p == 'f' || *j->p == 'n') {
    const char *literal = *j->p == 't'   ? "true"
                          : *j->p == 'f' ? "false"
                                         : "null";
    size_t n = strlen(literal);
    if ((size_t)(j->end - j->p) < n || memcmp(j->p, literal, n))
      return -1;
    j->p += n;
  } else {
    const char *begin = j->p;
    if (*j->p == '-')
      j->p++;
    if (j->p == j->end)
      return -1;
    if (*j->p == '0')
      j->p++;
    else {
      if (*j->p < '1' || *j->p > '9')
        return -1;
      while (j->p < j->end && isdigit((unsigned char)*j->p))
        j->p++;
    }
    if (j->p < j->end && *j->p == '.') {
      j->p++;
      if (j->p == j->end || !isdigit((unsigned char)*j->p))
        return -1;
      while (j->p < j->end && isdigit((unsigned char)*j->p))
        j->p++;
    }
    if (j->p < j->end && (*j->p == 'e' || *j->p == 'E')) {
      j->p++;
      if (j->p < j->end && (*j->p == '-' || *j->p == '+'))
        j->p++;
      if (j->p == j->end || !isdigit((unsigned char)*j->p))
        return -1;
      while (j->p < j->end && isdigit((unsigned char)*j->p))
        j->p++;
    }
    char number[80];
    size_t n = (size_t)(j->p - begin);
    if (n >= sizeof(number))
      return -1;
    memcpy(number, begin, n);
    number[n] = 0;
    errno = 0;
    char *last;
    v->number = strtod(number, &last);
    if (errno || *last || !isfinite(v->number))
      return -1;
    v->type = '#';
  }
  v->next = j->used;
  return at;
}
static inline int ng_json_parse(ng_json *j, const char *text, size_t size) {
  memset(j, 0, sizeof(*j));
  if (!size || size > 32768 || memchr(text, 0, size))
    return 0;
  j->p = text;
  j->end = text + size;
  if (ng_jvalue(j, 0) != 0)
    return 0;
  ng_jspace(j);
  return j->p == j->end;
}
static inline int ng_member(const ng_json *j, int obj, const char *key) {
  if (obj < 0 || j->v[obj].type != '{')
    return -1;
  for (int i = obj + 1; i < j->v[obj].next; i = j->v[i + 1].next)
    if (!strcmp(j->v[i].text, key))
      return i + 1;
  return -1;
}
static inline const char *ng_string(const ng_json *j, int obj,
                                    const char *key) {
  int t = ng_member(j, obj, key);
  return t >= 0 && j->v[t].type == '"' ? j->v[t].text : NULL;
}
static inline int ng_bool(const ng_json *j, int obj, const char *key,
                          int *out) {
  int t = ng_member(j, obj, key);
  if (t < 0 || (j->v[t].type != 't' && j->v[t].type != 'f'))
    return 0;
  *out = j->v[t].type == 't';
  return 1;
}
static inline int ng_number(const ng_json *j, int obj, const char *key,
                            double *out) {
  int t = ng_member(j, obj, key);
  if (t < 0 || j->v[t].type != '#')
    return 0;
  *out = j->v[t].number;
  return 1;
}
static inline void ng_json_quote(FILE *out, const char *s) {
  fputc('"', out);
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (*p == '"' || *p == '\\') {
      fputc('\\', out);
      fputc(*p, out);
    } else if (*p < 32)
      fprintf(out, "\\u%04x", *p);
    else
      fputc(*p, out);
  }
  fputc('"', out);
}
#endif
