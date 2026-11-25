#ifndef CC2_NATIVE_LANGUAGE_CORE_H
#define CC2_NATIVE_LANGUAGE_CORE_H

/* New display locales must never become native daemon/error-table indices. */
#define ND_NATIVE_LANGUAGES 11
#define ND_LANGUAGES 14
#define ND_STRINGS 648

struct nd_language {
    int loaded;
    int selected;
    int error;
};

int nd_language_native(int selected);
int nd_language_read(struct nd_language *state, const char *path);
int nd_language_save(struct nd_language *state, const char *path, int selected);
int nd_language_effective(const struct nd_language *state, int native_language);

#endif
