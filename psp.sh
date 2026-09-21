#!/usr/bin/env bash
set -euo pipefail

repo_root="$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$repo_root"

if [[ -n "${PSPDEV:-}" && -d "${PSPDEV}/bin" ]]; then
    case ":${PATH}:" in
        *":${PSPDEV}/bin:"*) ;;
        *) export PATH="${PSPDEV}/bin:${PATH}" ;;
    esac
fi

for tool in psp-config psp-gcc; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "error: unable to find $tool; install PSPSDK and add PSPDEV/bin to PATH" >&2
        exit 1
    fi
done

"$repo_root/tools/check_libme_core.sh"
echo

jobs="${JOBS:-$(nproc 2>/dev/null || getconf _NPROCESSORS_ONLN 2>/dev/null || echo 1)}"
exec make -j"$jobs" psp-port "$@"
