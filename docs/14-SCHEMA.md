# 14 — Rig State Schema

One document format serves three roles: the standalone rig file (`.blockrig`), the DAW state
chunk (VST3/AU), and (later) preset-pool entries. Implemented as a JUCE `ValueTree`
(binary `writeToStream` for chunks/files; XML export for debugging/diffing). This document is
the normative schema; examples shown as XML for readability.

## Design rules

1. **Versioned from day one.** `schemaVersion` integer; readers migrate old versions forward, never write old versions. Unknown attributes/children are preserved on load→save (forward compatibility for minor additions).
2. **Stages, not blocks, are the lane's children** — a stage holds 1 block now, N parallel rows in v1.1, so splits arrive without a schema migration.
3. **Third-party state embedded**, base64 of the plugin's own `getStateInformation` blob. Built-in NAM embeds its gzipped `.nam` (existing, proven). Large external payloads by reference + hash.
4. **Identity is the full PluginDescription tuple**, not a display name — `format` + `identifier` (JUCE `fileOrIdentifier`) + `uniqueId`, with `name`/`manufacturer`/`version` kept for error tiles and re-linking when the exact plugin is missing.
5. Everything a *reload* needs lives in the rig; everything *machine-local* (device selection, scan catalog) lives in app settings, **except** the standalone I/O binding, which is stored in the rig but treated as a hint (fall back gracefully when the interface isn't present).

## Schema (v1)

```xml
<BlockRig schemaVersion="1"
          name="Lead 5150 Wet"                 <!-- display name -->
          uuid="7f3c…"                         <!-- stable rig identity, for future setlists -->
          createdUtc="2026-07-29T19:04:00Z"
          modifiedUtc="2026-07-29T19:22:10Z"
          appVersion="0.2.0">

  <!-- I/O binding. Device fields are standalone hints only; ignored in DAW builds. -->
  <Input  mode="mono"                          <!-- mono | stereo -->
          deviceChannelL="0" deviceChannelR="-1"
          gainDb="0.0"/>
  <Output deviceChannelL="0" deviceChannelR="1"
          gainDb="0.0" monoSum="false"/>

  <Lane>
    <Stage>                                    <!-- v1: exactly one Row per Stage -->
      <Row gainDb="0.0" pan="0.0">             <!-- Row attrs reserved for v1.1 splits -->
        <Block uid="b1"                        <!-- unique within rig; stable across edits -->
               format="BlockRig"               <!-- internal format name for built-ins -->
               identifier="nam"
               uniqueId="0"
               name="NAM" manufacturer="BlockRig" version="1"
               bypassed="false">
          <State encoding="base64">…</State>   <!-- NAM block's own chunk (incl. gzipped .nam) -->
          <Editor open="false" x="120" y="240" w="760" h="480" onTop="false"/>
        </Block>
      </Row>
    </Stage>

    <Stage>
      <Row>
        <Block uid="b2"
               format="VST3"
               identifier="/Library/Audio/Plug-Ins/VST3/ValhallaVintageVerb.vst3"
               uniqueId="123456789"
               name="ValhallaVintageVerb" manufacturer="Valhalla DSP" version="4.0.4"
               bypassed="false">
          <State encoding="base64">…</State>
          <Editor open="true" x="900" y="180" w="640" h="400" onTop="false"/>
        </Block>
      </Row>
    </Stage>
  </Lane>

  <!-- Reserved, empty in v1 -->
  <Snapshots/>          <!-- v1.x: named bypass+parameter states over this lane -->
  <Mappings/>           <!-- v1.x: MIDI/parameter mappings -->
</BlockRig>
```

## Field notes

- **`Block.uid`**: short random id minted at insertion; keys editor-window state, CPU-panel rows, and (future) snapshot diffs. Never reused within a rig.
- **`format`**: `"VST3"`, `"AudioUnit"`, or `"BlockRig"` (internal). Matches JUCE `PluginDescription::pluginFormatName` for third-party.
- **`identifier` / `uniqueId`**: JUCE's `fileOrIdentifier` and `uniqueId`/`deprecatedUid`. On load, resolve via the catalog by `uniqueId` first (survives the plugin moving on disk), falling back to `identifier`, falling back to name+manufacturer search → error tile if unresolved. The error tile **retains the whole `<Block>` subtree untouched**, so state survives a missing→reinstalled round trip.
- **`<State>`**: exactly what the plugin's `getStateInformation` produced. For VST3 JUCE handles the component+controller pairing internally — we store the single JUCE blob. No size cap enforced; warn in UI above ~20 MB total rig size (research: folklore host-chunk risk zone).
- **`<Editor>`**: PatchWork-style per-block window memory. `open` honored only if the "reopen editors on rig load" preference is on.
- **`Input.mode`**: `mono` duplicates one device channel to the stereo lane; `stereo` maps L/R. In DAW builds the element is ignored (bus layout rules instead) but still round-tripped.
- **DAW chunk == this document**: `getStateInformation` writes the `ValueTree` stream; `.blockrig` files are the identical bytes. One serializer, one test suite.

## Migration policy

`schemaVersion` bumps only on breaking shape changes (attribute renames, semantic changes).
Additive fields don't bump. Migrations are pure `ValueTree → ValueTree` functions
`migrate_1_to_2(...)`, chained, unit-tested with fixture files for every released version.
The reader refuses versions *newer* than it knows, with a clear "made in a newer BlockRig"
message.

## App settings (NOT in the rig; app-support XML, sandbox-reachable path — see 12-ARCHITECTURE)

Plugin catalog (`KnownPluginList` XML) • denylist • scan paths • last audio device/buffer/rate •
UI prefs (accent, reopen-editors, always-on-top default) • recent rigs • picker recents.
