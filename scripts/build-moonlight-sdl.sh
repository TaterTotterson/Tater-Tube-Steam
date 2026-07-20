#!/usr/bin/env bash
set -euo pipefail

OUT_DIR="${1:-}"
if [ -z "$OUT_DIR" ]; then
    echo "usage: $0 <output-dir>" >&2
    exit 2
fi

MOONLIGHT_REPO="${MOONLIGHT_EMBEDDED_REPO:-https://github.com/moonlight-stream/moonlight-embedded.git}"
MOONLIGHT_REF="${MOONLIGHT_EMBEDDED_REF:-775444287305849ebdf4736c75298ad0713e2d5d}"
WORK_DIR="${MOONLIGHT_BUILD_WORK_DIR:-}"

cleanup() {
    if [ -n "${WORK_DIR:-}" ] && [ "${MOONLIGHT_KEEP_BUILD_DIR:-0}" != "1" ]; then
        rm -rf "$WORK_DIR"
    fi
}
trap cleanup EXIT

if [ -z "$WORK_DIR" ]; then
    WORK_DIR="$(mktemp -d "${TMPDIR:-/tmp}/240mp-moonlight.XXXXXX")"
else
    mkdir -p "$WORK_DIR"
fi

SRC_DIR="$WORK_DIR/moonlight-embedded"
BUILD_DIR="$WORK_DIR/build"

git clone --recursive "$MOONLIGHT_REPO" "$SRC_DIR"
git -C "$SRC_DIR" checkout "$MOONLIGHT_REF"
git -C "$SRC_DIR" submodule update --init --recursive

# Sunshine/Steam hosts are most reliable when clients advertise an Xbox-style
# gamepad. Keep the physical SDL mapping intact, but request an Xbox virtual pad
# on the host instead of passing through PlayStation/Nintendo controller types.
# Use exact text replacements instead of a unified diff so CI fails with a clear
# message if the pinned Moonlight source changes.
python3 - "$SRC_DIR/src/input/sdl.c" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()

replacements = [
    ("type = LI_CTYPE_PS;", "type = LI_CTYPE_XBOX;"),
    ("type = LI_CTYPE_NINTENDO;", "type = LI_CTYPE_XBOX;"),
    (
        """  LiSendControllerArrivalEvent(state->id, activeGamepadMask, type, supportedButtonFlags, capabilities);
#endif
}

static PGAMEPAD_STATE get_gamepad(SDL_JoystickID sdl_id, bool add) {
""",
        """  LiSendControllerArrivalEvent(state->id, activeGamepadMask, type, supportedButtonFlags, capabilities);
#endif
}

static void ensure_controller_arrival(PGAMEPAD_STATE state) {
  // Some controllers emit startup noise before the stream is ready. Re-send
  // arrival with real input so the host always sees a usable virtual gamepad.
  send_controller_arrival(state);
}

static PGAMEPAD_STATE get_gamepad(SDL_JoystickID sdl_id, bool add) {
""",
    ),
    (
        """void sdlinput_init(char* mappings) {
  memset(gamepads, 0, sizeof(gamepads));

  SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
""",
        """void sdlinput_init(char* mappings) {
  memset(gamepads, 0, sizeof(gamepads));

  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
  SDL_GameControllerEventState(SDL_ENABLE);
  SDL_JoystickEventState(SDL_ENABLE);
  SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
""",
    ),
    (
        """    default:
      return SDL_NOTHING;
    }
    LiSendMultiControllerEvent(gamepad->id, activeGamepadMask, gamepad->buttons, gamepad->leftTrigger, gamepad->rightTrigger, gamepad->leftStickX, gamepad->leftStickY, gamepad->rightStickX, gamepad->rightStickY);
""",
        """    default:
      return SDL_NOTHING;
    }
    ensure_controller_arrival(gamepad);
    LiSendMultiControllerEvent(gamepad->id, activeGamepadMask, gamepad->buttons, gamepad->leftTrigger, gamepad->rightTrigger, gamepad->leftStickX, gamepad->leftStickY, gamepad->rightStickX, gamepad->rightStickY);
""",
    ),
    (
        """    if ((gamepad->buttons & QUIT_BUTTONS) == QUIT_BUTTONS)
      return SDL_QUIT_APPLICATION;

    LiSendMultiControllerEvent(gamepad->id, activeGamepadMask, gamepad->buttons, gamepad->leftTrigger, gamepad->rightTrigger, gamepad->leftStickX, gamepad->leftStickY, gamepad->rightStickX, gamepad->rightStickY);
""",
        """    if ((gamepad->buttons & QUIT_BUTTONS) == QUIT_BUTTONS)
      return SDL_QUIT_APPLICATION;

    ensure_controller_arrival(gamepad);
    LiSendMultiControllerEvent(gamepad->id, activeGamepadMask, gamepad->buttons, gamepad->leftTrigger, gamepad->rightTrigger, gamepad->leftStickX, gamepad->leftStickY, gamepad->rightStickX, gamepad->rightStickY);
""",
    ),
]

for old, new in replacements:
    if old not in text:
        raise SystemExit(f"Moonlight SDL patch failed; expected block not found in {path}")
    text = text.replace(old, new, 1)

path.write_text(text)
PY

cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$OUT_DIR" \
    -DENABLE_SDL=ON \
    -DENABLE_X11=OFF \
    -DENABLE_CEC=OFF \
    -DENABLE_PULSE=OFF

cmake --build "$BUILD_DIR" --parallel "${MOONLIGHT_BUILD_JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}"
rm -rf "$OUT_DIR"
cmake --install "$BUILD_DIR" --prefix "$OUT_DIR"

if command -v strip >/dev/null 2>&1; then
    strip "$OUT_DIR/bin/moonlight" "$OUT_DIR"/lib/*.so.* 2>/dev/null || true
fi
