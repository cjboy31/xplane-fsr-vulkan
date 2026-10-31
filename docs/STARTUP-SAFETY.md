# Startup and session diagnostics correction

The installer and first-run preferences now start without a simulator path.
Existing saved installation paths are retained. The in-sim image-processor
selector offers Native AA, FSR and Original simulator. The old `auto` file value
remains readable for upgrades, but is no longer offered by either panel.

## Input scale

Setup prepares the selected renderer's input scale with X-Plane closed. A
background helper prepares it again after a normal exit. Native AA uses Full;
FSR upscaling uses the saved input choice. FXAA is Off for these renderers.
MSAA, display resolution, UI size, clouds and controller settings are preserved.
Unapplied graphics edits stay queued; the normal-exit graphics task applies
compatible input settings and keeps the selected providers. An Original
simulator selection makes no input edits. Each actual change has the existing
transactional preference backup. No write is attempted after an abnormal exit.

The installed settings mapping and public DataRefs/Commands lists were checked.
No supported writable live FSR-slider control was found. This implementation
does not write private art controls, patch settings.txt, or pretend that editing
preferences changes an already-running renderer. Laminar's
[DataRefEditor guidance](https://developer.x-plane.com/tools/datarefeditor/)
warns plugins against depending on private controls. Automatic activation still
adapts its running mode to the actual source when another launcher changes it.

## Memory and stutter investigation

The previous live log reached GPU-memory pressure with many retained depth and
frame images. Failed depth allocation was attempted and logged repeatedly.
Allocation failures now trigger a three-second retry delay while native
rendering continues. FSR waits for coherent scene samples before first-use
runtime setup, alongside the existing NR/FG startup gate.

Cold one-time recordings can now retire with FG enabled only after both:

- the captured application-queue fence completes for the same recording; and
- the FG presenter's monotonic lease completes at a successful SDK wait.

The synchronized presenter already provides that wait. The normal presenter
can provide it when memory pressure requires a drain. A mere source fence is
insufficient. Reusable, unsubmitted, changed or pending-SDK recordings remain
owned. Old per-command depth captures age out after successful application
re-recording; shared frame ownership keeps any still-borrowed depth alive.
These changes do not establish that all scenery/driver/NR/FG stutters are fixed.

## Session logs

Open Diagnostics > Open log folder for a current snapshot. After the simulator
exits, its own background helper copies the renderer log to
`<X-Plane>/Output/FSR-Logs/renderer.log`; `renderer.previous.log` retains the
previous exported session. A session that reaches plugin startup can export
after normal or abnormal process exit. Export is bounded to the latest 20 MiB
with its session header. PID/executable/token checks reject other sessions;
an older export cannot replace a newer session. Unrecognized files and redirected
log targets are kept. Models and settings are not copied. No automatic upload
or Explorer popup occurs on exit.

## Validation

The Windows regression suite passed before the final bounded Diagnostics
layout and compatible-input refinement. Relevant panel, deferred-graphics,
session-export and production GPU fixtures are rerun for those refinements.
The GPU fixture includes 64 fenced readbacks, descriptor reuse, retention while
an SDK lease is pending, retirement after both completions, native-depth
preservation and suppression of a second allocation during backoff. The six
SDK worker-routing cases from the earlier post-spawn freeze correction remain
covered. Inert child processes exercise normal and abnormal log export.

A fresh simulator flight and long-flight memory/pacing measurements remain
required before describing the stutter report as resolved. Independent Native
AA's selector change is shared with its Linux build; native Linux flight testing
remains a beta limitation. This does not add a Linux FSR or Metal renderer.
