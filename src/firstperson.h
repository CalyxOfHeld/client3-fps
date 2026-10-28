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
} FirstPerson;

extern FirstPerson _FirstPerson;

// Called once per client tick after the orbit camera update (input).
void firstperson_update(Client *c);
// Sets c->camera* for this frame and the per-frame visibility map.
void firstperson_camera(Client *c);
// Dev hooks (PC testing): auto-login and screenshots, from config.ini.
void firstperson_dev_update(Client *c);
void firstperson_dev_after_frame(Client *c);
