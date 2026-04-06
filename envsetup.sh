#!/bin/env bash

if [ "${BASH_SOURCE[0]}" = "$0" ]; then
    set -e
fi

if ! ulimit -Sn 65536 >/dev/null 2>&1; then
    printf 'Warning: unable to raise file descriptor limit to 65535.\n'
    printf 'Proceeding with the current ulimit.\n'
fi

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DOWNLOAD_DIR="$ROOT_DIR/download"
DEFAULT_POST_BUILD="$ROOT_DIR/postbuild"
DEFAULT_TOOLCHAIN="$ROOT_DIR/toolchain"
AUTO_INSTALL="${AUTO_INSTALL:-}"

is_sourced() {
    [ "${BASH_SOURCE[0]}" != "$0" ]
}

safe_exit() {
    if is_sourced; then
        return "$1"
    else
        exit "$1"
    fi
}

prompt_yes_no() {
    if [ -t 0 ]; then
        printf '%s [Y/n]: ' "$1"
        local answer
        if ! read -r answer; then
            return 1
        fi
        case "$answer" in
            [Nn]*) return 1;;
            ""|[Yy]*) return 0;;
            *) return 1;;
        esac
    fi
    return 1
}

log() {
    printf '%s\n' "$*" >&2
}

die() {
    printf 'Error: %s\n' "$*" >&2
    safe_exit 1
}

command_exists() {
    command -v "$1" >/dev/null 2>&1
}

resolve_remote_url() {
    local url="$1"
    if command_exists curl; then
        curl -L -s --connect-timeout 15 --max-time 30 -o /dev/null -w '%{url_effective}' "$url"
    elif command_exists wget; then
        wget --spider --max-redirect=20 --timeout=15 --tries=3 "$url" 2>&1 | awk '/^  Location: /{ loc=$3 } END{ if (loc) print loc }'
    else
        die "curl or wget is required to resolve remote URL"
    fi
}

download_archive() {
    local url="$1"
    local outdir="$2"
    mkdir -p "$outdir"
    local final_url
    final_url="$(resolve_remote_url "$url")"
    if [ -z "$final_url" ]; then
        die "Unable to resolve remote URL for $url"
    fi
    local filename="$(basename "$final_url")"
    local output_path="$outdir/$filename"
    local checksum_path="$output_path.sha256"

    if ! command_exists sha256sum; then
        die "sha256sum is required to validate downloaded archives"
    fi

    if [ -f "$output_path" ] && [ -f "$checksum_path" ]; then
        if sha256sum -c --status "$checksum_path"; then
            log "Already downloaded and valid: $filename"
            printf '%s' "$output_path"
            return
        fi
        log "Existing archive checksum mismatch, re-downloading: $filename"
        rm -f "$output_path" "$checksum_path"
    elif [ -f "$output_path" ]; then
        log "Existing archive found without checksum, re-downloading: $filename"
        rm -f "$output_path"
    fi

    log "Downloading $url -> $output_path"
    if command_exists curl; then
        if [ -t 2 ]; then
            curl -L --fail --progress-bar --show-error --connect-timeout 15 --max-time 1800 -o "$output_path" "$url"
        else
            curl -L --fail --show-error --connect-timeout 15 --max-time 1800 -o "$output_path" "$url"
        fi
    else
        wget --timeout=15 --tries=3 --progress=bar:force:noscroll -O "$output_path" "$url"
    fi
    log "Downloaded $filename"
    sha256sum "$output_path" > "$checksum_path"
    printf '%s' "$output_path"
}

extract_archive() {
    local archive="$1"
    local target="$2"
    mkdir -p "$target"
    case "$archive" in
        *.tar.xz)
            tar -xJf "$archive" -C "$target" --strip-components=1
            return
            ;;
        *.tar.gz|*.tgz)
            tar -xzf "$archive" -C "$target" --strip-components=1
            return
            ;;
        *.zip)
            unzip -q "$archive" -d "$target"
            return
            ;;
    esac

    if command_exists file; then
        local mime
        mime="$(file --brief --mime-type "$archive" 2>/dev/null || true)"
        case "$mime" in
            application/x-xz|application/x-xz-compressed)
                tar -xJf "$archive" -C "$target" --strip-components=1
                return
                ;;
            application/gzip|application/x-gzip)
                tar -xzf "$archive" -C "$target" --strip-components=1
                return
                ;;
            application/zip)
                unzip -q "$archive" -d "$target"
                return
                ;;
        esac
    fi

    die "Unsupported archive type: $archive"
}

resolve_dir() {
    local varname="$1"
    local default_dir="$2"
    local value="${!varname:-}"
    if [ -n "$value" ] && [ -d "$value" ]; then
        printf '%s' "$value"
        return
    fi
    if [ -d "$default_dir" ]; then
        printf '%s' "$default_dir"
        return
    fi
}

install_package() {
    local label="$1"
    local url="$2"
    local target="$3"

    mkdir -p "$DOWNLOAD_DIR"
    mkdir -p "$target"

    local remote_url="$(resolve_remote_url "$url")"
    if [ -z "$remote_url" ]; then
        die "Unable to resolve remote URL for $url"
    fi

    local archive_path="$(download_archive "$url" "$DOWNLOAD_DIR")"
    local archive_file="$(basename "$archive_path")"
    local archive_name="${archive_file%.tar.xz}"
    archive_name="${archive_name%.tar.gz}"
    archive_name="${archive_name%.tgz}"
    archive_name="${archive_name%.zip}"
    local marker_file="$target/$archive_name.txt"
    local current_url=""

    if [ -f "$marker_file" ]; then
        current_url="$(cat "$marker_file")"
    elif [ -f "$target/.source_url" ]; then
        current_url="$(cat "$target/.source_url")"
    fi

    if [ -d "$target" ] && [ -n "$current_url" ] && [ "$current_url" = "$remote_url" ]; then
        log "$label already installed and up to date"
        return 0
    fi

    log "Extracting $archive_path to $target"
    rm -rf "$target"
    extract_archive "$archive_path" "$target"
    printf '%s\n' "$remote_url" > "$marker_file"
    rm -f "$target/.source_url"
    log "$label installed to $target"
}

POST_BUILD_TOOLS_DIR="$(resolve_dir POST_BUILD_TOOLS_DIR "$DEFAULT_POST_BUILD")"
CROSS_COMPILER_PATH="$(resolve_dir CROSS_COMPILER_PATH "$DEFAULT_TOOLCHAIN")"

missing=0
if [ ! -d "$POST_BUILD_TOOLS_DIR" ]; then
    log 'Post-build tools not found.'
    log "Expected: $DEFAULT_POST_BUILD"
    missing=1
fi
if [ ! -d "$CROSS_COMPILER_PATH" ]; then
    log 'Cross compiler toolchain not found.'
    log "Expected: $DEFAULT_TOOLCHAIN"
    missing=1
fi

if [ "$missing" -eq 1 ]; then
    log ''
    log 'One or more required SDK tool directories are missing.'
    if [ -n "$AUTO_INSTALL" ]; then
        log 'AUTO_INSTALL is set, installing missing SDK tools automatically.'
        log 'Installing missing SDK tools...'
        install_package "Toolchain" "https://pkgman.jieliapp.com/s/linux-toolchain" "$DEFAULT_TOOLCHAIN"
        install_package "Postbuild" "https://pkgman.jieliapp.com/s/linux-postbuild" "$DEFAULT_POST_BUILD"
        POST_BUILD_TOOLS_DIR="$(resolve_dir POST_BUILD_TOOLS_DIR "$DEFAULT_POST_BUILD")"
        CROSS_COMPILER_PATH="$(resolve_dir CROSS_COMPILER_PATH "$DEFAULT_TOOLCHAIN")"
    elif [ -t 0 ]; then
        if prompt_yes_no 'Do you want envsetup.sh to automatically download and install them now?'; then
            log 'Installing missing SDK tools...'
            install_package "Toolchain" "https://pkgman.jieliapp.com/s/linux-toolchain" "$DEFAULT_TOOLCHAIN"
            install_package "Postbuild" "https://pkgman.jieliapp.com/s/linux-postbuild" "$DEFAULT_POST_BUILD"
            POST_BUILD_TOOLS_DIR="$(resolve_dir POST_BUILD_TOOLS_DIR "$DEFAULT_POST_BUILD")"
            CROSS_COMPILER_PATH="$(resolve_dir CROSS_COMPILER_PATH "$DEFAULT_TOOLCHAIN")"
        else
            log 'Skipping automatic install.'
        fi
    else
        log 'To install automatically, set AUTO_INSTALL=1 or run: source ./envsetup.sh in an interactive shell.'
    fi
fi

if [ ! -d "$POST_BUILD_TOOLS_DIR" ]; then
    log 'Error: Post-build tools directory still missing.'
    safe_exit 1
fi

if [ ! -d "$CROSS_COMPILER_PATH" ]; then
    log 'Error: Cross compiler path still missing.'
    safe_exit 1
fi

log ''
log 'envsetup.sh environment setup complete.'
log 'To build the SDK, switch to the sdk directory and run make:'
log '  cd sdk'
log '  make'

export POST_BUILD_TOOLS_DIR
export CROSS_COMPILER_PATH
