# CI/CD Pipeline Documentation

## Overview

This project uses a **self-hosted GitHub Actions runner** named `rp2350-builder` to build firmware releases. The runner lives on a dedicated Debian x86_64 build server and is shared across multiple RP2350-based repositories under the `rh1tech` GitHub account.

When a commit message starts with `release:` followed by a version number, the pipeline automatically builds all 40 firmware variants and publishes them as a GitHub Release.

## Architecture

```
Developer Machine                  GitHub                     Build Server
┌──────────────┐              ┌──────────────┐           ┌─────────────────────┐
│ git commit   │──── push ───>│ Actions      │── job ──> │ rp2350-builder      │
│ "release: X" │              │ workflow     │           │ (self-hosted)       │
└──────────────┘              │ triggered    │           │                     │
                              └──────┬───────┘           │ ~/pico-sdk/         │
                                     │                   │ ~/runners/          │
                              ┌──────▼───────┐           │   ├── frank-c64/      │
                              │ GitHub       │<── .uf2 ──│   ├── murmduke3d/   │
                              │ Release      │           │   └── ...           │
                              │ created      │           └─────────────────────┘
                              └──────────────┘
```

### Build server details

| Property | Value |
|----------|-------|
| Host | `rbx1.re-hash.org` |
| User | `xtreme` |
| OS | Debian 12 (Bookworm), x86_64 |
| Runner name | `rp2350-builder` |
| Runner labels | `self-hosted`, `linux`, `rp2350` |
| Pico SDK path | `/home/xtreme/pico-sdk` |
| Runner instances | `/home/xtreme/runners/<repo_name>/` |
| Service name pattern | `actions.runner.<owner>-<repo>.rp2350-builder.service` |

### Installed build toolchain

- `gcc-arm-none-eabi` — ARM cross-compiler for RP2350
- `cmake` — build system
- `libnewlib-arm-none-eabi` — C standard library for ARM
- `libstdc++-arm-none-eabi-newlib` — C++ standard library for ARM
- `pico-sdk` (cloned at `~/pico-sdk` with submodules)

## Triggering a Release

Commit with a message starting with `release:` followed by a version in `MAJOR.MINOR` format:

```bash
git commit -m "release: 1.04"
git push
```

### Accepted commit message formats

| Format | Parsed version | Valid |
|--------|---------------|-------|
| `release: 1.04` | 1.04 | ✅ |
| `release:1.04` | 1.04 | ✅ |
| `release: 2.00` | 2.00 | ✅ |
| `release: 1.04 - fixed audio` | 1.04 | ✅ (text after version ignored) |
| `Release: 1.04` | — | ❌ (case-sensitive) |
| `release: 1.4` | 1.04 | ✅ (minor zero-padded automatically) |
| `fix: something` | — | ❌ (not a release, workflow skipped) |

### Version rules

- **Major**: integer ≥ 1
- **Minor**: integer 0–99 (displayed zero-padded as two digits)
- Version is stored in `version.txt` as `MAJOR MINOR` (space-separated)

## What the Pipeline Does

When triggered by a `release:` commit on the `main` or `master` branch:

1. **Checks out** the repository with submodules
2. **Parses** the version from the first line of the commit message
3. **Runs `release-ci.sh`** which builds all 20 firmware variants:

### Build matrix

| Dimension | Values | Count |
|-----------|--------|-------|
| Board | M1, M2 | 2 |
| Video | VGA, HDMI | 2 |
| Audio | I2S, PWM | 2 |
| CPU Speed | 378, 428 (VGA only), 504 MHz | 2–3 |

**Total**: 20 variants (428 MHz only valid for VGA)

### Output filename convention

```
frank-c64_m<BOARD>_<video>_<audio>_<cpu>mhz_<MAJOR>_<MINOR>.uf2
```

- `BOARD` — `1` (M1) or `2` (M2)
- `video` — `vga` or `hdmi`
- `audio` — `i2s` or `pwm`
- `cpu` — CPU clock in MHz
- `MAJOR_MINOR` — version with minor zero-padded (e.g., `1_04`)

### CMake flags used per build

```
-DPICO_PLATFORM=rp2350
-DBOARD_VARIANT=<M1|M2>
-DVIDEO_TYPE=<VGA|HDMI>
-DAUDIO_TYPE=<I2S|PWM>
-DCPU_SPEED=<378|428|504>
-DUSB_HID_ENABLED=1
-DDEBUG_LOGS_ENABLED=OFF
-DFIRMWARE_VERSION="v<MAJOR>.<MINOR>"
```

4. **Creates a GitHub Release** tagged `vMAJOR.MINOR` with all firmware files attached

## Files

| File | Purpose |
|------|---------|
| `.github/workflows/release.yml` | GitHub Actions workflow definition |
| `release-ci.sh` | Non-interactive release build script (called by CI) |
| `release.sh` | Interactive release build script (for local manual builds) |
| `setup-runner.sh` | Initial server setup (installs toolchain, Pico SDK, first runner) |
| `add-runner.sh` | Adds the runner to additional repos (run on server) |
| `version.txt` | Current version as `MAJOR MINOR` |

## Workflow YAML Reference

The workflow file at `.github/workflows/release.yml`:

- **Trigger**: `push` to `main` or `master`
- **Condition**: `if: startsWith(github.event.head_commit.message, 'release:')`
- **Permissions**: `contents: write` (required to create releases and tags)
- **Runner**: `runs-on: [self-hosted, rp2350]`
- **Release action**: `softprops/action-gh-release@v2`
- **Auth**: Uses the automatic `GITHUB_TOKEN` secret (no manual token setup needed)

## Adding the Runner to This Repository

If the `rp2350-builder` runner is already set up for another repo (e.g., murmduke32), you only need to register it for this repo:

### Steps

From your local machine with `gh` CLI authenticated:

```bash
# 1. Generate a registration token for this repo
TOKEN=$(GH_PAGER=cat gh api --method POST repos/rh1tech/frank-c64/actions/runners/registration-token --jq .token)

# 2. SSH into the server and run the add script
ssh xtreme@rbx1.re-hash.org "~/add-runner.sh rh1tech/frank-c64 $TOKEN"
```

### What `add-runner.sh` does

1. Creates `~/runners/frank-c64/` on the server
2. Copies runner binaries from an existing instance (avoids re-downloading)
3. Registers with GitHub using name `rp2350-builder` and labels `self-hosted,linux,rp2350`
4. Sets `PICO_SDK_PATH=/home/xtreme/pico-sdk` in the runner's `.env` file
5. Installs and starts a systemd service for the runner

### Fresh server setup

If the build server hasn't been set up at all yet:

```bash
scp setup-runner.sh xtreme@rbx1.re-hash.org:~
ssh xtreme@rbx1.re-hash.org 'chmod +x setup-runner.sh && ./setup-runner.sh'
```

## Server Administration

### Check runner status

```bash
ssh xtreme@rbx1.re-hash.org 'sudo systemctl status actions.runner.rh1tech-frank-c64.rp2350-builder.service'
```

### View runner logs

```bash
ssh xtreme@rbx1.re-hash.org 'journalctl -u actions.runner.rh1tech-frank-c64.rp2350-builder.service -f'
```

### Restart runner

```bash
ssh xtreme@rbx1.re-hash.org 'cd ~/runners/frank-c64 && sudo ./svc.sh stop && sudo ./svc.sh start'
```

### List all runner services on the server

```bash
ssh xtreme@rbx1.re-hash.org 'systemctl list-units --type=service | grep actions.runner'
```

### Remove runner from this repo

```bash
# Get a removal token
TOKEN=$(GH_PAGER=cat gh api --method POST repos/rh1tech/frank-c64/actions/runners/remove-token --jq .token)

# Remove on server
ssh xtreme@rbx1.re-hash.org "cd ~/runners/frank-c64 && sudo ./svc.sh stop && sudo ./svc.sh uninstall && ./config.sh remove --token $TOKEN"
```

### Update Pico SDK

```bash
ssh xtreme@rbx1.re-hash.org 'cd ~/pico-sdk && git pull && git submodule update --init'
```

## Troubleshooting

### Workflow doesn't trigger

- Commit message must start with `release:` (lowercase, with colon)
- Push must be to `main` or `master` branch
- Check the Actions tab on GitHub for skipped runs

### Runner shows as offline

```bash
# Check service status
ssh xtreme@rbx1.re-hash.org 'cd ~/runners/frank-c64 && sudo ./svc.sh status'

# Restart
ssh xtreme@rbx1.re-hash.org 'cd ~/runners/frank-c64 && sudo ./svc.sh stop && sudo ./svc.sh start'
```

### Build fails with "PICO_SDK_PATH not set"

The runner's `.env` file may be missing or incorrect:

```bash
ssh xtreme@rbx1.re-hash.org 'cat ~/runners/frank-c64/.env'
# Should show: PICO_SDK_PATH=/home/xtreme/pico-sdk
```

Fix:

```bash
ssh xtreme@rbx1.re-hash.org 'echo "PICO_SDK_PATH=/home/xtreme/pico-sdk" > ~/runners/frank-c64/.env'
```

### Broken symlinks after runner auto-update

The GitHub runner auto-updates itself. If `bin` or `externals` become broken symlinks:

```bash
ssh xtreme@rbx1.re-hash.org 'cd ~/runners/frank-c64 && ls -la bin externals'
# If broken, fix by pointing to latest versioned directory:
ssh xtreme@rbx1.re-hash.org 'cd ~/runners/frank-c64 && rm -f bin externals && ln -s bin.* bin && ln -s externals.* externals'
```

### Version already exists on GitHub

If you push `release: 1.04` but tag `v1.04` already exists, the release step will fail. Either:
- Bump the version number, or
- Delete the existing release and tag on GitHub first
