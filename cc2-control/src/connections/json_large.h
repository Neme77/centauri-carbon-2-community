/* Large-document variant of the bounded JSON decoder.
 *
 * The appearance document, the display manifest, its baseline manifest and the
 * display state describe collections (profiles, layouts, widgets, assets) and
 * therefore need far more JSON tokens than a request body or an MQTT snapshot.
 * They are still bounded, just by budgets that match their documented maxima:
 *
 *   6 profiles x 4 layouts x 9 widgets + 8 themes  ~ 3 600 tokens
 *   154 display assets                             ~ 1 700 tokens
 *   154 baseline/state entries                     ~   780 tokens
 *
 * NG_LARGE_TOKENS is 6 144, above every supported document with headroom, and
 * the string arena is 32 KiB. Every caller heap-allocates the struct (~230 KiB);
 * none may place it on a stack. Every other consumer keeps the 640-token
 * decoder in json.h, unchanged.
 *
 * Symbols are renamed (ngl_*) so a translation unit may include both decoders;
 * main.c includes this header through src/appearance.h. */
#ifndef CC2_CONNECTIONS_JSON_LARGE_H
#define CC2_CONNECTIONS_JSON_LARGE_H
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The budgets this decoder uses, and the hard bounds its callers must respect.
 * The maximum document matches AP_MAX_JSON (32 KiB) with room for a future
 * field, and never permits an unbounded read. */
#define NG_LARGE_TOKENS 6144
#define NG_LARGE_STRINGS 32768
#define NG_LARGE_MAX_DOCUMENT 65536

#define NGI_TOKENS NG_LARGE_TOKENS
#define NGI_STRINGS NG_LARGE_STRINGS
#define NGI_MAX_DOC NG_LARGE_MAX_DOCUMENT

#define ng_token ngl_token
#define ng_json ngl_json
#define ng_jchar ngl_jchar
#define ng_jhex ngl_jhex
#define ng_junicode ngl_junicode
#define ng_jstring ngl_jstring
#define ng_jspace ngl_jspace
#define ng_jvalue ngl_jvalue
#define ng_json_parse ngl_json_parse
#define ng_member ngl_member
#define ng_string ngl_string
#define ng_bool ngl_bool
#define ng_number ngl_number
#define ng_json_quote ngl_json_quote
#include "json_body.h"
#undef ng_token
#undef ng_json
#undef ng_jchar
#undef ng_jhex
#undef ng_junicode
#undef ng_jstring
#undef ng_jspace
#undef ng_jvalue
#undef ng_json_parse
#undef ng_member
#undef ng_string
#undef ng_bool
#undef ng_number
#undef ng_json_quote
#undef NGI_TOKENS
#undef NGI_STRINGS
#undef NGI_MAX_DOC
#endif
