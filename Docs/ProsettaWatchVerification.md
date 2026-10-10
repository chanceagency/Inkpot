# Prosetta watch verification — October 9, 2026

The shipping plugin is verified in an isolated generated host through `python3 Scripts/agent-verify.py`. Host content and reports stay under ignored `Saved/`. This command builds the plugin with UE 5.8, requires every selected Unreal automation test to pass without skipped tests and a clean editor exit, and then launches another editor process to reload saved localization and SoundWave assets.

The final local run passed six `Inkpot.Prosetta` tests and one fresh-process `WatchPersistence` test on macOS/UE 5.8. Compiled JSON exact bytes and valid reimport identity, rejected truncated input, existing source compilation/runtime behavior, metadata/parser controls, Play/idle deferral, queued supersession, localization/audio updates, immutable checksum rejection, active retirement/resurrection, live/frozen isolation, and native staging/explicit import are exercised. The `WatchSidecar` test launches a real shell child emitting a completed NDJSON fixture and observes import through the shipping editor ticker. It does not contact the authenticated Prosetta service.

Regression sensitivity was verified by three temporary source mutations in one test run: appending a space to compiled JSON failed the exact-byte assertion; removing the PlayWorld guard failed deferral and no-publication-during-Play assertions; omitting translation assignment failed runtime and saved localization lookup assertions. The verifier rejected that run. All mutations were restored before final verification and publication.

An independent read-only agent reviewed the implementation twice. Unsafe unchecked physical deletion was replaced with active-registry retirement preserving user references, duplicated SoundWave objects were made persistent rather than transient, and nested active/retired story import metadata was included in rollback. The review agent's inherited model identifier was not exposed. A real sidecar fixture failure was traced to the fixture emitting pretty-printed multiline JSON; its serializer now emits compact one-object-per-line NDJSON.

## Remaining acceptance

A human must accept the actual CLI/plugin pairing in a supported game project, including Play/stop, localization/audio failure preservation, and explicit native import. Windows and UE 5.7 compatibility, compressed-audio FFmpeg setup, and cooked-game playback are not verified by these fixtures. The local UE 5.7 build is blocked by its SDK compatibility check against installed Xcode 27. Do not mark the Unreal pilot milestone accepted from this editor fixture evidence alone.

Removed keys leave the active generation registry and retain editor-only identities for safe user-reference preservation and resurrection. Physical package cleanup is deferred; external user references may still cook retired content. This behavior needs acceptance in the pilot review.
