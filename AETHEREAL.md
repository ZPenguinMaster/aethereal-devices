# Aethereal Device Service

This is a **modified version of [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB)** (release 1.0), made for the
Aethereal PC app. It isn't OpenRGB and isn't made or supported by the OpenRGB project. All of OpenRGB's device
support is the work of its authors; thank you.

It's OpenRGB's device drivers and its SDK server, run in the background with no window. The Aethereal PC app talks
to it over the SDK on this PC (127.0.0.1), the same way any OpenRGB SDK client does. The app is a separate program:
it doesn't contain or link any of this code.

## Licence

GPL, version 2 or later, as OpenRGB is: see [LICENSE](LICENSE). Every file keeps its copyright notice. Anyone may
use, change and share this, under the same licence. Each release on this repository has its exact source at its
tag and attached to the release.

## What's changed from OpenRGB 1.0

Listed with dates, as the GPL asks. The device drivers (`Controllers/`) are left unchanged, so new devices from
OpenRGB come in with a plain merge.

- **2026-10-15:** a build of its own, `CMakeLists.txt`, with no Qt. It takes OpenRGB's core (detection, the SDK
  server, settings, logging) and every driver, and leaves out the window (`qt/`, `startup/`), plugins
  (`PluginManager`, `OpenRGBPluginAPI`) and the Qt-only parts (`SuspendResume`, `ColorWheel`). OpenRGB's own build,
  `OpenRGB.pro`, is still there and still builds OpenRGB.
- **2026-10-15:** its own starting point, `aethereal/service_main.cpp`, based on OpenRGB's `startup/main_Windows.cpp`
  and `startup/main_FreeBSD_Linux_MacOS.cpp`:
  - the SDK server only, on 127.0.0.1 port **6743** (not OpenRGB's 6742, so both can run on one PC);
  - its own settings folder (`%APPDATA%\Aethereal\devices` by default, or `--config`), never OpenRGB's;
  - runs as a Windows service named `AetherealDevices`, or as a program with no window;
  - `--parent PID`: stops when the program that started it ends;
  - `--no-smbus`: leaves the SMBus alone (below);
  - the server's name is "Aethereal Device Service".
- **2026-10-15:** `DetectionManager.cpp`: a `smbus_disabled` setting under `Detectors`. When it's on, no I2C/SMBus
  interface or device is looked for or touched (RAM lighting, and some older motherboards). Small enough to offer
  back to OpenRGB.
- **2026-10-15:** OpenRGB's GitHub greeting workflows (for its mirror) removed, and a build of our own added
  (`.github/workflows/build.yml`).
- **2026-10-16:** `aethereal/service_main.cpp`: only one runs per PC. Two would drive the same devices and fight over
  them, and Windows lets both listen on one port. A second one stops before it looks for any device, with exit
  code 3. The lock is the `Global\AetherealDevices` mutex on Windows (shared by the service and a copy the app starts),
  and a `flock` on `$XDG_RUNTIME_DIR/aethereal-devices.lock` elsewhere.

## Building

Windows (64-bit, Visual Studio 2022 and CMake):

```
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
cmake --install build --config Release --prefix dist
```

`dist\` then has `aethereal-devices.exe`, the libraries it needs (libusb, hidapi, PawnIOLib) and PawnIO's modules.
SMBus devices (RAM) need the [PawnIO](https://pawnio.eu) driver installed and administrator rights, as in OpenRGB.

Linux (libusb, hidapi and mbedtls 3):

```
cmake -S . -B build [-DMBEDTLS_ROOT=/where/mbedtls3/is]
cmake --build build
```

## Running

```
aethereal-devices [--port 6743] [--config DIR] [--no-smbus] [--parent PID] [-v]
```

If one is already running on the PC (as the service, or started by the app), a new one stops at once with exit code 3.

As a Windows service (from an administrator prompt; the Aethereal installer does this):

```
sc create AetherealDevices binPath= "\"C:\path\aethereal-devices.exe\" --config \"C:\ProgramData\Aethereal\devices\"" start= auto DisplayName= "Aethereal Device Service"
sc start AetherealDevices
```

## Keeping up with OpenRGB

```
git remote add openrgb https://gitlab.com/CalcProgrammer1/OpenRGB.git
git fetch openrgb --tags
git merge release_1.1        # or the next release
```

Then build, and check that `CMakeLists.txt` still lists everything OpenRGB's core needs (a new core file shows up as a
link error). Fixes to drivers go to OpenRGB first.
