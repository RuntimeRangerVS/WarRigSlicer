# WarRig Slicer

WarRig Slicer is QIDI Studio in the WarRig theme, with extra options that talk to WarRig firmware on the QIDI Q2.
It is part of the WarRig set: WarRig firmware/screen, WarRig Companion and WarRig Slicer.

Based on **QIDI Studio 02.07.02.73** (upstream `main`, November 2026). Everything QIDI Studio does, WarRig Slicer does the same way.

## What's different from QIDI Studio

- **WarRig theme**: dark steel and WarRig orange, WR badge icon, splash and About screen.
- **Own settings folder** (`%APPDATA%\WarRigSlicer`), so it never mixes with an installed QIDI Studio.
- **WarRig page in Process settings**:
  bed mesh before print, gantry levelling (Z tilt), reload + flush, plate Z offset, WarRig prime line, quiet mode.
- **"Printer runs WarRig" switch** in Printer settings > Basic information.
  When it is off, the WarRig page is locked and the stock start G-code is used, so files still print on stock firmware.
- **Q2 start G-code**: with WarRig on, `PRINT_START` also gets
  `PLATE= MESH= ZTILT= KEEP_LOAD= PLATE_Z= QUIET=`, and the stock prime lines are replaced with `WARRIG_PRIME`.
  Choices made on the printer screen for the next print still win over the slicer.

## Download

Windows builds are on the [Releases](../../releases) page. Each push to `main` also produces a test build under
**Actions > WarRig Slicer – Windows build > Artifacts**.

## Building on Windows

GitHub Actions builds it automatically (`.github/workflows/build-windows.yml`).
To build locally you need Visual Studio 2022, CMake 3.31, Git and Strawberry Perl, then run `build_win.bat`.
The first build compiles all dependencies and takes a few hours.

## Licence and credits

WarRig Slicer is licensed under the **GNU Affero General Public License v3** (see `LICENSE`), like the projects it is built on:

- [QIDI Studio](https://github.com/QIDITECH/QIDIStudio) by QIDI Technology
- Bambu Studio by Bambu Lab
- PrusaSlicer by Prusa Research
- Slic3r by Alessandro Ranellucci and the RepRap community

The original QIDI Studio readme is kept in `README_QIDIStudio.md`.
The WR logo lettering uses Barlow Condensed (SIL Open Font License).
