# Descent 3 Pyra: upstream commits 76049bb, 9522300, 66a0d07

Base: Performance 9, Rumble-Fix 2 (SDL3, native PowerVR GLES2).

## Commit integration status

* **76049bb — startup crash:** Adopted the two-stage `pilot` construction/initialization, safe default member initialization, `Current_pilot.initialize()` after CLI processing, removal of unused restart state, and resolution ordering fixes. The SDL3 shutdown/Rumble lifecycle already present in the Pyra branch is preserved.
* **9522300 — string expressions and casts:** Integrated filesystem UTF-8 conversion (`PATH_TO_CSTR`), corresponding `logger` declaration, and relevant Linux runtime string and memory-copy fixes, including OSIRIS, missions, pilots, HOG paths, menus, network/file access and level loading. Some upstream editor-only, Windows-only, and non-Pyra tooling edits are deliberately not carried across, because the Pyra build does not compile these targets.
* **66a0d07 — merge branch main into cast:** Reconciled against the Pyra base; its relevant changes were **already present**: `SDL3 >= 3.2.6`, 32-bit/ARM-safe vector operations and signed index fixes, mission command-line initialization, index validation for saved resolutions, mouse-motion updates, HOG/level fixes and GLES2 renderer changes. No blanket replacement of the working Pyra PowerVR renderer was performed.

The three merge SHAs are **upstream references**, not new commits in this unpacked ZIP. This is a source-level selective integration, not a clean Git cherry-pick. The upstream patches contain editor/other-platform edits which are not all copied to the Pyra port.

## Verification

* C++20 host syntax validation completed for 16 relevant engine source files, including `Descent3/sdlmain.cpp`, `config.cpp`, `pilot_class.cpp`, `Mission.cpp`, `OsirisLoadandBind.cpp`, `Inventory.cpp`, `LoadLevel.cpp`, `game.cpp`, `gamesequence.cpp`, and `cfile/cfile.cpp`.
* Full Cortex-A15/armhf compilation and PowerVR/SDL3 hardware tests are **not** available in the build environment. Build on Pyra with `./build-pyra.sh`.
* SDL3 input/Rumble helper, framebuffer/internal 1024x576 upscaling and PowerVR orphaned VBO streaming are unchanged in this update.

## Test on Pyra

```sh
./build-pyra.sh 2>&1 | tee build-pyra-commits.log
./run-pyra.sh 2>&1 | tee run-pyra-commits.log
```

Check pilot selection and creation, changing display resolutions, campaign loading, level scripting, save/load, and the existing rumble helper.

The archive includes `PYRA-THREE-COMMITS.patch`, a reviewable patch against Performance 9 Rumble-Fix 2.
