/* Appearance profile storage and API for CC2 Control.
 *
 * One versioned JSON document holds the active dashboard/touchscreen
 * appearance, the custom themes and the saved profiles. It is validated field
 * by field, written atomically (temporary file, fsync, rename) after the
 * previous document has been copied to appearance.json.bak, and never stores
 * executable content: colours, identifiers, bounded numbers and names only.
 *
 * The dashboard owns the visual result; this module only keeps the document
 * consistent between browsers, reloads and reboots. Unknown fields are
 * ignored on read (forward compatibility) and never persisted.
 *
 * Depends on symbols defined earlier in main.c: respond(), json_builder,
 * json_builder_printf(), json_builder_string(), extract_json_string(),
 * preferences_theme(), ui_preferences_path, sha256_*.
 */
#ifndef CC2_APPEARANCE_H
#define CC2_APPEARANCE_H

#include "connections/json_large.h"

#define AP_VERSION 1
#define AP_MAX_PROFILES 6
#define AP_MAX_LAYOUTS 4
#define AP_MAX_WIDGETS 9
#define AP_MAX_THEMES 8
#define AP_MAX_JSON 32768
#define AP_TOKEN_COUNT 14
#define AP_NAME_MAX 64
#define AP_ID_MAX 40
/* A custom theme id must also satisfy the legacy /api/preferences theme field
 * (lowercase letters and single hyphens, at most 24 characters), because
 * lib/i18n.ts still writes the selected palette there. */
#define AP_THEME_ID_MAX 24
#define AP_COLOR_MAX 8

static const char *const ap_token_names[AP_TOKEN_COUNT] = {
    "bg", "panel", "edge", "text", "muted", "field", "ink",
    "cyan", "green", "amber", "red", "blue", "well", "wellFg"
};

/* The widgets a dashboard layout may contain. Kept in sync with
 * web-src/src/lib/appearance.ts by tests/test_appearance_static.py. */
static const char *const ap_widget_ids[AP_MAX_WIDGETS] = {
    "camera", "quick", "job", "tuning", "thermal", "fans", "firstLayer", "position", "system"
};

typedef struct {
    char id[AP_ID_MAX + 1];
    char name[AP_NAME_MAX + 1];
    char tokens[AP_TOKEN_COUNT][AP_COLOR_MAX];
} ap_theme;

typedef struct {
    char id[24];
    int enabled;
    int span;              /* 1, 2 or 4 grid columns */
    int compact;
    char accent[AP_COLOR_MAX]; /* empty: theme accent */
} ap_widget;

typedef struct {
    char id[AP_ID_MAX + 1];
    char name[AP_NAME_MAX + 1];
    int columns;           /* 2 or 4 */
    ap_widget widgets[AP_MAX_WIDGETS];
    int widget_count;
} ap_layout;

typedef struct {
    char theme[AP_ID_MAX + 1];  /* built-in palette id, custom-<slug>, auto */
    char mode[8];               /* dark, light or auto (custom palettes) */
    char layout[AP_ID_MAX + 1];
    int density;                /* 0 comfortable, 1 compact, 2 spacious */
    int font;                   /* 0 Inter, 1 system, 2 monospace */
    double scale;               /* 0.85 .. 1.30 */
    int radius;                 /* 0 .. 24 px */
    int spacing;                /* 0 .. 2 (0.75, 1, 1.25) */
    int animations;
    ap_layout layouts[AP_MAX_LAYOUTS];
    int layout_count;
} ap_dashboard;

typedef struct {
    int enabled;
    int link_accent;
    int experimental;
    char accent[AP_COLOR_MAX];
    char background[AP_COLOR_MAX];
    char boot[16];              /* community, custom */
} ap_touchscreen;

typedef struct {
    char id[AP_ID_MAX + 1];
    char name[AP_NAME_MAX + 1];
    ap_dashboard dashboard;
    ap_touchscreen touchscreen;
} ap_profile;

typedef struct {
    int loaded;                 /* a document was read */
    int recovered;              /* the file was invalid; defaults are in use */
    int unsupported;            /* a newer schema version was found */
    char error[96];
    char active[AP_ID_MAX + 1];
    ap_dashboard dashboard;
    ap_touchscreen touchscreen;
    ap_theme themes[AP_MAX_THEMES];
    int theme_count;
    ap_profile profiles[AP_MAX_PROFILES];
    int profile_count;
} ap_state;

static ap_state ap_current;
static char ap_path[PATH_MAX_LOCAL];
static char ap_backup_path[PATH_MAX_LOCAL];
static int ap_initialized;
static pthread_mutex_t ap_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ---- validation ------------------------------------------------------- */

static int ap_color_valid(const char *value) {
    if (!value || value[0] != '#' || strlen(value) != 7) return 0;
    for (int i = 1; i < 7; ++i)
        if (!isxdigit((unsigned char)value[i])) return 0;
    return 1;
}

static int ap_id_valid(const char *value) {
    size_t n = value ? strlen(value) : 0;
    if (n < 2 || n > AP_ID_MAX) return 0;
    if (!isalnum((unsigned char)value[0])) return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (!(islower(c) || isdigit(c) || c == '-')) return 0;
        if (c == '-' && (i == 0 || i + 1 == n || value[i - 1] == '-')) return 0;
    }
    return 1;
}

static int ap_name_valid(const char *value) {
    size_t n = value ? strlen(value) : 0;
    if (n < 1 || n > AP_NAME_MAX) return 0;
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)value[i];
        if (c < 32 || c == 127) return 0;
    }
    return 1;
}

static int ap_widget_valid(const char *id) {
    for (int i = 0; i < AP_MAX_WIDGETS; ++i)
        if (!strcmp(id, ap_widget_ids[i])) return 1;
    return 0;
}

/* Built-in palettes known to this release plus custom-<slug> and auto. */
static int ap_theme_id_valid(const char *id) {
    static const char *const builtin[] = {
        "dark", "light", "dracula", "nord", "monokai", "solarized-light",
        "obsidian", "aurora", "klipper", "dune", "swamp", "klipperscreen", "auto"
    };
    for (size_t i = 0; i < sizeof(builtin) / sizeof(builtin[0]); ++i)
        if (!strcmp(id, builtin[i])) return 1;
    if (strncmp(id, "custom-", 7) || !ap_id_valid(id)) return 0;
    if (strlen(id) > AP_THEME_ID_MAX) return 0;
    /* The legacy preferences endpoint accepts lowercase letters and single
     * hyphens only; a custom theme must be valid in both stores. */
    for (const char *p = id; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || *p == '-')) return 0;
    return 1;
}

static int ap_number_int(ngl_json *j, int obj, const char *key, int min, int max, int *out) {
    double value;
    if (!ngl_number(j, obj, key, &value)) return 0;
    if (!isfinite(value) || value < min || value > max || value != floor(value)) return 0;
    *out = (int)value;
    return 1;
}

static int ap_bool(ngl_json *j, int obj, const char *key, int *out) {
    return ngl_bool(j, obj, key, out);
}

static int ap_string(ngl_json *j, int obj, const char *key, char *out, size_t cap) {
    const char *value = ngl_string(j, obj, key);
    if (!value || strlen(value) >= cap) return 0;
    memcpy(out, value, strlen(value) + 1);
    return 1;
}

/* ---- defaults --------------------------------------------------------- */

static void ap_default_layout(ap_layout *layout) {
    memset(layout, 0, sizeof(*layout));
    snprintf(layout->id, sizeof(layout->id), "custom-grid");
    snprintf(layout->name, sizeof(layout->name), "Custom grid");
    layout->columns = 4;
    layout->widget_count = AP_MAX_WIDGETS;
    for (int i = 0; i < AP_MAX_WIDGETS; ++i) {
        ap_widget *w = &layout->widgets[i];
        snprintf(w->id, sizeof(w->id), "%s", ap_widget_ids[i]);
        w->enabled = 1;
        w->span = (!strcmp(w->id, "fans") || !strcmp(w->id, "firstLayer")) ? 1 : 2;
        w->accent[0] = '\0';
    }
}

static void ap_default_dashboard(ap_dashboard *d) {
    memset(d, 0, sizeof(*d));
    snprintf(d->theme, sizeof(d->theme), "dark");
    snprintf(d->mode, sizeof(d->mode), "dark");
    snprintf(d->layout, sizeof(d->layout), "classic");
    d->density = 0;
    d->font = 0;
    d->scale = 1.0;
    d->radius = 12;
    d->spacing = 1;
    d->animations = 1;
    /* The classic composition is the unchanged pre-customisation dashboard;
     * custom-grid is the editable flat layout. */
    memset(&d->layouts[0], 0, sizeof(d->layouts[0]));
    snprintf(d->layouts[0].id, sizeof(d->layouts[0].id), "classic");
    snprintf(d->layouts[0].name, sizeof(d->layouts[0].name), "Classic");
    d->layouts[0].columns = 4;
    ap_default_layout(&d->layouts[1]);
    d->layout_count = 2;
}

static void ap_default_touchscreen(ap_touchscreen *t) {
    memset(t, 0, sizeof(*t));
    t->enabled = 0;
    t->link_accent = 1;
    t->experimental = 0;
    snprintf(t->accent, sizeof(t->accent), "#7eead8");
    snprintf(t->background, sizeof(t->background), "#0a0c11");
    snprintf(t->boot, sizeof(t->boot), "community");
}

static void ap_default_profile(ap_profile *p, const char *id, const char *name) {
    memset(p, 0, sizeof(*p));
    snprintf(p->id, sizeof(p->id), "%s", id);
    snprintf(p->name, sizeof(p->name), "%s", name);
    ap_default_dashboard(&p->dashboard);
    ap_default_touchscreen(&p->touchscreen);
}

static void ap_defaults(ap_state *s) {
    memset(s, 0, sizeof(*s));
    snprintf(s->active, sizeof(s->active), "community");
    ap_default_dashboard(&s->dashboard);
    ap_default_touchscreen(&s->touchscreen);
    s->theme_count = 0;
    s->profile_count = 1;
    ap_default_profile(&s->profiles[0], "community", "Community Default");
}

/* ---- parse ------------------------------------------------------------ */

static int ap_parse_tokens(ngl_json *j, int obj, ap_theme *theme) {
    if (j->v[obj].type != '{') return 0;
    for (int i = 0; i < AP_TOKEN_COUNT; ++i) {
        int member = ngl_member(j, obj, ap_token_names[i]);
        if (member < 0) continue;
        if (j->v[member].type != '"' || !ap_color_valid(j->v[member].text)) return 0;
        snprintf(theme->tokens[i], sizeof(theme->tokens[i]), "%s", j->v[member].text);
    }
    return 1;
}

static int ap_parse_theme(ngl_json *j, int obj, ap_theme *theme) {
    memset(theme, 0, sizeof(*theme));
    if (!ap_string(j, obj, "id", theme->id, sizeof(theme->id)) ||
        !ap_theme_id_valid(theme->id) || strncmp(theme->id, "custom-", 7)) return 0;
    if (!ap_string(j, obj, "name", theme->name, sizeof(theme->name)) || !ap_name_valid(theme->name)) return 0;
    int tokens = ngl_member(j, obj, "tokens");
    if (tokens < 0 || !ap_parse_tokens(j, tokens, theme)) return 0;
    for (int i = 0; i < AP_TOKEN_COUNT; ++i)
        if (!ap_color_valid(theme->tokens[i])) return 0;
    return 1;
}

static int ap_parse_widget(ngl_json *j, int obj, ap_widget *w) {
    memset(w, 0, sizeof(*w));
    if (!ap_string(j, obj, "id", w->id, sizeof(w->id)) || !ap_widget_valid(w->id)) return 0;
    w->enabled = 1;
    if (ngl_member(j, obj, "enabled") >= 0 && !ap_bool(j, obj, "enabled", &w->enabled)) return 0;
    w->span = 2;
    if (ngl_member(j, obj, "span") >= 0 && !ap_number_int(j, obj, "span", 1, 4, &w->span)) return 0;
    if (w->span != 1 && w->span != 2 && w->span != 4) return 0;
    w->compact = 0;
    if (ngl_member(j, obj, "compact") >= 0 && !ap_bool(j, obj, "compact", &w->compact)) return 0;
    if (ngl_member(j, obj, "accent") >= 0) {
        if (!ap_string(j, obj, "accent", w->accent, sizeof(w->accent))) return 0;
        if (w->accent[0] && !ap_color_valid(w->accent)) return 0;
    }
    return 1;
}

static int ap_parse_layout(ngl_json *j, int obj, ap_layout *layout) {
    memset(layout, 0, sizeof(*layout));
    if (!ap_string(j, obj, "id", layout->id, sizeof(layout->id)) || !ap_id_valid(layout->id)) return 0;
    if (!ap_string(j, obj, "name", layout->name, sizeof(layout->name)) || !ap_name_valid(layout->name)) return 0;
    layout->columns = 4;
    if (ngl_member(j, obj, "columns") >= 0 && !ap_number_int(j, obj, "columns", 2, 4, &layout->columns)) return 0;
    if (layout->columns != 2 && layout->columns != 4) return 0;
    int widgets = ngl_member(j, obj, "widgets");
    if (widgets < 0 || j->v[widgets].type != '[') return 0;
    for (int i = widgets + 1; i < j->v[widgets].next; i = j->v[i].next) {
        if (layout->widget_count >= AP_MAX_WIDGETS || j->v[i].type != '{') return 0;
        if (!ap_parse_widget(j, i, &layout->widgets[layout->widget_count])) return 0;
        for (int p = 0; p < layout->widget_count; ++p)
            if (!strcmp(layout->widgets[p].id, layout->widgets[layout->widget_count].id)) return 0;
        layout->widget_count++;
    }
    /* The classic layout is a marker: its composition lives in the frontend,
     * so an empty widget list is valid (a custom layout with no widgets is
     * also accepted and simply renders nothing). */
    return 1;
}

static int ap_parse_dashboard(ngl_json *j, int obj, const ap_dashboard *base, ap_dashboard *out) {
    *out = *base;
    if (j->v[obj].type != '{') return 0;
    if (ngl_member(j, obj, "theme") >= 0) {
        if (!ap_string(j, obj, "theme", out->theme, sizeof(out->theme)) || !ap_theme_id_valid(out->theme)) return 0;
    }
    if (ngl_member(j, obj, "mode") >= 0) {
        if (!ap_string(j, obj, "mode", out->mode, sizeof(out->mode))) return 0;
        if (strcmp(out->mode, "dark") && strcmp(out->mode, "light") && strcmp(out->mode, "auto")) return 0;
    }
    if (ngl_member(j, obj, "layout") >= 0) {
        if (!ap_string(j, obj, "layout", out->layout, sizeof(out->layout)) || !ap_id_valid(out->layout)) return 0;
    }
    if (ngl_member(j, obj, "density") >= 0 && !ap_number_int(j, obj, "density", 0, 2, &out->density)) return 0;
    if (ngl_member(j, obj, "font") >= 0 && !ap_number_int(j, obj, "font", 0, 2, &out->font)) return 0;
    if (ngl_member(j, obj, "radius") >= 0 && !ap_number_int(j, obj, "radius", 0, 24, &out->radius)) return 0;
    if (ngl_member(j, obj, "spacing") >= 0 && !ap_number_int(j, obj, "spacing", 0, 2, &out->spacing)) return 0;
    if (ngl_member(j, obj, "animations") >= 0 && !ap_bool(j, obj, "animations", &out->animations)) return 0;
    if (ngl_member(j, obj, "scale") >= 0) {
        double value;
        if (!ngl_number(j, obj, "scale", &value) || !isfinite(value) || value < 0.85 || value > 1.30) return 0;
        out->scale = value;
    }
    if (ngl_member(j, obj, "layouts") >= 0) {
        int layouts = ngl_member(j, obj, "layouts");
        if (j->v[layouts].type != '[') return 0;
        out->layout_count = 0;
        for (int i = layouts + 1; i < j->v[layouts].next; i = j->v[i].next) {
            if (out->layout_count >= AP_MAX_LAYOUTS || j->v[i].type != '{') return 0;
            if (!ap_parse_layout(j, i, &out->layouts[out->layout_count])) return 0;
            for (int p = 0; p < out->layout_count; ++p)
                if (!strcmp(out->layouts[p].id, out->layouts[out->layout_count].id)) return 0;
            out->layout_count++;
        }
        if (!out->layout_count) return 0;
    }
    /* The active layout must exist; a document that names a missing one is
     * repaired to the first layout instead of being rejected. */
    int found = !strcmp(out->layout, "classic");
    for (int i = 0; i < out->layout_count; ++i)
        if (!strcmp(out->layouts[i].id, out->layout)) found = 1;
    if (!found) snprintf(out->layout, sizeof(out->layout), "%s", out->layouts[0].id);
    return 1;
}

static int ap_parse_touchscreen(ngl_json *j, int obj, const ap_touchscreen *base, ap_touchscreen *out) {
    *out = *base;
    if (j->v[obj].type != '{') return 0;
    if (ngl_member(j, obj, "enabled") >= 0 && !ap_bool(j, obj, "enabled", &out->enabled)) return 0;
    if (ngl_member(j, obj, "linkAccent") >= 0 && !ap_bool(j, obj, "linkAccent", &out->link_accent)) return 0;
    if (ngl_member(j, obj, "experimental") >= 0 && !ap_bool(j, obj, "experimental", &out->experimental)) return 0;
    if (ngl_member(j, obj, "accent") >= 0) {
        if (!ap_string(j, obj, "accent", out->accent, sizeof(out->accent)) || !ap_color_valid(out->accent)) return 0;
    }
    if (ngl_member(j, obj, "background") >= 0) {
        if (!ap_string(j, obj, "background", out->background, sizeof(out->background)) ||
            !ap_color_valid(out->background)) return 0;
    }
    if (ngl_member(j, obj, "boot") >= 0) {
        if (!ap_string(j, obj, "boot", out->boot, sizeof(out->boot))) return 0;
        if (strcmp(out->boot, "community") && strcmp(out->boot, "custom")) return 0;
    }
    return 1;
}

static int ap_parse_profile(ngl_json *j, int obj, ap_profile *out) {
    memset(out, 0, sizeof(*out));
    if (!ap_string(j, obj, "id", out->id, sizeof(out->id)) || !ap_id_valid(out->id)) return 0;
    if (!ap_string(j, obj, "name", out->name, sizeof(out->name)) || !ap_name_valid(out->name)) return 0;
    ap_dashboard dashboard;
    ap_default_dashboard(&dashboard);
    int d = ngl_member(j, obj, "dashboard");
    if (d < 0 || !ap_parse_dashboard(j, d, &dashboard, &dashboard)) return 0;
    out->dashboard = dashboard;
    ap_touchscreen touch;
    ap_default_touchscreen(&touch);
    int t = ngl_member(j, obj, "touchscreen");
    if (t >= 0 && !ap_parse_touchscreen(j, t, &touch, &touch)) return 0;
    out->touchscreen = touch;
    return 1;
}

/* Merge the accepted fields of `body` into `out`. Fields that are absent keep
 * the value in `base`. */
static int ap_parse_document(const char *body, size_t length, const ap_state *base, ap_state *out) {
    ngl_json *doc = malloc(sizeof(*doc));
    if (!doc) return 0;
    if (!ngl_json_parse(doc, body, length)) { free(doc); return 0; }
    if (doc->v[0].type != '{') { free(doc); return 0; }
    *out = *base;
    int member;
    /* A bare {"theme":"light"} is accepted as the legacy preference shape. */
    if ((member = ngl_member(doc, 0, "theme")) >= 0) {
        if (doc->v[member].type != '"' || !ap_theme_id_valid(doc->v[member].text)) { free(doc); return 0; }
        snprintf(out->dashboard.theme, sizeof(out->dashboard.theme), "%s", doc->v[member].text);
    }
    if ((member = ngl_member(doc, 0, "activeProfile")) >= 0) {
        if (doc->v[member].type != '"' || !ap_id_valid(doc->v[member].text)) { free(doc); return 0; }
        snprintf(out->active, sizeof(out->active), "%s", doc->v[member].text);
    }
    if ((member = ngl_member(doc, 0, "dashboard")) >= 0) {
        ap_dashboard next;
        if (!ap_parse_dashboard(doc, member, &out->dashboard, &next)) { free(doc); return 0; }
        out->dashboard = next;
    }
    if ((member = ngl_member(doc, 0, "touchscreen")) >= 0) {
        ap_touchscreen next;
        if (!ap_parse_touchscreen(doc, member, &out->touchscreen, &next)) { free(doc); return 0; }
        out->touchscreen = next;
    }
    if ((member = ngl_member(doc, 0, "themes")) >= 0) {
        if (doc->v[member].type != '[') { free(doc); return 0; }
        /* Upsert by id instead of replacing the whole library: two browsers
         * that add different themes no longer delete each other's. Removal is
         * an explicit POST /api/appearance/themes/delete. */
        ap_theme parsed[AP_MAX_THEMES];
        int count = 0;
        for (int i = member + 1; i < doc->v[member].next; i = doc->v[i].next) {
            if (count >= AP_MAX_THEMES || doc->v[i].type != '{') { free(doc); return 0; }
            if (!ap_parse_theme(doc, i, &parsed[count])) { free(doc); return 0; }
            for (int p = 0; p < count; ++p)
                if (!strcmp(parsed[p].id, parsed[count].id)) { free(doc); return 0; }
            count++;
        }
        for (int i = 0; i < count; ++i) {
            int slot = -1;
            for (int p = 0; p < out->theme_count; ++p)
                if (!strcmp(out->themes[p].id, parsed[i].id)) slot = p;
            if (slot < 0) {
                if (out->theme_count >= AP_MAX_THEMES) { free(doc); return 0; }
                slot = out->theme_count++;
            }
            out->themes[slot] = parsed[i];
        }
    }
    if ((member = ngl_member(doc, 0, "version")) >= 0) {
        double version;
        if (!ngl_number(doc, 0, "version", &version) || version != AP_VERSION) { free(doc); return 0; }
    }
    /* The active profile, when named, must exist; otherwise keep community. */
    int known = 0;
    for (int i = 0; i < out->profile_count; ++i)
        if (!strcmp(out->profiles[i].id, out->active)) known = 1;
    if (!known) snprintf(out->active, sizeof(out->active), "community");
    free(doc);
    return 1;
}

static int ap_parse_single_profile(const char *body, size_t length, ap_profile *profile, ap_theme *theme, int *has_theme) {
    ngl_json *doc = malloc(sizeof(*doc));
    if (!doc) return 0;
    if (!ngl_json_parse(doc, body, length) || doc->v[0].type != '{') { free(doc); return 0; }
    int p = ngl_member(doc, 0, "profile");
    if (p < 0 || !ap_parse_profile(doc, p, profile)) { free(doc); return 0; }
    *has_theme = 0;
    int t = ngl_member(doc, 0, "theme");
    if (t >= 0) {
        if (!ap_parse_theme(doc, t, theme)) { free(doc); return 0; }
        *has_theme = 1;
    }
    free(doc);
    return 1;
}

/* ---- serialize -------------------------------------------------------- */

static void ap_write_tokens(json_builder *b, const ap_theme *theme) {
    json_builder_printf(b, "{");
    for (int i = 0; i < AP_TOKEN_COUNT; ++i) {
        if (i) json_builder_printf(b, ",");
        json_builder_string(b, ap_token_names[i]);
        json_builder_printf(b, ":");
        json_builder_string(b, theme->tokens[i]);
    }
    json_builder_printf(b, "}");
}

static void ap_write_theme(json_builder *b, const ap_theme *theme) {
    json_builder_printf(b, "{\"id\":");
    json_builder_string(b, theme->id);
    json_builder_printf(b, ",\"name\":");
    json_builder_string(b, theme->name);
    json_builder_printf(b, ",\"tokens\":");
    ap_write_tokens(b, theme);
    json_builder_printf(b, "}");
}

static void ap_write_layout(json_builder *b, const ap_layout *layout) {
    json_builder_printf(b, "{\"id\":");
    json_builder_string(b, layout->id);
    json_builder_printf(b, ",\"name\":");
    json_builder_string(b, layout->name);
    json_builder_printf(b, ",\"columns\":%d,\"widgets\":[", layout->columns);
    for (int i = 0; i < layout->widget_count; ++i) {
        const ap_widget *w = &layout->widgets[i];
        if (i) json_builder_printf(b, ",");
        json_builder_printf(b, "{\"id\":");
        json_builder_string(b, w->id);
        json_builder_printf(b, ",\"enabled\":%s,\"span\":%d,\"compact\":%s",
                            w->enabled ? "true" : "false", w->span, w->compact ? "true" : "false");
        if (w->accent[0]) {
            json_builder_printf(b, ",\"accent\":");
            json_builder_string(b, w->accent);
        }
        json_builder_printf(b, "}");
    }
    json_builder_printf(b, "]}");
}

static void ap_write_dashboard(json_builder *b, const ap_dashboard *d) {
    json_builder_printf(b, "{\"theme\":");
    json_builder_string(b, d->theme);
    json_builder_printf(b, ",\"mode\":");
    json_builder_string(b, d->mode);
    json_builder_printf(b, ",\"layout\":");
    json_builder_string(b, d->layout);
    json_builder_printf(b, ",\"density\":%d,\"font\":%d,\"scale\":%.4g,\"radius\":%d,\"spacing\":%d,\"animations\":%s,\"layouts\":[",
                        d->density, d->font, d->scale, d->radius, d->spacing, d->animations ? "true" : "false");
    for (int i = 0; i < d->layout_count; ++i) {
        if (i) json_builder_printf(b, ",");
        ap_write_layout(b, &d->layouts[i]);
    }
    json_builder_printf(b, "]}");
}

static void ap_write_touchscreen(json_builder *b, const ap_touchscreen *t) {
    json_builder_printf(b, "{\"enabled\":%s,\"linkAccent\":%s,\"experimental\":%s,\"accent\":",
                        t->enabled ? "true" : "false", t->link_accent ? "true" : "false",
                        t->experimental ? "true" : "false");
    json_builder_string(b, t->accent);
    json_builder_printf(b, ",\"background\":");
    json_builder_string(b, t->background);
    json_builder_printf(b, ",\"boot\":");
    json_builder_string(b, t->boot);
    json_builder_printf(b, "}");
}

static void ap_write_profile(json_builder *b, const ap_profile *p) {
    json_builder_printf(b, "{\"id\":");
    json_builder_string(b, p->id);
    json_builder_printf(b, ",\"name\":");
    json_builder_string(b, p->name);
    json_builder_printf(b, ",\"dashboard\":");
    ap_write_dashboard(b, &p->dashboard);
    json_builder_printf(b, ",\"touchscreen\":");
    ap_write_touchscreen(b, &p->touchscreen);
    json_builder_printf(b, "}");
}

/* Returns the document in `buf`. `with_private_state` is always true for the
 * file and the API; kept explicit so a future export can omit nothing by
 * accident. */
static int ap_serialize_body(const ap_state *s, json_builder *builder) {
    json_builder *b = builder;
    json_builder_printf(b, "\"version\":%d,\"activeProfile\":", AP_VERSION);
    json_builder_string(b, s->active);
    json_builder_printf(b, ",\"dashboard\":");
    ap_write_dashboard(b, &s->dashboard);
    json_builder_printf(b, ",\"touchscreen\":");
    ap_write_touchscreen(b, &s->touchscreen);
    json_builder_printf(b, ",\"themes\":[");
    for (int i = 0; i < s->theme_count; ++i) {
        if (i) json_builder_printf(b, ",");
        ap_write_theme(b, &s->themes[i]);
    }
    json_builder_printf(b, "],\"profiles\":[");
    for (int i = 0; i < s->profile_count; ++i) {
        if (i) json_builder_printf(b, ",");
        ap_write_profile(b, &s->profiles[i]);
    }
    json_builder_printf(b, "]}");
    return !b->failed;
}

static int ap_serialize(const ap_state *s, char *buf, size_t cap, size_t *length) {
    json_builder b = {buf, 0, cap, 0};
    json_builder_printf(&b, "{");
    if (!ap_serialize_body(s, &b)) return 0;
    json_builder_printf(&b, "\n");
    if (b.failed) return 0;
    *length = b.length;
    return 1;
}

/* The API adds read-only diagnostics that are never written to the file. */
static int ap_serialize_api(const ap_state *s, char *buf, size_t cap, size_t *length) {
    json_builder b = {buf, 0, cap, 0};
    json_builder_printf(&b, "{\"recovered\":%s,\"unsupported\":%s,\"error\":",
                        s->recovered ? "true" : "false", s->unsupported ? "true" : "false");
    json_builder_string(&b, s->error);
    json_builder_printf(&b, ",");
    if (!ap_serialize_body(s, &b)) return 0;
    json_builder_printf(&b, "\n");
    if (b.failed) return 0;
    *length = b.length;
    return 1;
}

/* ---- persistence ------------------------------------------------------ */

static int ap_ensure_paths(void) {
    if (ap_path[0]) return 1;
    const char *slash = strrchr(ui_preferences_path, '/');
    int n = slash ? snprintf(ap_path, sizeof(ap_path), "%.*s/appearance.json",
                             (int)(slash - ui_preferences_path), ui_preferences_path)
                  : snprintf(ap_path, sizeof(ap_path), "appearance.json");
    if (n <= 0 || (size_t)n >= sizeof(ap_path)) return 0;
    n = snprintf(ap_backup_path, sizeof(ap_backup_path), "%s.bak", ap_path);
    return n > 0 && (size_t)n < sizeof(ap_backup_path);
}

/* fsync the directory that holds `path`, so a rename or unlink is durable
 * after a power loss and not only the file content. */
static void ap_sync_parent(const char *path) {
    char directory[PATH_MAX_LOCAL];
    const char *slash = strrchr(path, '/');
    if (slash) {
        size_t n = (size_t)(slash - path);
        if (n == 0) n = 1; /* "/file" -> "/" */
        if (n >= sizeof(directory)) return;
        memcpy(directory, path, n);
        directory[n] = '\0';
    } else {
        snprintf(directory, sizeof(directory), ".");
    }
    int fd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return;
    (void)fsync(fd);
    close(fd);
}

static int ap_write_all(const char *path, const char *data, size_t length, mode_t mode) {
    char temporary[PATH_MAX_LOCAL];
    if (snprintf(temporary, sizeof(temporary), "%s.new", path) >= (int)sizeof(temporary)) return 0;
    unlink(temporary);
    int fd = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    if (fd < 0) return 0;
    size_t done = 0;
    while (done < length) {
        ssize_t n = write(fd, data + done, length - done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { close(fd); unlink(temporary); return 0; }
        done += (size_t)n;
    }
    if (fsync(fd) != 0 || close(fd) != 0) { unlink(temporary); return 0; }
    if (chmod(temporary, mode) != 0) { unlink(temporary); return 0; }
    if (rename(temporary, path) != 0) { unlink(temporary); return 0; }
    ap_sync_parent(path);
    return 1;
}

/* Parse a serialized document with the same decoder the loader uses and check
 * that the collection sizes survived the round trip. A save that this rejects
 * is never written, so a successful response always means a readable file. */
static int ap_verify_serialized(const char *body, size_t length, const ap_state *s) {
    ngl_json *doc = malloc(sizeof(*doc));
    if (!doc) return 0;
    int ok = ngl_json_parse(doc, body, length) && doc->v[0].type == '{';
    if (ok) {
        int member = ngl_member(doc, 0, "profiles");
        int count = 0;
        if (member >= 0 && doc->v[member].type == '[')
            for (int i = member + 1; i < doc->v[member].next; i = doc->v[i].next) count++;
        ok = count == s->profile_count;
    }
    if (ok) {
        int member = ngl_member(doc, 0, "themes");
        int count = 0;
        if (member >= 0 && doc->v[member].type == '[')
            for (int i = member + 1; i < doc->v[member].next; i = doc->v[i].next) count++;
        ok = count == s->theme_count;
    }
    free(doc);
    return ok;
}

static int ap_store(const ap_state *s) {
    if (!ap_ensure_paths()) return 0;
    char *body = malloc(AP_MAX_JSON);
    if (!body) return 0;
    size_t length = 0;
    if (!ap_serialize(s, body, AP_MAX_JSON, &length)) { free(body); return 0; }
    if (!ap_verify_serialized(body, length, s)) { free(body); return 0; }
    /* Keep the previous accepted document as the single rollback copy. */
    struct stat st;
    if (stat(ap_path, &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0 && st.st_size < AP_MAX_JSON) {
        char *previous = malloc((size_t)st.st_size + 1);
        if (previous) {
            FILE *file = fopen(ap_path, "rb");
            if (file) {
                size_t got = fread(previous, 1, (size_t)st.st_size, file);
                fclose(file);
                if (got == (size_t)st.st_size) (void)ap_write_all(ap_backup_path, previous, got, 0600);
            }
            free(previous);
        }
    }
    int ok = ap_write_all(ap_path, body, length, 0600);
    free(body);
    return ok;
}

static void ap_normalize(ap_state *s) {
    /* Ensure the built-in profile and the classic layout always exist. */
    int has_community = 0, has_classic = 0, has_custom = 0;
    for (int i = 0; i < s->profile_count; ++i)
        if (!strcmp(s->profiles[i].id, "community")) has_community = 1;
    if (!has_community) {
        if (s->profile_count >= AP_MAX_PROFILES) s->profile_count = AP_MAX_PROFILES - 1;
        ap_default_profile(&s->profiles[s->profile_count++], "community", "Community Default");
    }
    for (int i = 0; i < s->dashboard.layout_count; ++i) {
        if (!strcmp(s->dashboard.layouts[i].id, "classic")) has_classic = 1;
        if (!strcmp(s->dashboard.layouts[i].id, "custom-grid")) has_custom = 1;
    }
    if (!has_classic) {
        if (s->dashboard.layout_count >= AP_MAX_LAYOUTS) s->dashboard.layout_count = AP_MAX_LAYOUTS - 1;
        /* Keep custom layouts and add the classic marker first. */
        memmove(&s->dashboard.layouts[1], &s->dashboard.layouts[0],
                sizeof(ap_layout) * (size_t)s->dashboard.layout_count);
        memset(&s->dashboard.layouts[0], 0, sizeof(ap_layout));
        snprintf(s->dashboard.layouts[0].id, sizeof(s->dashboard.layouts[0].id), "classic");
        snprintf(s->dashboard.layouts[0].name, sizeof(s->dashboard.layouts[0].name), "Classic");
        s->dashboard.layouts[0].columns = 4;
        s->dashboard.layouts[0].widget_count = 0;
        s->dashboard.layout_count++;
    }
    (void)has_custom;
    if (!s->dashboard.theme[0]) snprintf(s->dashboard.theme, sizeof(s->dashboard.theme), "dark");
    if (!ap_theme_id_valid(s->dashboard.theme)) snprintf(s->dashboard.theme, sizeof(s->dashboard.theme), "dark");
    if (!s->active[0]) snprintf(s->active, sizeof(s->active), "community");
    /* active must name an existing profile */
    int known = 0;
    for (int i = 0; i < s->profile_count; ++i)
        if (!strcmp(s->profiles[i].id, s->active)) known = 1;
    if (!known) snprintf(s->active, sizeof(s->active), "community");
    /* A custom theme that no longer exists falls back to dark. */
    if (!strncmp(s->dashboard.theme, "custom-", 7)) {
        known = 0;
        for (int i = 0; i < s->theme_count; ++i)
            if (!strcmp(s->themes[i].id, s->dashboard.theme)) known = 1;
        if (!known) snprintf(s->dashboard.theme, sizeof(s->dashboard.theme), "dark");
    }
}

static void ap_load(void) {
    if (ap_initialized) return;
    ap_initialized = 1;
    ap_defaults(&ap_current);
    if (!ap_ensure_paths()) {
        snprintf(ap_current.error, sizeof(ap_current.error), "Appearance path unavailable");
        return;
    }
    struct stat ap_stat;
    if (lstat(ap_path, &ap_stat) == 0 && !S_ISREG(ap_stat.st_mode)) {
        ap_current.recovered = 1;
        snprintf(ap_current.error, sizeof(ap_current.error), "Appearance file is not a regular file");
        return;
    }
    FILE *file = fopen(ap_path, "rb");
    if (!file) {
        /* First run: inherit the theme already stored by the older
         * ui-preferences.json so nothing visibly changes on upgrade. */
        const char *legacy = preferences_theme();
        if (legacy && ap_theme_id_valid(legacy))
            snprintf(ap_current.dashboard.theme, sizeof(ap_current.dashboard.theme), "%s", legacy);
        return;
    }
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); ap_current.recovered = 1; return; }
    long size = ftell(file);
    if (size <= 0 || size >= AP_MAX_JSON) {
        fclose(file);
        ap_current.recovered = 1;
        snprintf(ap_current.error, sizeof(ap_current.error), "Appearance file size is invalid");
        return;
    }
    rewind(file);
    char *body = malloc((size_t)size + 1);
    if (!body) { fclose(file); ap_current.recovered = 1; return; }
    size_t got = fread(body, 1, (size_t)size, file);
    fclose(file);
    body[got] = '\0';
    if (got != (size_t)size) {
        free(body);
        ap_current.recovered = 1;
        snprintf(ap_current.error, sizeof(ap_current.error), "Appearance file could not be read");
        return;
    }
    ngl_json *doc = malloc(sizeof(*doc));
    int ok = doc && ngl_json_parse(doc, body, got) && doc->v[0].type == '{';
    if (ok) {
        double version = AP_VERSION;
        int v = ngl_member(doc, 0, "version");
        if (v >= 0 && (!ngl_number(doc, 0, "version", &version) || version != AP_VERSION)) ok = 0;
        if (version > AP_VERSION) {
            ap_current.unsupported = 1;
            snprintf(ap_current.error, sizeof(ap_current.error), "Appearance schema %g is newer than this release", version);
        }
    }
    if (!ok) {
        free(doc);
        free(body);
        ap_current.recovered = 1;
        if (!ap_current.error[0])
            snprintf(ap_current.error, sizeof(ap_current.error),
                     "Appearance file is not readable (invalid JSON or above the document limits)");
        return;
    }
    int member;
    if ((member = ngl_member(doc, 0, "activeProfile")) >= 0 && doc->v[member].type == '"')
        snprintf(ap_current.active, sizeof(ap_current.active), "%s", doc->v[member].text);
    member = ngl_member(doc, 0, "dashboard");
    if (member >= 0) {
        ap_dashboard d = ap_current.dashboard;
        if (!ap_parse_dashboard(doc, member, &d, &d)) {
            ap_current.recovered = 1;
            snprintf(ap_current.error, sizeof(ap_current.error), "Appearance dashboard is invalid");
        } else ap_current.dashboard = d;
    }
    member = ngl_member(doc, 0, "touchscreen");
    if (member >= 0) {
        ap_touchscreen t = ap_current.touchscreen;
        if (!ap_parse_touchscreen(doc, member, &t, &t)) {
            ap_current.recovered = 1;
            snprintf(ap_current.error, sizeof(ap_current.error), "Appearance touchscreen is invalid");
        } else ap_current.touchscreen = t;
    }
    member = ngl_member(doc, 0, "themes");
    if (member >= 0 && doc->v[member].type == '[') {
        ap_current.theme_count = 0;
        for (int i = member + 1; i < doc->v[member].next && ap_current.theme_count < AP_MAX_THEMES;
             i = doc->v[i].next) {
            if (doc->v[i].type != '{' ||
                !ap_parse_theme(doc, i, &ap_current.themes[ap_current.theme_count])) continue;
            ap_current.theme_count++;
        }
    }
    member = ngl_member(doc, 0, "profiles");
    if (member >= 0 && doc->v[member].type == '[') {
        ap_profile parsed[AP_MAX_PROFILES];
        int count = 0;
        for (int i = member + 1; i < doc->v[member].next && count < AP_MAX_PROFILES; i = doc->v[i].next) {
            if (doc->v[i].type != '{' || !ap_parse_profile(doc, i, &parsed[count])) continue;
            int duplicate = 0;
            for (int p = 0; p < count; ++p)
                if (!strcmp(parsed[p].id, parsed[count].id)) duplicate = 1;
            if (duplicate) continue;
            count++;
        }
        if (count) {
            memcpy(ap_current.profiles, parsed, sizeof(ap_profile) * (size_t)count);
            ap_current.profile_count = count;
        }
    }
    free(doc);
    free(body);
    ap_normalize(&ap_current);
    ap_current.loaded = 1;
}

/* Small local hash helper so appearance.h does not depend on display_theme.h. */
static int disp_hash_file_for_copy(const char *path, char out[65]) {
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    sha256_state state;
    sha256_init(&state);
    unsigned char buffer[16384];
    size_t n;
    int ok = 1;
    while ((n = fread(buffer, 1, sizeof(buffer), file)) > 0) sha256_update(&state, buffer, n);
    if (ferror(file)) ok = 0;
    fclose(file);
    if (ok) sha256_hex(&state, out);
    return ok;
}

/* Copy `source` over `destination` through a temporary file in the target
 * directory, fsync it, verify both hashes and rename. Returns 1 on success and
 * leaves `destination` untouched on any failure. */
static int ap_copy_verified(const char *source, const char *destination, mode_t mode) {
    char hash_source[65], hash_copy[65];
    if (!disp_hash_file_for_copy(source, hash_source)) return 0;
    char temporary[PATH_MAX_LOCAL];
    if (snprintf(temporary, sizeof(temporary), "%s.cc2-new-%ld", destination, (long)getpid()) >=
        (int)sizeof(temporary)) return 0;
    unlink(temporary);
    int in = open(source, O_RDONLY | O_CLOEXEC);
    if (in < 0) return 0;
    int out = open(temporary, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    if (out < 0) { close(in); return 0; }
    unsigned char buffer[32768];
    int ok = 1;
    ssize_t n;
    while ((n = read(in, buffer, sizeof(buffer))) > 0) {
        size_t done = 0;
        while (done < (size_t)n) {
            ssize_t w = write(out, buffer + done, (size_t)n - done);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) { ok = 0; break; }
            done += (size_t)w;
        }
        if (!ok) break;
    }
    if (n < 0) ok = 0;
    close(in);
    if (ok && (fsync(out) != 0 || close(out) != 0)) ok = 0;
    else if (!ok) close(out);
    if (!ok) { unlink(temporary); return 0; }
    if (chmod(temporary, mode) != 0 || !disp_hash_file_for_copy(temporary, hash_copy) ||
        strcasecmp(hash_source, hash_copy)) {
        unlink(temporary);
        return 0;
    }
    if (rename(temporary, destination) != 0) { unlink(temporary); return 0; }
    ap_sync_parent(destination);
    return 1;
}

/* ---- HTTP ------------------------------------------------------------- */

/* A successful save means the file on disk is valid and current: drop the
 * diagnostics of an earlier damaged read for every mutation handler. */
static void ap_saved(void) {
    ap_current.recovered = 0;
    ap_current.unsupported = 0;
    ap_current.error[0] = '\0';
}

static void ap_json_error(int fd, int code, const char *status, const char *message) {
    char body[192];
    int length = snprintf(body, sizeof(body), "{\"saved\":false,\"error\":\"%s\"}\n", message);
    if (length > 0 && (size_t)length < sizeof(body))
        respond(fd, code, status, "application/json; charset=utf-8", body, (size_t)length);
}

static void ap_get_response(int fd) {
    ap_load();
    pthread_mutex_lock(&ap_mutex);
    char *body = malloc(AP_MAX_JSON);
    size_t length = 0;
    if (!body || !ap_serialize_api(&ap_current, body, AP_MAX_JSON, &length)) {
        pthread_mutex_unlock(&ap_mutex);
        free(body);
        ap_json_error(fd, 500, "Internal Server Error", "Appearance document is too large");
        return;
    }
    pthread_mutex_unlock(&ap_mutex);
    respond(fd, 200, "OK", "application/json; charset=utf-8", body, length);
    free(body);
}

static void ap_put_response(int fd, const char *body, size_t length) {
    if (!body || length < 2 || length >= AP_MAX_JSON) {
        ap_json_error(fd, 400, "Bad Request", "Invalid appearance document");
        return;
    }
    ap_load();
    pthread_mutex_lock(&ap_mutex);
    ap_state next;
    if (!ap_parse_document(body, length, &ap_current, &next)) {
        pthread_mutex_unlock(&ap_mutex);
        ap_json_error(fd, 400, "Bad Request", "Unsupported appearance value");
        return;
    }
    ap_normalize(&next);
    if (!ap_store(&next)) {
        pthread_mutex_unlock(&ap_mutex);
        ap_json_error(fd, 500, "Internal Server Error", "Cannot save appearance");
        return;
    }
    ap_current = next;
    ap_saved();
    pthread_mutex_unlock(&ap_mutex);
    const char *ok = "{\"saved\":true}\n";
    respond(fd, 200, "OK", "application/json; charset=utf-8", ok, strlen(ok));
}

static void ap_theme_put_response(int fd, const char *body, size_t length) {
    if (!body || length < 2 || length >= AP_MAX_JSON) {
        ap_json_error(fd, 400, "Bad Request", "Invalid theme document");
        return;
    }
    ap_load();
    pthread_mutex_lock(&ap_mutex);
    ngl_json *doc = malloc(sizeof(*doc));
    ap_theme theme;
    int ok = doc && ngl_json_parse(doc, body, length) && doc->v[0].type == '{';
    int member = ok ? ngl_member(doc, 0, "theme") : -1;
    if (member < 0 || !ap_parse_theme(doc, member, &theme)) ok = 0;
    free(doc);
    if (!ok) {
        pthread_mutex_unlock(&ap_mutex);
        ap_json_error(fd, 400, "Bad Request", "Unsupported theme");
        return;
    }
    int slot = -1;
    for (int i = 0; i < ap_current.theme_count; ++i)
        if (!strcmp(ap_current.themes[i].id, theme.id)) slot = i;
    if (slot < 0) {
        if (ap_current.theme_count >= AP_MAX_THEMES) {
            pthread_mutex_unlock(&ap_mutex);
            ap_json_error(fd, 409, "Conflict", "Too many custom themes");
            return;
        }
        slot = ap_current.theme_count++;
    }
    ap_state next = ap_current;
    next.themes[slot] = theme;
    if (!ap_store(&next)) {
        pthread_mutex_unlock(&ap_mutex);
        ap_json_error(fd, 500, "Internal Server Error", "Cannot save theme");
        return;
    }
    ap_current = next;
    ap_saved();
    pthread_mutex_unlock(&ap_mutex);
    const char *reply = "{\"saved\":true}\n";
    respond(fd, 200, "OK", "application/json; charset=utf-8", reply, strlen(reply));
}

/* Delete one custom theme. Removal is explicit so that a document save from a
 * stale browser can never delete a theme created elsewhere. */
static void ap_theme_delete_response(int fd, const char *body, size_t length) {
    if (!body || length < 2 || length >= AP_MAX_JSON) {
        ap_json_error(fd, 400, "Bad Request", "Invalid theme selection");
        return;
    }
    ap_load();
    pthread_mutex_lock(&ap_mutex);
    ngl_json *doc = malloc(sizeof(*doc));
    int ok = doc && ngl_json_parse(doc, body, length) && doc->v[0].type == '{';
    const char *id = ok ? ngl_string(doc, 0, "id") : NULL;
    int valid = id && ap_theme_id_valid(id) && !strncmp(id, "custom-", 7);
    free(doc);
    if (!valid) {
        pthread_mutex_unlock(&ap_mutex);
        ap_json_error(fd, 400, "Bad Request", "Only a custom theme can be removed");
        return;
    }
    ap_state next = ap_current;
    int removed = 0;
    for (int i = 0; i < next.theme_count; ++i) {
        if (!strcmp(next.themes[i].id, id)) {
            memmove(&next.themes[i], &next.themes[i + 1],
                    sizeof(ap_theme) * (size_t)(next.theme_count - i - 1));
            next.theme_count--;
            removed = 1;
            break;
        }
    }
    if (!removed) {
        pthread_mutex_unlock(&ap_mutex);
        ap_json_error(fd, 404, "Not Found", "Theme not found");
        return;
    }
    ap_normalize(&next);
    if (!ap_store(&next)) {
        pthread_mutex_unlock(&ap_mutex);
        ap_json_error(fd, 500, "Internal Server Error", "Cannot save themes");
        return;
    }
    ap_current = next;
    ap_saved();
    pthread_mutex_unlock(&ap_mutex);
    const char *reply = "{\"saved\":true}\n";
    respond(fd, 200, "OK", "application/json; charset=utf-8", reply, strlen(reply));
}

static void ap_profile_put_response(int fd, const char *body, size_t length) {    if (!body || length < 2 || length >= AP_MAX_JSON) {
        ap_json_error(fd, 400, "Bad Request", "Invalid profile document");
        return;
    }
    ap_load();
    pthread_mutex_lock(&ap_mutex);
    ap_profile profile;
    ap_theme theme;
    int has_theme = 0;
    if (!ap_parse_single_profile(body, length, &profile, &theme, &has_theme)) {
        pthread_mutex_unlock(&ap_mutex);
        ap_json_error(fd, 400, "Bad Request", "Unsupported profile");
        return;
    }
    ap_state next = ap_current;
    int slot = -1;
    for (int i = 0; i < next.profile_count; ++i)
        if (!strcmp(next.profiles[i].id, profile.id)) slot = i;
    if (slot < 0) {
        if (next.profile_count >= AP_MAX_PROFILES) {
            pthread_mutex_unlock(&ap_mutex);
            ap_json_error(fd, 409, "Conflict", "Too many profiles");
            return;
        }
        slot = next.profile_count++;
    }
    next.profiles[slot] = profile;
    if (has_theme) {
        int theme_slot = -1;
        for (int i = 0; i < next.theme_count; ++i)
            if (!strcmp(next.themes[i].id, theme.id)) theme_slot = i;
        if (theme_slot < 0) {
            if (next.theme_count >= AP_MAX_THEMES) {
                pthread_mutex_unlock(&ap_mutex);
                ap_json_error(fd, 409, "Conflict", "Too many custom themes");
                return;
            }
            theme_slot = next.theme_count++;
        }
        next.themes[theme_slot] = theme;
    }
    if (!ap_store(&next)) {
        pthread_mutex_unlock(&ap_mutex);
        ap_json_error(fd, 500, "Internal Server Error", "Cannot save profile");
        return;
    }
    ap_current = next;
    ap_saved();
    pthread_mutex_unlock(&ap_mutex);
    const char *reply = "{\"saved\":true}\n";
    respond(fd, 200, "OK", "application/json; charset=utf-8", reply, strlen(reply));
}

static void ap_profile_delete_response(int fd, const char *body, size_t length) {
    if (!body || length < 2 || length >= AP_MAX_JSON) {
        ap_json_error(fd, 400, "Bad Request", "Invalid profile selection");
        return;
    }
    ap_load();
    pthread_mutex_lock(&ap_mutex);
    ngl_json *doc = malloc(sizeof(*doc));
    int ok = doc && ngl_json_parse(doc, body, length) && doc->v[0].type == '{';
    const char *id = ok ? ngl_string(doc, 0, "id") : NULL;
    int valid = id && ap_id_valid(id) && strcmp(id, "community");
    free(doc);
    if (!valid) {
        pthread_mutex_unlock(&ap_mutex);
        ap_json_error(fd, 400, "Bad Request", "The built-in profile cannot be removed");
        return;
    }
    ap_state next = ap_current;
    int removed = 0;
    for (int i = 0; i < next.profile_count; ++i) {
        if (!strcmp(next.profiles[i].id, id)) {
            memmove(&next.profiles[i], &next.profiles[i + 1],
                    sizeof(ap_profile) * (size_t)(next.profile_count - i - 1));
            next.profile_count--;
            removed = 1;
            break;
        }
    }
    if (!removed) {
        pthread_mutex_unlock(&ap_mutex);
        ap_json_error(fd, 404, "Not Found", "Profile not found");
        return;
    }
    if (!strcmp(next.active, id)) snprintf(next.active, sizeof(next.active), "community");
    if (!ap_store(&next)) {
        pthread_mutex_unlock(&ap_mutex);
        ap_json_error(fd, 500, "Internal Server Error", "Cannot save profiles");
        return;
    }
    ap_current = next;
    ap_saved();
    pthread_mutex_unlock(&ap_mutex);
    const char *reply = "{\"saved\":true}\n";
    respond(fd, 200, "OK", "application/json; charset=utf-8", reply, strlen(reply));
}

/* Copy a stored profile into the active document. */
static int ap_apply_profile(ap_state *s, const char *id) {
    for (int i = 0; i < s->profile_count; ++i) {
        if (strcmp(s->profiles[i].id, id)) continue;
        s->dashboard = s->profiles[i].dashboard;
        s->touchscreen = s->profiles[i].touchscreen;
        snprintf(s->active, sizeof(s->active), "%s", id);
        return 1;
    }
    return 0;
}

static void ap_profile_apply_response(int fd, const char *body, size_t length) {
    if (!body || length < 2 || length >= AP_MAX_JSON) {
        ap_json_error(fd, 400, "Bad Request", "Invalid profile selection");
        return;
    }
    ap_load();
    pthread_mutex_lock(&ap_mutex);
    ngl_json *doc = malloc(sizeof(*doc));
    int ok = doc && ngl_json_parse(doc, body, length) && doc->v[0].type == '{';
    const char *id = ok ? ngl_string(doc, 0, "id") : NULL;
    if (!id || !ap_id_valid(id)) ok = 0;
    ap_state next = ap_current;
    if (ok && !ap_apply_profile(&next, id)) {
        pthread_mutex_unlock(&ap_mutex);
        free(doc);
        ap_json_error(fd, 404, "Not Found", "Profile not found");
        return;
    }
    free(doc);
    if (!ok) {
        pthread_mutex_unlock(&ap_mutex);
        ap_json_error(fd, 400, "Bad Request", "Invalid profile selection");
        return;
    }
    ap_normalize(&next);
    if (!ap_store(&next)) {
        pthread_mutex_unlock(&ap_mutex);
        ap_json_error(fd, 500, "Internal Server Error", "Cannot save profile selection");
        return;
    }
    ap_current = next;
    ap_saved();
    pthread_mutex_unlock(&ap_mutex);
    const char *reply = "{\"saved\":true}\n";
    respond(fd, 200, "OK", "application/json; charset=utf-8", reply, strlen(reply));
}

static void ap_reset_response(int fd, const char *body, size_t length) {
    (void)length;
    int all = 0;
    if (body && length >= 2) {
        ngl_json *doc = malloc(sizeof(*doc));
        if (doc && ngl_json_parse(doc, body, length) && doc->v[0].type == '{')
            (void)ngl_bool(doc, 0, "profiles", &all);
        free(doc);
    }
    ap_load();
    pthread_mutex_lock(&ap_mutex);
    ap_state next = ap_current;
    ap_default_dashboard(&next.dashboard);
    ap_default_touchscreen(&next.touchscreen);
    snprintf(next.active, sizeof(next.active), "community");
    if (all) {
        next.theme_count = 0;
        next.profile_count = 1;
        ap_default_profile(&next.profiles[0], "community", "Community Default");
    }
    ap_normalize(&next);
    int ok = ap_store(&next);
    if (ok) { ap_current = next; ap_saved(); }
    pthread_mutex_unlock(&ap_mutex);
    if (!ok) {
        ap_json_error(fd, 500, "Internal Server Error", "Cannot save appearance");
        return;
    }
    /* Resetting the dashboard does not touch the native display; the caller
     * asks separately when it wants the touchscreen restored. */
    const char *reply = "{\"saved\":true}\n";
    respond(fd, 200, "OK", "application/json; charset=utf-8", reply, strlen(reply));
}

#endif /* CC2_APPEARANCE_H */
