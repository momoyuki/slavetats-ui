# Catalog preview runtime capability — 2026-10-16

Read-only research for the approved hover-preview prerequisite. No product edits, native injection, builds or deployment.

## Installed evidence

- MO2 RaceMenu directory: `D:/Modding/SKYRIM-MOD/mods/RaceMenu`.
- meta.ini reports version 0.4.20.0, archive `RaceMenu Anniversary Edition v0-4-20-0-19080-0-4-20-0-1776620918.7z`.
- `SKSE/Plugins/skee64.dll`: 2,521,088 bytes; SHA256 `5225E4E3B185E6FC57C8D31B0CEDBE5A030A951D9A744D33071B64C45A38C208`.
- Local SKSE log reports skee loaded correctly as handle 134; NetImmerse Override Enabled appears in skee64.log. A separate SKEEcosaveLoadCrashFix DLL is also loaded. These establish installation/loading, not the public interface version.
- No SDK/interface header was found inside the installed RaceMenu directory. No external source was downloaded as a local checkout by this research; cited raw files were read in memory.

## Public native interface IS registered upstream

Primary source revision throughout: `expired6978/SKSE64Plugins` commit `9ebcb733e17be695f994cd2e9cc383043446bc02`.

- main.cpp explicitly registers `Override` in the public InterfaceMap; it is not only a private implementation class. [Registration](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/main.cpp#L943-L953).
- Exchange message type is 0x9E3779B9. The payload contains one IInterfaceMap pointer initialized null. SKEE's listener replaces it with its global map. Exchange after PostLoad listener registration, normally from our PostPostLoad callback, targeting the loaded SKSE plugin name `skee`. Require Dispatch success, nonnull map and QueryInterface("Override"). [Payload/map](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/IPluginInterface.h#L24-L50), [handler/lifecycle](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/main.cpp#L555-L578).
- IOverrideInterface current version is 2; implementation GetVersion returns that constant. Only enable the binding after runtime GetVersion matches the supported ABI. [Version](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/IPluginInterface.h#L389-L401), [return](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/OverrideInterface.cpp#L24-L27).
- IPluginInterface has a virtual destructor before GetVersion/Revert; IInterfaceMap has its own three-method vtable. Preserve every preceding IOverrideInterface virtual slot, signatures and integer widths when binding; do not declare a two-method truncated interface or reinterpret an unrelated SDK interface. Get/SetNodeProperty are public virtuals. [Complete official declaration](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/IPluginInterface.h#L389-L464).

### Variant ABI

The public API uses visitors, not the private OverrideVariant memory layout. SetVariant is a polymorphic class with methods in order GetType, Int, Float, String, Bool, TextureSet; Type enum order None/Int/Float/String/Bool/TextureSet. GetVariant has Int/Float/String/Bool/TextureSet callbacks and no declared virtual destructor. These distinctions affect vtable layout. Use copied strings inside GetVariant callbacks; never retain an engine-owned const char pointer. SetVariant String backing must remain alive through synchronous call; implementation packs it into its own string value. [Visitors](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/IPluginInterface.h#L403-L423), [packing](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/OverrideInterface.cpp#L2525-L2577).

## Callable nonpersistent route

`IOverrideInterface::GetNodeProperty(actor, firstPerson, nodeName, key, index, visitor)` captures actual visual values. `SetNodeProperty(..., setter, immediate=true)` modifies loaded geometry without persistent registry writes. Native AddNodeOverride always writes registry; ApplyNodeOverrides republishes persistent values and must not follow a temporary paint. [Public implementations](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/OverrideInterface.cpp#L2723-L2774).

Impl_SetNodeProperty finds an existing node under the chosen first/third-person root. With immediate=true it calls SetShaderProperty directly; false queues NIOVTaskSetShaderProperty. SetNodeProperty returns void, so success requires explicit readback and node/material identity checks, not merely return from the call. Missing node is silently ignored. [Node lookup](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/OverrideInterface.cpp#L750-L774).

Relevant keys are 0 emissive color, 1 emissive multiplier, 2 glossiness, 3 specular strength, 7 tint, 8 alpha, 9 texture (index selects diffuse/normal/glow layer). Public integer widths: key uint16, index uint8; scalar index -1 is represented 0xFF. [Key constants](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/OverrideVariant.h#L25-L42).

### Texture lifetime and synchronous behavior

SetShaderProperty's texture case runs NIOVTaskUpdateTexture immediately when requested; otherwise it queues it. The task clones texture-path set, sets the requested path, loads a NiTexture into NiPointer, assigns the material texture reference, and initializes the shader. This establishes a synchronous task path and retained texture reference, not proof that all engine texture loading work has completed. Capture/paint/readback/restore should run in our single scheduled game-thread operation using immediate=true to avoid a late SKEE task painting after Save/Restore. [Texture dispatch](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/ShaderUtilities.cpp#L298-L312), [texture lifetime](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/ShaderUtilities.cpp#L147-L180).

GetShaderProperty supports actual texture paths and the alpha/gloss/specular/emissive fields, enabling exact copied-value snapshot/readback on a compatible material. [Readback](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/ShaderUtilities.cpp#L90-L111). Node/material rebuilds during unload/equipment changes must invalidate the visual snapshot; do not write an old material snapshot onto a new unrelated node.

## Empty slot feasibility and blocker

An authoritative empty slot can still have an existing blank overlay geometry: SetNodeProperty can paint that geometry without adding a record. Confirm the exact named node exists, is the right geometry/material, and snapshot its original blank diffuse/normal/glow plus scalar properties before enabling preview. Do not confuse empty tattoo metadata with absent overlay geometry.

If the node is absent, SetNodeProperty cannot create it. OverlayInterface AddOverlays inserts the actor into `overlays.m_data` before building, and that set is serialized. Therefore AddOverlays is not a safe nonpersistent bootstrap for an NPC that lacks overlays. Player HasOverlays always returns true and AddOverlays is a no-op for Player; neither guarantees a particular loaded slot geometry exists. [Creation and membership](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/OverlayInterface.cpp#L821-L871), [serialization](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/OverlayInterface.cpp#L998-L1008).

Initial capability should be **compatible existing nodes only**, with a typed unavailable result for missing node/material/API. A missing-node creation backend is a separate prerequisite; do not silently persist overlay membership or bypass SlaveTatsNG authority to make hover work.

## Remaining gates

1. Runtime handshake/version probe for the installed DLL; metadata alone cannot prove v2 ABI. No runtime query was performed in this research.
2. Verify matching public declaration/SDK reuse route. The upstream tree contains no general LICENSE/COPYING file for this source (only bundled fmt license discovered); do not assume source redistribution permission. Prefer an authorized published modder SDK/header or independently authored minimal ABI declaration preserving the full documented vtable, with provenance. No upstream code/header was vendored here.
3. Existing-node probes for Player and exact loaded NPC, all areas, including blank slots and missing emissive storage. Fail before write when snapshot incomplete.
4. Readback/restore every changed property and texture layer; avoid clearing pack bump/glow metadata. Detect authority/generation/node changes before each operation.
5. In-game save/co-save comparison and reload acceptance: setter source bypasses nodeData serialization, but our complete backend and installed patch combination remain unverified.

Conclusion: upstream exposes a concrete nonpersistent native setter route. Installed loading is confirmed; actual installed public ABI, existing slot geometry and full save/reload behavior remain runtime capability gates, not established support.

## Addendum: exact reflection copy signature and shader color hazards

### JContainers shallowCopy

Installed primary declaration `D:/Modding/SKYRIM-MOD/mods/JContainers SE/scripts/source/JValue.psc:51` is `Int function shallowCopy(Int object) global native`, described as not copying child objects.

Official source revision `ryobg/JContainers` at `0dcf61e0aa040b08473c851942357994ab4fea12` defines the internal function as `ref shallowCopy(tes_context& ctx, ref obj)` and registers it as JValue.shallowCopy. [Implementation](https://github.com/ryobg/JContainers/blob/0dcf61e0aa040b08473c851942357994ab4fea12/src/JContainers/src/api_3/tes_object.h#L176-L185).

The public reflection lookup returns `fi->tes_func`, not that internal function pointer. Stateful TES wrapper accepts State& followed by converted parameters; object conversion maps references to HandleT, and HandleT is UInt32. Thus the native reflected ABI is a domain/context pointer followed by one 32-bit object handle, returning one 32-bit handle: `uint32_t (*)(void* domain, uint32_t object)` at the erased binding boundary. Do not call it as a bare one-argument native function or add a Papyrus StaticFunctionTag. The repository's existing int32_t handle spelling has the same x64 register width but the official reflected HandleT is unsigned; preserve bit patterns and treat zero as invalid. [Lookup](https://github.com/ryobg/JContainers/blob/0dcf61e0aa040b08473c851942357994ab4fea12/src/JContainers/src/jc_interface.cpp#L9-L14), [wrapper](https://github.com/ryobg/JContainers/blob/0dcf61e0aa040b08473c851942357994ab4fea12/src/JContainers/src/reflection/tes_binding.h#L228-L247), [conversion](https://github.com/ryobg/JContainers/blob/0dcf61e0aa040b08473c851942357994ab4fea12/src/JContainers/src/collections/bind_traits.h#L13-L35), [handle definition](https://github.com/ryobg/JContainers/blob/0dcf61e0aa040b08473c851942357994ab4fea12/src/JContainers/src/object/object_base.h#L15-L18).

Only top-level fields are isolated by shallow copy. Do not mutate nested maps/arrays expecting independence; retain/pool the copied object using existing lifetime conventions and never edit the shared catalog source.

### SKEE key 0 glow channel order

For BSLightingShaderProperty, key0 is packed/unpacked as NiColor: red at bits16..23, green8..15, blue0..7, i.e. **0xRRGGBB**, without reversal. SetVariant must report Int for this value. GetVariant returns Int in the same representation. BSEffectShader material uses NiColorA, including alpha in the high byte. [Unpack](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/OverrideVariant.cpp#L249-L275), [pack](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/OverrideVariant.h#L127-L142). This visual backend mapping must be explicit and separate from any existing JContainers glow-storage channel conversion.

### Key7 tint can change material type

Key7 readback only yields tint for FaceGenRGBTint or HairTint materials. Key7 writes on another lighting material allocate a FacegenTint material, copy the old material, change shader flags, replace material and initialize the shader before assigning tint. It is not merely a color field write. [Read subtype restriction](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/ShaderUtilities.cpp#L119-L125), [conversion](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/ShaderUtilities.cpp#L340-L358).

Therefore require an already-compatible tint material for scalar snapshot/restore backend, or explicitly design and test whole material/flag restoration. Do not use API GetNodeProperty(key7) success as proof of a valid tint snapshot when no visitor callback occurred; preflight the material subtype and validate the actual typed callback.

### Missing emissive storage is a crash hazard, not a zero default

Lighting-shader key0 getter passes `lightingShader->emissiveColor` into PackValue<NiColor>, which dereferences it; setter passes it to UnpackValue<NiColor>, which also dereferences it. Neither call site nor helper guards null. Missing emissive storage must be detected via runtime material/shader inspection **before** invoking either API. Do not probe key0 by calling it and hoping for false/zero, and do not silently allocate storage without a separately approved restoration strategy. [Getter](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/ShaderUtilities.cpp#L90-L100), [setter](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/ShaderUtilities.cpp#L283-L293), [dereferencing helpers](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/OverrideVariant.cpp#L91-L103).

Initial capability predicate should require existing loaded exact slot geometry, compatible BSLightingShader material/tint subtype, nonnull emissiveColor, complete typed snapshot and stable material identity before any mutation. Unsupported materials return typed capability-unavailable; no persistent fallback.

## Addendum: exact overlay node names for read-only preflight

Official SKEE OverlayInterface.h at revision `9ebcb733e17be695f994cd2e9cc383043446bc02` defines fixed case-sensitive templates:

| Area | Regular slot | Spell slot |
| --- | --- | --- |
| Body | `Body [Ovl{}]` | `Body [SOvl{}]` |
| Face | `Face [Ovl{}]` | `Face [SOvl{}]` |
| Hands | `Hands [Ovl{}]` | `Hands [SOvl{}]` |
| Feet | `Feet [Ovl{}]` | `Feet [SOvl{}]` |

The braces are std::format numeric insertion, indexed from zero in installation loops. Capitalization is `Ovl`, not `ovl` or `Ovl` with another area spelling. HAND uses plural `Hands`. [Definitions](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/OverlayInterface.h#L23-L44), [area mapping](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/OverlayInterface.cpp#L913-L935), [installation loops](https://github.com/expired6978/SKSE64Plugins/blob/9ebcb733e17be695f994cd2e9cc383043446bc02/skee64/OverlayInterface.cpp#L1050-L1107).

These names are compile-time macros in the reviewed source, not INI-configurable name templates. Installed `D:/Modding/SKYRIM-MOD/mods/RaceMenu/SKSE/Plugins/skee64.ini` explicitly documents the same node patterns at lines120/126/132/138. Its physical mod copy configures 20 regular overlays for each area, spell count1 for Body/Hands/Feet and0 Face, with bPlayerOnly=1. This is not proof of the final MO2 virtual winning INI or actual loaded node presence. The preflight should inspect exact loaded nodes for the selected runtime slot, log missing nodes and material capability, and neither create nodes nor change overlay membership. Do not probe spell templates for ordinary SlaveTats slots unless the runtime explicitly identifies spell-slot ownership.
