// First-person camera (fork addition, not part of the original client).
#include "firstperson.h"

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
// Stick dead zone and full-tilt turn rates (2048ths of a turn per tick at full tilt).
#define FP_STICK_DEAD 8000
#define FP_STICK_YAW 28
#define FP_STICK_PITCH 18
// A stick walk targets this many tiles ahead and is re-sent as the player moves.
#define FP_WALK_AHEAD 2
#define FP_WALK_RESEND 30

FirstPerson _FirstPerson;

static int stick(int value) {
    if (value > -FP_STICK_DEAD && value < FP_STICK_DEAD) {
        return 0;
    }
    return value;
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
    // Snap to 8 directions (tan 22.5 degrees is about 0.414).
    int dx = abs(mx) * 1000 > abs(mz) * 414 ? (mx > 0 ? 1 : -1) : 0;
    int dz = abs(mz) * 1000 > abs(mx) * 414 ? (mz > 0 ? 1 : -1) : 0;
    int **flags = c->levelCollisionMap[c->currentLevel]->flags;
    int steps = 0;
    while (steps < FP_WALK_AHEAD && can_step(flags, tileX + dx * steps, tileZ + dz * steps, dx, dz)) {
        steps++;
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
        c->orbit_camera_yaw = c->orbit_camera_yaw - rx * FP_STICK_YAW / 32768 & 0x7ff;
        _FirstPerson.pitch += ry * FP_STICK_PITCH / 32768;
    }
    clamp_pitch();

    if (rx || ry || stick(_FirstPerson.axis[0]) || stick(_FirstPerson.axis[1])) {
        // pad play aims with the crosshair (a touch moves the mouse until the next stick use)
        mouse_to_crosshair(c);
    }
    stick_walk(c);
}

// world3d_test_point with this frame's eye angles and a margin around the viewport.
static bool point_visible(int x, int y, int z, int sinYaw, int cosYaw, int sinPitch, int cosPitch) {
    int px = (z * sinYaw + x * cosYaw) >> 16;
    int tmp = (z * cosYaw - x * sinYaw) >> 16;
    int pz = (y * sinPitch + tmp * cosPitch) >> 16;
    int py = (y * cosPitch - tmp * sinPitch) >> 16;
    if (pz < 50 || pz > 3500) {
        return false;
    }
    int vx = 256 + (px << 9) / pz;
    int vy = 167 + (py << 9) / pz;
    return vx >= -64 && vx <= 512 + 64 && vy >= -64 && vy <= 334 + 64;
}

static void build_visibility(Client *c, int eyeX, int eyeY, int eyeZ, int yaw, int pitch) {
    int sinYaw = _Pix3D.sin_table[yaw], cosYaw = _Pix3D.cos_table[yaw];
    int sinPitch = _Pix3D.sin_table[pitch], cosPitch = _Pix3D.cos_table[pitch];
    int eyeTileX = eyeX >> 7, eyeTileZ = eyeZ >> 7;
    static const int lift[] = {0, 256, 640, 1200};
    for (int dx = -25; dx <= 25; dx++) {
        for (int dz = -25; dz <= 25; dz++) {
            bool visible = abs(dx) <= FP_NEAR_TILES && abs(dz) <= FP_NEAR_TILES;
            int tx = eyeTileX + dx, tz = eyeTileZ + dz;
            for (int corner = 0; !visible && corner < 4; corner++) {
                int cx = tx + (corner & 1), cz = tz + (corner >> 1);
                if (cx < 0 || cz < 0 || cx > 104 || cz > 104) {
                    continue;
                }
                int ground = c->levelHeightmap[c->currentLevel][cx][cz] - eyeY;
                for (int i = 0; !visible && i < 4; i++) {
                    visible = point_visible((cx << 7) - eyeX, ground - lift[i], (cz << 7) - eyeZ, sinYaw, cosYaw, sinPitch, cosPitch);
                }
            }
            _FirstPerson.visibility[dx + 25][dz + 25] = visible;
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
    _FirstPerson.pad_click = button;
}

void firstperson_pad_release(Client *c) {
    c->shell->mouse_button = 0;
}

static void dev_capture(Client *c);

void firstperson_after_frame(Client *c) {
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
    // Retries every 15 s: after a dev run exits, the server holds the session for a while.
    static int next_try = 100;
    if (_Custom.dev_autologin == 1 && !c->ingame && _Client.loop_cycle > next_try) {
        next_try = _Client.loop_cycle + 750;
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
