# Development Checklist

Use this checklist when submitting pull requests to ensure code quality and consistency.

## Pre-Submission

- [ ] Code compiles without errors
- [ ] All tests pass locally: `cargo test` (app)
- [ ] No clippy warnings: `cargo clippy` (app)
- [ ] Code is formatted: `cargo fmt` (app)
- [ ] Firmware compiles: Arduino IDE or Arduino CLI
- [ ] Firmware code passes linting

## Git & Commits

- [ ] Branch is up to date with base branch
- [ ] Commits have clear, descriptive messages
- [ ] No merge commits (rebase if needed)
- [ ] Branch follows naming convention:
  - `feature/description` for features
  - `bugfix/issue-number` for bug fixes
  - `hotfix/version` for hotfixes

## Code Quality

- [ ] No hardcoded values (use constants)
- [ ] Code is well-commented for complex logic
- [ ] Error messages are user-friendly
- [ ] No unnecessary dependencies added
- [ ] Follows project code style

### Rust (App)

- [ ] No `unwrap()` without justification
- [ ] Proper error handling with `Result<T, E>`
- [ ] No deprecated functions used
- [ ] Lifetimes are explicit where needed

### C++ (Firmware)

- [ ] No memory leaks
- [ ] Proper null pointer checks
- [ ] Clear variable naming
- [ ] Comments explain non-obvious logic

## Documentation

- [ ] README.md updated if applicable
- [ ] CHANGELOG.md updated with changes
- [ ] Code comments added for complex sections
- [ ] Public APIs documented
- [ ] Installation/setup steps clear

## Testing

- [ ] Manual testing completed
- [ ] Edge cases considered
- [ ] Tested on supported platforms (if applicable)
- [ ] No console errors/warnings

## Pull Request

- [ ] PR title is descriptive
- [ ] PR description explains the change
- [ ] Related issues linked (closes #123)
- [ ] No merge conflicts
- [ ] GitHub Actions checks pass
- [ ] No sensitive data committed

## Final Review

- [ ] Changes don't introduce technical debt
- [ ] Performance implications considered
- [ ] Security implications reviewed
- [ ] Breaking changes documented
- [ ] Backward compatibility maintained (unless major version)

## Checklist Template for PR Description

```markdown
## Description
Brief description of changes

## Related Issues
Closes #123

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Breaking change
- [ ] Documentation update

## Testing
- [ ] Unit tests added/updated
- [ ] Manual testing completed
- [ ] Tested on: Linux / Windows / macOS

## Checklist
- [ ] Code follows project style
- [ ] All tests pass
- [ ] No new warnings (clippy, linting)
- [ ] CHANGELOG.md updated
- [ ] Documentation updated
```
