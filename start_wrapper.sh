#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
exec uv run python -m wrapper "${1:-geometry-divB.yml}" "${@:2}"
