# Project release workflow

The project owner requests that application updates also be published to this
repository's GitHub Releases, not delivered only as local files or pull requests.

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
