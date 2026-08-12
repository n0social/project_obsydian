#!/bin/bash
# Wowee Clean & Reset Utility
# Removes generated/cached data to return to a fresh state

set -e
cd "$(dirname "$0")"

usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --all          Reset everything (build + assets + cache + user data)"
    echo "  --build        Remove build directory only"
    echo "  --assets       Remove extracted assets (Data/ contents, keeps expansion configs)"
    echo "  --cache        Remove runtime cache, logs, and saves"
    echo "  --user         Remove user config (~/.wowee/) and warden cache"
    echo "  --csvs         Remove generated CSV files from expansion db/ folders"
    echo "  --dry-run      Show what would be removed without deleting"
    echo "  -h, --help     Show this help"
    echo ""
    echo "With no options, removes build + cache (safe default)."
}

DRY_RUN=false
DO_BUILD=false
DO_ASSETS=false
DO_CACHE=false
DO_USER=false
DO_CSVS=false
ANYTHING=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --all)
            DO_BUILD=true; DO_ASSETS=true; DO_CACHE=true; DO_USER=true; DO_CSVS=true; ANYTHING=true ;;
        --build)
            DO_BUILD=true; ANYTHING=true ;;
        --assets)
            DO_ASSETS=true; ANYTHING=true ;;
        --cache)
            DO_CACHE=true; ANYTHING=true ;;
        --user)
            DO_USER=true; ANYTHING=true ;;
        --csvs)
            DO_CSVS=true; ANYTHING=true ;;
        --dry-run)
            DRY_RUN=true ;;
        -h|--help)
            usage; exit 0 ;;
        *)
            echo "Unknown option: $1"; usage; exit 1 ;;
    esac
    shift
done

# Default: build + cache
if [ "$ANYTHING" = false ]; then
    DO_BUILD=true
    DO_CACHE=true
fi

remove() {
    local target="$1"
    if [ -e "$target" ] || [ -L "$target" ]; then
        if [ "$DRY_RUN" = true ]; then
            echo "  [dry-run] Would remove: $target"
        else
            echo "  Removing: $target"
            rm -rf "$target"
        fi
    fi
}

if [ "$DO_BUILD" = true ]; then
    echo "=== Cleaning build artifacts ==="
    remove build
    remove build-sanitize
    remove bin
    remove lib
    echo ""
fi

if [ "$DO_ASSETS" = true ]; then
    echo "=== Cleaning extracted assets ==="
    # Extracted MPQ content directories
    for dir in db character creature terrain world interface item sound spell environment misc enUS Character Creature World; do
        remove "Data/$dir"
    done
    remove Data/manifest.json
    remove Data/hd
    remove Data/override
    # Per-expansion extracted assets and manifests
    for exp in Data/expansions/*/; do
        [ -d "$exp" ] || continue
        remove "${exp}manifest.json"
        remove "${exp}assets"
        remove "${exp}overlay"
    done
    echo ""
fi

if [ "$DO_CSVS" = true ]; then
    echo "=== Cleaning generated CSVs ==="
    for exp in Data/expansions/*/; do
        [ -d "$exp" ] || continue
        local_db="${exp}db"
        if [ -d "$local_db" ]; then
            csv_count=$(find "$local_db" -name "*.csv" 2>/dev/null | wc -l)
            if [ "$csv_count" -gt 0 ]; then
                if [ "$DRY_RUN" = true ]; then
                    echo "  [dry-run] Would remove $csv_count CSVs from $local_db"
                else
                    echo "  Removing $csv_count CSVs from $local_db"
                    find "$local_db" -name "*.csv" -delete
                fi
            fi
        fi
    done
    echo ""
fi

if [ "$DO_CACHE" = true ]; then
    echo "=== Cleaning cache, logs, and saves ==="
    remove cache
    remove logs
    remove saves
    remove asset_pipeline
    remove ingest
    remove imgui.ini
    remove config.ini
    remove config.json
    # Remove save snapshots (wowee_NNNN pattern)
    for f in wowee_[0-9][0-9][0-9][0-9]; do
        remove "$f"
    done
    echo ""
fi

if [ "$DO_USER" = true ]; then
    echo "=== Cleaning user data ==="
    remove "$HOME/.wowee"
    remove "$HOME/.local/share/wowee/warden_cache"
    echo ""
fi

if [ "$DRY_RUN" = true ]; then
    echo "Dry run complete. No files were removed."
else
    echo "Clean complete."
fi
