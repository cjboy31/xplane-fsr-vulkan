# Frame counter: private Windows test

Tested November 1, 2026 (local time), source
`6041659d3d4c6b71f7490f6f241b9bda2ea784ce`, private draft `test-2.0.0.2`.
This is a feature check, not a v2 promotion or public release.

The downloaded draft's installer and archive matched their SHA-256 manifest.
The installed renderer's path and digest were verified inside each live
simulator process. The renderer digest was
`2582373d9b52a10b28ba6aa53a92df9463fa59b21834ad546ae1beaed2e51e7f`.

System: Windows, Radeon AI PRO R9700, driver 26.10.2, Ryzen 7 9850X3D,
4K 165 Hz. X-Plane 12.4.4-r1, stock Cirrus SR22 at KSEA, overcast daylight.
Other add-ons were present; LiveTraffic was disabled. NR was Off. These are
not vanilla-simulator, Linux, other-GPU or scanout-quality results.

| Check | Result |
| --- | --- |
| Default Off, independent saved choice, stale-editor merge | PASS: automated preferences and panel tests |
| Plugin-menu shortcut shows and hides the overlay | PASS: live |
| Rendering > Show frame counter toggle, help and feedback | PASS: live; help remains over settings, Esc closes pinned help |
| FG running with counter visible and settings closed | PASS: separate overlay does not mark the settings panel visible |
| FG Off | PASS: real FPS continues, FG extra reads 0; 10 s probe counted 383 real frames and 0 generated |
| FG resumed | PASS: extra-frame count advances again |
| Simulator normal exit and restart | PASS: counter returns without reopening settings; 10 s probe counted 321 real frames and 321 generated |
| Sampling, clock/counter resets, long gaps, unavailable renderer | PASS: CPU tests; no underflow or assumed multiplier |

In sequential 30 s cockpit samples, counter On measured 31.85 real FPS
(1% low 21.48), versus Off 31.38 (20.23). These noisy, sequential samples do
not establish an overlay performance benefit or an exact overhead bound.

Three external ground-pan segments (45 + 45 + 35 seconds) measured 33.33,
39.10 and 42.08 real FPS, with 1,500, 1,761 and 1,474 generated frames.
The middle segment missed 17 camera publications, so its 1% low is incomplete.
No crash or device loss was observed. This does not establish freedom from
short scanout flicker or temporal artifacts.

The same roughly 20-minute session sampled at most one SDK-owned completed
source set (195 MiB); pending ownership is bounded by three sets between
drains. Total reported device heap usage still reached 17,611 MiB, including
simulator/SDK/driver memory. It is not a total-VRAM cap. The previous
ordinary-presenter test retained 95 sets / 18,596 MiB and stopped processing
under memory pressure. The corrected session kept FSR and FG running.

Real FPS uses the camera publisher's once-per-rendered-frame sequence.
FG extra FPS uses successful SDK generation counts. Neither line measures
monitor refresh, actual display scanout, input latency or GPU execution time.
Generated frames can still be dropped later by presentation or the display.

Full regression, exact-candidate loader and native-motion fixtures, both FG
algorithms, both presenters/all pacing profiles, generated-image GPU readbacks
and installer lifecycle fixtures passed for the tested source. Broader live
pacing, baseline comparison, night/airborne/motion and uninstall checks remain
separate promotion gates. Later commits, including texture-detail changes,
are not covered by this flight evidence.
