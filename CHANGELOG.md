# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Firmware reloads the active profile when the app saves it, so changes apply without reselecting it (returns to the profile selection if the profile was deleted)

### Fixed
- LEDs of action buttons A and B were swapped for both players

### Changed
- Profile editor labels the action buttons "A P1"/"B P1"/"A P2"/"B P2" instead of "Action P1.1"/"Action P1.2"/"Action P2.1"/"Action P2.2"

## [3.0.0] - 2026-09-26

### Added
- CI/CD pipeline with GitFlow releases: `release-start.yml` creates release/hotfix branches, `release-finish.yml` tags, builds and publishes GitHub Releases
- SemVer versioning via `scripts/version.sh`, injected into app and firmware at build time (unofficial builds are `0.0.0+<sha>`)
- Firmware version on the OLED splash screen
- App warns when app and firmware versions don't match
- macOS universal binary (Apple Silicon + Intel)

## [1.0.0] - 2026-02-01

### Added
- Initial B.L.A.S.T. Configuration Tool release
- Serial connection management for firmware
- Button mapping configuration interface
- Profile management (create, edit, delete, save, load)
- Key capture with modifier support
- Profile editor with visual feedback
- Sync status indicator and error messages
- Multi-platform support (Linux, Windows, macOS)
- Theme selector (System/Dark/Light modes)

### Changed

### Fixed

### Security

---

## Guide for Updating the Changelog

When making changes to the project:

1. Add your changes to the "Unreleased" section under the appropriate category
2. When a release starts, `release-start.yml` moves the "Unreleased" entries into a new
   `## [X.Y.Z] - date` section (`scripts/changelog.sh release`). That section becomes the
   GitHub Release notes (`scripts/changelog.sh notes`), empty categories are left out.

### Categories

- **Added**: New features
- **Changed**: Changes in existing functionality
- **Fixed**: Bug fixes
- **Deprecated**: Soon-to-be removed features
- **Removed**: Removed features
- **Security**: Security fixes

### Examples

```markdown
### Added
- Support for importing profiles from JSON files (#42)
- Keyboard shortcut Ctrl+S for saving profiles

### Fixed
- Fixed serial connection timeout on Windows (#38)
- Corrected firmware version detection for RP2040

### Changed
- Improved UI responsiveness during sync operations
```
