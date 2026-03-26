#!/bin/bash
#
# add-runner.sh - Add the rp2350-builder runner to another GitHub repo
#
# Run on the build server (xtreme@rbx1.re-hash.org):
#   ./add-runner.sh <owner/repo> <registration_token>
#
# To get a registration token locally:
#   gh api --method POST repos/<owner>/<repo>/actions/runners/registration-token --jq .token
#
# This creates a new runner instance under ~/runners/<repo_name>/ that shares
# the same build toolchain (Pico SDK, ARM gcc) but has its own work directory.
#

set -e

if [[ $# -lt 2 ]]; then
    echo "Usage: $0 <owner/repo> <registration_token>"
    echo ""
    echo "Example: $0 rh1tech/frank-c64 ABCDEFG123456"
    echo ""
    echo "Get a token with:"
    echo "  gh api --method POST repos/<owner>/<repo>/actions/runners/registration-token --jq .token"
    exit 1
fi

REPO="$1"
TOKEN="$2"
REPO_NAME=$(basename "$REPO")

RUNNER_DIR="$HOME/runners/$REPO_NAME"

echo "======================================"
echo " Adding rp2350-builder to $REPO"
echo "======================================"

# Check if already exists
if [[ -d "$RUNNER_DIR" && -f "$RUNNER_DIR/.runner" ]]; then
    echo "Runner already exists at $RUNNER_DIR"
    echo "To replace, first remove it:"
    echo "  cd $RUNNER_DIR && sudo ./svc.sh stop && sudo ./svc.sh uninstall && ./config.sh remove --token <token>"
    exit 1
fi

# Download runner if needed (reuse binaries from existing runner if available)
EXISTING_RUNNER=$(find "$HOME/runners" -maxdepth 2 -name "config.sh" -print -quit 2>/dev/null)
if [[ -n "$EXISTING_RUNNER" ]]; then
    EXISTING_DIR=$(dirname "$EXISTING_RUNNER")
    echo "Copying runner binaries from $EXISTING_DIR..."
    mkdir -p "$RUNNER_DIR"
    # Copy binaries, scripts, and templates (but not config/work dirs)
    for item in bin bin.* externals externals.* *.template; do
        if ls "$EXISTING_DIR"/$item 1>/dev/null 2>&1; then
            cp -a "$EXISTING_DIR"/$item "$RUNNER_DIR/" 2>/dev/null || true
        fi
    done
    # Copy .sh scripts separately (glob *.sh doesn't match in all shells via cp -a)
    for f in "$EXISTING_DIR"/*.sh; do
        [[ -f "$f" ]] && cp -a "$f" "$RUNNER_DIR/"
    done
else
    echo "Downloading GitHub Actions runner..."
    mkdir -p "$RUNNER_DIR"
    cd "$RUNNER_DIR"
    RUNNER_VERSION=$(curl -s https://api.github.com/repos/actions/runner/releases/latest | jq -r '.tag_name' | sed 's/^v//')
    curl -o actions-runner.tar.gz -L "https://github.com/actions/runner/releases/download/v${RUNNER_VERSION}/actions-runner-linux-x64-${RUNNER_VERSION}.tar.gz"
    tar xzf actions-runner.tar.gz
    rm actions-runner.tar.gz
fi

cd "$RUNNER_DIR"

# Configure
echo ""
echo "Configuring runner for $REPO..."
./config.sh \
    --url "https://github.com/$REPO" \
    --token "$TOKEN" \
    --name "rp2350-builder" \
    --labels "self-hosted,linux,rp2350" \
    --unattended \
    --replace

# Set environment (shared Pico SDK)
echo "PICO_SDK_PATH=/home/xtreme/pico-sdk" > .env

# Install as service
echo ""
echo "Installing as service..."
sudo ./svc.sh install
sudo ./svc.sh start

echo ""
echo "======================================"
echo " Done! Runner added for $REPO"
echo "======================================"
echo ""
echo "Location: $RUNNER_DIR"
echo ""
echo "In your repo's workflow, use:"
echo "  runs-on: [self-hosted, rp2350]"
