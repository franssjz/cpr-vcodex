# GitHub Actions CI/CD Pipeline

This document describes the automated build, test, and release infrastructure for cpr-vcodex using GitHub Actions.

## Table of Contents

- [Overview](#overview)
- [CI Workflow](#ci-workflow)
- [Release Workflow](#release-workflow)
- [Developer Workflow](#developer-workflow)
- [Troubleshooting](#troubleshooting)
- [Best Practices](#best-practices)

## Overview

**Goal**: Ensure code quality, prevent regressions, and automate professional releases.

**Workflows**:
1. **CI (Continuous Integration)** - Runs on every commit/PR
2. **Release (GitHub Release) ** - Runs on version tags

**Key Principles**:
- ✅ Fail-fast on errors (no silent failures)
- ✅ Cache dependencies (faster builds)
- ✅ Clear error messages (debugging support)
- ✅ Minimal resources (GitHub Actions free tier)
- ✅ Multi-environment support (dev + production builds)

---

## CI Workflow

**File**: `.github/workflows/ci-improved.yml`

**Trigger**: 
- Push to `master` or `develop` branches
- All pull requests
- Manual trigger (`workflow_dispatch`)

**Jobs** (parallel execution):

### 1. Code Formatting Check

```yaml
Job: clang-format
Duration: ~30 seconds
```

Ensures C++ code follows project style guidelines using clang-format-21.

**Failure Reason**: Code doesn't match `.clang-format` rules

**Fix**:
```bash
./bin/clang-format-fix
git diff   # Review changes
git add .
git commit -m "fix: apply code formatting"
```

**Why it matters**:
- Consistent readability across the codebase
- Prevents style debates in code reviews
- Enables automatic formatting tools

---

### 2. Static Analysis (cppcheck)

```yaml
Job: cppcheck
Duration: ~1-2 minutes
```

Scans code for potential bugs, memory issues, and style problems.

**Common Issues**:
- Uninitialized variables
- Buffer overflows
- Memory leaks
- Unused variables
- Logic errors

**Failure Reason**: cppcheck found defects rated as `low`, `medium`, or `high`

**Debug**:
```bash
# Run locally to see exact issues
pio check --fail-on-defect low --fail-on-defect medium --fail-on-defect high
```

**Fix Strategy**:
1. Read the error message carefully
2. Some are false positives - review code logic
3. If false positive, add comment to suppress:
   ```cpp
   // cppcheck-suppress unusedVariable
   int unused = getValue();
   ```

---

### 3. Build Verification (development)

```yaml
Job: build (default environment)
Duration: ~2-3 minutes
```

Compiles firmware using the `default` environment (development build with full logging):
- Output: `.pio/build/default/firmware.bin`
- Includes debug symbols
- Serial logging enabled

**Artifacts Created**:
- `firmware.bin` - Development binary
- `pio-build.log` - Build output for debugging (10 day retention)

**Failure Reason**: Compilation error or warning treated as error

**Debug**:
```bash
pio run -e default 2>&1 | head -200
```

**Extract Build Stats**:
- RAM usage automatically extracted
- Flash size automatically extracted
- Compared to previous builds

---

### 4. Build Verification (production)

```yaml
Job: build-vcodex-release (vcodex_release environment)
Duration: ~2-3 minutes
```

Compiles production firmware using `vcodex_release` environment:
- Output: `.pio/build/vcodex_release/firmware.bin`
- Optimizations enabled
- Minimal logging
- **This is the firmware flashed to devices**

**Artifacts Created**:
- `firmware.bin` - Production binary (used for flashing)
- Build stats extracted

---

### 5. Test Execution

```yaml
Job: test
Duration: ~1-2 minutes
```

Runs unit/integration tests if a `[env:test]` environment exists in `platformio.ini`.

**Current Status**: No test environment defined (placeholder job)

**To Add Tests**:
1. Create `[env:test]` environment in `platformio.ini`
2. Workflow automatically detects and runs tests
3. See PlatformIO unit testing docs

---

### 6. CI Status Gate

```yaml
Job: ci-status (final check)
Duration: ~5 seconds
```

Ensures all jobs pass before CI is marked complete.

**Result**: 
- ✅ All checks passed → PR can be merged
- ❌ Any check failed → PR blocked until fixed

---

## Release Workflow

**File**: `.github/workflows/release-improved.yml`

**Trigger**:
- Push a git tag matching `v[0-9]+.[0-9]+.[0-9]+*` (e.g., `v1.1.20-vcodex`)
- Manual trigger with version input (`workflow_dispatch`)

**Steps**:

### 1. Determine Version

Extracts semantic version from git tag or manual input.

**Examples**:
- `v1.1.20-vcodex` → version `1.1.20-vcodex`
- `v1.1.20-vcodex-rc1` → version `1.1.20-vcodex-rc1` (pre-release)
- `v1.2.0-beta` → version `1.2.0-beta` (pre-release marker `-beta`)

### 2. Build Firmware

Compiles using `gh_release` environment (production optimized):
- Output: `.pio/build/gh_release/firmware.bin`
- Optimizations enabled
- Minimal logging
- **Duration**: ~3-4 minutes

### 3. Extract Changelog

Parses `CHANGELOG.md` to find section matching the release version.

**CHANGELOG Format** (required):
```markdown
## X.Y.Z-vcodex (Optional description)

- Feature or fix description
- Another entry

## X.Y.Z-1-vcodex (Previous version)

...
```

**Looks for**: `##` heading matching version number

**Failure Handling**: If not found, release notes include warning but release still completes

### 4. Generate Commit Summary

Extracts commits since the previous tag using `git log`.

**Output**:
```
### Recent Commits
`<commit message>`  (up to 20 commits shown)
```

### 5. Create Release Notes

Combines:
- Version header with build date
- Extracted changelog section
- Commit summary
- Build information (platform, binary target)
- Installation instructions (esptool.py command)

**Example Output**:
```markdown
# CrossPoint Reader v1.1.20-vcodex

## Overview
ESP32-C3 optimized e-reader firmware for Xteink X4 device.

## Changelog
[content from CHANGELOG.md]

### Recent Commits
[commit history]

## Build Information
- Version: v1.1.20-vcodex
- Build Date: 2024-04-03 10:15:30 UTC
- Platform: ESP32-C3 (RISC-V, 160MHz, 380KB RAM)
- Binary Target: Xteink X4

## Installation
esptool.py --chip esp32c3 -p /dev/ttyUSB0 write_flash 0x0 firmware.bin
```

### 6. Collect Build Artifacts

Gathers all build outputs:
- `firmware.bin` (required)
- `bootloader.bin` (if available)
- `firmware.elf` (for debugging)
- `firmware.map` (memory analysis)
- `partitions.csv` (partition table)

### 7. Create GitHub Release

**Action**: Uses `softprops/action-gh-release@v2.0.4`

**Creates**:
- GitHub Release page with version as tag
- Release notes as release body
- All artifacts attached as downloadable files
- Pre-release flag if version contains `-rc`, `-beta`, or `-alpha`

**Result**: Available on [Releases](https://github.com/franssjz/crosspoint-reader-codex/releases)

### 8. Artifacts Backup

Uploads build outputs to GitHub Actions for 30-day backup retention.

---

## Developer Workflow

### For Code Contributors

**Recommended Flow**:

```bash
# 1. Create feature branch
git checkout -b feature/my-feature

# 2. Make changes
# ... edit files ...

# 3. Format code
./bin/clang-format-fix

# 4. Test locally
pio run -e default         # Development build
pio run -e vcodex_release  # Production build
pio device monitor         # Test device integration

# 5. Commit with conventional format
git add .
git commit -m "feat: add new feature description"

# 6. Push and create PR
git push origin feature/my-feature
# ... create PR on GitHub ...
```

**PR Submission Checklist**:
- ✅ `pio run -e default` succeeds
- ✅ `./bin/clang-format-fix` applied (no diff)
- ✅ `pio check` passes (or legitimate false positives documented)
- ✅ Commit message uses conventional format (`feat:`, `fix:`, `refactor:`)
- ✅ Build artifacts generated without warnings
- ✅ Tests pass (if test environment exists)

**Monitoring CI**:
- Watch workflow status on PR page
- Click "Details" link next to workflow name for logs
- Check for specific step failures
- Read error messages carefully

### For Release Managers

**Creating a Release**:

```bash
# 1. Update version in platformio.ini
nano platformio.ini
# Change: build_flags with new FIRMWARE_VERSION_CODE
# Example: -DFIRMWARE_VERSION_CODE=2026040500

# 2. Update CHANGELOG.md
nano CHANGELOG.md
# Add at top:
# ## X.Y.Z-vcodex (Release description)
# - Feature 1
# - Feature 2
# - Bug fix

# 3. Commit version change
git add platformio.ini CHANGELOG.md
git commit -m "chore: bump version to X.Y.Z-vcodex"

# 4. Create git tag
git tag v1.1.20-vcodex

# 5. Push both commit and tag
git push origin master --follow-tags
# OR explicitly:
git push origin master
git push origin v1.1.20-vcodex
```

**What Happens Next**:
1. GitHub Actions automatically detects the tag
2. Release workflow runs (3-5 minutes)
3. GitHub Release created automatically
4. Firmware available for download
5. No manual steps required!

**Verify Release**:
- Check [Releases page](https://github.com/franssjz/crosspoint-reader-codex/releases)
- Verify assets are present (`firmware.bin` file should be there)
- Read release notes generated from changelog

### Rollback a Release (If Needed)

```bash
# Delete the local tag
git tag -d v1.1.20-vcodex

# Delete the remote tag
git push origin :refs/tags/v1.1.20-vcodex

# Delete the GitHub Release (manual, via GitHub web)
```

---

## Troubleshooting

### Build Fails with "clang-format error"

**Problem**: Code formatting doesn't match project style

**Solution**:
```bash
./bin/clang-format-fix
git add .
git commit -m "fix: apply code formatting"
git push origin branch-name
```

### Build Fails with "cppcheck defects found"

**Problem**: Static analysis found potential bugs

**Solution**:
```bash
# Run locally to see exact issues
pio check --fail-on-defect low

# Either fix the issue, or annotate as false positive:
// cppcheck-suppress [error-type]
// code here
```

### Build Fails with Compilation Error

**Problem**: C++ code doesn't compile

**Solution**:
```bash
# Run same build locally
pio run -e default 2>&1 | tail -100

# Look for first error message
# Most common: 
# - Missing #include
# - Typo in variable/function name
# - Syntax error
```

### Release Fails: "CHANGELOG entry not found"

**Problem**: Release version not in CHANGELOG.md

**Solution**:
```bash
# Add entry to top of CHANGELOG.md:
## X.Y.Z-vcodex

- Feature 1
- Feature 2

# Commit and re-tag:
git add CHANGELOG.md
git commit -m "docs: add v1.1.20-vcodex changelog"
git tag -f v1.1.20-vcodex  # Force update tag
git push origin :refs/tags/v1.1.20-vcodex  # Delete remote tag
git push origin v1.1.20-vcodex  # Re-push
```

### Release Creates but No Artifacts

**Problem**: Compiled but artifacts missing from GitHub Release

**Solution**:
1. Check GitHub Actions log for build step failures
2. Manually re-run workflow for debugging:
   - Visit Actions → Release workflow
   - Click "Run workflow" → input version
   - Check "Collect Build Artifacts" step
3. Common cause: Build directory paths don't match

### Can't See Build Logs

**How to Access Logs**:
1. Go to Actions tab on GitHub
2. Click the failing workflow
3. Click the failing job
4. Expand steps to see full output
5. Or use GitHub CLI: `gh run view <run-id> --log`

---

## Best Practices

### 1. Version Numbering

Follow semantic versioning: `MAJOR.MINOR.PATCH-variant`

**Examples**:
- `1.1.20-vcodex` - Release version
- `1.1.20-vcodex-rc1` - Release candidate
- `1.1.20-vcodex-beta` - Beta version
- `1.2.0` - Major bump (breaking changes)

### 2. Changelog Format

**Required Format**:
```markdown
## X.Y.Z-vcodex (Human-readable description)

- **Feature Category**: description of feature
- **Bug Fix**: what was fixed and impact
- **Performance**: metrics if significant
- **Breaking Changes**: if any

Previous version entries below...
```

**Guidelines**:
- Use present tense ("Add feature" not "Added feature")
- Group by category (Features, Fixes, Performance, Breaking)
- Include version code in `platformio.ini` comments
- Link to related issues if applicable

### 3. Build Environments

Use correct environment for each purpose:

| Environment | Purpose | Use Case |
|---|---|---|
| `default` | Development | Local testing, CI verification |
| `vcodex_release` | Production | Device flashing, releases |
| `gh_release` | Release Build | GitHub Actions releases |
| `slim` | Minimal | Size/RAM constraint testing |

### 4. Commit Messages

Use conventional commits format for clarity:

```
<type>: <short description (50 chars max)>

<optional detailed description>
```

**Types**:
- `feat:` - New feature
- `fix:` - Bug fix
- `perf:` - Performance improvement
- `refactor:` - Code restructuring (no behavior change)
- `docs:` - Documentation only
- `chore:` - Build config, dependencies, version bumps
- `test:` - Adding/modifying tests

**Examples**:
```
feat: implement atomic JSON writes for crash safety

fix: reduce achievement popup freeze from 700ms to 10ms

perf: optimize settings string allocations (150 → 3 buffers)

docs: add GitHub Actions CI/CD documentation
```

### 5. Testing Before Release

**Pre-Release Checklist**:
- ✅ All CI jobs pass on `master`
- ✅ Device testing in all 4 orientations
- ✅ Memory usage stable (`ESP.getFreeHeap() > 50KB`)
- ✅ No heap corruption detected
- ✅ CHANGELOG.md updated
- ✅ Version code incremented in `platformio.ini`

### 6. Monitoring Build Performance

**Track These Metrics**:
- **RAM Usage**: Should remain below 380 KB
- **Flash Size**: Monitor firmware.bin size trends
- **Build Time**: Watch for regressions
- **Artifact Size**: Check if new dependencies added

**CI Automatically Reports**:
```
## 📊 Firmware Build Stats
- RAM: XXX bytes (Y% of 380KB)
- Flash: XXX bytes
```

### 7. Caching and Speed Optimization

**CI Caches**:
- PlatformIO packages (~500 MB) - 10+ minute savingss
- `.pio` build artifacts - 3-5 minute savings
- Python pip packages - 1-2 minute savings

**First Build**: ~5-6 minutes (cache miss)
**Subsequent Builds**: ~2-3 minutes (cache hit)

---

## Architecture Diagram

```
Developer Push
    ↓
GitHub Detects Tag/Push
    ↓
┌─ Parallel Jobs ─────────────────┐
│                                 │
│ • clang-format check (30s)       │
│ • cppcheck analysis (90s)        │
│ • build default (180s)           │
│ • build vcodex_release (180s)    │
│ • test execution (120s)          │
│                                 │
└─────────────────────────────────┘
    ↓
ci-status gate (all jobs pass?)
    ↓
┌─ If Tag Detected ────────────────┐
│                                  │
│ • Extract changelog              │
│ • Generate release notes         │
│ • Collect artifacts              │
│ • Create GitHub Release          │
│                                  │
└──────────────────────────────────┘
    ↓
✅ Release Available on GitHub
   (firmware.bin downloadable)
```

---

## Further Reading

- [GitHub Actions Docs](https://docs.github.com/en/actions)
- [PlatformIO CI/CD Integration](https://docs.platformio.org/en/latest/integration/github-actions/)
- [Conventional Commits](https://www.conventionalcommits.org/)
- [Semantic Versioning](https://semver.org/)
- [CHANGELOG.md Format](https://keepachangelog.com/)

---

**Last Updated**: 2024-04-03
**Maintained By**: cpr-vcodex Project Team
