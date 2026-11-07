/* See settings.h. A plain key = value file; unknown keys are ignored. */
#include "settings.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void settings_default(Settings *s)
{
    memset(s, 0, sizeof *s);
    s->aspect = ASPECT_4_3;
    s->integer_scale = false;
    s->filter = FILTER_SHARP;
    s->sides = SIDES_BLUR;
    s->skip = SKIP_START;
    s->rewind = true;
    s->volume = 10;
    s->slot = 1;
    s->language = LANG_EN;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void settings_load(Settings *s, const char *path)
{
    settings_default(s);
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[1200];
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *key = line, *val = eq + 1;
        while (*key == ' ') key++;
        for (char *e = key + strlen(key); e > key && e[-1] == ' '; ) *--e = 0;
        while (*val == ' ') val++;
        int n = atoi(val);
        if (!strcmp(key, "fullscreen")) s->fullscreen = n != 0;
        else if (!strcmp(key, "aspect")) s->aspect = clampi(n, 0, ASPECT_COUNT - 1);
        else if (!strcmp(key, "integer_scale")) s->integer_scale = n != 0;
        else if (!strcmp(key, "filter")) s->filter = clampi(n, 0, FILTER_COUNT - 1);
        else if (!strcmp(key, "sides")) s->sides = clampi(n, 0, SIDES_COUNT - 1);
        else if (!strcmp(key, "skip_cinematics")) s->skip = clampi(n, 0, SKIP_COUNT - 1);
        else if (!strcmp(key, "rewind")) s->rewind = n != 0;
        else if (!strcmp(key, "volume")) s->volume = clampi(n, 0, 10);
        else if (!strcmp(key, "slot")) s->slot = clampi(n, 1, 9);
        else if (!strcmp(key, "cheat_health")) s->cheat_health = n != 0;
        else if (!strcmp(key, "cheat_hearts")) s->cheat_hearts = n != 0;
        else if (!strcmp(key, "cheat_lives")) s->cheat_lives = n != 0;
        else if (!strcmp(key, "language")) s->language = clampi(n, 0, LANG_COUNT - 1);
        else if (!strcmp(key, "disc_ja")) snprintf(s->disc_ja, sizeof s->disc_ja, "%s", val);
    }
    fclose(f);
}

bool settings_save(const Settings *s, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return false;
    fprintf(f,
            "# Rondo Recomp options (also changed from the in-game menu: F1 / Back)\n"
            "fullscreen = %d\naspect = %d\ninteger_scale = %d\nfilter = %d\nsides = %d\n"
            "skip_cinematics = %d\nrewind = %d\nvolume = %d\nslot = %d\n"
            "cheat_health = %d\ncheat_hearts = %d\ncheat_lives = %d\n"
            "language = %d\ndisc_ja = %s\n",
            s->fullscreen, s->aspect, s->integer_scale, s->filter, s->sides,
            s->skip, s->rewind, s->volume, s->slot,
            s->cheat_health, s->cheat_hearts, s->cheat_lives,
            s->language, s->disc_ja);
    return fclose(f) == 0;
}
