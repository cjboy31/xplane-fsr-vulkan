# Installer and automatic updates

Setup.nsi is the canonical original MIT NSIS source. Build-Installer.ps1 creates
the allow-listed licensed payload, manifests, setup executable and RAR. Supply
NSIS 3, WinRAR's licensed CLI, the LLVM-MinGW runtime notices, an existing clean
`outputs/<ReleaseFolder>/app` build plus its LICENSE, and a DocumentationFolder
containing INSTALLATION-AND-USE.txt and REDISTRIBUTION.txt. WorkspaceRoot defaults
to the repository; use an explicit build workspace to keep artifacts separate.
Tool paths can be overridden; no SDK/runtime binaries or archive tools are committed.

Use PackageVersion/WindowsVersion, ReleaseFolder/PackageFolder/ArchiveName and
IconPath matching the release. Generated installer files stay under ignored
installer/build-<version>. Run Test-Installer.ps1 against that exact RAR using
the same WorkspaceRoot/PackageFolder/ArchiveName. Tests use isolated folders,
skip registry/shortcuts via /TEST=1, and never start the simulator. Simulator
validation and preservation checks use inert workspace fixtures; test mode skips
host simulator discovery. The setup
included in that tested archive is the standalone asset passed to Prepare-Release.

The launcher requests `/S /AUTOUPDATE=1 /PARENT=<pid> /D=<existing app folder>`.
NSIS requires the owned-install marker, waits up to 30 seconds for the launcher,
holds its exclusive lock, checks payload files are not in use, retains settings
and shortcut choices, and reopens the installed launcher on success. /D is last
and is unquoted as required by NSIS. /TEST=1 suppresses restart and registry work.
Normal setup remains available for first installs and repair. Update mode does
not remove simulator plugins or edit simulator preferences. Installation is not
an atomic directory swap; a disk/write failure may require repair using full setup.

The installer tests cover ordinary install/update/uninstall, outer/payload hashes,
saved-choice preservation, unowned-target and running-launcher refusal, automatic
update settings preservation, and waiting for the parent process. See
../docs/RELEASING.md for publishing the assets required by the update feed.

Starting with 0.7.2, the allow-list includes IntegrationSetup.exe and the implicit
layer manifest. Full setup offers in-sim activation for the selected simulator.
Automatic updates refresh only an existing opt-in, preserving legacy desktop-only
and opted-out installations. Uninstall removes owned registration and recognized
plugin files before removing app payload; disconnected drives and unrelated
plugins remain safe. A helper failure aborts with instructions for full repair.
Test mode continues to skip all real simulator/registry integration operations;
Test-Activation.ps1 tests plugin ownership and isolated registry behavior separately.
