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

