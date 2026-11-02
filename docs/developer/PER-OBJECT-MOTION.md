# Per-object motion (stage E): groundwork and design

Status: groundwork in Test, 2026-11-01. The survey tool is delivered. The
per-object vectors themselves are **not** implemented yet. They need data from
a live X-Plane session, which this tool collects.

## The problem

Native renderer motion (stage B) gives exact vectors for the world, the sky and
the own aircraft as one rigid body. That body is marked by X-Plane's aircraft
stencil flag and moved with the aircraft-local reprojection. Several things
still move on their own:

- AI and multiplayer traffic
- propellers and rotors
- control surfaces, gear and doors
- clouds, rain, particles
- transparent glass

For these, X-Plane draws no velocity, and its SSR pass carries no transform
for them.

## Options, in order of risk

| Option | What it takes | Risk | Notes |
| --- | --- | --- | --- |
| 1 Reuse transforms X-Plane already has | Find previous-frame matrices in existing uniform blocks of object, traffic or aircraft-part draws | Low, read only | Unknown whether they exist. **The survey below answers this** |
| 2 Reactive masks instead of vectors | Mark pixels we cannot trust (props, glass, particles, clouds) so FSR, FG and TAA use the new frame there | Low | Removes the worst trails without exact motion. Needs a reliable way to tell those draws apart, again from the survey |
| 3 In-memory draw instrumentation | Rewrite selected vertex/fragment shaders in memory to also write a velocity attachment, and keep previous transforms per object | High: render-pass changes, extra attachment, per-draw state, X-Plane updates | Only original code. *Motion Vectors V2* (GPL-3.0) does this; we may not use its code. Measure main-thread cost first ([SIM-PERFORMANCE](SIM-PERFORMANCE.md)) |

The plan is option 1 if the survey finds usable transforms; otherwise option 2.
Option 3 comes only when 1 and 2 are measured and still not enough.

## Survey tool (delivered)

1. Set a user environment variable `XPFSR24_MOTION_SURVEY=1`.
2. Start X-Plane and fly for one minute: cockpit and external views, near
   traffic, propellers turning.
3. Close X-Plane and clear the variable.

The FSR layer runs `describeInterface()` (in the shared
`native_motion/shader_classify.cpp`) on every shader module X-Plane creates.
For each module whose resource or member names look motion-related (*prev,
last, old, reproj, velocity, motion*), it logs one line:

- the execution model
- the descriptor resources (set.binding)
- uniform-block member names with offsets
- whether push constants exist

Only names are logged; no shader code or bytecode is saved. A summary line at
exit gives the counts.

Attach the `Motion survey:` lines from the session log (Diagnostics → Open log
folder). They show which passes carry previous-frame data, so option 1 or 2 can
be designed against real interfaces rather than guesses.

## Done when (stage E)

- Using the same flight and camera move, the `tools/motion_quality.py` gain for
  traffic and propeller regions rises against stage B.
- No new device losses.
- The added CPU time on X-Plane's render thread stays below the run-to-run
  spread.
