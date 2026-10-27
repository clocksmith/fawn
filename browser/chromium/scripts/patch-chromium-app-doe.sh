#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/lane-paths.sh"
LANE_ROOT="${FAWN_CHROMIUM_LANE_DIR}"

usage() {
  cat <<'EOF'
Usage:
  ./scripts/patch-chromium-app-doe.sh --app PATH [--doe-lib PATH] [--app-name NAME] [--force]

Patches a Chromium.app bundle so direct launch defaults to:
  --use-webgpu-runtime=doe
  --doe-webgpu-library-path=<resolved lib>

The script bundles the selected runtime (and C compiler sibling) under
Contents/Frameworks, renames the original binary to Chromium-real, and
installs a relocatable wrapper at Contents/MacOS/Chromium.

It also updates bundle metadata so the app displays as `Fawn` by default and
forces the dock icon from the canonical Fawn icon asset.
EOF
}

fawn_default_doe_lib() {
  fawn_default_doe_lib_candidates
}

fawn_default_doe_icon_path() {
  printf "%s\n" "${LANE_ROOT}/assets/logo/source/fawn-icon-main.svg"
}

fawn_default_doe_compiled_icns() {
  printf "%s\n" "${LANE_ROOT}/assets/logo/compiled/macos/fawn-icon-main.icns"
}

fawn_default_doe_lib_name() {
  local source="$1"
  local ext
  ext="$(printf '%s' "${source##*.}" | tr '[:upper:]' '[:lower:]')"
  if [[ "${ext}" == "svg" ]]; then
    echo "fawn-icon-main.icns"
    return 0
  fi
  echo "$(basename "${source}")"
}

fawn_convert_svg_to_icns() {
  local svg_path="$1"
  local out_icns="$2"
  local work_dir
  local iconset_dir

  work_dir="$(mktemp -d)"
  iconset_dir="${work_dir}/fawn.iconset"
  mkdir -p "${iconset_dir}"

  # Keep a single temporary root for cleanup regardless of conversion path.
  TMP_ICON_WORK="${work_dir}"

  sips --setProperty format png "${svg_path}" --out "${iconset_dir}/icon_16x16.png" --resampleHeightWidthMax 16 >/dev/null
  sips --setProperty format png "${svg_path}" --out "${iconset_dir}/icon_16x16@2x.png" --resampleHeightWidthMax 32 >/dev/null
  sips --setProperty format png "${svg_path}" --out "${iconset_dir}/icon_32x32.png" --resampleHeightWidthMax 32 >/dev/null
  sips --setProperty format png "${svg_path}" --out "${iconset_dir}/icon_32x32@2x.png" --resampleHeightWidthMax 64 >/dev/null
  sips --setProperty format png "${svg_path}" --out "${iconset_dir}/icon_64x64.png" --resampleHeightWidthMax 64 >/dev/null
  sips --setProperty format png "${svg_path}" --out "${iconset_dir}/icon_64x64@2x.png" --resampleHeightWidthMax 128 >/dev/null
  sips --setProperty format png "${svg_path}" --out "${iconset_dir}/icon_128x128.png" --resampleHeightWidthMax 128 >/dev/null
  sips --setProperty format png "${svg_path}" --out "${iconset_dir}/icon_128x128@2x.png" --resampleHeightWidthMax 256 >/dev/null
  sips --setProperty format png "${svg_path}" --out "${iconset_dir}/icon_256x256.png" --resampleHeightWidthMax 256 >/dev/null
  sips --setProperty format png "${svg_path}" --out "${iconset_dir}/icon_256x256@2x.png" --resampleHeightWidthMax 512 >/dev/null
  sips --setProperty format png "${svg_path}" --out "${iconset_dir}/icon_512x512.png" --resampleHeightWidthMax 512 >/dev/null
  sips --setProperty format png "${svg_path}" --out "${iconset_dir}/icon_512x512@2x.png" --resampleHeightWidthMax 1024 >/dev/null

  iconutil -c icns "${iconset_dir}" -o "${out_icns}"
}

APP_PATH=""
DOE_LIB="${FAWN_DOE_LIB:-}"
APP_NAME="${FAWN_APP_NAME:-Fawn}"
APP_ICON="${FAWN_APP_ICON:-}"
FORCE=0
TMP_ICON_WORK=""
CANONICAL_APP_ICON="$(fawn_default_doe_icon_path)"

while [[ $# -gt 0 ]]; do
  case "$1" in
    --app)
      if [[ $# -lt 2 ]]; then
        echo "missing value for --app" >&2
        exit 2
      fi
      APP_PATH="$2"
      shift 2
      ;;
    --doe-lib)
      if [[ $# -lt 2 ]]; then
        echo "missing value for --doe-lib" >&2
        exit 2
      fi
      DOE_LIB="$2"
      shift 2
      ;;
    --app-name)
      if [[ $# -lt 2 ]]; then
        echo "missing value for --app-name" >&2
        exit 2
      fi
      APP_NAME="$2"
      shift 2
      ;;
    --force)
      FORCE=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ -z "${APP_PATH}" ]]; then
  echo "--app is required" >&2
  usage
  exit 2
fi

trap '[[ -n "${TMP_ICON_WORK}" ]] && rm -rf "${TMP_ICON_WORK}"' EXIT

if [[ ! -d "${APP_PATH}" ]]; then
  echo "missing app bundle: ${APP_PATH}" >&2
  exit 1
fi

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "Mac-only operation (darwin only)" >&2
  exit 1
fi

if [[ -z "${DOE_LIB}" ]]; then
  while IFS= read -r candidate; do
    if [[ -f "${candidate}" ]]; then
      DOE_LIB="${candidate}"
      break
    fi
  done < <(fawn_default_doe_lib)
fi

if [[ -z "${DOE_LIB}" || ! -f "${DOE_LIB}" ]]; then
  echo "missing libwebgpu_doe path; set --doe-lib or FAWN_DOE_LIB" >&2
  exit 1
fi

# The C runtime is a two-library deployment; fail before mutating the app if
# its compiler sibling is absent. Explicit legacy library overrides remain valid.
if [[ "$(basename "${DOE_LIB}")" == libwebgpu_doe_c.dylib ]] && \
   [[ ! -f "$(dirname "${DOE_LIB}")/libdoe_compiler.dylib" ]]; then
  echo "missing C compiler sibling beside ${DOE_LIB}" >&2
  exit 1
fi

# The GPU sandbox can read its own app bundle, not arbitrary build directories.
# C packages must retain their sibling-only compiler lookup after relocation.
if [[ "$(basename "${DOE_LIB}")" == libwebgpu_doe_c.dylib ]]; then
  if ! otool -l "${DOE_LIB}" | grep -Fq 'path @loader_path (offset'; then
    echo "C runtime needs @loader_path; build it with runtime/c/tools/build-native-package.py" >&2
    exit 1
  fi
fi

FRAMEWORKS_DIR="${APP_PATH}/Contents/Frameworks"
DOE_LIB_NAME="$(basename "${DOE_LIB}")"
mkdir -p "${FRAMEWORKS_DIR}"
RUNTIME_DESTINATION="${FRAMEWORKS_DIR}/${DOE_LIB_NAME}"
if [[ ! "${DOE_LIB}" -ef "${RUNTIME_DESTINATION}" ]]; then
  cp -p "${DOE_LIB}" "${RUNTIME_DESTINATION}"
fi
if [[ "${DOE_LIB_NAME}" == libwebgpu_doe_c.dylib ]]; then
  COMPILER_SOURCE="$(dirname "${DOE_LIB}")/libdoe_compiler.dylib"
  COMPILER_DESTINATION="${FRAMEWORKS_DIR}/libdoe_compiler.dylib"
  if [[ ! "${COMPILER_SOURCE}" -ef "${COMPILER_DESTINATION}" ]]; then
    cp -p "${COMPILER_SOURCE}" "${COMPILER_DESTINATION}"
  fi
fi
DOE_LIB="${FRAMEWORKS_DIR}/${DOE_LIB_NAME}"

MACOS_BIN="${APP_PATH}/Contents/MacOS/Chromium"
REAL_BIN="${MACOS_BIN}-real"
WRAPPER_MARKER="${MACOS_BIN}.fawn-doe-wrapped"

is_fawn_doe_wrapper() {
  local binary_path="$1"
  [[ -f "${binary_path}" ]] \
    && grep -Fq 'REAL_BINARY="${SCRIPT_DIR}/Chromium-real"' "${binary_path}"
}

if [[ ! -x "${MACOS_BIN}" ]]; then
  echo "missing executable: ${MACOS_BIN}" >&2
  exit 1
fi

current_is_wrapper=0
if is_fawn_doe_wrapper "${MACOS_BIN}"; then
  current_is_wrapper=1
fi

write_wrapper=0
if [[ "${current_is_wrapper}" -eq 1 ]]; then
  if [[ ! -f "${REAL_BIN}" ]]; then
    echo "wrapped app is missing real binary: ${REAL_BIN}" >&2
    exit 1
  fi
  if [[ "${FORCE}" -eq 1 ]]; then
    write_wrapper=1
  else
    echo "already wrapped: ${MACOS_BIN}"
  fi
else
  if [[ -f "${REAL_BIN}" ]]; then
    echo "refreshing relinked Chromium binary: ${REAL_BIN}"
  fi
  mv -f "${MACOS_BIN}" "${REAL_BIN}"
  write_wrapper=1
fi

if [[ "${write_wrapper}" -eq 1 ]]; then

  cat > "${MACOS_BIN}" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REAL_BINARY="${SCRIPT_DIR}/Chromium-real"
EOF
  printf 'DOE_LIB_NAME=%q\n' "${DOE_LIB_NAME}" >> "${MACOS_BIN}"
  cat >> "${MACOS_BIN}" <<'EOF'
DOE_LIB="${SCRIPT_DIR}/../Frameworks/${DOE_LIB_NAME}"

if [[ -n "${FAWN_DOE_LIB:-}" ]]; then
  DOE_LIB="${FAWN_DOE_LIB}"
fi

if [[ ! -x "${REAL_BINARY}" ]]; then
  echo "missing real Chromium binary: ${REAL_BINARY}" >&2
  exit 1
fi

# Chromium trims ASCII whitespace and accepts one or two leading dashes on
# POSIX. Protect the selected provider against those equivalent spellings.
RUNTIME=doe
MODE_SEEN=0
PROFILE_SEEN=0
POSITIONAL=0
FORWARDED=(--no-first-run)
for ARGUMENT in "$@"; do
  VIEW="${ARGUMENT#"${ARGUMENT%%[!$' \t\r\n\v\f']*}"}"
  VIEW="${VIEW%"${VIEW##*[!$' \t\r\n\v\f']}"}"
  if [[ "${POSITIONAL}" -eq 0 && "${VIEW}" == -- ]]; then
    POSITIONAL=1
  elif [[ "${POSITIONAL}" -eq 0 && "${VIEW}" == -* ]]; then
    KEY="${VIEW#-}"
    KEY="${KEY#-}"
    VALUE=""
    if [[ "${KEY}" == *=* ]]; then VALUE="${KEY#*=}"; fi
    KEY="${KEY%%=*}"
    case "${KEY}" in
      use-webgpu-runtime)
        if [[ "${MODE_SEEN}" -ne 0 || ( "${VALUE}" != doe && "${VALUE}" != dawn ) ]]; then
          echo "Fawn: select exactly one explicit doe or dawn runtime" >&2
          exit 1
        fi
        RUNTIME="${VALUE}"
        MODE_SEEN=1
        continue
        ;;
      doe-webgpu-library-path)
        echo "Fawn: the product launcher selects its bundled runtime; use the raw image for diagnostics" >&2
        exit 1
        ;;
      user-data-dir)
        if [[ "${PROFILE_SEEN}" -ne 0 || "${VALUE}" != /* ]]; then
          echo "Fawn: select one absolute user-data-dir" >&2
          exit 1
        fi
        PROFILE_SEEN=1
        ;;
    esac
  fi
  FORWARDED+=("${ARGUMENT}")
done

SELECTED=(--use-webgpu-runtime=doe)
if [[ "${RUNTIME}" == dawn ]]; then
  SELECTED=(--use-webgpu-runtime=dawn)
else
  if [[ -z "${DOE_LIB}" || ! -f "${DOE_LIB}" ]]; then
    echo "missing libwebgpu_doe runtime: ${DOE_LIB}; no fallback" >&2
    exit 1
  fi
  SELECTED+=(--doe-webgpu-library-path="${DOE_LIB}")
fi
if [[ "${PROFILE_SEEN}" -eq 0 ]]; then
  if [[ "${HOME:-}" != /* ]]; then
    echo "Fawn: cannot resolve a private profile; select an absolute user-data-dir" >&2
    exit 1
  fi
  PROFILE=doe-c
  if [[ "${RUNTIME}" == dawn ]]; then PROFILE=dawn; fi
  SELECTED+=(--user-data-dir="${HOME}/Library/Application Support/Fawn/${PROFILE}")
fi
FAWN_START="${SCRIPT_DIR}/../Resources/fawn-start.html"

exec -a fawn-doe "${REAL_BINARY}" "${SELECTED[@]}" \
  --homepage="file://${FAWN_START}" \
  --new-window "file://${FAWN_START}" \
  "${FORWARDED[@]}"
EOF
  chmod 0755 "${MACOS_BIN}"
fi
touch "${WRAPPER_MARKER}"

INFO_PLIST="${APP_PATH}/Contents/Info.plist"
RESOURCES_DIR="${APP_PATH}/Contents/Resources"

if [[ -x "/usr/libexec/PlistBuddy" && -f "${INFO_PLIST}" ]]; then
  if [[ -n "${APP_NAME}" ]]; then
    /usr/libexec/PlistBuddy -c "Set :CFBundleName ${APP_NAME}" "${INFO_PLIST}" >/dev/null 2>&1 \
      || /usr/libexec/PlistBuddy -c "Add :CFBundleName string ${APP_NAME}" "${INFO_PLIST}" >/dev/null 2>&1
    /usr/libexec/PlistBuddy -c "Set :CFBundleDisplayName ${APP_NAME}" "${INFO_PLIST}" >/dev/null 2>&1 \
      || /usr/libexec/PlistBuddy -c "Add :CFBundleDisplayName string ${APP_NAME}" "${INFO_PLIST}" >/dev/null 2>&1
    /usr/libexec/PlistBuddy -c "Set :CFBundleSpokenName ${APP_NAME}" "${INFO_PLIST}" >/dev/null 2>&1 \
      || /usr/libexec/PlistBuddy -c "Add :CFBundleSpokenName string ${APP_NAME}" "${INFO_PLIST}" >/dev/null 2>&1
  fi
fi

if [[ -z "${APP_ICON}" ]]; then
  APP_ICON="${CANONICAL_APP_ICON}"
fi

if [[ "${APP_ICON}" != "${CANONICAL_APP_ICON}" ]]; then
  echo "error: app icon is fixed for this lane; expected ${CANONICAL_APP_ICON}" >&2
  echo "  provided: ${APP_ICON}" >&2
  exit 1
fi

if [[ ! -f "${APP_ICON}" ]]; then
  echo "missing app icon path: ${APP_ICON}" >&2
  exit 1
fi

if [[ -n "${APP_ICON}" && -f "${APP_ICON}" ]]; then
  APP_ICON_NAME="$(fawn_default_doe_lib_name "${APP_ICON}")"
  APP_ICON_EXT="$(printf '%s' "${APP_ICON##*.}" | tr '[:upper:]' '[:lower:]')"
  if [[ "${APP_ICON_EXT}" == "svg" ]]; then
    APP_ICON_PATH="${RESOURCES_DIR}/${APP_ICON_NAME}"
    if [[ "${APP_ICON}" == "${CANONICAL_APP_ICON}" ]]; then
      PRECOMPILED_APP_ICON="$(fawn_default_doe_compiled_icns)"
      if [[ -f "${PRECOMPILED_APP_ICON}" ]]; then
        APP_ICON_PATH="${PRECOMPILED_APP_ICON}"
      else
        fawn_convert_svg_to_icns "${APP_ICON}" "${RESOURCES_DIR}/${APP_ICON_NAME}"
        APP_ICON_PATH="${RESOURCES_DIR}/${APP_ICON_NAME}"
      fi
    else
      fawn_convert_svg_to_icns "${APP_ICON}" "${RESOURCES_DIR}/${APP_ICON_NAME}"
    fi
  else
    APP_ICON_PATH="${APP_ICON}"
  fi

  if [[ -d "${RESOURCES_DIR}" ]]; then
    cp -f "${APP_ICON_PATH}" "${RESOURCES_DIR}/app.icns"
    if [[ -x "/usr/libexec/PlistBuddy" ]]; then
      /usr/libexec/PlistBuddy -c "Delete :CFBundleIconName" "${INFO_PLIST}" >/dev/null 2>&1 || true
      /usr/libexec/PlistBuddy -c "Set :CFBundleIconFile app.icns" "${INFO_PLIST}" >/dev/null 2>&1 \
        || /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string app.icns" "${INFO_PLIST}" >/dev/null 2>&1
    fi
  else
    echo "missing resources dir: ${RESOURCES_DIR}" >&2
  fi
else
  if [[ -n "${APP_ICON}" && ! -f "${APP_ICON}" ]]; then
    echo "warning: provided app icon not found: ${APP_ICON}" >&2
  fi
fi

if [[ -d "${RESOURCES_DIR}" ]]; then
  for RESOURCE_SRC in "${LANE_ROOT}"/resources/fawn-*.{html,svg,js}; do
    if [[ -f "${RESOURCE_SRC}" ]]; then
      cp -f "${RESOURCE_SRC}" "${RESOURCES_DIR}/$(basename "${RESOURCE_SRC}")"
    fi
  done
fi

echo "patched app bundle: ${APP_PATH}"
echo "runtime default: --use-webgpu-runtime=doe --doe-webgpu-library-path=${DOE_LIB}"
if [[ -n "${APP_NAME}" ]]; then
  echo "app name: ${APP_NAME}"
fi
if [[ -f "${RESOURCES_DIR}/app.icns" ]]; then
  echo "app icon: ${APP_ICON:-auto-discovered}"
fi
