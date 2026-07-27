#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PYTHON="${BANG_PYTHON:-python3}"
BUILD="$ROOT/build-train"

if ! "$PYTHON" -c 'import torch; print(torch.__version__)' >/dev/null 2>&1; then
  echo "PyTorch is required. Install it for $PYTHON before training." >&2
  exit 2
fi

# OpenBLAS defaults to one thread per logical core, which oversubscribes against
# Torch's own OpenMP pool. Measured on this host: leaving it unset makes a
# rollout 60s instead of 0.9s, which is worse than the unoptimized reference
# BLAS. Keep it aligned with --torch-threads.
export OPENBLAS_NUM_THREADS="${OPENBLAS_NUM_THREADS:-6}"

TORCH_PREFIX="$("$PYTHON" -c 'import torch; print(torch.utils.cmake_prefix_path)')"
echo "▶ Configuring optimized CPU training build"
cmake -S "$ROOT" -B "$BUILD" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBANG_ENABLE_TORCH=ON \
  -DBANG_BUILD_TESTS=ON \
  -DCMAKE_PREFIX_PATH="$TORCH_PREFIX"
echo "▶ Building with 6 parallel jobs"
cmake --build "$BUILD" --parallel 6
echo "▶ Running native correctness and information-boundary tests"
ctest --test-dir "$BUILD" --output-on-failure
echo "▶ Starting League PPO campaign"
exec "$PYTHON" "$ROOT/tools/train.py" --repo-root "$ROOT" "$@"
