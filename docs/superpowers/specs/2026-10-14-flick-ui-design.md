# FLICK Launcher and UI Navigation Design

## Intent

Open SlaveTatsUI directly from FLICK and improve the existing Menu Framework UI
across all workflow screens. The left-side, actor-visible layout takes cues from
SlavetatsUIPzx and Fitting Room, without copying their runtime or assets.

## Scope

- One PR, built in testable slices, updates Current Slots, Browse,
  Preview/Apply/Replace, Slot Actions, Edit Appearance, and Loadouts, including
  confirmations, error states, and retry actions.
- FLICK is an optional launcher. Its sidebar tool closes the FLICK menu and
  schedules the existing Tattoo Browser to open on the game thread. It does
  not render or mutate tattoo state.
- The existing Menu Framework entry and hotkey remain available when FLICK is
  absent or incompatible. Repeated FLICK clicks cannot queue duplicate opens.
- The current UI uses a consistent left-side panel, actor/area header,
  wrapped workflow breadcrumb on navigation screens (including confirmation states),
  Tattoos/Loadouts tabs on Current Slots, slot-to-Browse navigation, contextual actions, and clear
  Back/Cancel/Save controls. All current features remain reachable.
- Current Slot cards show their slot number and thumbnail without Add/Edit action
  text. External slots show a red dot in the top-right of the card header; their
  tooltip explains Read Only status and external ownership. Existing tooltips
  retain tattoo details and empty-slot click guidance.
- The Current Slots landing screen omits its redundant breadcrumb because the
  Target title includes the Actor name and Form ID on one line across Tattoos,
  Loadouts, and other workflow screens. The target header contains two buttons:
  Player and Choose NPC; Followers are included in the nearby NPC picker. The
  redundant Nearby Actor combo is hidden. Choose NPC toggles the picker open/closed.
  Under the Tattoos tab, Body, Face, Hands,
  and Feet form a compact second tab row; Refresh is an action button after
  Feet on that row. Loadouts stays in the primary tab row.
- Area tabs synchronize with the workflow only on initial presentation or an
  external area change. Idle frames must not force selection over a user click.
  Slot snapshots and thumbnails are read after the selected area changes.
- Current Slots uses four columns and three rows, up to twelve slots per page.
  Lock and Favorite icon buttons are temporarily hidden pending a control-layout
  redesign. Edit Appearance now offers a text-only Favorite/Favorited button when
  its applied tattoo resolves to one unambiguous catalog identity. It writes the
  existing global favorite setting independently of appearance Save and Undo.
  Existing lock enforcement and saved favorites remain intact.

## Architecture and safety

- The FLICK callback only draws its launcher, requests FLICK close, and
  schedules a menu-open command. It never reads or writes the workflow,
  thumbnails, SlaveTatsNG, JContainers, or actor state.
- Workflow rendering remains in SKSE Menu Framework. Existing model,
  scheduler, service, and runtime boundaries are unchanged.
- Explicit actor identity, target generation, external-slot read-only behavior,
  stale-handle revalidation, and synchronization-only retry remain mandatory.
- An unavailable FLICK host logs once and leaves Menu Framework usable.

## FLICK dependency

- Pin the official FLICK API v6 header to a reviewed revision, retain GPL-3.0
  attribution, and connect dynamically rather than linking a FLICK binary.
  This repository is GPL-3.0-or-later.
- Use FLICK API wrappers and matching public ImGui types; never access FLICK's
  private ImGui context.
- The registered tool has plugin lifetime and is registered once.

## Reference feature assessment

| Reference feature | Decision | Reason |
| --- | --- | --- |
| Left-side, actor-visible panel | Adopt | Makes browsing and viewing the actor practical. |
| Slot cards, area tabs, search, thumbnail grid | Adapt | Reuse the current models and DDS/BSA thumbnail cache. |
| FLICK sidebar launch | Adopt | Adds direct access without changing the hotkey. |
| Clear Save/Cancel/Back controls | Adopt | Keeps actions and safe exits consistent. |
| FLICK themes and controller widgets | Defer | The browser remains in Menu Framework. |
| Free camera and SmoothCam integration | Defer | Camera ownership needs separate compatibility design. |
| Hover-to-apply tattoo preview | Defer | Transient overlay writes can leak into a save/co-save. |
| Bundled SlaveTatsNG and direct storage writes | Reject | Violates runtime authority and version support. |
| PNG thumbnail cache | Defer | The current DDS/BSA pipeline already works. |

## Acceptance

- With FLICK v6 installed, selecting SlaveTatsUI in its sidebar opens the
  existing browser after FLICK closes.
- Without FLICK, the Menu Framework entry and hotkey still work. Incompatible
  FLICK does not crash plugin load.
- All redesigned screens preserve actions, disabled states, confirmations,
  pending states, errors, and retry behavior.
- Debug and Release builds and relevant CTest suites pass. In-game QA covers
  FLICK launch/close, all screens, Player/Nearby Actor selection, external
  locked slots, apply/replace/remove, appearance Save/Cancel, loadouts,
  thumbnails, and synchronization-only retry.

## Source references

- FLICK API v6: https://github.com/Fuzzlesz/FUCK_API/tree/22b5362aa3d8c3d01c267339724aa895cba124b0
- SlavetatsUIPzx browser: https://github.com/pethzero/SlavetatsUIPzx/tree/d81b2c610574113305b99f9244b02a36013694ea
- Fitting Room: https://github.com/maartenharms/fitting-room
