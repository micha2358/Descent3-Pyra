# Descent 3 v1.6.0 — DragonBox Pyra (bereinigtes Release)

**Basis:** das auf der Pyra erfolgreich getestete Release
`Descent3-Pyra-PowerVR-GLES2-SDL3-mainmenu-mve-gameplay-rumble-fix.zip`.

## Spielergebnis und Rumble

- **Kein Rumble beim Schießen** auf der Pyra (`PYRA_GLES2`). Der direkte
  Waffenfeuer-Trigger in `Descent3/WeaponFire.cpp` wurde entfernt.
- **Rumble bei tatsächlich erlittenem Schildschaden** bleibt in
  `Descent3/damage.cpp` unverändert vorhanden und respektiert die
  Force-Feedback-Spieloption sowie den Master-Gain.
- Die SDL2-Helper-Ausgabe, Mindestimpulsdauer, Überlappungsschutz und
  explizite Motorabschaltung bleiben erhalten.
- Der bisherige, einmalige `PYRA_RUMBLE_GAME_TEST`-Sondermodus wurde
  entfernt. Der separate optionale Start-/Hardwaretest
  `PYRA_RUMBLE_TEST=1` bleibt verfügbar.
- Nicht-Pyra-Plattformen behalten den ursprünglichen Waffenrückstoß.

## Grafik und Audio

- Natives PowerVR SGX544MP2 EGL/OpenGL ES 2.0, **keine** Mesa-/GL4ES-Umleitung.
- Die bestätigten MVE-RGB555-Korrekturen (Bewegungsvektoren und
  zweifarbige Blöcke) sowie Stereo-DPCM-Audio bleiben erhalten.
- `mainmenu.mve`: die vorzeichenlose 16-Bit-Segmentlänge einschließlich
  Grenzprüfung bleibt korrigiert, sodass große Segmente nicht mit
  negativem Wert verworfen werden.
- Die bisherigen Renderer-/Performance-Pfade und die unterstützten
  ALSA-/PulseAudio-Wege wurden nicht verändert.

## Auf der Pyra bauen und starten

```sh
./test-pyra-menu-mve.sh
./test-pyra-movie.sh
./build-pyra.sh
./run-pyra.sh
```

Erfordert die PowerVR-DDK-Bibliotheken unter
`/opt/omap5-sgx-ddk-um-linux/lib`. SDL3 liegt weiterhin im
Projekt unter `third_party/SDL` und wird mitgebaut.

## Release-Bereinigung

Alle vorherigen `PYRA-*.patch`-Zwischenstände sind bereits im Quellstand
integriert und werden daher nicht mehr als separate, potenziell
veraltete Patches mitgeliefert. Frühere Entwicklungsnotizen liegen unter
`docs/pyra-history/`; die Build-, Test- und Startskripte sowie die
Projektquellen bleiben vorhanden. Diagnosefunktionen, deren Entfernung
funktionierendes Laufzeitverhalten verändern könnte, bleiben verfügbar.

**Prüfung:** Quellcodevergleich mit dem bestätigten Vorgängerrelease;
Gezielte Tests und Syntaxprüfung auf dem Build-Host. Die neue Variante
wurde noch nicht erneut auf der realen Pyra getestet.

## Scharfe Menüs während des Spiels (Sharp Ingame UI)

Die vorherige Version renderte den logischen 1280x720-Spielmodus immer
auf einer internen 1024x576-FBO und skalierte das fertige Bild hoch.
Dadurch wurden insbesondere Text und feine Menügrafik weich.

Diese Version schaltet während `RunGameMenu()` (Optionen, Hilfe, Pause,
Speichern/Laden, Exit-Dialog usw.) sowie bei Quick-Save-Menüs auf eine
**native interne 1280x720-FBO** um. Beim Rückweg in den laufenden
Spielbetrieb wird die performantere **1024x576-FBO** verwendet.
Das SDL-Fenster und die PowerVR-GLES2/EGL-Instanz werden nicht neu
erstellt; nur die Farb- und Tiefenanhänge der Offscreen-FBO wechseln am
nächsten Frame-Anfang.

### Umschalter / Diagnose

- Standard: scharfe Ingame-Menüs **ein**, 3D-Spielbetrieb schnell
- `PYRA_SHARP_UI=0 ./run-pyra.sh`: bisherige Darstellung der
  Ingame-Menüs (1024x576, für einen direkten A/B-Vergleich)
- `PYRA_3D_1024=0 ./run-pyra.sh`: volle 1280x720 auch für die 3D-Szene
- `PYRA_PERF=1 ./run-pyra.sh`: protokolliert die tatsächliche interne
  Rendergröße. Beim Menüwechsel steht außerdem `PYRA UI: scene resized ...`.
- `./test-pyra-sharp-ui.sh`: prüft den Auflösungswechsel unabhängig vom
  Grafiktreiber.

Die höhere Auflösung kostet im geöffneten Ingame-Menü Leistung; das
normale 3D-Gameplay bleibt auf der bisherigen Renderauflösung. Die
Menüumschaltung ist auf dem Host durch Logiktests geprüft; ein realer
Pyra-Hardwaretest bleibt erforderlich.

## Upstream updates from October–November 2026

This Pyra package incorporates a **select, platform-compatible subset** of
upstream changes from October 22–November 2, 2026. See
`UPSTREAM-2026-OCT-NOV-INTEGRATION.md` for exact upstream SHAs, intentionally
excluded large refactors, preserved Pyra behavior, and verification.

## November 2026 upstream update

The 2026-11-09 Pyra update integrates upstream PR #727 (SDL3 event polling on the main thread), adapted for PowerVR-safe shutdown, plus independent Super/AltGr keys. See `UPSTREAM-2026-NOV-09-INTEGRATION.md` for all included/excluded upstream commits, tests, and hardware validation requirements.
