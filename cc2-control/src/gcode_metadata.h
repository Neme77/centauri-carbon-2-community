/* Read a complete slicer comment key, never a substring in a formula, G-code
 * command or another setting. Missing/malformed values remain unavailable. */
static const char *gcode_metadata_value(const char *line, const char *key) {
    const char *p = line;
    while (isspace((unsigned char)*p)) p++;
    if (*p++ != ';') return NULL;
    while (isspace((unsigned char)*p)) p++;
    size_t n = strlen(key);
    if (strncasecmp(p, key, n)) return NULL;
    p += n;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=' && *p != ':') return NULL;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static double gcode_metadata_number(const char *line, const char *key, double maximum) {
    const char *p = gcode_metadata_value(line, key);
    if (!p) return -1;
    char *end;
    double value = strtod(p, &end);
    if (end == p || !isfinite(value) || value < 0 || value > maximum) return -1;
    /* Temperature profiles can contain one value per tool. The existing API
     * reports the first tool; do not scan forward for digits in expressions. */
    while (*end == ' ' || *end == '\t') end++;
    if (*end && *end != '\r' && *end != '\n' && *end != ',' && *end != ';') return -1;
    return value;
}

/* Only executable bed-heater commands, never their text in a comment or a
 * slicer start-script setting. Return a wait flag so M190 outranks preheat. */
static double gcode_metadata_bed_command(const char *line, int *wait) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != 'M' && *p != 'm') return -1;
    char *end;
    long command = strtol(p + 1, &end, 10);
    if ((command != 140 && command != 190) || (*end != ' ' && *end != '\t')) return -1;
    for (p = end; *p && *p != ';'; p++) {
        if ((*p != 'S' && *p != 's' && !(command == 190 && (*p == 'R' || *p == 'r'))) ||
            (p[-1] != ' ' && p[-1] != '\t')) continue;
        double value = strtod(p + 1, &end);
        if (end == p + 1 || !isfinite(value) || value <= 0 || value > 200) return -1;
        if (*end && !isspace((unsigned char)*end) && *end != ';') return -1;
        *wait = command == 190;
        return value;
    }
    return -1;
}

static double gcode_metadata_grams(const char *line) {
    static const char *keys[] = {"filament used [g]", "filament weight", "filament_used_g",
        "filament weight [g]", "filament weight (g)", "total filament weight [g]", "total filament used [g]"};
    const char *p = NULL;
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]) && !p; i++)
        p = gcode_metadata_value(line, keys[i]);
    if (!p) return -1;
    double total = 0;
    unsigned count = 0;
    do {
        char *end;
        double value = strtod(p, &end);
        if (end == p || !isfinite(value) || value < 0 || value > 1e7 || ++count > 64) return -1;
        total += value;
        if (!isfinite(total) || total > 1e7) return -1;
        p = end;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 'g' || *p == 'G') {
            p++;
            while (*p == ' ' || *p == '\t') p++;
        }
        if (*p == ',' || *p == ';') { p++; continue; }
        return !*p || *p == '\r' || *p == '\n' ? total : -1;
    } while (1);
}
