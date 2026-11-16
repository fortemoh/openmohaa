// Bot movement keys must be ones a keyboard can send (-127/0/127 per axis),
// change no faster than the hold time, and keep the wanted average heading.

#include "../playerbot_keyboard.h"

#include <cmath>
#include <cstdio>

static int failures;

#define CHECK(cond)                                                       \
    do {                                                                  \
        if (!(cond)) {                                                    \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                   \
        }                                                                 \
    } while (0)

static bool KeyValue(int v)
{
    return v == 0 || v == 127 || v == -127;
}

struct RunResult {
    int   shortestHoldMsec; // shortest time between two move-key changes
    int   changes;
    float meanHeadingDeg;   // heading of the summed key directions
    bool  allKeyboard;
};

// Feeds a constant analog command for durationMsec at frameMsec steps.
static RunResult Run(float headingDeg, float length, int durationMsec, int frameMsec, int minHold)
{
    BotKeyboardState state;
    RunResult        res  = {1 << 30, 0, 0, true};
    float            sumF = 0, sumR = 0;
    int              lastChange = -1;
    signed char      prevF = 0, prevR = 0;

    for (int t = 0; t < durationMsec; t += frameMsec) {
        signed char f  = (signed char)lroundf(cosf(headingDeg * 3.14159265f / 180) * length);
        signed char r  = (signed char)lroundf(sinf(headingDeg * 3.14159265f / 180) * length);
        signed char up = 0;

        BotKeyboard_Apply(state, t, minHold, f, r, up);

        if (!KeyValue(f) || !KeyValue(r) || !KeyValue(up)) {
            res.allKeyboard = false;
        }
        if (t > 0 && (f != prevF || r != prevR)) {
            if (lastChange >= 0 && t - lastChange < res.shortestHoldMsec) {
                res.shortestHoldMsec = t - lastChange;
            }
            lastChange = t;
            res.changes++;
        }
        prevF = f;
        prevR = r;

        // Unit direction of the pressed keys.
        const float len = sqrtf((float)f * f + (float)r * r);
        if (len > 0) {
            sumF += f / len;
            sumR += r / len;
        }
    }

    res.meanHeadingDeg = atan2f(sumR, sumF) * 180 / 3.14159265f;
    if (res.meanHeadingDeg < 0) {
        res.meanHeadingDeg += 360;
    }
    return res;
}

int main()
{
    failures = 0;

    // Exactly on a key direction: that combination, never changed.
    {
        RunResult r = Run(45, 127, 2000, 50, 100);
        CHECK(r.allKeyboard);
        CHECK(r.changes == 0);
        CHECK(fabsf(r.meanHeadingDeg - 45) < 0.5f);
    }

    // Between two directions: alternates, average heading kept, hold kept.
    const float headings[] = {10, 22.5f, 30, 67, 100, 160, 200, 290, 350};
    for (float h : headings) {
        RunResult r = Run(h, 127, 4000, 50, 100);
        CHECK(r.allKeyboard);
        CHECK(r.shortestHoldMsec >= 100);
        const float err = fabsf(BotKeyboard_AngleDelta(r.meanHeadingDeg, h));
        if (err > 4.0f) {
            fprintf(stderr, "heading %.1f -> mean %.1f\n", h, r.meanHeadingDeg);
        }
        CHECK(err <= 4.0f);
    }

    // At 125 Hz commands the hold still applies.
    {
        RunResult r = Run(22.5f, 127, 3000, 8, 100);
        CHECK(r.allKeyboard);
        CHECK(r.shortestHoldMsec >= 100);
        CHECK(fabsf(BotKeyboard_AngleDelta(r.meanHeadingDeg, 22.5f)) <= 4.0f);
    }

    // A unit-circle diagonal (89, 89) is pressed as forward+right.
    {
        BotKeyboardState state;
        signed char      f = 89, r = 89, up = 0;
        BotKeyboard_Apply(state, 0, 100, f, r, up);
        CHECK(f == 127 && r == 127);
    }

    // Barely moving: no key.
    {
        BotKeyboardState state;
        signed char      f = 20, r = -15, up = 0;
        BotKeyboard_Apply(state, 0, 100, f, r, up);
        CHECK(f == 0 && r == 0);
    }

    // Jump toggled every 50 ms frame is held at least 100 ms each way.
    {
        BotKeyboardState state;
        int              lastChange = -1, shortest = 1 << 30;
        signed char      prev       = 0;
        for (int t = 0, i = 0; t < 2000; t += 50, i++) {
            signed char f = 127, r = 0, up = (i & 1) ? 127 : 0;
            BotKeyboard_Apply(state, t, 100, f, r, up);
            CHECK(KeyValue(up));
            if (up != prev) {
                if (lastChange >= 0 && t - lastChange < shortest) {
                    shortest = t - lastChange;
                }
                lastChange = t;
            }
            prev = up;
        }
        CHECK(lastChange >= 0);
        CHECK(shortest >= 100);
    }

    // The game clock restarting (map load) must not freeze the keys.
    {
        BotKeyboardState state;
        signed char      f = 127, r = 0, up = 0;
        BotKeyboard_Apply(state, 90000, 100, f, r, up);
        CHECK(f == 127 && r == 0);
        f = 0, r = 127, up = 0;
        BotKeyboard_Apply(state, 50, 100, f, r, up);
        CHECK(f == 0 && r == 127);
    }

    // Hold 0: keys may change every command.
    {
        RunResult r = Run(22.5f, 127, 2000, 50, 0);
        CHECK(r.allKeyboard);
        CHECK(r.shortestHoldMsec == 50);
    }

    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }

    printf("test_bot_keyboard: ok\n");
    return 0;
}
