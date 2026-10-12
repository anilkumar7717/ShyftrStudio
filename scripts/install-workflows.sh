#!/usr/bin/env bash
# Copies the GitHub Actions workflows from ci/workflows/ into .github/workflows/
# (GitHub only runs workflows from .github/workflows). Run once, then commit and push.
set -euo pipefail
cd "$(dirname "$0")/.."
mkdir -p .github/workflows
cp ci/workflows/*.yml .github/workflows/
echo "Installed:"; ls -1 .github/workflows
echo "Now commit and push, then open the repository's Actions tab on GitHub."
