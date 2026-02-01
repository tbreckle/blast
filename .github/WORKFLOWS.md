# GitHub Actions & CI/CD

This directory contains GitHub Actions workflows for B.L.A.S.T. automated building, testing, and release management.

## Workflows

### 1. App Build (`app-build.yml`)

**Purpose**: Build and test the Rust configuration tool

**Triggers**:
- Push to `main`, `develop`, feature/*, release/* branches
- Pull requests to `main` or `develop`
- Changes in `app/` directory

**Jobs**:
- **build**: Compiles release binary on Linux, Windows, macOS
  - Runs cargo build and tests
  - Caches dependencies for speed
  - Uploads platform-specific artifacts
- **clippy**: Lint checks with Clippy
  - Ensures code quality
  - Fails on warnings
- **fmt**: Code formatting check
  - Ensures consistent style

**Artifacts**:
- `blast-linux` - Linux executable
- `blast-windows` - Windows executable (.exe)
- `blast-macos` - macOS executable

---

### 2. Firmware Build (`firmware-build.yml`)

**Purpose**: Compile and verify the Arduino firmware

**Triggers**:
- Push to `main`, `develop`, feature/*, release/* branches
- Pull requests to `main` or `develop`
- Changes in `firmware/` or `pcb/` directories

**Jobs**:
- **build**: Compiles Arduino sketch for RP2040
  - Uses Arduino CLI
  - Installs required cores and libraries
  - Generates UF2 binary
- **lint**: C++ code linting
  - Runs cppcheck
  - Fails on lint errors

**Artifacts**:
- `firmware-uf2` - UF2 firmware image (ready for upload to device)

**Dependencies**:
- Arduino CLI
- Raspberry Pi RP2040 core
- Required libraries (Adafruit_*, etc.)

---

### 3. Release Workflow (`release.yml`)

**Purpose**: Orchestrate the release process following GitFlow

**Triggers**:
- Manual workflow dispatch via GitHub Actions interface
- Input parameters:
  - `version`: Target version (e.g., 1.0.0)
  - `release_type`: major/minor/patch

**Steps**:
1. Validates version format (semantic versioning)
2. Creates `release/X.Y.Z` branch from `develop`
3. Updates version in:
   - `app/Cargo.toml`
   - `firmware/firmware.ino` (if version defined)
4. Commits version bump
5. Creates pull request to `main`
6. Builds release artifacts on all platforms

**Outputs**:
- Pull request ready for merge
- Release artifacts for all platforms
- Version bumps committed

---

### 4. Auto-Tag & Release (`tag-release.yml`)

**Purpose**: Finalize release by tagging and creating GitHub Release

**Triggers**:
- PR merge to `main` from `release/` branch

**Steps**:
1. Creates annotated git tag (`vX.Y.Z`)
2. Merges release branch back to `develop`
3. Creates GitHub Release with changelog reference
4. Cleans up (can delete release branch)

**Outputs**:
- Version tag pushed to repository
- GitHub Release created
- Changes merged back to develop

---

## Quick Start

### Setting Up Branch Protection

Set up branch protection rules in GitHub Settings for `main` and `develop`:

```bash
# Using GitHub CLI:
gh api repos/{owner}/{repo}/branches/main/protection \
  -f required_status_checks='{"strict":true}' \
  -f enforce_admins=true \
  -f required_pull_request_reviews='{"required_approving_review_count":1}' \
  -f dismiss_stale_reviews=true
```

Or manually in GitHub Settings:
1. Go to Settings → Branches
2. Add rule for `main`
3. Require 1 PR review
4. Require status checks to pass
5. Require branches up to date
6. Repeat for `develop`

### Creating a Release

1. Go to Actions tab
2. Select "GitFlow Release" workflow
3. Click "Run workflow"
4. Enter version (e.g., 1.0.0) and release type
5. Workflow creates PR to main
6. Review and merge PR
7. Tag and release created automatically

### Manual Release Process

If workflows need adjustment:

```bash
# 1. Create release branch
git checkout develop
git pull origin develop
git checkout -b release/1.0.0

# 2. Update versions
# Edit app/Cargo.toml and firmware/firmware.ino

# 3. Commit and push
git add .
git commit -m "chore: release 1.0.0"
git push -u origin release/1.0.0

# 4. Create PR to main on GitHub

# 5. After merge, tag locally or via tag-release workflow
git tag -a v1.0.0 -m "Release 1.0.0"
git push origin v1.0.0

# 6. Merge back to develop
git checkout develop
git merge main
git push origin develop
```

---

## Status Badges

Add these to README.md:

```markdown
[![Build App](https://github.com/username/blast/actions/workflows/app-build.yml/badge.svg)](https://github.com/username/blast/actions/workflows/app-build.yml)
[![Build Firmware](https://github.com/username/blast/actions/workflows/firmware-build.yml/badge.svg)](https://github.com/username/blast/actions/workflows/firmware-build.yml)
```

---

## Troubleshooting

### Build Failures

**App build fails**:
- Check Rust version: `rustup update`
- Verify dependencies: `cargo check`
- Check for clippy issues: `cargo clippy`

**Firmware build fails**:
- Verify Arduino libraries are installed
- Check RP2040 core is installed
- Review Arduino CLI output in logs

### Workflow Not Triggering

- Verify branch names match trigger conditions
- Check file paths in `paths` filters
- Ensure branch is not protected from CI workflows
- Review GitHub Settings → Actions permissions

### Artifact Not Found

- Check job succeeded (review logs)
- Verify artifact path matches uploaded location
- Artifacts are retained for 30 days by default

---

## Customization

To modify workflows:

1. Edit `.github/workflows/*.yml` files
2. Refer to [GitHub Actions Documentation](https://docs.github.com/en/actions)
3. Test changes in a feature branch
4. Common customizations:
   - Add new build platforms
   - Change artifact retention
   - Add Slack/Discord notifications
   - Add deployment steps

---

## References

- [GitHub Actions Documentation](https://docs.github.com/en/actions)
- [Workflow Syntax Reference](https://docs.github.com/en/actions/using-workflows/workflow-syntax-for-github-actions)
- [Semantic Versioning](https://semver.org/)
- [Keep a Changelog](https://keepachangelog.com/)
- [GitFlow Workflow](https://nvie.com/posts/a-successful-git-branching-model/)
