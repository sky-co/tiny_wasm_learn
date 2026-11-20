#!/usr/bin/env bash
set -euo pipefail

usage() {
    printf 'Usage: bash %s [--check | --help]\n' "$0"
    printf 'Install missing toolbox dependencies using apt, or check without changing the system.\n'
}

check_only=false
case "${1:-}" in
    '') ;;
    --check) check_only=true ;;
    --help|-h) usage; exit 0 ;;
    *) usage >&2; exit 1 ;;
esac
if [[ $# -gt 1 ]]; then
    usage >&2
    exit 1
fi

node_supported() {
    command -v node >/dev/null 2>&1 &&
        node -e 'const [major, minor] = process.versions.node.split(".").map(Number);
            process.exit(major > 18 || (major === 18 && minor >= 17) ? 0 : 1);'
}

check_dependencies() {
    local failed=0 tool version
    for tool in node wast2json wasm-objdump llvm-mc aarch64-linux-gnu-objdump; do
        if ! command -v "$tool" >/dev/null 2>&1; then
            printf '[MISSING] %s\n' "$tool" >&2
            failed=1
        elif ! version="$("$tool" --version 2>&1)"; then
            printf '[ERROR] %s: %s\n' "$tool" "$version" >&2
            failed=1
        else
            printf '[OK] %s: %s (%s)\n' "$tool" "${version%%$'\n'*}" "$(command -v "$tool")"
        fi
    done
    if command -v node >/dev/null 2>&1 && ! node_supported; then
        printf '[ERROR] Node.js must be >= 18.17. Check the active node executable and PATH.\n' >&2
        failed=1
    fi
    return "$failed"
}

if "$check_only"; then
    check_dependencies
    exit 0
fi

packages=()
if ! node_supported; then
    packages+=(nodejs)
fi
if ! command -v wast2json >/dev/null 2>&1 || ! command -v wasm-objdump >/dev/null 2>&1; then
    packages+=(wabt)
fi
if ! command -v llvm-mc >/dev/null 2>&1; then
    packages+=(llvm)
fi
if ! command -v aarch64-linux-gnu-objdump >/dev/null 2>&1; then
    packages+=(binutils-aarch64-linux-gnu)
fi

if [[ ${#packages[@]} -gt 0 ]]; then
    if ! command -v apt-get >/dev/null 2>&1; then
        printf 'Automatic installation requires apt-get (Ubuntu/Debian).\n' >&2
        printf 'Install Node.js >= 18.17, WABT, LLVM and AArch64 binutils manually, then run --check.\n' >&2
        exit 1
    fi
    privilege=()
    if [[ $EUID -ne 0 ]]; then
        if ! command -v sudo >/dev/null 2>&1; then
            printf 'Installation requires root privileges; sudo is missing. Run from a root shell.\n' >&2
            exit 1
        fi
        privilege=(sudo -A)
    fi
    printf 'Installing packages: %s\n' "${packages[*]}"
    "${privilege[@]}" apt-get update
    "${privilege[@]}" apt-get install -y --no-install-recommends "${packages[@]}"
    hash -r
fi

if ! check_dependencies; then
    printf 'Dependency verification failed. Fix the errors above before starting the toolbox.\n' >&2
    printf 'If apt provides an old Node.js, use a newer distribution/repository or install a supported Node.js yourself.\n' >&2
    printf 'If another installation shadows apt packages, adjust PATH; this script does not change shell configuration.\n' >&2
    exit 1
fi
printf 'Dependencies are ready. Start from the project root: node tooling/server.mjs\n'
