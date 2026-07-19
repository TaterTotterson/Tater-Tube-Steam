#!/usr/bin/env bash
set -euo pipefail

DEPOT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_ROOT="${DEPOT_ROOT}/usr/share/240mp"

export APP_ROOT
export LD_LIBRARY_PATH="${DEPOT_ROOT}/usr/lib:${DEPOT_ROOT}/usr/lib/tater-tube${LD_LIBRARY_PATH:+:${LD_LIBRARY_PATH}}"
export QT_PLUGIN_PATH="${DEPOT_ROOT}/usr/plugins${QT_PLUGIN_PATH:+:${QT_PLUGIN_PATH}}"
export QML2_IMPORT_PATH="${DEPOT_ROOT}/usr/qml${QML2_IMPORT_PATH:+:${QML2_IMPORT_PATH}}"
export QT_QUICK_CONTROLS_STYLE="${QT_QUICK_CONTROLS_STYLE:-Basic}"

exec "${DEPOT_ROOT}/usr/bin/tater-tube" "$@"
