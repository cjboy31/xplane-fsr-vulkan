# Using Native AA and FSR on the same installation

Both mods may remain installed. Only one renderer should process a session.
In Native AA, open **Plugins > Native AA > Open AA controls > Renderer**.
In the updated FSR panel, use **Rendering > Renderer - next launch**.

- **Native AA**: independent SMAA, FXAA, TAA or combined AA. FSR, NR and frame
  generation stay inactive. Set X-Plane input scale to **Off / Full resolution**.
- **FSR / NR / frame generation**: Native AA stays inactive. Configure the FSR
  input scale and provider in its panel; this choice does not install its runtime.
- **Original simulator**: both mod renderers stay inactive; native X-Plane AA
  settings still apply.
- **Automatic**: Native AA takes priority when its standalone activation marker
  is installed; otherwise FSR can activate. Choose explicitly when using both.

The selection saves in the selected simulator's
`Output/preferences/XP-Renderer-Selection.ini`. Close X-Plane normally and reopen
with the usual shortcut or third-party launcher. Switching renderers requires
a restart because Vulkan interception is established before plugins load.
Your AA, FSR, NR, frame-generation and graphics choices are retained separately.
An explicit launch from the updated FSR desktop application overrides the saved
choice for that launch only; its native-comparison button bypasses both renderers.

The panel shows current-session renderer activity separately from the next-launch
selection. Both control panels can remain loaded. A panel opening, or a selected
mode, is not evidence that its renderer is active: Native AA needs fresh fenced
GPU completions before reporting ACTIVE.

Update both mods to versions implementing this selection protocol for load-order
independent switching. If an older FSR release has already activated, Native AA
refuses processing and reports the FSR owner rather than stacking reconstruction.
Do not interpret that compatibility fallback as a successful switch to Native AA.
Third-party reconstruction layers are not covered by this two-project protocol.

## Implementation and tests

The identical original MIT protocol-v1 header is shared with XPFSR24. A short OS
lock serializes startup, a process-identity-tagged environment snapshot latches
the decision, and only its owner may activate. There are no per-frame preference
reads or changes to unrelated registry entries, plugins, camera or controllers.
Atomic preference replacement preserves the previous file on save failure.
Malformed/unknown selections fail closed. Existing GPU work is never unloaded
or transferred by a button press.

Tests cover both load orders, all four choices, repeated/concurrent requests,
mid-session preference edits, explicit launches, inherited stale owners, an older
active FSR session, malformed files and FSR-only startup. Native Windows and Linux
loader/installer fixtures exercise saved choices in the actual AA layer. The FSR
activation test also exercises its real admission entry before and after the AA
claim. Linux AA selection does not add FSR 4/NR/FG Linux rendering support.
