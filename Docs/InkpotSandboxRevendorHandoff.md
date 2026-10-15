## InkpotSandbox handoff: re-vendor Inkpot 1.45.23

Context: `InkpotSandbox/Plugins/Inkpot` is a squashed git subtree of this
repository's `release` branch (see the sandbox's `Plugins/Inkpot/VENDOR.md`).
Inkpot 1.45.23 ships the case-sensitive variable snapshot, the per-line
variable-name cache (no template parsing while a story runs) and the
NativeBindings test cleanup on top of the 1.44.23 template fixes. Nothing in
the sandbox's own code needs to change for this release: the public API is
additive (`FProsettaLineMetadata::VariableNames`,
`UProsettaMetadata::CollectVariableNames`, `UProsettaLine::FVariableSnapshot`,
`FProsettaInlineTemplate::GetParseCount`).

Do this in the sandbox once the Inkpot PR has merged to `release` and the
`v1.45.23` release exists.

### 1. Pull the subtree
```bash
cd InkpotSandbox
git switch -c codex/update-inkpot-1.45.23
git subtree pull --prefix=Plugins/Inkpot https://github.com/chanceagency/Inkpot.git release --squash
```
Resolve conflicts, if any, in favour of upstream: the sandbox carries no local
edits to `Plugins/Inkpot`.

### 2. Update `Plugins/Inkpot/VENDOR.md`
- Source release: `1.45.23` (tag `v1.45.23`), link
  `https://github.com/chanceagency/Inkpot/releases/tag/v1.45.23`.
- Pinned source commit: the `release` commit the subtree pull squashed
  (`git log -1 --format=%H origin/release` in an Inkpot checkout, or read it
  from the squash commit message).

### 3. Build and test
```bash
./build-mac.sh build
./build-mac.sh test
```
`Inkpot.Prosetta.*` (now including `InlineTemplate.StoryParseOnce`) and the
sandbox's own `Inkpot.Prosetta.Sandbox.*` suites must pass with no failures,
no skips and no warnings. If a sandbox test keyed variable snapshots by a
`TMap<FString, FString>`, switch it to `UProsettaLine::FVariableSnapshot`;
`FString` map keys compare case-insensitively.

### 4. Merge
Open the sandbox PR from the temporary branch with the VENDOR.md change and
the squash commit; keep the plugin at `Plugins/Inkpot`, never a symlink.
