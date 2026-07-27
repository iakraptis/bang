#!/usr/bin/env bash
set -euo pipefail

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  echo "usage: tools/build_static_web.sh [output_dir] [static_policy_json] [training_run_dir]"
  exit 0
fi
if [[ $# -gt 3 ]]; then
  echo "usage: tools/build_static_web.sh [output_dir] [static_policy_json] [training_run_dir]" >&2
  exit 2
fi

OUTPUT_DIR="${1:-web/dist-static}"
STATIC_POLICY="${2:-}"
TRAINING_RUN_DIR="${3:-}"

if ! command -v em++ >/dev/null 2>&1; then
  echo "error: em++ is required; install or activate Emscripten first" >&2
  exit 1
fi
if [[ -n "$STATIC_POLICY" && ! -f "$STATIC_POLICY" ]]; then
  echo "error: static policy does not exist: $STATIC_POLICY" >&2
  exit 1
fi
if [[ -n "$TRAINING_RUN_DIR" && ! -d "$TRAINING_RUN_DIR" ]]; then
  echo "error: training run directory does not exist: $TRAINING_RUN_DIR" >&2
  exit 1
fi

OUTPUT_ABS="$(realpath -m -- "$OUTPUT_DIR")"
REPOSITORY_ABS="$(pwd -P)"
case "$OUTPUT_ABS" in
  "/"|"$HOME"|"$REPOSITORY_ABS"|"$REPOSITORY_ABS/web")
    echo "error: refusing unsafe output directory: $OUTPUT_DIR" >&2
    exit 2
    ;;
esac

BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/bang-static-web.XXXXXX")"
trap 'rm -rf "$BUILD_DIR"' EXIT

rm -rf "$OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"
cp web/index.html web/app.js web/static-engine.js web/targeting.js web/card-fx.js web/history.js web/styles.css \
  web/voxel-models.js web/voxel-stage.js \
  web/about.html web/about.css web/about.js web/about-data.js web/favicon.svg \
  "$OUTPUT_DIR/"
cp -R web/cards "$OUTPUT_DIR/"
# three.js, for the voxel battlefield in the middle of the board
cp -R web/vendor "$OUTPUT_DIR/"
if [[ -n "$TRAINING_RUN_DIR" ]]; then
  python3 tools/build_about_data.py \
    "$TRAINING_RUN_DIR" "$OUTPUT_DIR/about-data.js"
fi

SOURCES=(
  src/action_space.cpp
  src/game.cpp
  src/heuristic.cpp
  src/json.cpp
  src/static_data.cpp
  src/static_policy.cpp
  src/training_state.cpp
  src/web.cpp
)
OBJECTS=()
CXXFLAGS=(-std=c++20 -fexceptions -Iinclude)

for source in "${SOURCES[@]}"; do
  object="$BUILD_DIR/$(basename "${source%.cpp}").o"
  em++ "${CXXFLAGS[@]}" -O3 -c "$source" -o "$object"
  OBJECTS+=("$object")
done

PRELOAD_ARGS=()
if [[ -n "$STATIC_POLICY" ]]; then
  PRELOAD_ARGS=(--preload-file "$STATIC_POLICY@/static-policy.json")
fi

em++ "${CXXFLAGS[@]}" -O3 \
  "${OBJECTS[@]}" \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=createBangStaticModule \
  -sALLOW_MEMORY_GROWTH=1 \
  -sDISABLE_EXCEPTION_CATCHING=0 \
  -sENVIRONMENT=web \
  -sEXPORTED_RUNTIME_METHODS='["cwrap"]' \
  -sEXPORTED_FUNCTIONS='["_bang_static_web_init","_bang_static_web_new_game","_bang_static_web_current_state","_bang_static_web_apply_action"]' \
  "${PRELOAD_ARGS[@]}" \
  -o "$OUTPUT_DIR/bang_static_web.js"

ASSET_VERSION="$(
  python3 - "$OUTPUT_DIR" <<'PY'
from hashlib import sha256
from pathlib import Path
import sys

root = Path(sys.argv[1])
digest = sha256()
for name in (
    "bang_static_web.js",
    "bang_static_web.wasm",
    "static-engine.js",
    "app.js",
    "targeting.js",
    "card-fx.js",
    "voxel-models.js",
    "voxel-stage.js",
    "styles.css",
    "about.html",
    "about.css",
    "about.js",
    "about-data.js",
    "favicon.svg",
):
    digest.update((root / name).read_bytes())
data = root / "bang_static_web.data"
if data.is_file():
    digest.update(data.read_bytes())
print(digest.hexdigest()[:16])
PY
)"

python3 - "$OUTPUT_DIR/index.html" "$ASSET_VERSION" "$STATIC_POLICY" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
version = sys.argv[2]
has_policy = bool(sys.argv[3])
policy_attribute = (
    ' data-policy-path="/static-policy.json"' if has_policy else ""
)
html = path.read_text()
html = html.replace(
    'href="styles.css"',
    f'href="styles.css?v={version}"',
)
html = html.replace(
    'href="favicon.svg"',
    f'href="favicon.svg?v={version}"',
)
html = html.replace(
    '<script src="targeting.js"></script>',
    f'<script src="targeting.js?v={version}"></script>',
)
html = html.replace(
    '<script src="card-fx.js"></script>',
    f'<script src="card-fx.js?v={version}"></script>',
)
html = html.replace(
    '<script src="voxel-models.js"></script>',
    f'<script src="voxel-models.js?v={version}"></script>',
)
# The stage imports three.js by a relative path of its own, which stays
# unversioned on purpose: it is pinned to a release and never changes.
html = html.replace(
    '<script type="module" src="voxel-stage.js"></script>',
    f'<script type="module" src="voxel-stage.js?v={version}"></script>',
)
html = html.replace(
    '<script src="app.js"></script>',
    f'<script src="bang_static_web.js?v={version}"></script>\n'
    f'  <script src="static-engine.js?v={version}" '
    f'data-asset-version="{version}"'
    f'{policy_attribute}'
    f'></script>\n'
    f'  <script src="app.js?v={version}"></script>',
)
path.write_text(html)
PY

python3 - "$OUTPUT_DIR/about.html" "$ASSET_VERSION" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
version = sys.argv[2]
html = path.read_text()
for asset in ("about.css", "about-data.js", "about.js", "favicon.svg"):
    html = html.replace(
        f'"{asset}"',
        f'"{asset}?v={version}"',
    )
path.write_text(html)
PY

echo "Static webapp written to $OUTPUT_DIR"
