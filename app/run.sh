#!/usr/bin/env bash
# Starts the TUSP Visualizer on http://localhost:${PORT:-8000}
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

if [ ! -d app/.venv ]; then
  python3 -m venv app/.venv
fi
# shellcheck disable=SC1091
source app/.venv/bin/activate
pip install -q -r app/backend/requirements.txt

if [ ! -d app/frontend/node_modules ]; then
  (cd app/frontend && npm install --no-audit --no-fund)
fi
if [ ! -d app/frontend/dist ] || [ -n "$(find app/frontend/src -newer app/frontend/dist -print -quit)" ]; then
  (cd app/frontend && npm run build)
fi

if [ -x build/cli_tusp_solver ] || [ -n "${TUSP_SOLVER:-}" ]; then
  echo "Solver: ${TUSP_SOLVER:-build/cli_tusp_solver}"
else
  echo "Solver binary not found: replay mode only (see app/README.md to build it)."
fi
exec uvicorn main:app --app-dir app/backend --host 127.0.0.1 --port "${PORT:-8000}"
