#!/usr/bin/env bash
set -euo pipefail

OUT_DIR="${1:-}"
if [ -z "$OUT_DIR" ]; then
    echo "usage: $0 <output-dir>" >&2
    exit 2
fi

MOONLIGHT_REPO="${MOONLIGHT_EMBEDDED_REPO:-https://github.com/moonlight-stream/moonlight-embedded.git}"
MOONLIGHT_REF="${MOONLIGHT_EMBEDDED_REF:-f32e415aea6797d261d6b470dcf8bf18727341c2}"
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
git -C "$SRC_DIR" apply <<'PATCH'
diff --git a/src/input/sdl.c b/src/input/sdl.c
index 8ee2eec..8e03246 100644
--- a/src/input/sdl.c
+++ b/src/input/sdl.c
@@ -317,13 +317,13 @@ static void send_controller_arrival(PGAMEPAD_STATE state) {
   case SDL_CONTROLLER_TYPE_XBOX360:
   case SDL_CONTROLLER_TYPE_XBOXONE:
     type = LI_CTYPE_XBOX;
     break;
   case SDL_CONTROLLER_TYPE_PS3:
   case SDL_CONTROLLER_TYPE_PS4:
   case SDL_CONTROLLER_TYPE_PS5:
-    type = LI_CTYPE_PS;
+    type = LI_CTYPE_XBOX;
     break;
   case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO:
 #if SDL_VERSION_ATLEAST(2, 24, 0)
   case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
   case SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT:
@@ -331,12 +331,16 @@ static void send_controller_arrival(PGAMEPAD_STATE state) {
 #endif
-    type = LI_CTYPE_NINTENDO;
+    type = LI_CTYPE_XBOX;
     break;
   }
 
   LiSendControllerArrivalEvent(state->id, activeGamepadMask, type, supportedButtonFlags, capabilities);
 #endif
 }
 
+static void ensure_controller_arrival(PGAMEPAD_STATE state) {
+  // Some controllers emit startup noise before the stream is ready. Re-send
+  // arrival with real input so the host always sees a usable virtual gamepad.
+  send_controller_arrival(state);
+}
+
 static PGAMEPAD_STATE get_gamepad(SDL_JoystickID sdl_id, bool add) {
   // See if a gamepad already exists
   for (int i = 0;i<MAX_GAMEPADS;i++) {
@@ -431,6 +438,9 @@ static void remove_gamepad(SDL_JoystickID sdl_id) {
 void sdlinput_init(char* mappings) {
   memset(gamepads, 0, sizeof(gamepads));
 
+  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
+  SDL_GameControllerEventState(SDL_ENABLE);
+  SDL_JoystickEventState(SDL_ENABLE);
   SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER);
 #if !SDL_VERSION_ATLEAST(2, 0, 9)
   SDL_InitSubSystem(SDL_INIT_HAPTIC);
@@ -568,6 +578,7 @@ int sdlinput_handle_event(SDL_Window* window, SDL_Event* event) {
     default:
       return SDL_NOTHING;
     }
+    ensure_controller_arrival(gamepad);
     LiSendMultiControllerEvent(gamepad->id, activeGamepadMask, gamepad->buttons, gamepad->leftTrigger, gamepad->rightTrigger, gamepad->leftStickX, gamepad->leftStickY, gamepad->rightStickX, gamepad->rightStickY);
     break;
   case SDL_CONTROLLERBUTTONDOWN:
@@ -586,6 +597,7 @@ int sdlinput_handle_event(SDL_Window* window, SDL_Event* event) {
     if ((gamepad->buttons & QUIT_BUTTONS) == QUIT_BUTTONS)
       return SDL_QUIT_APPLICATION;
 
+    ensure_controller_arrival(gamepad);
     LiSendMultiControllerEvent(gamepad->id, activeGamepadMask, gamepad->buttons, gamepad->leftTrigger, gamepad->rightTrigger, gamepad->leftStickX, gamepad->leftStickY, gamepad->rightStickX, gamepad->rightStickY);
     break;
   case SDL_CONTROLLERDEVICEADDED:
PATCH

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
