#!/usr/bin/env bash
set -euo pipefail

: "${PSPDEV:?PSPDEV is not set}"

LIBDIR="$PSPDEV/psp/lib"
INCDIR="$PSPDEV/psp/include/me-core-mapper"

# The MM-PSP VME mixer uses these headers/macros directly.
REQUIRED_HEADERS=(
    me-core.h
    me-core-mapper.h
    me-lib.h
    vme-lib.h
)

REQUIRED_VME_DEFINES=(
    VME_BASE_BUFFERS
    VME_TOP_BUFFERS
    VME_CYCLE_6
    VME_DEF_MAPPER
    VME_DEF_MODE
    VME_DEF_STEP
    VME_END_TOKEN
    vmeLibStart
    vmeLibFinish
    vme_icn
    vme_pe0
    vme_pe1
    vme_pe2
    vme_pe3
    vme_fu
    vme_mux
    fu_reg
    agu_top
    agu_base
    agu_write
)

fail=0

check_installed() {
    local quiet="${1:-0}"
    local local_fail=0

    if [[ "$quiet" == "0" ]]; then
        echo "=== CHECK INSTALLED LIBME-CORE ==="
    fi

    for h in "${REQUIRED_HEADERS[@]}"; do
        if [[ ! -f "$INCDIR/$h" ]]; then
            [[ "$quiet" == "0" ]] && echo "missing header: $INCDIR/$h" >&2
            local_fail=1
        fi
    done

    if [[ -f "$INCDIR/vme-lib.h" ]]; then
        for name in "${REQUIRED_VME_DEFINES[@]}"; do
            if ! grep -E '^[[:space:]]*#define[[:space:]]+'"${name}"'([[:space:](]|$)' \
                "$INCDIR/vme-lib.h" >/dev/null
            then
                [[ "$quiet" == "0" ]] && echo "missing VME API: $name" >&2
                local_fail=1
            fi
        done
    fi

    if [[ -f "$INCDIR/me-lib.h" ]]; then
        if ! grep -E '(^|[^A-Za-z0-9_])meLibSync[[:space:]]*(\(|$)' \
            "$INCDIR/me-lib.h" >/dev/null
        then
            [[ "$quiet" == "0" ]] && echo "missing me-core API: meLibSync" >&2
            local_fail=1
        fi
    fi

    for lib in libme-core.a libme-core-lib.a libme-core-mapper.a; do
        if [[ ! -f "$LIBDIR/$lib" ]]; then
            [[ "$quiet" == "0" ]] && echo "missing library: $LIBDIR/$lib" >&2
            local_fail=1
        fi
    done

    if [[ -f "$LIBDIR/libme-core.a" ]]; then
        if ! ar t "$LIBDIR/libme-core.a" 2>/dev/null |
            grep -x 'vme-lib.c.obj' >/dev/null
        then
            [[ "$quiet" == "0" ]] &&
                echo "missing library object: vme-lib.c.obj" >&2
            local_fail=1
        fi

        if ! psp-nm -C "$LIBDIR/libme-core.a" 2>/dev/null |
            grep -E ' [Tt] _vmeLibStart$' >/dev/null
        then
            [[ "$quiet" == "0" ]] &&
                echo "missing library symbol: _vmeLibStart" >&2
            local_fail=1
        fi

        if ! psp-nm -C "$LIBDIR/libme-core.a" 2>/dev/null |
            grep -E ' [Tt] _vmeLibFinish$' >/dev/null
        then
            [[ "$quiet" == "0" ]] &&
                echo "missing library symbol: _vmeLibFinish" >&2
            local_fail=1
        fi
    fi

    return "$local_fail"
}

# Check whether a source tree exposes everything required by MM-PSP.
check_source_api() {
    local root="$1"
    local f="$root/vme-lib.h"
    local me="$root/me-lib.h"

    [[ -f "$root/me-core.h" ]] &&
    [[ -f "$root/me-core-mapper.h" ]] &&
    [[ -f "$me" ]] &&
    [[ -f "$f" ]] || return 1

    for name in "${REQUIRED_VME_DEFINES[@]}"; do
        grep -E '^[[:space:]]*#define[[:space:]]+'"${name}"'([[:space:](]|$)' \
            "$f" >/dev/null || return 1
    done

    grep -E '(^|[^A-Za-z0-9_])meLibSync[[:space:]]*(\(|$)' \
        "$me" >/dev/null || return 1

    grep -E '(^|[^A-Za-z0-9_])_vmeLibStart[[:space:]]*\(' \
        "$root/vme-lib.c" >/dev/null || return 1

    grep -E '(^|[^A-Za-z0-9_])_vmeLibFinish[[:space:]]*\(' \
        "$root/vme-lib.c" >/dev/null || return 1

    return 0
}

check_candidate_build() {
    local build="$1"
    local local_fail=0

    for lib in libme-core.a libme-core-lib.a libme-core-mapper.a; do
        [[ -f "$build/$lib" ]] || local_fail=1
    done

    if [[ -f "$build/libme-core.a" ]]; then
        ar t "$build/libme-core.a" 2>/dev/null |
            grep -x 'vme-lib.c.obj' >/dev/null || local_fail=1

        psp-nm -C "$build/libme-core.a" 2>/dev/null |
            grep -E ' [Tt] _vmeLibStart$' >/dev/null || local_fail=1

        psp-nm -C "$build/libme-core.a" 2>/dev/null |
            grep -E ' [Tt] _vmeLibFinish$' >/dev/null || local_fail=1
    fi

    return "$local_fail"
}

install_candidate() {
    local root="$1"
    local build="$2"
    local stage
    stage="$(mktemp -d)"

    trap 'rm -rf "$stage"' RETURN

    mkdir -p "$stage/psp/lib" "$stage/psp/include/me-core-mapper"

    cp -f "$build"/libme-core.a \
          "$build"/libme-core-lib.a \
          "$build"/libme-core-mapper.a \
          "$stage/psp/lib/"

    find "$root" -maxdepth 1 -type f -name '*.h' \
        ! -name 'me-core-mapping.def.h' \
        -exec cp -f {} "$stage/psp/include/me-core-mapper/" \;

    if [[ -d "$root/kernel" ]]; then
        mkdir -p "$stage/psp/include/me-core-mapper/kernel"
        find "$root/kernel" -maxdepth 1 -type f -name '*.h' \
            -exec cp -f {} "$stage/psp/include/me-core-mapper/kernel/" \;
    fi

    mkdir -p "$LIBDIR" "$INCDIR"

    cp -f "$stage/psp/lib/"*.a "$LIBDIR/"
    cp -f "$stage/psp/include/me-core-mapper/"*.h "$INCDIR/"

    if [[ -d "$stage/psp/include/me-core-mapper/kernel" ]]; then
        mkdir -p "$INCDIR/kernel"
        cp -f "$stage/psp/include/me-core-mapper/kernel/"*.h \
            "$INCDIR/kernel/"
    fi
}

resolve_and_install() {
    local repo="$1"
    local worktree
    local candidate
    local build
    local commits
    local found=0

    if [[ ! -d "$repo/.git" ]]; then
        echo "error: custom core repository not found: $repo" >&2
        return 1
    fi

    echo
    echo "=== UPDATE CUSTOM CORE HISTORY ==="
    git -C "$repo" fetch --quiet origin main

    commits="$(git -C "$repo" rev-list origin/main)"

    worktree="$(mktemp -d "${TMPDIR:-/tmp}/mm-psp-me-core.XXXXXX")"
    rm -rf "$worktree"

    cleanup_worktree() {
        git -C "$repo" worktree remove --force "$worktree" 2>/dev/null || true
    }
    trap cleanup_worktree RETURN

    while IFS= read -r candidate; do
        [[ -n "$candidate" ]] || continue

        echo
        echo "--- CANDIDATE $(git -C "$repo" show -s \
            --format='%h %ad %s' --date=short "$candidate") ---"

        # Cheap source-level rejection before compiling.
        if ! git -C "$repo" show "$candidate:vme-lib.h" >/dev/null 2>&1 ||
           ! git -C "$repo" show "$candidate:me-lib.h" >/dev/null 2>&1 ||
           ! git -C "$repo" show "$candidate:me-core.h" >/dev/null 2>&1 ||
           ! git -C "$repo" show "$candidate:me-core-mapper.h" >/dev/null 2>&1
        then
            echo "source API: incompatible"
            continue
        fi

        git -C "$repo" worktree add --quiet --detach "$worktree" "$candidate"

        if ! check_source_api "$worktree"; then
            echo "source API: incompatible"
            git -C "$repo" worktree remove --force "$worktree"
            continue
        fi

        echo "source API: compatible"
        echo "building..."

        build="$worktree/build"

        if ! cmake -S "$worktree" -B "$build" \
            -DCMAKE_BUILD_TYPE=Release >/dev/null 2>&1
        then
            echo "build configure: FAILED"
            git -C "$repo" worktree remove --force "$worktree"
            continue
        fi

        if ! cmake --build "$build" -j"${JOBS:-$(nproc 2>/dev/null || echo 1)}" \
            >/dev/null 2>&1
        then
            echo "build: FAILED"
            git -C "$repo" worktree remove --force "$worktree"
            continue
        fi

        if ! check_candidate_build "$build"; then
            echo "artifact check: FAILED"
            git -C "$repo" worktree remove --force "$worktree"
            continue
        fi

        echo "artifact check: OK"
        echo
        echo "=== SELECTED COMPATIBLE VERSION ==="
        git -C "$repo" show -s \
            --format='%H%n%ad%n%s' --date=iso-strict "$candidate"

        echo
        echo "=== INSTALL ==="
        install_candidate "$worktree" "$build"

        found=1
        git -C "$repo" worktree remove --force "$worktree"
        break
    done <<< "$commits"

    if [[ "$found" -ne 1 ]]; then
        echo
        echo "libme-core resolver: no compatible version found." >&2
        return 1
    fi

    echo
    echo "Совместимый libme-core установлен."
    echo "Для продолжения введите: ./psp.sh"

    return 0
}

# Fast path: current installed dependency is already compatible.
if check_installed 1; then
    exit 0
fi

echo "Установленная версия libme-core несовместима с MM-PSP."
echo

if [[ ! -t 0 ]]; then
    echo "Non-interactive shell: refusing automatic dependency replacement." >&2
    exit 1
fi

if [[ -r /dev/tty ]]; then
    IFS= read -r -p "Установить последнюю совместимую версию libme-core? [Y/N] " answer < /dev/tty
else
    IFS= read -r -p "Установить последнюю совместимую версию libme-core? [Y/N] " answer
fi

answer="${answer//$'\r'/}"
answer="${answer#"${answer%%[![:space:]]*}"}"
answer="${answer%"${answer##*[![:space:]]}"}"

case "${answer,,}" in
    y|yes)
        ;;
    *)
        echo "libme-core installation cancelled." >&2
        exit 1
        ;;
esac

CORE_REPO="${MM_PSP_ME_CORE_REPO:-$HOME/psp-media-engine-custom-core-check}"

if ! resolve_and_install "$CORE_REPO"; then
    exit 1
fi

echo
echo "=== FINAL INSTALLED CHECK ==="

if ! check_installed 0; then
    echo
    echo "libme-core capability check: FAILED after installation." >&2
    exit 1
fi

echo "libme-core capability check: OK"
exit 0
