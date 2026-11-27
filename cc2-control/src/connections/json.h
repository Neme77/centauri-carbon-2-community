/* Bounded JSON decoder for local API messages. No pointers into secret storage
 * are returned by status; caller erases command documents after processing.
 *
 * The implementation body (json_body.h) is included with a capacity set that
 * the including wrapper chooses. This wrapper keeps the historical small
 * budget for every existing consumer (MQTT snapshots, Connections, RFID,
 * request bodies); connections/json_large.h includes the same body with a
 * larger, still bounded budget for the appearance document, the display
 * manifest and its state/baseline files. */
#ifndef CC2_CONNECTIONS_JSON_H
#define CC2_CONNECTIONS_JSON_H
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define NGI_TOKENS 640
#define NGI_STRINGS 16384
#define NGI_MAX_DOC 32768
#include "json_body.h"
#undef NGI_TOKENS
#undef NGI_STRINGS
#undef NGI_MAX_DOC
#endif
