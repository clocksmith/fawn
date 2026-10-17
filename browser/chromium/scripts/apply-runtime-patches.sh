#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PATCH_DIR="${SCRIPT_DIR}/../patches"
CHROMIUM_REVISION="4a6fb38567da21dc86f8ba468ac0ca9002f4f606"
DAWN_REVISION="758e5e6a974e8ca8039b1d7b209646dabccbcb7f"
check_only=false
if [[ "${1:-}" == "--check" ]]; then
  check_only=true
  shift
fi
if [[ "$#" -gt 1 ]]; then
  echo "usage: $0 [--check] [chromium-source-root]" >&2
  exit 2
fi
source_root="${1:-${SCRIPT_DIR}/../src}"
roots=("${source_root}" "${source_root}/third_party/dawn")
revisions=("${CHROMIUM_REVISION}" "${DAWN_REVISION}")
patches=("${PATCH_DIR}/chromium-doe-runtime.patch" "${PATCH_DIR}/dawn-doe-runtime.patch")
pending=()

# Check both repositories before changing either; preserve unrelated local work.
for index in 0 1; do
  actual="$(git -C "${roots[index]}" rev-parse HEAD)"
  if [[ "${actual}" != "${revisions[index]}" ]]; then
    echo "source revision mismatch: ${roots[index]}: expected ${revisions[index]}, received ${actual}" >&2
    exit 1
  fi
  if git -C "${roots[index]}" apply --reverse --check "${patches[index]}" 2>/dev/null; then
    echo "already applied: ${patches[index]}"
  elif git -C "${roots[index]}" apply --check "${patches[index]}"; then
    pending+=("${index}")
    echo "ready to apply: ${patches[index]}"
  else
    echo "patch conflicts with checkout: ${patches[index]}" >&2
    exit 1
  fi
done

if [[ "${check_only}" == true ]]; then
  exit 0
fi
for index in "${pending[@]}"; do
  git -C "${roots[index]}" apply "${patches[index]}"
  echo "applied: ${patches[index]}"
done
