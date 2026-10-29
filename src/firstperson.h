#pragma once

#include <stdbool.h>

#include "client.h"

// First-person camera (fork addition, not part of the original client).
// _Custom.first_person switches between it and the original orbit camera.
typedef struct {
    int pitch;            // signed: 0 level, positive looks down (original convention), +-FP_PITCH_LIMIT
    int pitch_velocity;
    int frame;            // in-game frames drawn, for the dev capture
    bool visibility[51][51]; // per-frame tile visibility replacing the orbit tables

    // GamePad: sticks as SDL axis values (-32768..32767; 0 LX, 1 LY, 2 RX, 3 RY, y down)
    int axis[4];
    int pad_click;        // 1 left / 2 right: click at the crosshair after the next frame
    int walk_dx, walk_dz; // direction of the last stick walk (0, 0: not walking)
    int walk_tile_x, walk_tile_z, walk_cycle;
} FirstPerson;

extern FirstPerson _FirstPerson;

// Called once per client tick after the orbit camera update (input).
void firstperson_update(Client *c);
// Sets c->camera* for this frame and the per-frame visibility map.
void firstperson_camera(Client *c);
// Draws the crosshair into the viewport (before it is copied to the screen).
void firstperson_draw_crosshair(Client *c);
// GamePad events from the platform layer.
void firstperson_pad_axis(int axis, int value);
void firstperson_pad_click(Client *c, int button);
void firstperson_pad_release(Client *c);
void firstperson_screenshot(Client *c);
// Per frame after the screen update: pending pad clicks and the dev capture.
void firstperson_after_frame(Client *c);
// Dev hook (PC testing): auto-login from config.ini.
void firstperson_dev_update(Client *c);

// client.c: walk to a scene tile as a game click would (fork addition).
bool client_walk_to(Client *c, int tileX, int tileZ);
