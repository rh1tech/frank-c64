#!/bin/bash
#
# setup-runner.sh - Set up GitHub Actions self-hosted runner on the build server
#
# Run this on the build server (xtreme@rbx1.re-hash.org):
#   scp setup-runner.sh xtreme@rbx1.re-hash.org:~
#   ssh xtreme@rbx1.re-hash.org 'chmod +x setup-runner.sh && ./setup-runner.sh'
#
# Prerequisites: The server must have internet access.
#
# What this script installs:
#   1. Build dependencies (cmake, gcc-arm-none-eabi, etc.)
#   2. Raspberry Pi Pico SDK
#   3. GitHub Actions runner
#
# NOTE: If rp2350-builder is already set up for another repo (e.g. murmduke32),
# you only need add-runner.sh to register it for this repo.
#

set -e

echo "========================================"
echo " MurmC64 CI Runner Setup"
echo "========================================"

# --- Step 1: Install build dependencies ---
echo ""
echo "[1/4] Installing build dependencies..."

sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    gcc-arm-none-eabi \
    libnewlib-arm-none-eabi \
    libstdc++-arm-none-eabi-newlib \
    git \
    python3 \
    curl \
    jq \
    tar \
    gzip

echo "✓ Build dependencies installed"

# --- Step 2: Install Pico SDK ---
echo ""
echo "[2/4] Setting up Raspberry Pi Pico SDK..."

PICO_SDK_DIR="$HOME/pico-sdk"
if [[ ! -d "$PICO_SDK_DIR" ]]; then
    git clone https://github.com/raspberrypi/pico-sdk.git "$PICO_SDK_DIR"
    cd "$PICO_SDK_DIR"
    git submodule update --init
    cd "$HOME"
else
    echo "Pico SDK already exists at $PICO_SDK_DIR, updating..."
    cd "$PICO_SDK_DIR"
    git pull
    git submodule update --init
    cd "$HOME"
fi

# Set PICO_SDK_PATH in .bashrc if not already there
if ! grep -q "PICO_SDK_PATH" "$HOME/.bashrc"; then
    echo "" >> "$HOME/.bashrc"
    echo "# Pico SDK" >> "$HOME/.bashrc"
    echo "export PICO_SDK_PATH=$PICO_SDK_DIR" >> "$HOME/.bashrc"
fi
export PICO_SDK_PATH="$PICO_SDK_DIR"

echo "✓ Pico SDK installed at $PICO_SDK_DIR"

# --- Step 3: Install GitHub Actions runner ---
echo ""
echo "[3/4] Setting up GitHub Actions runner..."
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " You need a runner registration token from GitHub."
echo ""
echo " Go to: https://github.com/rh1tech/murmc64/settings/actions/runners/new"
echo " Copy the token shown in the 'Configure' section."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""
read -p "Paste the runner token here: " RUNNER_TOKEN

if [[ -z "$RUNNER_TOKEN" ]]; then
    echo "Error: No token provided. You can re-run this step later."
    echo "Skipping runner setup."
else
    RUNNER_DIR="$HOME/actions-runner"
    mkdir -p "$RUNNER_DIR"
    cd "$RUNNER_DIR"

    # Download latest runner
    RUNNER_VERSION=$(curl -s https://api.github.com/repos/actions/runner/releases/latest | jq -r '.tag_name' | sed 's/^v//')
    RUNNER_ARCH=$(dpkg --print-architecture)

    if [[ "$RUNNER_ARCH" == "amd64" ]]; then
        RUNNER_ARCH="x64"
    elif [[ "$RUNNER_ARCH" == "arm64" || "$RUNNER_ARCH" == "aarch64" ]]; then
        RUNNER_ARCH="arm64"
    fi

    RUNNER_URL="https://github.com/actions/runner/releases/download/v${RUNNER_VERSION}/actions-runner-linux-${RUNNER_ARCH}-${RUNNER_VERSION}.tar.gz"
    echo "Downloading runner v${RUNNER_VERSION} for ${RUNNER_ARCH}..."
    curl -o actions-runner.tar.gz -L "$RUNNER_URL"
    tar xzf actions-runner.tar.gz
    rm actions-runner.tar.gz

    # Configure runner
    ./config.sh \
        --url https://github.com/rh1tech/murmc64 \
        --token "$RUNNER_TOKEN" \
        --name "rp2350-builder" \
        --labels "self-hosted,linux,rp2350" \
        --unattended \
        --replace

    echo "✓ Runner configured"
fi

# --- Step 4: Install runner as a service ---
echo ""
echo "[4/4] Installing runner as systemd service..."

cd "$HOME/actions-runner"
sudo ./svc.sh install
sudo ./svc.sh start

echo "✓ Runner service installed and started"

# --- Done ---
echo ""
echo "========================================"
echo " Setup Complete!"
echo "========================================"
echo ""
echo "Runner status: sudo ./svc.sh status"
echo "Runner logs:   journalctl -u actions.runner.rh1tech-murmc64.rp2350-builder.service -f"
echo ""
echo "PICO_SDK_PATH=$PICO_SDK_PATH"
echo ""
echo "To test the build manually:"
echo "  cd /tmp && git clone https://github.com/rh1tech/murmc64.git && cd murmc64"
echo "  chmod +x release-ci.sh && ./release-ci.sh 1 00"
echo ""
echo "The runner will now pick up jobs from GitHub Actions automatically."
echo "Trigger a release by committing with message: 'release: 1.04'"
