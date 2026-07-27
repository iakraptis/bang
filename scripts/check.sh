#!/usr/bin/env bash
# Pre-PR contribution checkpoint.
#
# Runs the same gates CI runs, in the same order, so a green run here means a
# green run on your pull request. Fast path takes about a minute.
#
#   scripts/check.sh                  engine build + all tests + smoke checks
#   scripts/check.sh --with-web       also build the static WASM webapp (needs em++)
#   scripts/check.sh --with-training  also build and test the LibTorch trainer
#   scripts/check.sh --all            everything
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PYTHON="${BANG_PYTHON:-python3}"
JOBS="${BANG_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

WITH_WEB=0
WITH_TRAINING=0
for argument in "$@"; do
  case "$argument" in
    --with-web) WITH_WEB=1 ;;
    --with-training) WITH_TRAINING=1 ;;
    --all) WITH_WEB=1; WITH_TRAINING=1 ;;
    -h|--help) sed -n '2,10p' "${BASH_SOURCE[0]}" | cut -c3-; exit 0 ;;
    *) echo "unknown option: $argument" >&2; exit 2 ;;
  esac
done

step() { printf '\n\033[1m▶ %s\033[0m\n' "$1"; }
pass() { printf '\033[32m✓ %s\033[0m\n' "$1"; }

step "Configuring engine build (Release)"
cmake -S "$ROOT" -B "$ROOT/build" -DCMAKE_BUILD_TYPE=Release >/dev/null

step "Building engine with $JOBS jobs"
cmake --build "$ROOT/build" --parallel "$JOBS"

step "Running native, web, and data tests"
ctest --test-dir "$ROOT/build" --output-on-failure

step "Deterministic simulation smoke"
"$ROOT/build/bang_duel" simulate 1000 42 4

step "Web session smoke"
"$ROOT/build/bang_duel" web-smoke >/dev/null

step "Browser policy smoke"
"$ROOT/build/bang_duel" static-policy-smoke "$ROOT/policy/static-policy.json" >/dev/null

if [[ "$WITH_WEB" == 1 ]]; then
  if ! command -v em++ >/dev/null 2>&1; then
    echo "em++ not found; install Emscripten or drop --with-web" >&2
    exit 2
  fi
  step "Building static WASM webapp"
  "$ROOT/tools/build_static_web.sh" "$ROOT/web/dist-static" "$ROOT/policy/static-policy.json"
  for artifact in index.html bang_static_web.js bang_static_web.wasm bang_static_web.data; do
    test -s "$ROOT/web/dist-static/$artifact" || {
      echo "missing static artifact: $artifact" >&2
      exit 1
    }
  done
  # The bundle must stay path-relative so it can be hosted under a prefix.
  ! grep -R -E '(src|href)="/' "$ROOT/web/dist-static/index.html"
fi

if [[ "$WITH_TRAINING" == 1 ]]; then
  if ! "$PYTHON" -c 'import torch' >/dev/null 2>&1; then
    echo "PyTorch not found for $PYTHON; install it or drop --with-training" >&2
    exit 2
  fi
  step "Configuring training build (LibTorch)"
  TORCH_PREFIX="$("$PYTHON" -c 'import torch; print(torch.utils.cmake_prefix_path)')"
  cmake -S "$ROOT" -B "$ROOT/build-train" \
    -DCMAKE_BUILD_TYPE=Release \
    -DBANG_ENABLE_TORCH=ON \
    -DCMAKE_PREFIX_PATH="$TORCH_PREFIX" >/dev/null

  step "Building trainer with $JOBS jobs"
  cmake --build "$ROOT/build-train" --parallel "$JOBS"

  step "Native and training lifecycle tests"
  ctest --test-dir "$ROOT/build-train" --output-on-failure
fi

printf '\n'
pass "All checks passed. Ready to open a pull request."
