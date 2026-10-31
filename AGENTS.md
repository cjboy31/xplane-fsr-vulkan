# Project release workflow

The project owner requests that application updates also be published to this
repository's GitHub Releases, not delivered only as local files or pull requests.

Feature-video revisions require the owner's local preview and approval before
publication. Prepare and validate a clearly marked private review MP4 first;
do not publish the revision or replace release media until that approval arrives.
This is the owner's explicit preference and takes priority over the general
release-publication request above.

- Finish the implementation, relevant checks and redistribution review first.
- Build a versioned installer RAR and confirm install/update/uninstall behavior,
  including preservation of saved user choices. Keep previous releases available.
- Publish a versioned GitHub Release with the tested RAR, SHA256SUMS.txt,
  INSTALLATION-AND-USE.txt and clear release notes. Tag the exact source commit
  used for the build. Alpha/experimental builds must be marked pre-release.
- Preserve upstream license notices in the installer. Do not commit SDK runtime
  binaries, simulator/aircraft assets, user preferences, logs or credentials.
- Follow docs/RELEASING.md. Report the public release URL and verify its assets.
  A local archive, source branch or draft release alone is not publication.
- If account access blocks publication, prepare the reviewed files and explain
  the specific blocker; do not claim that a release was published.

## Discord release announcements

The owner requests a Discord announcement after each published application
update. Post in the **updates** channel of **AMD FSR 3/4 for X-Plane 12**:
https://discord.com/channels/1565487749832777830/1565816821523681290

- Publish and verify the GitHub Release and download assets first. Keep the
  owner's feature-video review requirement above.
- Check recent updates messages for the same release URL/version to avoid a
  duplicate announcement. Clearly identify Normal or Experimental.
- Post a short, user-friendly summary with the GitHub release link, useful setup
  changes and material hardware/model or experimental limitations. Announce only
  features actually delivered. Routine release announcements to this channel
  are explicitly authorized by the owner.
- Keep credentials, private logs and proprietary model files out of messages.
  Use ordinary posts; do not add @everyone or @here unless requested.
- Verify the sent message and record its permalink. If Discord access fails,
  retain the prepared announcement and report that it has not been posted.

## Repository maintenance after major features

After publishing and verifying a major feature release, refresh the README's
download/channel table and repository description to match delivered features.
Review stale drafts, completed pull requests and branches. Remove only verified
obsolete drafts or fully merged temporary development branches; retain active
experimental/platform work. Keep versioned published releases, tags and assets
available for existing users and updaters. Do not commit runtime/model/user files.

## Regression checks

Recheck proposed work against the published main and development changes before
merging, and retain their fixes. Run Test-Regression.ps1 for launcher, settings,
logging, update/channel or shared-policy changes. Push/PR CI runs source policy
and the Windows CPU/native-UI suite. Renderer changes also require relevant
SDK/GPU fixtures and live simulator checks when available; CI alone does not
establish visual quality. Resolve failures before releasing and report limits.

## In-simulator setup direction

After publishing the current NR/FG update, implement the in-simulator setup.
The owner requests LuminaXP as the reference for its in-sim workflow and controls:
an accessible X-Plane panel, live supported adjustments and activation compatible
with third-party launchers. Study documented behavior; implement original code
and artwork without copying restricted LuminaXP internals. Distinguish plugin
presence from confirmed renderer activity. See docs/IN-SIM-PLUGIN.md.

The owner's latest direction makes the in-sim panel the primary interface for
presets, graphics, NR/FG, models, diagnostics, guides and updates. Keep the
desktop application available on GitHub as a clearly labeled optional secondary
interface. Both interfaces must share saved choices. Installation/early
activation may use background support components but must not require a desktop
dashboard or a special simulator launcher. Preserve existing choices during
migration. Check feature parity and activation/update/uninstall behavior before
advertising the in-sim workflow as complete.

After the in-sim panel, investigate multi-GPU options using Lossless Scaling's
documented behavior as a reference. Evaluate explicit render/processing GPU
selection, transfer/synchronization costs, device support and safe fallback.
Implement original code and verify actual two-GPU behavior before claiming
compatibility or performance benefits. Do not substitute this for the panel work.

Latest owner priority: after the current NR memory/blur corrections are tested,
begin multi-GPU settings/prototypes alongside the remaining in-sim parity work.
Also maintain a concrete optimization/feature roadmap based on measured findings.
Mac installers must distinguish the existing diagnostic probe from a future
working Metal rendering backend; do not package an unsupported rendering claim.

Also after in-sim integration, investigate the owner's CPU frame time being much
higher than GPU time. Compare the same scene with the mod bypassed, FSR only,
NR and FG; distinguish simulator CPU work from synchronization/wait time and
source/display frame timing before proposing an optimization.


## Owner update: 2026-10-31

The owner replaced the feature-video workflow: new versions use a viewable in-sim changelog, with no startup video or video approval step. The desktop dashboard is retired; build it only as a plugin updater. Rendering options belong in the in-sim panel. Preserve saved choices. Use plain, short setting labels and help at an eighth-grade reading level. Main-page descriptions must cover delivered features only; keep developer details in developer docs. Remove aircraft-specific screen branding while retaining honest screen-layout limits. Optional shadow Lua is separate, opt-in, and must probe writable controls and restore owned values. Shimmer reduction is the next rendering priority.
