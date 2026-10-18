# Camera reference research — 2026-10-16

Research only; no implementation or deployment.

## SlavetatsUIPzx

Reviewed revision `3bca8156dc154dc5904e0e125e123ee378a82989`.

- The render-thread UI accumulates orbit/pitch/pan/zoom intent and schedules ApplyTurntable through SKSE AddTask every frame. Gestures start outside all UI windows; wheel is excluded over UI. [Input loop](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/src/tattoo_browser.cpp#L862-L894).
- ApplyTurntable controls vanilla ThirdPersonState inputs: freeRotationEnabled, freeRotation, posOffsetExpected, targetZoomOffset and optional FOV. It does not write custom NiCamera transforms or install a PlayerCamera Update hook in this path. [State application](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/src/tattoo_browser.cpp#L667-L718).
- Only when paused it also snaps posOffsetActual/currentZoomOffset, updates player 3D and explicitly calls camera->Update(). This call is paired with coherent camera-state inputs. Copying the call alone is not a continuity fix. [Paused branch](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/src/tattoo_browser.cpp#L709-L717).
- It borrows SmoothCam before touching camera state, accepts OK/AlreadyGiven, and restores saved heading/free rotation/offsets/zoom/FOV/first-person before returning ownership. [Ownership](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/src/tattoo_browser.cpp#L720-L750), [lifecycle](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/src/tattoo_browser.cpp#L753-L801).
- Its opening shot is deliberately recomposed using front yaw PI and configured offsets/pitch/zoom. Those defaults do not meet our preserve-existing-view contract. The reusable idea is engine-state control, not its initial composition. [Opening pose](https://github.com/pethzero/SlavetatsUIPzx/blob/3bca8156dc154dc5904e0e125e123ee378a82989/src/tattoo_browser.cpp#L692-L707).

## SmoothCam

Reviewed revision `e0afd09aff26e5849dac461252e5bffbd5b69d62`.

- SmoothCam hooks TESCameraState Update slot 3, chaining the original. It runs pre-update, original state update, then custom camera update; its pre/post work is skipped while paused. This differs from our unobserved PlayerCamera slot 2 dispatch. [Wrapper](https://github.com/mwilsnd/SkyrimSE-SmoothCam/blob/e0afd09aff26e5849dac461252e5bffbd5b69d62/SmoothCam/source/hooks.cpp#L15-L71), [installation](https://github.com/mwilsnd/SkyrimSE-SmoothCam/blob/e0afd09aff26e5849dac461252e5bffbd5b69d62/SmoothCam/source/hooks.cpp#L430-L454).
- NiCamera and cameraRoot rotations have permuted columns: root 1 = NiCamera 0, root 2 = NiCamera 1, root 0 = NiCamera 2. The root rotation write checks ownership; NiCamera world rotation is written before that check. [Rotation](https://github.com/mwilsnd/SkyrimSE-SmoothCam/blob/e0afd09aff26e5849dac461252e5bffbd5b69d62/SmoothCam/source/thirdperson.cpp#L685-L724).
- SetPosition returns while another consumer owns the camera. Ownership does not disable vanilla camera placement. [Position](https://github.com/mwilsnd/SkyrimSE-SmoothCam/blob/e0afd09aff26e5849dac461252e5bffbd5b69d62/SmoothCam/source/thirdperson.cpp#L726-L731).

## Fitting Room

The coordinating agent inspected revision `8717ae7a8fe6185a65721564bac7237ddd411f65`: CameraFrame handles camera target selection and relies on an external inventory-camera integration. Targeting must not be confused with an independent orbit/zoom solution. [Implementation](https://github.com/maartenharms/fitting-room/blob/8717ae7a8fe6185a65721564bac7237ddd411f65/src/CameraFrame.cpp), [header](https://github.com/maartenharms/fitting-room/blob/8717ae7a8fe6185a65721564bac7237ddd411f65/src/CameraFrame.h).

## Current failure and next investigation

Our current SkyrimBrowserCameraPort publishes rendered/root transforms. The 11:31 diagnostic test confirms drag/wheel intent arrives and immediate readback matches the requested pose, then another frame overwrites it. PlayerCamera slot 2 installation is logged but callback entry is never logged. That hook is a failed/unproven approach, not an established post-engine seam.

Inference: investigate controlling vanilla third-person state, keeping existing game-thread scheduling, SmoothCam lease, explicit Player scope and frame/UI isolation. Do not install another hook without runtime dispatch evidence. Exact continuity is still unresolved: SmoothCam rendered pose can differ from vanilla freeRotation/zoom inputs. Capturing those fields alone does not prove a seamless handoff. Preserve the opening view, requested vertical direction, side composition and restoration; validate actual engine behavior in-game. Pure helper tests cannot prove camera dispatch or rendered continuity.

No external implementation code was copied.

## Addendum: Photo Mode free-camera implementation seam

Reviewed `powerof3/PhotoMode` at `bf50c35ffbf0b71a174411800e826cc40635443b` on 2026-10-16. Research only; no implementation/build/deploy.

### Verified native state, capture and write

- CameraPositions explicitly toggles into free camera before setting FreeCameraState::translation and rotation.x/y. Position capture requires currentState id kFree, calls GetTranslation, and stores the rotation fields directly. This is not third-person expected/actual offset interpolation. [Apply](https://github.com/powerof3/PhotoMode/blob/bf50c35ffbf0b71a174411800e826cc40635443b/src/PhotoMode/Tabs/CameraPositions.cpp#L64-L88), [capture](https://github.com/powerof3/PhotoMode/blob/bf50c35ffbf0b71a174411800e826cc40635443b/src/PhotoMode/Tabs/CameraPositions.cpp#L310-L333).
- Its IGCS bridge makes the naming explicit: rotation.x is pitch; rotation.y is yaw; translation is world position. CaptureCamera and ApplyCamera pair these fields with FOV and separately managed roll. [Snapshot and application](https://github.com/powerof3/PhotoMode/blob/bf50c35ffbf0b71a174411800e826cc40635443b/src/IGCSBridge/Bridge.cpp#L201-L233).
- Native rotation construction is intercepted at FreeCamera::GetRotation's FromEulerAnglesZXY callsite (relocation IDs 49814/50744, offset 0x1B) to add view roll. BuildBasis describes native Right/Forward/Up columns and invokes EulerAnglesToAxesZXY(pitch,0,yaw). Its printed matrix has forward.z = -sin(pitch). [Rotation hook](https://github.com/powerof3/PhotoMode/blob/bf50c35ffbf0b71a174411800e826cc40635443b/src/Hooks.cpp#L13-L20), [installation](https://github.com/powerof3/PhotoMode/blob/bf50c35ffbf0b71a174411800e826cc40635443b/src/Hooks.cpp#L66-L72), [basis](https://github.com/powerof3/PhotoMode/blob/bf50c35ffbf0b71a174411800e826cc40635443b/src/IGCSBridge/Bridge.cpp#L412-L438).
- Caution: that same revision's toe-in code derives pitch with positive asin(newForward.z), which is inconsistent with its documented negative-sine forward axis. Do not import its inverse formula without a forward→inverse round-trip test using the actual runtime matrix. [Inverse in toe-in path](https://github.com/powerof3/PhotoMode/blob/bf50c35ffbf0b71a174411800e826cc40635443b/src/IGCSBridge/Bridge.cpp#L583-L590).

### Reliable output seam demonstrated by reference

PhotoMode's IGCS extension hooks FreeCameraState::GetTranslation at vfunc slot 0x05, calls the original getter, then overrides the returned translation. It leaves native rotation/FOV intact and has diagnostics before/after override. This demonstrates a mature implementation consuming a camera-state getter, rather than a guessed post-update PlayerCamera vfunc. It does not prove this exact hook dispatch on our installation; any use still needs an entry trace. [Getter and timing](https://github.com/powerof3/PhotoMode/blob/bf50c35ffbf0b71a174411800e826cc40635443b/src/Hooks.cpp#L22-L36).

### Lifecycle and input: useful reference, narrower guarantees

- Activate records original camera state ID, enters free camera if needed and disables player controls. Deactivate toggles free camera back only when it entered it. This restores camera mode; it does not capture/reapply an arbitrary prior SmoothCam rendered transform or guarantee exact opening-view restoration. [Activation](https://github.com/powerof3/PhotoMode/blob/bf50c35ffbf0b71a174411800e826cc40635443b/src/PhotoMode/Manager.cpp#L71-L104), [deactivation](https://github.com/powerof3/PhotoMode/blob/bf50c35ffbf0b71a174411800e826cc40635443b/src/PhotoMode/Manager.cpp#L162-L181).
- TogglePlayerControls uses ControlMap and individual PlayerControls handlers; it is reasserted while active. The input manager separately checks cursor-over-window before allowing the camera pan gesture and uses the TFC input context for vertical movement. These are separate responsibilities: disabling player movement is not sufficient evidence that native free-camera mouse/keyboard motion is blocked while our UI owns input. [Controls](https://github.com/powerof3/PhotoMode/blob/bf50c35ffbf0b71a174411800e826cc40635443b/src/PhotoMode/Manager.cpp#L118-L148), [UI gesture exclusion](https://github.com/powerof3/PhotoMode/blob/bf50c35ffbf0b71a174411800e826cc40635443b/src/Input.cpp#L755-L792), [native TFC fields](https://github.com/powerof3/PhotoMode/blob/bf50c35ffbf0b71a174411800e826cc40635443b/src/Input.cpp#L159-L194).

### Implications for our approved Player-only orbit/pan/zoom

A free-camera state with controlled translation/pitch/yaw is an evidence-backed alternative to third-person offsets: it avoids coupling desired distance to vanilla zoom/interpolator state. For exact continuity, first capture the **rendered** opening pose before switching/ownership changes; seed free-camera state from that pose, and verify zero-intent readback is unchanged before accepting gestures. CameraRoot and NiCamera bases must remain distinct. Keep one immutable opening-view snapshot for Reset and exit restoration; Center is a separate action that changes the pivot to the selected loaded Player bone and must not overwrite that snapshot. No external source reviewed here establishes our complete arbitrary-pose handoff, bone focus or exact restoration; those are our implementation/verification obligations. Test input zeroing/exclusion, state transitions, SmoothCam release order and third-person return independently. No source code was copied.
