// First-person camera (fork addition, not part of the original client).
#include "firstperson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "custom.h"
#include "gameshell.h"
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

FirstPerson _FirstPerson;

void firstperson_update(Client *c) {
    // Yaw stays the orbit camera's (left/right keys); up/down look up/down here.
    if (c->shell->action_key[3] == 1) {
        _FirstPerson.pitch_velocity += (-_FirstPerson.pitch_velocity - 16) / 2;
    } else if (c->shell->action_key[4] == 1) {
        _FirstPerson.pitch_velocity += (16 - _FirstPerson.pitch_velocity) / 2;
    } else {
        _FirstPerson.pitch_velocity /= 2;
    }
    _FirstPerson.pitch += _FirstPerson.pitch_velocity / 2;
    if (_FirstPerson.pitch < -FP_PITCH_LIMIT) {
        _FirstPerson.pitch = -FP_PITCH_LIMIT;
    }
    if (_FirstPerson.pitch > FP_PITCH_LIMIT) {
        _FirstPerson.pitch = FP_PITCH_LIMIT;
    }
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

// Dev hooks. config.ini: dev_autologin = 1 logs in with the configured
// username and CLIENT3_DEV_PASSWORD (or the configured password); dev_capture_frame = N saves fps-capture-<i>.bmp at
// in-game frame N and every 40 frames after, dev_capture_count times, turning
// the view a quarter turn between captures, then exits.
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

void firstperson_dev_after_frame(Client *c) {
    if (_Custom.dev_capture_frame <= 0) {
        return;
    }
    fflush(stdout);
    if (!c->ingame) {
#if SDL == 2
        // one title-screen capture, to see why a login did not happen
        static int title_frames;
        if (++title_frames == 400) {
            platform_dev_capture("fps-capture-title.bmp");
        }
#endif
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
    rs2_log("dev capture %s: camera=(%d,%d,%d) yaw=%d pitch=%d first_person=%d\n", path, c->cameraX, c->cameraY, c->cameraZ, c->cameraYaw, c->cameraPitch, _Custom.first_person);
#endif
    if (i + 1 >= count) {
        exit(0);
    }
    // Four quarter turns level, then down and up at the pitch limits.
    if (i < 3) {
        c->orbit_camera_yaw = c->orbit_camera_yaw + 512 & 0x7ff;
    } else {
        _FirstPerson.pitch = i == 3 ? FP_PITCH_LIMIT : -FP_PITCH_LIMIT;
    }
}
