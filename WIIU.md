# Client3 for the Wii U (local build setup)

This is a build copy of `2004scape-LostCityRS/2004scape C Rewrite/Client3-main/`, the third-party rev-225 C99 client. It lives here because the devkitPro Wii U makefile fails on the space in "2004scape C Rewrite". The user chose Client3 for the Wii U on October 28, 2026.

## Changes from Client3-main
- `src/entry/client.c:185`: `archive_checksum[4]` (media) changed from `855958935` to `292972807`.
  - That is the CRC32 of the local server's `engine/data/pack/client/media`.
  - Without it, login fails with "RuneScape has been updated!".
  - On the Wii U, Client3 uses these hardcoded CRCs; there is no fetch of the server's CRC list.
- `rom/cache/client/*`: replaced with the server's `engine/data/pack/client/*`. Client3's bundled config, media, textures, title and some maps differ from the server's.
- `rom/config.ini` settings:
  - `socketip = 192.168.1.100`: the PC.
  - `portoff = 1`: server port 43595.
  - `lowmem = 0`.

- Sound, added October 28, 2026; untested on hardware so far:
  - `src/thirdparty/tsf.h`: SF2 values and 16-bit samples are byte-swapped on big-endian CPUs.
    - TSF read the little-endian `.sf2` fields as they were stored, which is why Client3's README says tinysoundfont "doesn't work on powerpc".
    - `genAmount`'s byte pair is reversed on big-endian CPUs, so `range.lo`/`hi` stay correct after the 16-bit swap.
    - Little-endian builds are unchanged.
  - `src/platform/sdl2.c`: the MIDI device is opened as `AUDIO_F32SYS` instead of `AUDIO_F32`. `AUDIO_F32` is little-endian float; TSF renders native floats.
  - Music confirmed playing on hardware (log `logs/run1-log.txt`). That log also showed `SDL_OpenAudioDevice(): Audio device already open`: the Wii U SDL driver allows one device, so sound effects never played.
  - Fix in `src/platform/sdl2.c`: when the wave device cannot open, each wave is converted (SDL_AudioCVT) to the MIDI format, float stereo 44100 Hz, and mixed in `midi_callback`.
    - It keeps Client3's rule: a new sound starts only when none is playing.
    - `wavBuffer`/`wavLength` are now initialised in case `SDL_LoadWAV_RW` fails.
  - Confirmed on hardware: music and sound effects both play (`logs/run2-log.txt`; the "already open" line is expected and handled).
- Log, added: on the Wii U, SDL_Log output (rs2_log/rs2_error) goes to `rom/log.txt` on the SD card, flushed per line. It includes an `audio:` line with the driver, soundfont, MIDI status and wave device.

## Build (PowerShell)
```
& "C:\devkitPro\msys2\usr\bin\bash.exe" -lc "export DEVKITPRO=/c/devkitPro DEVKITPPC=/c/devkitPro/devkitPPC; export PATH=/c/devkitPro/portlibs/wiiu/bin:/c/devkitPro/devkitPPC/bin:`$PATH; cd /c/Users/USER/Documents/client3-wiiu && make -f wiiu.mk APP_NAME=Client3 APP_SHORTNAME=Client3 APP_AUTHOR=Client3"
```
The output is `client.wuhb`. `out/client3.wuhb` is the build that ran on hardware (SHA256 `789da63b…`, music and sound effects working).

## Install (FTPiiU, Wii U at 192.168.1.139)
- `client.wuhb` goes to `/fs/vol/external01/wiiu/apps/client3-fps/client3-fps.wuhb` (this fork, "Client3 FPS"; the plain Client3 build is in `wiiu/apps/client3/`).
- `rom/` goes to `/fs/vol/external01/rom/` (the SD root). Client3 opens `rom/...` relative to the SD root.
- Escape spaces in FTP paths as `%20`. Some jingle names contain spaces.

## Controls (Client3 README)
- Touch: tap to click; B plus tap for right-click.
- D-pad: arrow keys.
- X: run toggle.
- Y: `::perf` fps display.

Hardware result, October 28, 2026: about 20 fps in game with `lowmem = 0`, by the user's estimate. RS2004-C reached 9.8 fps.
