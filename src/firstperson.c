// First-person camera (fork addition, not part of the original client).
#include "firstperson.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "collisionmap.h"
#include "custom.h"
#include "gameshell.h"
#include "pix2d.h"
#include "pix3d.h"
#include "platform.h"
#include "world3d.h"

extern Custom _Custom;
extern ClientData _Client;
extern Pix3D _Pix3D;
extern SceneData _World3D;

// Eye height above the ground under the player, in scene units (a tile is 128).
#define FP_EYE_HEIGHT 176
// Pitch limit, in 2048ths of a turn: 400 is about 70 degrees up or down.
#define FP_PITCH_LIMIT 400
// Tiles this close to the eye are always drawn: their corners can sit behind
// the eye or outside the view while the tile itself still fills it.
#define FP_NEAR_TILES 2
// The crosshair is the viewport centre; the viewport is drawn at (8, 11).
#define FP_CROSS_X 256
#define FP_CROSS_Y 167
#define FP_VIEWPORT_X 8
#define FP_VIEWPORT_Y 11
// Stick dead zone and full-tilt turn rates (2048ths of a turn per 20 ms tick).
// The rate follows the square of the tilt: fine aim near the centre, fast turns at full tilt.
#define FP_STICK_DEAD 7000
#define FP_STICK_YAW 36
#define FP_STICK_PITCH 22
// A stick walk targets this many tiles ahead and is re-sent as the player moves.
#define FP_WALK_AHEAD 3
#define FP_WALK_RESEND 30
// A walk keeps its direction while the stick stays within about 35 degrees of it
// (cos 35 = 0.819), so turning the view a little does not flip straight/diagonal.
#define FP_WALK_KEEP 0.819f

FirstPerson _FirstPerson;

static int stick(int value) {
    if (value > -FP_STICK_DEAD && value < FP_STICK_DEAD) {
        return 0;
    }
    return value;
}

// Turn rate for a stick value: 0 inside the dead zone, rising with the square of the tilt to max.
static int stick_rate(int value, int max) {
    int mag = abs(stick(value));
    if (!mag) {
        return 0;
    }
    float t = (float)(mag - FP_STICK_DEAD) / (float)(32767 - FP_STICK_DEAD);
    if (t > 1.0f) {
        t = 1.0f;
    }
    int rate = (int)(t * t * max + 0.5f);
    if (rate < 1) {
        rate = 1;
    }
    return value < 0 ? -rate : rate;
}

static void clamp_pitch(void) {
    if (_FirstPerson.pitch < -FP_PITCH_LIMIT) {
        _FirstPerson.pitch = -FP_PITCH_LIMIT;
    }
    if (_FirstPerson.pitch > FP_PITCH_LIMIT) {
        _FirstPerson.pitch = FP_PITCH_LIMIT;
    }
}

static void mouse_to_crosshair(Client *c) {
    c->shell->mouse_x = FP_VIEWPORT_X + FP_CROSS_X;
    c->shell->mouse_y = FP_VIEWPORT_Y + FP_CROSS_Y;
}

// One step from (x, z) by (dx, dz) is open: the collision masks client_try_move uses.
static bool can_step(int **flags, int x, int z, int dx, int dz) {
    int tx = x + dx, tz = z + dz;
    if (tx < 1 || tz < 1 || tx > 102 || tz > 102) {
        return false;
    }
    if (dx == -1 && (flags[x - 1][z] & 0x280108)) {
        return false;
    }
    if (dx == 1 && (flags[x + 1][z] & 0x280180)) {
        return false;
    }
    if (dz == -1 && (flags[x][z - 1] & 0x280102)) {
        return false;
    }
    if (dz == 1 && (flags[x][z + 1] & 0x280120)) {
        return false;
    }
    if (dx && dz) {
        static const int diagonal[2][2] = {{0x28010E, 0x280138}, {0x280183, 0x2801E0}};
        if (flags[tx][tz] & diagonal[dx > 0][dz > 0]) {
            return false;
        }
    }
    return true;
}

// Left stick: walk one direction (8-way, relative to the view) while held, in a
// straight line only (a click's path would route around walls, through doors).
static void stick_walk(Client *c) {
    int lx = stick(_FirstPerson.axis[0]), ly = stick(_FirstPerson.axis[1]);
    // the path source client_walk_to uses (the newest tile the server has moved us to)
    int tileX = c->local_player->pathing_entity.pathTileX[0];
    int tileZ = c->local_player->pathing_entity.pathTileZ[0];
    if (!lx && !ly) {
        if (_FirstPerson.walk_dx || _FirstPerson.walk_dz) {
            // released: stop where the server has us now instead of at the target ahead
            int visualX = c->local_player->pathing_entity.x >> 7, visualZ = c->local_player->pathing_entity.z >> 7;
            if (visualX != _FirstPerson.walk_tile_x || visualZ != _FirstPerson.walk_tile_z) {
                client_walk_to(c, tileX, tileZ);
            }
            _FirstPerson.walk_dx = _FirstPerson.walk_dz = 0;
        }
        return;
    }
    // View yaw 0 faces north (+z) and 512 faces west: forward (-sin, cos), right (cos, sin).
    int yaw = c->orbit_camera_yaw + c->camera_anticheat_angle & 0x7ff;
    int sinYaw = _Pix3D.sin_table[yaw], cosYaw = _Pix3D.cos_table[yaw];
    int forward = -ly, right = lx;
    int mx = (int)(((long long)forward * -sinYaw + (long long)right * cosYaw) >> 16);
    int mz = (int)(((long long)forward * cosYaw + (long long)right * sinYaw) >> 16);
    // Snap to 8 directions (tan 22.5 degrees is about 0.414), keeping the current one
    // while the stick stays close to it.
    int dx = abs(mx) * 1000 > abs(mz) * 414 ? (mx > 0 ? 1 : -1) : 0;
    int dz = abs(mz) * 1000 > abs(mx) * 414 ? (mz > 0 ? 1 : -1) : 0;
    if (_FirstPerson.walk_dx || _FirstPerson.walk_dz) {
        float wx = (float)_FirstPerson.walk_dx, wz = (float)_FirstPerson.walk_dz;
        float dot = (float)mx * wx + (float)mz * wz;
        float len = sqrtf((float)mx * mx + (float)mz * mz) * sqrtf(wx * wx + wz * wz);
        if (len > 0.0f && dot > FP_WALK_KEEP * len) {
            dx = _FirstPerson.walk_dx;
            dz = _FirstPerson.walk_dz;
        }
    }
    int **flags = c->levelCollisionMap[c->currentLevel]->flags;
    int steps = 0;
    while (steps < FP_WALK_AHEAD && can_step(flags, tileX + dx * steps, tileZ + dz * steps, dx, dz)) {
        steps++;
    }
    if (steps == 0 && dx && dz) {
        // diagonal blocked (a wall or a corner): slide along it, the stick's larger axis first
        int first = abs(mx) >= abs(mz) ? 0 : 1;
        for (int i = 0; i < 2 && steps == 0; i++) {
            int sx = (i == first) ? dx : 0, sz = (i == first) ? 0 : dz;
            while (steps < FP_WALK_AHEAD && can_step(flags, tileX + sx * steps, tileZ + sz * steps, sx, sz)) {
                steps++;
            }
            if (steps) {
                dx = sx;
                dz = sz;
            }
        }
    }
    int targetX = tileX + dx * steps, targetZ = tileZ + dz * steps;
    if (dx == _FirstPerson.walk_dx && dz == _FirstPerson.walk_dz && targetX == _FirstPerson.walk_tile_x && targetZ == _FirstPerson.walk_tile_z && _Client.loop_cycle - _FirstPerson.walk_cycle < FP_WALK_RESEND) {
        return;
    }
    _FirstPerson.walk_dx = dx;
    _FirstPerson.walk_dz = dz;
    _FirstPerson.walk_tile_x = targetX;
    _FirstPerson.walk_tile_z = targetZ;
    _FirstPerson.walk_cycle = _Client.loop_cycle;
    if (steps > 0) {
        client_walk_to(c, targetX, targetZ);
    }
}

// Dev: dev_wander = S drives random stick input, crosshair clicks and camera
// toggles for S seconds in game, then exits (a crash test for PC debug builds).
static void dev_wander(Client *c) {
    static int ticks, next_change, next_click, next_toggle = 1000;
    c->shell->idle_cycles = 0; // the test also runs with the orbit camera, where the sticks are not input
    ticks++;
    if (ticks >= _Custom.dev_wander * 50) {
        rs2_log("dev wander: done after %d ticks, tile=(%d,%d) first_person=%d\n", ticks, c->local_player->pathing_entity.x >> 7, c->local_player->pathing_entity.z >> 7, _Custom.first_person);
        fflush(stdout);
        exit(0);
    }
    if (ticks >= next_change) {
        next_change = ticks + 40 + rand() % 120;
        for (int i = 0; i < 4; i++) {
            _FirstPerson.axis[i] = rand() % 3 == 0 ? 0 : rand() % 65535 - 32767;
        }
    }
    if (ticks >= next_click) {
        next_click = ticks + 100 + rand() % 300;
        firstperson_pad_click(c, rand() % 4 == 0 ? 2 : 1);
    }
    if (ticks >= next_toggle) {
        next_toggle = ticks + 1000 + rand() % 1500;
        _Custom.first_person = !_Custom.first_person;
        rs2_log("dev wander: tick %d first_person=%d tile=(%d,%d)\n", ticks, _Custom.first_person, c->local_player->pathing_entity.x >> 7, c->local_player->pathing_entity.z >> 7);
    }
}

void firstperson_update(Client *c) {
    // Up/down keys look up/down; the yaw is the orbit camera's (left/right keys).
    if (c->shell->action_key[3] == 1) {
        _FirstPerson.pitch_velocity += (-_FirstPerson.pitch_velocity - 16) / 2;
    } else if (c->shell->action_key[4] == 1) {
        _FirstPerson.pitch_velocity += (16 - _FirstPerson.pitch_velocity) / 2;
    } else {
        _FirstPerson.pitch_velocity /= 2;
    }
    _FirstPerson.pitch += _FirstPerson.pitch_velocity / 2;

    // Dev: hold the left stick forward (dev_walk > 0) or back (< 0) for |dev_walk| ticks from the first capture.
    if (_Custom.dev_walk && _Custom.dev_capture_frame > 0) {
        int f = _FirstPerson.frame - _Custom.dev_capture_frame;
        _FirstPerson.axis[1] = f >= 0 && f < abs(_Custom.dev_walk) ? (_Custom.dev_walk > 0 ? -32767 : 32767) : 0;
    }

    // Right stick looks (right turns right, down looks down).
    int rx = stick(_FirstPerson.axis[2]), ry = stick(_FirstPerson.axis[3]);
    if (rx || ry) {
        c->orbit_camera_yaw = c->orbit_camera_yaw - stick_rate(rx, FP_STICK_YAW) & 0x7ff;
        _FirstPerson.pitch += stick_rate(ry, FP_STICK_PITCH);
    }
    clamp_pitch();

    if (rx || ry || stick(_FirstPerson.axis[0]) || stick(_FirstPerson.axis[1])) {
        // pad play aims with the crosshair (a touch moves the mouse until the next stick use)
        mouse_to_crosshair(c);
        // stick play is activity: without this the client's idle logout (90 s without
        // mouse or key input) ends a stick-only session
        c->shell->idle_cycles = 0;
    }
    stick_walk(c);
}

// Which sides of the view (with a margin) a point lies beyond, as bits; 0 is inside.
// The projection is world3d_test_point's with this frame's eye angles.
enum { OUT_LEFT = 1, OUT_RIGHT = 2, OUT_TOP = 4, OUT_BOTTOM = 8, OUT_NEAR = 16, OUT_FAR = 32 };
static int outcode(int x, int y, int z, int sinYaw, int cosYaw, int sinPitch, int cosPitch) {
    int px = (z * sinYaw + x * cosYaw) >> 16;
    int tmp = (z * cosYaw - x * sinYaw) >> 16;
    int pz = (y * sinPitch + tmp * cosPitch) >> 16;
    int py = (y * cosPitch - tmp * sinPitch) >> 16;
    if (pz < 50) {
        return OUT_NEAR;
    }
    if (pz > 3500) {
        return OUT_FAR;
    }
    int vx = 256 + (px << 9) / pz;
    int vy = 167 + (py << 9) / pz;
    return (vx < -64 ? OUT_LEFT : 0) | (vx > 512 + 64 ? OUT_RIGHT : 0) | (vy < -64 ? OUT_TOP : 0) | (vy > 334 + 64 ? OUT_BOTTOM : 0);
}

// A tile is drawn unless the box from its ground up to FP_TILE_TOP lies wholly
// beyond one side of the view: each of its 8 corners shares an outcode bit.
// Corners are shared between tiles, so each is projected once (52 x 52 x 2).
#define FP_TILE_TOP 1200
static void build_visibility(Client *c, int eyeX, int eyeY, int eyeZ, int yaw, int pitch) {
    int sinYaw = _Pix3D.sin_table[yaw], cosYaw = _Pix3D.cos_table[yaw];
    int sinPitch = _Pix3D.sin_table[pitch], cosPitch = _Pix3D.cos_table[pitch];
    int eyeTileX = eyeX >> 7, eyeTileZ = eyeZ >> 7;
    static int corner[52][52];
    for (int i = 0; i < 52; i++) {
        for (int j = 0; j < 52; j++) {
            int cx = eyeTileX - 25 + i, cz = eyeTileZ - 25 + j;
            int hx = cx < 0 ? 0 : cx > 104 ? 104 : cx, hz = cz < 0 ? 0 : cz > 104 ? 104 : cz;
            int ground = c->levelHeightmap[c->currentLevel][hx][hz] - eyeY;
            int x = (cx << 7) - eyeX, z = (cz << 7) - eyeZ;
            corner[i][j] = outcode(x, ground, z, sinYaw, cosYaw, sinPitch, cosPitch) & outcode(x, ground - FP_TILE_TOP, z, sinYaw, cosYaw, sinPitch, cosPitch);
        }
    }
    for (int dx = 0; dx < 51; dx++) {
        for (int dz = 0; dz < 51; dz++) {
            bool near = abs(dx - 25) <= FP_NEAR_TILES && abs(dz - 25) <= FP_NEAR_TILES;
            int out = corner[dx][dz] & corner[dx + 1][dz] & corner[dx][dz + 1] & corner[dx + 1][dz + 1];
            _FirstPerson.visibility[dx][dz] = near || out == 0;
        }
    }
}

void firstperson_camera(Client *c) {
    int x = c->local_player->pathing_entity.x;
    int z = c->local_player->pathing_entity.z;
    c->cameraX = x;
    c->cameraZ = z;
    c->cameraY = getHeightmapY(c, c->currentLevel, x, z) - FP_EYE_HEIGHT;
    c->cameraYaw = c->orbit_camera_yaw + c->camera_anticheat_angle & 0x7ff;
    c->cameraPitch = _FirstPerson.pitch & 0x7ff;
    build_visibility(c, c->cameraX, c->cameraY, c->cameraZ, c->cameraYaw, c->cameraPitch);
    // The orbit tables only cover pitches 128..383; world3d_draw uses this map instead.
    _World3D.customVisibilityMap = _FirstPerson.visibility;
}

void firstperson_draw_crosshair(Client *c) {
    (void)c;
    int x = FP_CROSS_X, y = FP_CROSS_Y;
    // white plus with a dark outline, readable on sky and on bright walls
    pix2d_fill_rect(x - 6, y - 1, 0x000000, 13, 3);
    pix2d_fill_rect(x - 1, y - 6, 0x000000, 3, 13);
    pix2d_hline(x - 5, y, 0xffffff, 11);
    pix2d_vline(x, y - 5, 0xffffff, 11);
}

void firstperson_pad_axis(int axis, int value) {
    // first event per axis, to confirm the numbering in the hardware log
    static bool seen[8];
    if (axis >= 0 && axis < 8 && !seen[axis] && (value > 16000 || value < -16000)) {
        seen[axis] = true;
        rs2_log("pad axis %d first moved: %d\n", axis, value);
    }
    if (axis >= 0 && axis < 4) {
        _FirstPerson.axis[axis] = value;
    }
}

// The client builds the menu while drawing, from the mouse position, so the
// mouse moves to the crosshair now and the click lands after the next frame.
void firstperson_pad_click(Client *c, int button) {
    mouse_to_crosshair(c);
    c->shell->idle_cycles = 0;
    _FirstPerson.pad_click = button;
}

void firstperson_pad_release(Client *c) {
    c->shell->mouse_button = 0;
}

static void dev_capture(Client *c);

void firstperson_after_frame(Client *c) {
    // heartbeat for hardware logs: what the client was doing before a crash, and the fps
    static int last_beat;
    if (c->ingame && _Client.loop_cycle - last_beat >= 1500) {
        last_beat = _Client.loop_cycle;
        rs2_log("beat: cycle=%d fps=%d first_person=%d tile=(%d,%d) level=%d yaw=%d pitch=%d\n", _Client.loop_cycle, c->shell->fps, _Custom.first_person,
            c->local_player->pathing_entity.x >> 7, c->local_player->pathing_entity.z >> 7, c->currentLevel, c->orbit_camera_yaw, _FirstPerson.pitch);
    }
    if (_FirstPerson.pad_click) {
        update_touch = true;
        last_touch_x = c->shell->mouse_x;
        last_touch_y = c->shell->mouse_y;
        last_touch_button = _FirstPerson.pad_click;
        c->shell->mouse_button = _FirstPerson.pad_click;
        _FirstPerson.pad_click = 0;
    }
    dev_capture(c);
}

// Dev hooks. config.ini: dev_autologin = 1 logs in with the configured
// username and CLIENT3_DEV_PASSWORD (or the configured password); dev_capture_frame = N
// saves fps-capture-<i>.bmp at in-game frame N and every 40 frames after,
// dev_capture_count times (turning a quarter turn between the first four, then
// looking down and up), then exits; dev_walk = T holds the left stick forward (back if negative)
// for T ticks from the first capture.
void firstperson_dev_update(Client *c) {
    if (_Custom.dev_wander > 0 && c->ingame) {
        dev_wander(c);
    }
    // Retries every 15 s: after a dev run exits, the server holds the session for a while.
    static int next_try = 100;
    if (_Custom.dev_autologin == 1 && !c->ingame && _Client.loop_cycle > next_try) {
        next_try = _Client.loop_cycle + 750;
        // A logout clears the username and password: keep the configured username.
        static char username[sizeof(c->username)];
        if (!username[0]) {
            strcpy(username, c->username);
        }
        strcpy(c->username, username);
        // The test password comes from the environment so it stays out of config.ini.
        const char *password = getenv("CLIENT3_DEV_PASSWORD");
        if (password) {
            strncpy(c->password, password, sizeof(c->password) - 1);
        }
        client_login(c, c->username, c->password, false);
        rs2_log("dev autologin: ingame=%d msg0='%s' msg1='%s'\n", c->ingame, c->login_message0, c->login_message1);
    }
}

static void dev_capture(Client *c) {
    if (_Custom.dev_capture_frame <= 0) {
        return;
    }
    fflush(stdout);
    if (!c->ingame) {
        return;
    }
    int f = _FirstPerson.frame++ - _Custom.dev_capture_frame;
    int count = _Custom.dev_capture_count > 0 ? _Custom.dev_capture_count : 1;
    if (f < 0 || f % 40 != 0) {
        return;
    }
    int i = f / 40;
#if SDL == 2
    char path[64];
    snprintf(path, sizeof(path), "fps-capture-%d.bmp", i);
    platform_dev_capture(path);
    rs2_log("dev capture %s: camera=(%d,%d,%d) yaw=%d pitch=%d first_person=%d tile=(%d,%d)\n", path, c->cameraX, c->cameraY, c->cameraZ, c->cameraYaw, c->cameraPitch, _Custom.first_person, c->local_player->pathing_entity.x >> 7, c->local_player->pathing_entity.z >> 7);
#endif
    if (i + 1 >= count) {
        exit(0);
    }
    if (_Custom.dev_walk) {
        return; // walking test: keep the view still
    }
    // Four quarter turns level, then down and up at the pitch limits.
    if (i < 3) {
        c->orbit_camera_yaw = c->orbit_camera_yaw + 512 & 0x7ff;
    } else {
        _FirstPerson.pitch = i == 3 ? FP_PITCH_LIMIT : -FP_PITCH_LIMIT;
    }
}
