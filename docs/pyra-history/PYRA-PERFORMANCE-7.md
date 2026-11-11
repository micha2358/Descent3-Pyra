# Descent 3 – Performance 7 (DragonBox Pyra)

Basis: Performance 6 mit native PowerVR SGX544MP2 EGL/GLES2, SDL3 und 32-Bit-Textur-IDs. **Kein GL4ES, keine Mesa-OpenGL-Ausweichlösung.**

## Änderungen

- **Sichere GL-No-op-Unterdrückung:** `glActiveTexture`, `glBindTexture(GL_TEXTURE_2D)`, `glBindBuffer(GL_ARRAY_BUFFER)`, `glBindFramebuffer(GL_FRAMEBUFFER)` und `glUseProgram` werden bei nachweislich identischem, zwischengespeichertem Zustand übersprungen. Cache wird bei Kontextinitialisierung und relevanten Objektlöschungen verworfen.
- **Barrieren:** Lesende GL-Abfragen und reine Objekt-ID-Erzeugungen leeren keinen ausstehenden Batch. Zustandsänderungen und Textur-Uploads flushen weiterhin in der ursprünglichen Reihenfolge. `PYRA_SMART=0` deaktiviert diese Optimierungen für den Vergleich.
- **Shader-Uniforms:** Identische View-/Projektionsmatrizen, Nebelparameter, Nebelfarbe und Gammawerte werden unter `PYRA_SMART=1` nicht erneut übertragen. Im konservativen Modus bleiben die bisherigen Uniform-Updates aktiv.
- **Adaptives Polygon-Batching:** Ein einzelner Polygon-Fan wird ohne Triangulation und mit der ursprünglichen Vertexzahl ausgegeben. Erst bei einem zweiten Polygon ohne zwischenzeitlichen Zustandwechsel entstehen gemeinsame Dreiecks-Vertices; die Reihenfolge bleibt erhalten. Die Grenze bleibt bei 4096 Vertices.
- **Zwei GLES2-kompatible VBO-Uploads:** `PYRA_STREAM=ring` (Standard; Ringpuffer plus `glBufferSubData`) und `PYRA_STREAM=orphan` (experimentell; `glBufferData` mit neuen Daten je Draw-Call). Kein MapBufferRange, kein VAO, kein Instancing.
- **Profiling:** `PYRA_PERF=1` protokolliert FPS, Polygon- und Draw-Call-Zahlen, Vertex-Uploads, deren CPU-Zeit und die tatsächlichen Ursachen für erzwungene Batch-Flushes. Zeitangaben sind CPU-Wallclock-Messungen, keine isolierten GPU-Zeiten.

## Bauen

```sh
./build-pyra.sh 2>&1 | tee build-pyra.log
```

## Testmatrix

```sh
./benchmark-pyra.sh baseline       # PYRA_BATCH=0 PYRA_SMART=1 PYRA_STREAM=ring
./benchmark-pyra.sh conservative   # PYRA_BATCH=1 PYRA_SMART=0 PYRA_STREAM=ring
./benchmark-pyra.sh smart          # PYRA_BATCH=1 PYRA_SMART=1 PYRA_STREAM=ring
./benchmark-pyra.sh orphan         # PYRA_BATCH=1 PYRA_SMART=1 PYRA_STREAM=orphan
python3 compare-perf7.py pyra-perf7-*.log
```

Jede Variante zunächst in derselben 3D-Szene bei **640×480** und anschließend bei **1280×720** testen. Nach einer Auflösungsänderung einige Sekunden abwarten; Ladevorgänge und Hauptmenüs nicht als 3D-Leistungsmessung bewerten. `PYRA_GL_CALL_TRACE` und `PYRA_GL_TRACE` während der Benchmarks **nicht** setzen. Die Skripte deaktivieren sie ausdrücklich.

**Wichtige Einschränkung:** Der Orphan-Modus ist eine experimentelle Alternative. Je nach PowerVR-Treiber kann er schneller oder langsamer sein. `ring` bleibt der Standard. Durch CPU-Zeiten innerhalb von `glBufferData`/`glBufferSubData` können auch Synchronisationskosten enthalten sein.

Ein ARMv7-Build und eine Laufzeitprüfung auf der DragonBox Pyra müssen auf dem Zielgerät erfolgen.
