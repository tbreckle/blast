# B.L.A.S.T. Development Workflow

This project uses **GitFlow** branching model for release management and continuous integration with GitHub Actions.

## Branch Structure

### Main Branches

- **`main`** - Production-ready code
  - Protected branch (requires pull request reviews)
  - Automatically tagged with version releases
  - All code must pass CI/CD checks

- **`develop`** - Development integration branch
  - Protected branch (requires pull request reviews)
  - Base branch for feature development
  - Should always be in a working state

### Supporting Branches

- **`feature/*`** - Feature branches (e.g., `feature/theme-selector`)
  - Created from: `develop`
  - Merged back into: `develop`
  - Naming convention: `feature/descriptive-name`

- **`bugfix/*`** - Bug fix branches (e.g., `bugfix/serial-connection`)
  - Created from: `develop`
  - Merged back into: `develop`
  - Naming convention: `bugfix/issue-number-description`

- **`release/*`** - Release branches (e.g., `release/1.0.0`)
  - Created from: `develop`
  - Merged into: `main` and back to `develop`
  - Automatically created by release workflow
  - Only version bumps and critical fixes

- **`hotfix/*`** - Hotfix branches (e.g., `hotfix/1.0.1`)
  - Created from: `main`
  - Merged into: `main` and `develop`
  - For critical production fixes

## Workflow

### Creating a Feature

```bash
# Create feature branch from develop
git checkout develop
git pull origin develop
git checkout -b feature/my-feature

# Work on feature
# ... make commits ...

# Push and create pull request
git push origin feature/my-feature
```

Then create a Pull Request on GitHub targeting the `develop` branch.

### Creating a Release

1. Go to GitHub Actions → GitFlow Release
2. Click "Run workflow"
3. Enter the version number (e.g., `1.0.0`) following semantic versioning
4. Select release type (major/minor/patch)
5. The workflow will:
   - Create a release branch
   - Update version numbers
   - Create a pull request to `main`
   - Build release artifacts

6. Review and merge the PR to `main`
7. The workflow automatically:
   - Creates a version tag (`v1.0.0`)
   - Merges changes back to `develop`
   - Creates a GitHub Release

### Hotfix (Critical Production Fixes)

```bash
# Create hotfix from main
git checkout main
git pull origin main
git checkout -b hotfix/1.0.1

# Fix the critical issue
# ... make commits ...

# Create PR to main
git push origin hotfix/1.0.1
```

After merging to `main`:
```bash
git checkout develop
git pull origin develop
git merge main
git push origin develop
```

## CI/CD Pipelines

### App Build (`app-build.yml`)
- Triggers on: Push/PR to main/develop, or changes in `app/`
- Runs on: Linux, Windows, macOS
- Steps:
  - Build release binary
  - Run tests
  - Clippy linting
  - Format checking
  - Upload artifacts

### Firmware Build (`firmware-build.yml`)
- Triggers on: Push/PR to main/develop, or changes in `firmware/`
- Runs on: Ubuntu
- Steps:
  - Compile Arduino sketch
  - Build UF2 file
  - C++ linting
  - Upload artifacts

### Release Workflow (`release.yml`)
- Manual trigger via GitHub Actions
- Creates release branch with version bumps
- Builds artifacts for all platforms

### Auto-Tag & Release (`tag-release.yml`)
- Triggers on: PR merge to main from release branch
- Steps:
  - Create version tag
  - Merge back to develop
  - Create GitHub Release

## Branch Protection Rules

Configured on `main` and `develop`:

- ✅ Require pull request reviews (minimum 1)
- ✅ Require status checks to pass (all CI/CD workflows)
- ✅ Require branches to be up to date before merging
- ✅ Require code review dismissal when new commits pushed
- ✅ Enforce all configured restrictions for administrators

## Semantic Versioning

Versions follow [Semantic Versioning](https://semver.org/):

- **MAJOR** (X.0.0) - Breaking changes
- **MINOR** (0.X.0) - New features, backward compatible
- **PATCH** (0.0.X) - Bug fixes

Example progression: 0.1.0 → 0.2.0 → 1.0.0 → 1.0.1 → 1.1.0

## Common Commands

```bash
# Ensure local branches are up to date
git fetch origin

# Create and push a feature branch
git checkout -b feature/my-feature origin/develop
git push -u origin feature/my-feature

# Update develop from origin
git checkout develop
git pull origin develop

# Merge another branch locally (avoid if possible, use PRs)
git merge origin/some-branch
```

## Tips

- Keep feature branches short-lived (few days maximum)
- Write clear commit messages
- Link PRs to related issues
- Use meaningful branch names
- Test locally before pushing
- Ensure CI passes before requesting review
