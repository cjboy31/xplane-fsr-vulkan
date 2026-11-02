# Contributing

Issues, reproducible test cases and pull requests are welcome. Discuss large
renderer changes before implementing them so work can be coordinated.

## Scope and review

Keep changes focused. Explain the trigger, before/after behavior and validation.
Include the exact simulator/GPU/driver/aircraft combination for live checks.
Avoid claiming an image-quality or performance improvement from API counters.
Document experimental limitations instead of presenting prototypes as finished.

Add a regression when changing GPU synchronization, resource ownership, IPC,
projection, masking or preference transactions. Use fenced GPU readbacks and
meaningful references. A passing analytic fixture does not establish live quality.
Preserve a native/bypass path for unsupported configurations.

## Code and assets

Submit only work you have the right to contribute. Contributions to original
project code/assets are under the repository's MIT license. Keep upstream
notices and identify any third-party dependency you introduce.

Do not submit simulator binaries, aircraft/scenery files, extracted shader
bytecode, SDK proprietary source, paid mod files, user preferences, credentials,
tokens or unreviewed logs. Do not copy GPL/proprietary implementation code into
the MIT project without discussing a compatible distribution plan first.

Read BUILDING.md, docs/developer/ARCHITECTURE.md and docs/developer/STATUS.md. Run the tests relevant
to your change. Record which tests were not run and why. Screenshots/video help
with visual regressions, but should not replace frame-time or pixel evidence.

## Community conduct

Be respectful and specific. Critique code and measurements rather than people.
Do not share private information or other people's copyrighted assets. Maintainers
may remove abusive content or material that cannot lawfully be shared.
