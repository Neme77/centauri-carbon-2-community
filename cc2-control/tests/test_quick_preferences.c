#define main cc2_main_original
#include "../src/main.c"
#undef main
#include <assert.h>

static int put_preferences(const char *body) {
    int peers[2]; assert(socketpair(AF_UNIX, SOCK_STREAM, 0, peers) == 0);
    preferences_put_response(peers[0], body, strlen(body));
    char reply[2048]; ssize_t n = recv(peers[1], reply, sizeof(reply)-1, 0);
    assert(n > 0); reply[n] = 0;
    close(peers[0]); close(peers[1]);
    return atoi(strchr(reply, ' ')+1);
}
int main(void) {
    char dir[] = "/tmp/cc2-quick-prefs-XXXXXX", path[256];
    assert(mkdtemp(dir));
    snprintf(path, sizeof(path), "%s/preferences.json", dir);
    ui_preferences_path = path;
    assert(put_preferences("{\"language\":\"it\",\"theme\":\"obsidian\",\"quick1\":\"grille:open\",\"quick2\":\"grille:close\",\"quick3\":\"control:tuning\",\"quick4\":\"settings:connection\"}") == 200);
    assert(!strcmp(preferences_language(), "it"));
    assert(!strcmp(preferences_theme(), "obsidian"));
    assert(!strcmp(preferences_quick_action(0), "grille:open"));
    assert(!strcmp(preferences_quick_action(1), "grille:close"));
    assert(!strcmp(preferences_quick_action(2), "control:tuning"));
    assert(!strcmp(preferences_quick_action(3), "settings:connection"));
    assert(put_preferences("{\"quick1\":\"control:fans\",\"quick2\":\"control:heating\"}") == 200);
    assert(!strcmp(preferences_quick_action(0), "control:fans"));
    assert(!strcmp(preferences_quick_action(1), "control:heating"));
    assert(!strcmp(preferences_quick_action(3), "settings:connection"));
    assert(put_preferences("{\"quick1\":\"cell:tare\"}") == 400);
    assert(put_preferences("{\"quick1\":\"grille:open;M112\"}") == 400);
    assert(put_preferences("{\"quick1\":\"control:unknown\"}") == 400);
    assert(!strcmp(preferences_quick_action(0), "control:fans"));
    assert(!strcmp(preferences_language(), "it"));
    assert(!strcmp(preferences_theme(), "obsidian"));
    assert(unlink(path) == 0); assert(rmdir(dir) == 0);
    puts("PASS: new quick-action persistence, partial updates, retained language/theme and rejected cell/unknown/injected actions");
    return 0;
}
