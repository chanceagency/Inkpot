# Prosetta watch verification — October 9, 2026

The shipping plugin is verified in an isolated generated host through `python3 Scripts/agent-verify.py`. Host content and reports stay under ignored `Saved/`. This command builds the plugin with UE 5.8, requires every selected Unreal automation test to pass without skipped tests and a clean editor exit. Every test removes the packages it created under `/Game/Prosetta/Project199999` and `/Game/ProsettaUserFixtures` (memory and `.uasset` files) when it ends, pass or fail, so a run leaves no files in the host Content folder; saved localization and SoundWave assets are reloaded from disk inside the tests by unloading the fixture packages first.

The final local run passed the `Inkpot.Prosetta` tests on macOS/UE 5.8. Compiled JSON exact bytes and valid reimport identity, rejected truncated input, existing source compilation/runtime behavior, metadata/parser controls, Play/idle deferral, queued supersession, localization/audio updates, immutable checksum rejection, checked unused-owned package deletion, referenced retirement/resurrection, unrelated user-package preservation, live/frozen isolation, and native staging/explicit import are exercised. The `WatchSidecar` test launches a real shell child emitting a completed NDJSON fixture and observes import through the shipping editor ticker. It does not contact the authenticated Prosetta service.

Regression sensitivity was verified by three temporary source mutations in one test run: appending a space to compiled JSON failed the exact-byte assertion; removing the PlayWorld guard failed deferral and no-publication-during-Play assertions; omitting translation assignment failed runtime and saved localization lookup assertions. The verifier rejected that run. A fourth mutation omitting automatic checked cleanup failed physical package removal, obsolete identity destruction, retirement reconciliation, and saved removal assertions. All mutations were restored before final verification and publication.

An independent read-only agent reviewed the implementation twice. Unsafe unchecked physical deletion was replaced with Unreal's reference-aware `FAssetDeleteModel` scan/`CanDelete`/`DoDelete` route, with explicit undo protection and retention for referenced/unconfirmed candidates, duplicated SoundWave objects were made persistent rather than transient, and nested active/retired story import metadata was included in rollback. The review agent's inherited model identifier was not exposed. A real sidecar fixture failure was traced to the fixture emitting pretty-printed multiline JSON; its serializer now emits compact one-object-per-line NDJSON.

## Remaining acceptance

A human must accept the actual CLI/plugin pairing in a supported game project, including Play/stop, localization/audio failure preservation, and explicit native import. Windows and UE 5.7 compatibility, compressed-audio FFmpeg setup, and cooked-game playback are not verified by these fixtures. The local UE 5.7 build is blocked by its SDK compatibility check against installed Xcode 27. Do not mark the Unreal pilot milestone accepted from this editor fixture evidence alone.

Removed keys leave the active generation registry. `WatchOwnedRemoval` verifies that an unused owned SoundWave wrapper and its package are physically deleted while unrelated user packages survive. Saved external-reference fixtures retain referenced wrappers; one fixture removes the memory edge after saving to exercise disk-only reference protection. A separate fresh process verifies the deleted package remains absent, retained and unrelated packages exist, and the saved user reference resolves to the same retired identity. Referenced/unconfirmed identities remain editor-only for safe reference preservation and resurrection; external user references may still cook retired content. Cleanup never invokes force deletion, source-file deletion, or reference replacement.

## CLI 0.10.0 watch and inline Ink expressions — October 10, 2026

`python3 Scripts/agent-verify.py` passed 14 `Inkpot.Prosetta` tests and the fresh-process `WatchPersistence` test on macOS/UE 5.8.

- `WatchCLI` covers the 0.10.0 version gate (build metadata, `v` prefix, numeric rather than lexical comparison, prereleases of 0.10.0 rejected), argument construction for `--channel`, `--volumes` and `--no-mirror`, the CLI's own usage rules (fixed tags, multiple latest-release volumes, case-sensitive channel, argument injection), and status reporting for `error`/`blocked`/`status` event codes and the `--json` fatal result.
- `WatchSidecar` now answers `--version` with `0.10.0` and asserts the launched argument list.
- `WatchSidecarExit` launches real child processes. It checks that a 0.9.0 CLI is never started with `watch`, and that an exit-code-2 usage error printed to stderr after a valid NDJSON event is reported once the process exits, confirming on macOS that stderr shares the watch pipe.
- `InlineTemplate.Parse/Extract/Render/Metadata` cover the port of the Prosetta web Inline Ink Player: brace, quote and escape handling; anchored alignment with backtracking and a bounded step budget; ambiguity rejection; per-occurrence values; literal and variable fallback; and nested rendering limits.
- `InlineTemplate.Story` runs compiled Ink through `UInkpotStory` with a sidecar that uses Engine Sync `id`/`lineId` records. It checks evaluated text for arithmetic, variables, function calls, sequences, a choice, and post-choice state in the default language, Italian, and two placeholder-reordering test languages, plus fallback for a missing language.

The authenticated Prosetta stream was not contacted; a human should still run `Prosetta.Watch.Start` against a real project with CLI 0.10.0.

## Inkpot 1.43.23 — October 14, 2026

`python3 Scripts/agent-verify.py --tests Inkpot` passed all 207 `Inkpot` tests on macOS/UE 5.8 with Xcode 27.0, and the plugin was checked by hand in the Unreal Editor.

- `WatchVersionCheck` checks that `StartWatch` returns in under a second while a fixture CLI takes 2 s to answer `--version`, and that the watch launches only after the check passes. It also checks that a hung `--version` is dropped after `VersionCheckTimeoutSeconds` and never launched, that a `--version` exiting non-zero reports its exit code, that a stderr notice before the version line is ignored, and that `StopWatch` cancels a pending check.
- `WatchSidecarExit` now waits for the 0.9.0 refusal to arrive asynchronously. It adds a CLI that crashes with a .NET exception and checks that the status shows the exception message, not a stack frame.
- `WatchCLI` covers `SelectDiagnostic`: an error block keeps its first line, while stack frames, `--- End of` markers and indented continuations are skipped.
- `LocalizedLanguageCodes` covers `UProsettaLine::GetLocalizedText`: language codes match regardless of case and separator, a regional code falls back through its parent codes (`zh-hant-tw` to `zh-hant` to `zh`), a base code never guesses between regions, and inline expressions render through a normalised match.
- `InlineTemplate.RenderOnly` checks that `EUsage::RenderOnly` builds no alignment, renders the same output as an aligned template, still recovers values through `ExtractValues`, and still resolves nested values.
