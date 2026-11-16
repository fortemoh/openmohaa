/*
===========================================================================
Copyright (C) 2026 the OpenMoHAA team

This file is part of OpenMoHAA source code.

OpenMoHAA source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

OpenMoHAA source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with OpenMoHAA source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/

// playerbot_keyboard.h: keyboard-legal bot movement keys.
//
// A keyboard client (CL_KeyMove) sends -127, 0 or 127 on each move axis, so a
// human can only move in eight directions relative to the view. The bot
// movement code steers with analog values on a circle (for example 109
// forward and 63 right), which no keyboard can send. This stage turns the
// final bot command into keys a human could press:
//
// - forward/right become one of the eight key combinations, or no key when
//   the bot barely wants to move;
// - when the wanted direction lies between two combinations, it alternates
//   between them so the accumulated sideways error stays near zero and the
//   path keeps its average heading;
// - a combination, once pressed, stays pressed at least minHoldMsec before
//   it changes, so keys change at a human rate and not every server frame;
// - the jump/crouch axis gets the same three values and the same hold.
//
// The view, aim and buttons are not changed. Pure code, no game state, so it
// can be unit tested on its own.

#pragma once

#include <cmath>

struct BotKeyboardState {
    signed char forward      = 0;
    signed char right        = 0;
    signed char up           = 0;
    int         moveSince    = -1000000; // time the current move keys were pressed
    int         upSince      = -1000000; // time the current up key state began
    int         lastTime     = -1;
    float       lateralError = 0;        // sum of sin(held - wanted) * seconds
    float       lastWanted   = -1;       // last wanted direction in degrees, -1 = none
};

static const int   BOT_KEYBOARD_DEADZONE         = 40;    // analog length below this = no key
static const float BOT_KEYBOARD_MAX_ERROR        = 0.25f; // clamp of the accumulated error
static const float BOT_KEYBOARD_RESET_TURN_DEG   = 60.0f; // wanted turn that restarts the error

inline void BotKeyboard_Reset(BotKeyboardState& state)
{
    state = BotKeyboardState();
}

// Key combination for a direction index 0..7 (0 = forward, 2 = right,
// 4 = back, 6 = left; odd = diagonals), -1 = no key.
inline void BotKeyboard_Keys(int dir, signed char& forward, signed char& right)
{
    static const signed char table[8][2] = {
        {127,  0   },
        {127,  127 },
        {0,    127 },
        {-127, 127 },
        {-127, 0   },
        {-127, -127},
        {0,    -127},
        {127,  -127},
    };

    if (dir < 0) {
        forward = 0;
        right   = 0;
        return;
    }

    forward = table[dir & 7][0];
    right   = table[dir & 7][1];
}

// Direction index of held keys, -1 if none.
inline int BotKeyboard_Dir(signed char forward, signed char right)
{
    for (int dir = 0; dir < 8; dir++) {
        signed char f, r;
        BotKeyboard_Keys(dir, f, r);
        if (f == forward && r == right) {
            return dir;
        }
    }
    return -1;
}

inline signed char BotKeyboard_Axis(int value)
{
    if (value >= 64) {
        return 127;
    }
    if (value <= -64) {
        return -127;
    }
    return 0;
}

inline float BotKeyboard_AngleDelta(float a, float b)
{
    float d = fmodf(a - b + 540.0f, 360.0f) - 180.0f;
    return d;
}

// Rewrites forward/right/up in place. timeMsec is the game time of this
// command; minHoldMsec is the shortest time keys stay as they are.
inline void BotKeyboard_Apply(
    BotKeyboardState& state, int timeMsec, int minHoldMsec, signed char& forward, signed char& right, signed char& up
)
{
    if (state.lastTime >= 0 && timeMsec < state.lastTime) {
        // The game clock restarted (map load): forget the old hold times.
        BotKeyboard_Reset(state);
    }

    const float dt = state.lastTime < 0 ? 0.0f : (timeMsec - state.lastTime) / 1000.0f;
    state.lastTime = timeMsec;

    //
    // Move keys
    //
    const float f      = forward;
    const float r      = right;
    const float length = sqrtf(f * f + r * r);
    int         target = -1;
    float       wanted = -1;

    if (length >= BOT_KEYBOARD_DEADZONE) {
        wanted = atan2f(r, f) * (180.0f / 3.14159265f);
        if (wanted < 0) {
            wanted += 360.0f;
        }

        if (state.lastWanted < 0 || fabsf(BotKeyboard_AngleDelta(wanted, state.lastWanted)) > BOT_KEYBOARD_RESET_TURN_DEG) {
            state.lateralError = 0;
        }

        // The two combinations around the wanted direction.
        const int   lo      = (int)floorf(wanted / 45.0f) & 7;
        const int   hi      = (lo + 1) & 7;
        const float errLo   = state.lateralError + sinf(BotKeyboard_AngleDelta(lo * 45.0f, wanted) * (3.14159265f / 180.0f)) * dt;
        const float errHi   = state.lateralError + sinf(BotKeyboard_AngleDelta(hi * 45.0f, wanted) * (3.14159265f / 180.0f)) * dt;
        const float fracLo  = fabsf(BotKeyboard_AngleDelta(wanted, lo * 45.0f));

        if (dt <= 0) {
            // First command: the nearest combination.
            target = fracLo <= 22.5f ? lo : hi;
        } else {
            target = fabsf(errLo) <= fabsf(errHi) ? lo : hi;
        }
    } else {
        state.lateralError = 0;
    }
    state.lastWanted = wanted;

    int held = BotKeyboard_Dir(state.forward, state.right);
    if (target != held && timeMsec - state.moveSince >= minHoldMsec) {
        held            = target;
        state.moveSince = timeMsec;
        BotKeyboard_Keys(held, state.forward, state.right);
    }

    if (held >= 0 && wanted >= 0) {
        state.lateralError += sinf(BotKeyboard_AngleDelta(held * 45.0f, wanted) * (3.14159265f / 180.0f)) * dt;
        if (state.lateralError > BOT_KEYBOARD_MAX_ERROR) {
            state.lateralError = BOT_KEYBOARD_MAX_ERROR;
        } else if (state.lateralError < -BOT_KEYBOARD_MAX_ERROR) {
            state.lateralError = -BOT_KEYBOARD_MAX_ERROR;
        }
    }

    forward = state.forward;
    right   = state.right;

    //
    // Jump / crouch key
    //
    const signed char upTarget = BotKeyboard_Axis(up);
    if (upTarget != state.up && timeMsec - state.upSince >= minHoldMsec) {
        state.up      = upTarget;
        state.upSince = timeMsec;
    }
    up = state.up;
}
