# Client3 FPS: Wii U notes

This repository is a fork of [Client3](https://github.com/lesleyrs/Client3), lesleyrs' revision-225 C99 client, for first-person play on the Wii U. It started on October 28, 2026.
- Commit `53c4cf8` is upstream Client3 (`lesleyrs/Client3` main, October 26, 2026).
- Commit `69a0c6e` is the Wii U setup, also published as the `wiiu` branch of [CalyxOfHeld/Client3](https://github.com/CalyxOfHeld/Client3).

Two paths are involved:
- `C:\Users\USER\Documents\client3-wiiu` is the plain Client3 Wii U build. It stays as it is.
- The repository path must have no spaces, because the devkitPro Wii U makefile fails on them.

## Changes from upstream Client3 (commit 69a0c6e)
- `src/entry/client.c:185`: `archive_checksum[4]` (media) changed from `855958935` to `292972807`.
  - That is the CRC32 of the local server's `engine/data/pack/client/media`.
  - Without it, login fails with "RuneScape has been updated!".
  - On the Wii U, Client3 uses these hardcoded CRCs; there is no fetch of the server's CRC list.
- `rom/cache/client/*`: replaced with the server's `engine/data/pack/client/*`. Client3's bundled config, media, textures, title and some maps differ from the server's.
- `rom/config.ini` settings:
  - `socketip = 192.168.1.100`: the PC.
  - `portoff = 1`: server port 43595.
  - `lowmem = 0`.

- Sound, added October 28, 2026; confirmed on hardware:
  - `src/thirdparty/tsf.h`: SF2 values and 16-bit samples are byte-swapped on big-endian CPUs.
    - TSF read the little-endian `.sf2` fields as they were stored, which is why Client3's README says tinysoundfont "doesn't work on powerpc".
    - `genAmount`'s byte pair is reversed on big-endian CPUs, so `range.lo`/`hi` stay correct after the 16-bit swap.
    - Little-endian builds are unchanged.
  - `src/platform/sdl2.c`: the MIDI device is opened as `AUDIO_F32SYS` instead of `AUDIO_F32`. `AUDIO_F32` is little-endian float; TSF renders native floats.
  - Music confirmed playing on hardware (log `wiiu-logs/run1-log.txt`). That log also showed `SDL_OpenAudioDevice(): Audio device already open`: the Wii U SDL driver allows one device, so sound effects never played.
  - Fix in `src/platform/sdl2.c`: when the wave device cannot open, each wave is converted (SDL_AudioCVT) to the MIDI format, float stereo 44100 Hz, and mixed in `midi_callback`.
    - It keeps Client3's rule: a new sound starts only when none is playing.
    - `wavBuffer`/`wavLength` are now initialised in case `SDL_LoadWAV_RW` fails.
  - Confirmed on hardware: music and sound effects both play (`wiiu-logs/run2-log.txt`; the "already open" line is expected and handled).
- Log, added: on the Wii U, SDL_Log output (rs2_log/rs2_error) goes to `rom/log.txt` on the SD card, flushed per line. It includes an `audio:` line with the driver, soundfont, MIDI status and wave device.

## Build (PowerShell)
```
& "C:\devkitPro\msys2\usr\bin\bash.exe" -lc "export DEVKITPRO=/c/devkitPro DEVKITPPC=/c/devkitPro/devkitPPC; export PATH=/c/devkitPro/portlibs/wiiu/bin:/c/devkitPro/devkitPPC/bin:`$PATH; cd /c/Users/USER/Documents/client3-fps && make -f wiiu.mk -j8"
```
The output is `client.wuhb`. The app is named "Client3 FPS" in `wiiu.mk`.

## Install (FTPiiU, Wii U at 192.168.1.139)
- `client.wuhb` goes to `/fs/vol/external01/wiiu/apps/client3-fps/client3-fps.wuhb` (this fork, "Client3 FPS"; the plain Client3 build is in `wiiu/apps/client3/`).
- `rom/` goes to `/fs/vol/external01/rom/` (the SD root). Client3 opens `rom/...` relative to the SD root.
- Escape spaces in FTP paths as `%20`. Some jingle names contain spaces.

## Controls (Client3 README)
- Touch: tap to click; B plus tap for right-click.
- D-pad: arrow keys.
- X: run toggle.
- Y: `::perf` fps display.

## First-person controls (this fork; `first_person = 1` in `rom/config.ini`)
- Left stick: walk in 8 directions relative to the view, in a straight line (3 tiles ahead, re-sent as you move). The direction holds while the stick stays within about 35 degrees of it; a blocked diagonal slides along the wall. It never paths around walls.
- Right stick: look around (up and down limited to about 70 degrees). The turn rate follows the square of the tilt: slow near the centre, fast at full tilt. Stick input counts as activity for the idle logout.
- ZR: left click at the crosshair. ZL: right click (opens the menu at the crosshair); pick the option by touch.
- Minus (−): switch between first-person and the original camera.
- D-pad: left and right turn the view, up and down look up and down. Touch works as before; the next stick movement moves the pointer back to the crosshair.
- On PC: the arrow keys do the same; type `::fp` to toggle (needs `allow_commands = 1`).
- The hardware log (`rom/log.txt`) prints `pad axis N first moved` once per stick axis, to confirm the axis numbering (0 LX, 1 LY, 2 RX, 3 RY assumed).

Hardware result, October 28, 2026: about 20 fps in game with `lowmem = 0`, by the user's estimate. RS2004-C reached 9.8 fps.

## Logs (this fork)
- `rom/log.txt` is the current run; `rom/log-prev.txt` keeps the run before it (so a crash's log survives a relaunch).
- Every 30 s in game: a `beat:` line (fps, tile, camera). Logouts print a `logout:` line with the cause.
- Hardware run 3 (`wiiu-logs/run3-fps-log.txt`): axes confirmed 0 LX, 1 LY, 2 RX, 3 RY. The user reported rough controls and one crash. One cause found: stick-only play never reset the idle timer, so the client's 90 s idle logout ended the session.
- Hardware run 4 (`wiiu-logs/run4-log.txt`): crashed within 30 s of login, with no cause in the log. Reproduced on PC with random input (`dev_wander` plus arrow keys): `model_draw2` indexed the face depth buckets below 0. `min_depth` only allows for the part of a model below its origin, because the original camera always looks down. Looking up in first person (D-pad up or the right stick) puts higher parts of tall models out of range. Fixed in `model.c` (`model_depth_bucket`).
- The server logged out stick-only sessions after about 3.5 minutes. Its input-tracking (AFK) report was empty, because the sticks are not mouse input. Crosshair moves and pad clicks are now reported to input tracking.
- `out/client-<hash>.elf` keeps the symbols for each uploaded build, for looking up crash addresses.
- Hardware run 5 (`wiiu-logs/run5-log.txt`): no crash, 22–32 fps. The user reported holes in the ground when looking down on slopes, walls and buildings that could be seen through up close, and stretched or smeared surfaces. Three causes, all fixed for first person:
  - Ground (`world3d.c`): a tile was skipped whole when any corner was behind the camera's near plane. Ground triangles are now cut at the near plane and drawn (`ground_triangle_clipped`).
  - Visibility (`model_draw`): the bounds that decide whether a model is drawn assume the overhead camera, so a nearby wall could be skipped while part of it was on screen. First person uses a sphere around the whole model.
  - Smears (`model_draw_near_clipped_face`): faces that cross behind the camera were cut using screen positions instead of 3D positions. Upstream Client3 has the same bug; the overhead camera seldom hits it.
  - Dev settings used to check this on PC: `dev_eye_x`/`dev_eye_z` (camera on a chosen tile), `dev_capture_pitch`, `dev_nofix = 1` (turns off the first two fixes for comparison).
- After run 5 the user reported objects seen through walls from any distance, and odd views when standing against a wall and turning. Both reproduced on PC; neither came from the run 5 fixes (same with `dev_nofix = 1`). Fixed for first person:
  - Draw order (`world3d.c`): the scene is painted far to near, but nothing made a ground-floor tile wait for the upper floor behind it. At eye level the ground-floor wall was drawn first and the upstairs beams and furniture painted over it. Front walls now wait for the higher levels beyond them, and an upper tile waits for the lower levels beyond it instead of the tile under it (the old wait formed a loop with objects covering several tiles; found by dumping a stuck frame).
  - Near plane (`model_near_plane`): 20 instead of 50. A wall on the edge of your tile is about 45 units from the eye, so its face was cut away whole (a black screen, or the inside of a window frame).
- Screenshot button: A in first person saves `rom/shot-N.bmp` and logs `shot ...` with the camera and world tile, so a view seen on hardware can be rebuilt on PC. `dev_eye_x/z` also accept scene units (>= 128) and `dev_capture_turn` sets the turn between captures.
