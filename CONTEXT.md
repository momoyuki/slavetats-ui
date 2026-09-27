# SlaveTats UI

SlaveTats UI provides a slot-first interface for browsing and managing an explicitly selected Actor's SlaveTats overlays.

## Language

**Tattoo Identity**:
The combination of section and name that SlaveTats uses to resolve a tattoo definition.
_Avoid_: Source identity, file identity

**Actor Target**:
The explicit Actor identity whose SlaveTats slots the native workflow currently manages. The target is either the Player or a deliberately resolved Crosshair Target and is identified by form ID, never by display name or stale UI state.
_Avoid_: Current actor, implicit actor

**Target Generation**:
The workflow generation that scopes target resolution, slot snapshots, and operation completions to one Actor Target. Results from an older Target Generation are stale and must not update the current workflow.
_Avoid_: Actor cache version

**Current Slot**:
A configured overlay slot in the currently selected body area, whether empty, managed by SlaveTats, or managed externally.
_Avoid_: Tattoo card

**Selected Area**:
The Actor Target overlay area whose Current Slots are being managed: Body, Face, Hands, or Feet.
_Avoid_: Area filter

**Area-compatible Tattoo**:
A catalog tattoo whose declared area matches the Selected Area without regard to letter case. Only Area-compatible Tattoos are eligible for selection and application.
_Avoid_: Cross-area tattoo

**Contextual Filter**:
A Picker filter whose available values are limited by the Selected Area and any preceding filter. A value that is not available in a new context is cleared.
_Avoid_: Global filter

**Slot Snapshot**:
The most recently loaded Current Slot state for one Actor Target and Selected Area. It is the source of In-use Tattoo status and is refreshed at workflow boundaries rather than polled continuously.
_Avoid_: Live slot polling

**In-use Tattoo**:
A catalog tattoo whose Tattoo Identity matches at least one SlaveTats-managed Current Slot in the selected area. The UI labels this selectable state `In Use`; its matching Current Slot indices are supporting detail rather than part of the label.
_Avoid_: Used tattoo

**Applied-only Filter**:
A Picker filter that retains only In-use Tattoos for the current Actor Target and Selected Area. It combines with the other Contextual Filters, uses runtime-exact Tattoo Identity matching, and excludes external overlays.
_Avoid_: Global applied filter, external overlay filter

**Favorite**:
A persistent personal catalog preference identified by the exact tuple `(domain, sourceId, section, name)`. Favorites are independent of Actor, savegame, and applied state; unavailable catalog entries remain stored until explicitly removed.
_Avoid_: Per-Actor favorite, applied tattoo

**Recently Used**:
A persistent personal history identified by the exact tuple `(domain, sourceId, section, name, area)`. It records only successful Apply/Replace operations after synchronization, retains the ten newest identities independently for Body, Face, Hands, and Feet, and presents matching installed tattoos newest-first. It is independent of Actor Target and savegame.
_Avoid_: Preview history, per-Actor history, global unscoped history
