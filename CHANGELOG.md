# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
2. When creating a release:
   - Create a new section with the version and date
   - Move items from "Unreleased" to the new section
   - Update the version number to match the release tag

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
