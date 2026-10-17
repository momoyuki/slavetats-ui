# Browser Position and Camera Compatibility Design

## Intent

Keep the Actor visible beside the tattoo browser on either side of the screen,
and allow a camera view of the exact Actor Target while browsing. The user requested a
left/right choice in the SKSE Menu Framework settings and asked us to examine
SlavetatsUIPzx's SmoothCam compatibility.

## Browser position

- The Menu Framework launcher shows `Browser position: Left / Right` beside the
  existing Hotkey setting. Left is the default for existing installations.
- Selection is written immediately as `menuSide` in `SlaveTatsUI.json` through
  `PluginConfigFile`, preserving unrelated configuration. Invalid values keep
  the safe left default and are reported during load.
- Every workflow window uses the same side and retains its width, height, and
  viewport margin. The next browser opening uses the new side, whether launched
  by the Hotkey, Menu Framework, or FLICK.
- The setting affects presentation only; it does not change Actor Target or
  tattoo state. A running menu is not teleported while it is being edited.

## SmoothCam research and camera follow-up

SlavetatsUIPzx asks SmoothCam for exclusive camera control when its FLICK
browser opens. Its own game-thread turntable then controls Skyrim's third-person
camera. On close, it restores captured camera and character values, asks
SmoothCam V2+ to move to its goal, and releases camera control. SmoothCam is
optional. This is a compatibility handshake, not a camera implementation.

Our browser runs inside SKSE Menu Framework. For the first in-game QA slice,
camera control is enabled only while the Player is the selected Actor Target.
Selecting an NPC releases camera control and restores the previous camera
state; NPC tattoo workflow remains available. The user initially considered
camera focus for Player and NPC, then narrowed the first test to Player. NPC
camera focus remains a later slice. Keep Actor Target selection in the
workflow as the authority; camera movement must not retarget the workflow.

The camera slice captures and restores game camera state on the game thread,
transfers SmoothCam ownership only after a successful request, avoids camera
writes if SmoothCam refuses ownership, routes pointer intent from the UI through
a game-thread coordinator, and releases ownership on every close/failure path.
Camera input outside the panel orbits and zooms around the Player in this slice.
Opening the browser with Player selected captures the current third-person view
and makes one side-aware Whole body framing request. The Player should appear in
the control area opposite the menu while retaining the captured camera angle
and distance. No fixed yaw, pitch, or zoom is imposed. If camera ownership is
unavailable, leave the current view alone. Idle refreshes reassert an acquired
pose without repeating the initial framing. A real pointer gesture queued
before initial framing supersedes that framing rather than being discarded.
Closing or changing targets restores only an acquired session. Reset View
restores the view captured when the menu opened.
Pointer ownership is checked after the UI renders: a drag that starts on any
menu window or active widget never controls the camera, and a wheel event over
the tattoo gallery scrolls the gallery without zooming. While Menu Framework
pauses gameplay, the game-thread camera task reapplies the desired rotation
and zoom so the view follows pointer input.
The UI may render camera status but does not access `RE::PlayerCamera` directly.
The first gesture snapshots native ThirdPersonState controls and retains the
captured state lifetime. Preserve existing enabled native freeRotation inputs;
when disabled, start with neutral relative free look. Do not reinterpret the
world quaternion as native relative pitch/yaw. Preserve native target zoom exactly,
including negative values observed in-game. Keep stable zoom bounds that include
the captured value, and change/clamp only axes with real gesture input. Keep yaw
continuous across pi. Advance free rotation and native zoom without rotating the
Actor. During active control, hold both expected and actual framing offsets at
the captured actual offset. SmoothCam's first post-acquire OnEnd resets offsets
to INI defaults; reasserting the captured actual offset prevents persistent
framing changes, including unfinished interpolation at capture. Keep the distinct
original expected/actual offsets for exact restoration on close. While paused, snap
actual offset/current zoom and explicitly call
PlayerCamera::Update on the game thread after setting coherent native inputs.
Do not write camera-root or rendered NiCamera transforms directly.
Validate captured state/root/rendered identity and Actor load state before moves.
Active sessions reassert controls each render through coalesced game-thread tasks.
In-game CameraTrace demonstrated that input, ownership and scene updates succeed,
but the engine resets the rendered pose before the next task. The attempted
PlayerCamera virtual Update hook (slot 2) is installed, but the 11:32 in-game
trace contains no hook-entry messages. It therefore does not establish a usable
post-engine update seam and has not fixed drag or zoom. Remove that hook and use
native state inputs, following the verified upstream scheduling pattern.
On close or invalidation, restore all written controls on the retained captured
state even if the current view changed. Restore camera target/first-person only
while the complete original view identity still matches, then release SmoothCam.
The native port reports loss of state/root/target ownership back to the session.
After releasing an invalidated camera, idle refreshes must not reacquire it;
the next real gesture may acquire the same requested Actor again.
Vertical pointer drag is inverted relative to the previous build: dragging up
raises the orbit position. Wheel direction remains forward-to-zoom-in.
The menu-side value travels with the target through the coordinator and positions
the input frame. Do not reflect the world position at acquisition: the prior
reflection caused a first-gesture jump. Automatic side composition uses the
center of the available control area as the initial framing target and remains
subject to in-game visual acceptance. The launcher exposes independent Left and
Right initial camera X offsets in normalized viewport coordinates, each from
-0.15 to +0.15 with zero as the default. The offset applies only to the first
automatic Player composition for that side when no session view is remembered.
Explicit Center and Reset View still use the control area's unadjusted center.
Changing an offset takes effect on the next first composition, not on an
already open camera view.
Native state orientation/zoom are not proof of identical SmoothCam rendered
distance at handoff; actual continuity remains an acceptance requirement.
In-game acceptance must verify the actual NiCamera update, first-drag continuity,
both menu sides, drag direction, wheel isolation, close/restore, and near-wall
camera behavior; pure pose tests do not establish engine rendering or collision.

### Transparent camera control frame

The user approved a visible transparent rectangle over the existing game scene,
not a separate character render. Draw its border and short drag/zoom hint in the
background draw list opposite the configured menu side, preserving the scene and
creating no extra ImGui input window. Show the control frame only for Player,
the currently supported camera target. A gesture must start inside the frame and
outside all UI; movement/zoom outside the frame or over menus is ignored. A drag
that started elsewhere cannot activate the camera by entering the frame while
held. Menus remain authoritative input owners even if dragged over the frame.
The frame follows the configured menu side, rather than the menu's transient
drag position; insufficient scene width produces no visible or active frame.

No source or assets from SlavetatsUIPzx are copied.
The public SmoothCam modder header is included from its upstream project as
permitted by that header. Skyrim camera ownership and movement stay in the
native game-thread port, separate from tattoo runtime mutations.

## Verification

### Deferred camera feedback (2026-10-16)

The user observed that selecting an NPC zoomed toward the Player rather than the
NPC. This is an in-game observation, not a confirmed diagnosis. The resulting
Player framing was useful and should inform a later Player camera mode, possibly
with dedicated settings. Investigate the NPC selection/restore transition and
SmoothCam goal handoff before adopting that framing deliberately. The user
requested UI work first; this follow-up does not authorize NPC camera focus or
change the current Player-only camera scope.

The user then supplied paired Player/NPC screenshots and requested the latter
framing for Player. Source tracing found that Player eagerly took the SmoothCam
lease, while NPC selection released it and requested SmoothCam's goal position.
The first corrective slice defers ownership until camera input so normal Player
framing can proceed. In-game verification must compare initial Player framing,
first drag, wheel zoom, close/reopen, and NPC transition with SmoothCam enabled.
If normal paused-menu updates do not reach the desired goal, capture runtime
camera diagnostics before adding explicit goal placement or distance settings.

### Existing checks

- Unit tests cover left default, persistence, invalid values, and left/right
  viewport geometry.
- Debug and Release builds and complete CTest suites pass.
- In-game checks cover all three launch paths, both sides at normal and narrow
  resolutions, and every workflow screen. Camera validation covers Player,
  switching to an NPC (which releases camera control), rapid open/close, and
  operation with and without SmoothCam.

## Sources

- https://github.com/pethzero/SlavetatsUIPzx/blob/main/src/tattoo_browser.cpp
- https://github.com/mwilsnd/SkyrimSE-SmoothCam/blob/master/SmoothCam/include/SmoothCamAPI.h

## Inspectable initial Pan — 2026-10-17

Show Current Pan X/Y in the in-browser camera controls. These are the current
pivot displacement from the selected focus, projected onto the camera's right
and up axes, in game world units. Show the side's saved Initial Pan X/Y beside
them and provide a one-shot action to copy the current pair to that side.
Expose editable Initial Pan X/Y for Left and Right in the launcher. Persist
each side independently in the plugin configuration, accepting only finite
values within +/-500 units. Apply the saved pair after the normal initial
Whole body composition, translating camera and pivot together. The existing
normalized Opening frame X offset is a separate composition setting.
Session recall takes precedence on subsequent openings of the same side;
saved Initial Pan applies when that side has no recalled view. Current Pan
is unavailable without ownership of a Player browser camera. No raw world
position or native camera pointer is persisted.

## Player camera recall within one game session — 2026-10-17

On the first Player browser opening after a new game or save load, use the
existing side-aware Whole body composition. After a successfully owned browser
camera session closes, remember its final orbit, pan/focus and zoom separately
for Left and Right. Opening that side again restores the remembered view. A
side without a remembered view still uses the initial composition. Reset View
continues to restore the game view captured on the current opening.

Remember the final rendered NiCamera pose, not only the FreeCamera position
requested by the UI; those may differ after an engine or SmoothCam update.
Store only finite camera and pivot offsets relative to the loaded Player's
current 3D root, plus orientation. Rebase those offsets against the Player's
current root on reopening. Never retain native pointers, world positions or a
savegame-persistent value. Capture only while the same Player camera state,
root, rendered camera and target are still owned. Ownership refusal, a changed
camera identity or an unloaded Player does not replace a remembered view.
Closing the browser always restores the captured normal game camera and releases
SmoothCam ownership; recall never changes that restoration path.

Clear both sides on PreLoadGame and NewGame before any later Player opening.
Route invalidation through the game-thread coordinator so a queued old gesture
cannot restore or repopulate a view after the reset. The game process exit
naturally discards the in-memory cache. NPC selection releases Player camera
control and does not create an NPC remembered view. No new setting is needed.

## Approved inspection controls — 2026-10-16

The user approved this interaction after continued first-gesture framing changes:

- Keep the existing scene and compact opposite-side control frame.
- Left drag orbits the focus point; Alt+left drag pans camera/focus together.
- Wheel zooms about focus; Alt+A/D orbits and Alt+W/S pans vertically.
- Keyboard control requires pointer inside the frame, no hovered UI or active
  text/widget, and Alt. Inputs must not move the Player in the world.
- A small hint shows Drag: Rotate / Alt+Drag: Pan / Wheel: Zoom. Show keyboard
  instructions only while Alt is held. Scale text down relative to regular UI.
- Focus presets cover Whole body, Face, Body, Hands, Feet. Center puts the chosen
  focus at the center of the scene frame while retaining viewing direction and
  distance. Reset View restores the camera view captured when opening the menu.
- Prefer loaded skeleton landmarks; use actor world bounds for missing landmarks.
  Do not assume the same fixed world height for different Actors. Missing/invalid
  geometry disables focus instead of falling back to an unrelated Actor.
- Center/pan changes the orbit and zoom pivot. Focus changes are explicit; area
  tabs do not unexpectedly move the camera.
- Menu side determines the scene frame and explicit Center composition. Opening
  keeps its initial view; changing input devices must not hide a handoff bug.
- Preserve the Player-only camera scope. NPC tattoo selection remains usable.
- Check source/update traces before blaming another camera mod. Mod-isolation QA
  changes one relevant camera mod at a time and records the configuration.

The 13:27/13:28 test confirms native offsets and signed zoom can remain stable
while rendered framing still changes. Prior offset corrections are insufficient
proof of seamless handoff. The implementation must seed control from the rendered
camera pose and verify continuity independently of skeleton focus/pan features.

### Rendered-pose FreeCamera candidate

This supersedes the earlier third-person offset-control implementation. Capture
the rendered NiCamera position and center ray before acquiring ownership. Seed
the native FreeCamera state's world translation and pitch/yaw from this view;
do not write NiCamera/root transforms or install an update hook. PhotoMode's
native FreeCamera usage is documented in the camera-control research record.
Native FreeCamera input and Player input are suppressed while the session owns
control. The UI emits bounded motion through the existing game-thread coordinator.

Opening capture is read-only and retained separately from the mutable pivot.
Restore the original state only while state, target, root and rendered-camera
identity still match; release owned input/lease resources on invalidation without
overwriting a replacement view. Restore captured third-person controls on normal
close. The candidate does not provide collision avoidance or arbitrary roll;
near-wall clipping and first-input continuity require in-game acceptance.
