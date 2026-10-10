## InkpotSandbox handoff: integrate and test Inkpot 1.41.23 native bindings

Context: `InkpotSandbox/Plugins/Inkpot` is an older embedded copy of the plugin (it predates the Prosetta watch subsystem and this PR). The sandbox syncs with the CLI into `Saved/Prosetta`, so the mirror path (not the editor watch) is the primary route to test here.

### 1. Update the embedded plugin
1. In InkpotSandbox, replace `Plugins/Inkpot` with this branch (or the 1.41.23 release ZIP once published). Keep the folder named `Inkpot`.
2. `./build-mac.sh build` (or the Windows equivalent). Fix any first-build issues in the plugin on this branch, not in the sandbox copy — this PR was authored without a local UE install and has not been compiled yet.
3. `./build-mac.sh test` — the existing `Inkpot` suite plus the new `Inkpot.Prosetta.NativeBindings.*` tests must pass.

### 2. Author a module in Prosetta
1. In the sandbox's Prosetta project, enable Game Data Scripts if needed and set **Engine targets** to include Unreal.
2. **New Native Binding module** → name `SandboxDemo`, engine language **C++ (Unreal)**, functions e.g. `describe_item(item_name: string, quantity: int) -> string` and `play_chime() -> none`. Save; make sure the module page shows no validation errors (an invalid module is `excluded` in the manifest and never shipped).
3. Call the functions from the sandbox Ink (`EXTERNAL describe_item(item_name, quantity)` … `{describe_item("lantern", 1)}` and `~ play_chime()`).

### 3. Sync and generate loaders
```bash
./Scripts/prosetta-sync.sh sync --json
ls Saved/Prosetta/native-bindings/cpp/          # expect SandboxDemo.cpp
cat Saved/Prosetta/native-bindings/manifest.json
```
Copy `Saved/Prosetta/native-bindings/cpp/SandboxDemo.cpp` to `Source/InkpotSandbox/Prosetta/NativeBindings/SandboxDemo.cpp` (new folder; consider adding a small script step to the sync wrapper for this). Then in the editor Output Log:
```text
Prosetta.NativeBindings.Scan "/abs/InkpotSandbox/Source/InkpotSandbox/Prosetta/NativeBindings"
Prosetta.NativeBindings.Generate "/abs/InkpotSandbox/Source/InkpotSandbox/Prosetta/NativeBindings"
```
(The manifest argument defaults to `Saved/Prosetta/native-bindings/manifest.json`.) Expect `SandboxDemo.Bindings.cpp` beside the module. Rebuild.

### 4. Verify at runtime
- `Prosetta.NativeBindings.List` shows `SandboxDemo` with both functions and the manifest sha.
- `Prosetta.NativeBindings.Audit` passes (0 errors). Then break something on purpose — change the arity in Prosetta, re-sync without regenerating — and confirm `source_sha_mismatch` / `arity_mismatch` findings appear.
- Press Play in `ProsettaTestMap1`: the story line shows the string returned by `describe_item`, and `play_chime` runs once (add a `UE_LOG` in a local helper or a breakpoint; the module itself must stay engine-agnostic).
- With `bAutoBindNativeFunctions` off in Project Settings → Plugins → Inkpot, confirm the first Continue logs `calls EXTERNAL '…' but nothing is bound` instead of crashing, then call `Bind Registered Native Functions` from `UProsettaStoryWidget::StartStory` (or Blueprint) and confirm it works again.

### 5. Local module path
Add `Source/InkpotSandbox/Prosetta/NativeBindings/SandboxLocal.cpp` with `namespace Prosetta::Bindings::SandboxLocal { bool god_mode(bool enabled) { return enabled; } }`, declare `EXTERNAL god_mode(enabled)` in a local Ink file, run `Generate` again, rebuild, confirm it binds alongside the Prosetta module and that `Audit` reports it only as a local module (warning, not error).

### 6. Observers and errors
From `UProsettaStoryWidget` (C++) call `Story->ObserveAllVariablesNative(...)` and log changes while playing; bind `OnStoryError` in `WBP_ProsettaStory` and trigger a runtime warning to see it arrive.

### 7. Editor watch path (optional)
If the watch sidecar is configured: `Prosetta.Watch.Start …`, accept a generation, then `Prosetta.Watch.ImportNative "<namespace from Prosetta.Watch.Status>" "/abs/Source/InkpotSandbox/Prosetta/NativeBindings" generate` — it copies the staged sources, validates them against the generation's manifest and writes the loaders in one step.

Report back: build fixes needed, test results, and whether the `Saved/Prosetta → Source/…` copy step should become part of the sandbox sync wrapper.
