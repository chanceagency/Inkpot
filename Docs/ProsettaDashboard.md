# Prosetta Dashboard

The Prosetta Dashboard is an editor tab that runs the whole Prosetta sync loop
without a terminal: credentials, a named sync profile, volumes, the watch, and a
log of recent changes. It ships in the Inkpot plugin as the `ProsettaDashboard`
editor module (Inkpot 1.48.24 and later), so any Unreal C++ project gets it by
adding the plugin.

## Prerequisites

| What | Why | Notes |
| --- | --- | --- |
| Unreal Engine 5.7.4 or later, C++ project | Inkpot is a source plugin | Tested on UE 5.8 (macOS). A Blueprint-only project needs one C++ class first, so Unreal can compile the plugin. |
| Prosetta Sync CLI 0.10.0 or later | The dashboard runs it through the wrapper | Install with `curl -fsSL https://downloads.prosetta.studio/cli/install.sh \| bash`, so `prosetta-sync` is on the `PATH` or in `~/.local/bin`. Alternatively, vendor a published build at `<Project>/Tools/ProsettaSync/publish/Prosetta.SyncTool.dll`, or point `PROSETTA_SYNC_BIN` (or the profile's **CLI Binary Override**) at an executable. |
| .NET 10 runtime | The CLI is a .NET 10 application | `dotnet --list-runtimes` should list `Microsoft.NETCore.App 10.x`. GUI editors do not inherit a login shell's `PATH`, so the wrapper also checks `/opt/homebrew/bin`, `/usr/local/share/dotnet` and `C:\Program Files\dotnet`. |
| Python 3.11 or later | Runs `prosetta.py` | The editor uses Unreal's bundled Python (`Engine/Binaries/ThirdParty/Python3`), so you don't need to install anything. Set `PROSETTA_PYTHON` to use another interpreter. Terminal use needs any Python 3.11+. |
| FFmpeg (optional) | Decodes compressed voice takes | The first watch generation with compressed audio fetches a checksum-pinned FFmpeg into `Saved/Tools/FFmpeg` in the background (`Scripts/ffmpeg.py`). WAV-only projects never need it. Set the profile's **Audio Decoder Path** to use your own. |

## Install in a new project

1. Copy the plugin to `<Project>/Plugins/Inkpot` (the folder must be named
   `Inkpot`).
2. Enable it. Either tick **Inkpot** in **Edit > Plugins** and restart, or add it
   to the `.uproject`:

   ```json
   "Plugins": [
     { "Name": "Inkpot", "Enabled": true }
   ]
   ```

   `EditorScriptingUtilities` is enabled through the plugin's own dependency
   list, so you don't need to add it.
3. Build the `<Project>Editor` target (the editor offers to do this when it
   opens) and open the project.
4. Optional: add these lines to the project's `.gitignore`. The mirror and
   credentials are derived, local data and must not be committed:

   ```gitignore
   .prosetta-sync/
   Saved/
   ```

   Commit `Config/ProsettaSync/Profiles/` if you want the team to share sync
   profiles. Profiles never contain a token.

You don't need to copy any script into the project. The dashboard runs
`Plugins/Inkpot/Scripts/prosetta.py`. If the project has its own
`Scripts/prosetta.py`, that copy takes precedence.

## Open the dashboard

Any of these:

- **Tools > Prosetta Dashboard**
- the **Prosetta** button on the level editor toolbar
- **Ctrl+Alt+P** (bound once the level editor has loaded)

The tab can be docked like any other nomad tab.

## First run

Until there is a wrapper, a CLI and credentials, the tab shows a checklist:

1. **CLI**: the `prosetta.py` wrapper, its Python and the `prosetta-sync` CLI are
   all present. If the wrapper itself is missing, the row names every place it
   looked (`<Project>/Scripts/prosetta.py`, then
   `Plugins/Inkpot/Scripts/prosetta.py`).
2. **Credentials**: **Connect…** opens a form for the base URL, project ID and
   project token. The token is piped to `prosetta.py init --token-stdin` on
   standard input. It never appears on a command line, in a log or in a settings
   file, and it is written only to `.prosetta-sync/config.json` (ignored, mode
   0600).
3. **Scope**: a mirror exists, or the profile names volumes.

**Retry** re-checks the CLI and credentials.

## Daily use

- **Connection header**: the credential source, project, scope, channel, mirror
  SHA and data age (amber after 30 minutes). **Reconnect** re-reads the
  credentials and runs `whoami`.
- **Settings**: the active sync profile (mode Watch / Sync once / Verify,
  channel, volumes with an optional `@tag`, no-mirror, repair, snapshot, audio
  decoder, CLI override, auto-start, import on accept). **Run** starts or
  restarts the watch, syncs once, or verifies.
  An empty volume list means the whole live project: **Run** clears the CLI's
  local `volumes` config first if an earlier volume switch left a scope there
  (a watch started at editor launch skips this and follows the local config).
  It does not select the default volume, which would be an explicit scope with
  its own cache.
- **Volumes**: the project's volumes from `volumes --json`. **Use** switches
  the mirror to a volume and can re-point the open level (see below).
- **Recent changes**: generations, syncs, verifies, errors and blocks, seeded
  from `Saved/ProsettaSync/Watch` receipts.
- **Banners**: Play is running (generations wait until it stops), the last
  error, and *mirror changed outside the editor* with **Apply** (rebuild the
  story asset) or **Dismiss**.

The dashboard only runs `prosetta.py` subcommands (`credentials`, `whoami`,
`volumes`, `sync`, `verify`, `watch`, `config`, `init`). It never writes to
Prosetta.

## Project settings

**Project Settings > Plugins > Prosetta Sync** (`Config/DefaultEditor.ini`,
section `[/Script/ProsettaDashboard.ProsettaSyncProjectSettings]`):

| Setting | Default | Meaning |
| --- | --- | --- |
| Profiles Directory | `Config/ProsettaSync/Profiles` | Where named profiles live (project-relative). |
| Default Profile Name | `Default` | The profile a fresh checkout starts with. Created on first use. |
| Story Asset Path | `/Game/ProsettaSyncedStory` | The asset that **Rebuild** writes from the canonical mirror. Volumes use `/Game/Prosetta/Volumes/<slug>/Story`. |
| Story Source Host Class | `ProsettaStorySourceActor` | The actor spawned when a level without a story source host is re-pointed at a volume. |

Per-user state (active profile, recent connections) is stored in
`Saved/Config/<Platform>/EditorPerProjectUserSettings.ini`.

## Levels and volumes

Which story a level plays is an `FProsettaStorySource` (Inkpot runtime) held by
an actor that implements `IProsettaStorySourceHost`.
`ProsettaStorySource::ForWorld(World)` returns it, and
`ProsettaStorySource::CompiledStoryPathForWorld(World)` resolves the compiled
story in the mirror through `manifest.json`.

When **Use** re-points the level, the dashboard finds the first host in the open
level, or spawns **Story Source Host Class**, and calls
`SetProsettaStorySource` inside an undo transaction. If the project already has
a level setup actor, implement the interface on it and select it in the setting:

```cpp
UCLASS()
class AMyLevelSetup : public AActor, public IProsettaStorySourceHost
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere) FProsettaStorySource StorySource;
	virtual FProsettaStorySource GetProsettaStorySource() const override { return StorySource; }
	virtual bool SetProsettaStorySource(const FProsettaStorySource& New) override
	{
		if (StorySource == New) { return false; }
		Modify();
		StorySource = New;
		return true;
	}
};
```

## Extending from a project editor module

The project's editor module can depend on `ProsettaDashboard`:

- `FProsettaDashboardModule::Get().SetPresentationImporter(...)` installs an
  importer for voice and portrait presentation assets. **Rebuild** calls it
  when the profile has **Import On Accept** on. Packages it writes as
  `<StoryAsset>_Presentation` or under `<folder>/ProsettaMedia/` are saved with
  the story. Without an importer, Rebuild builds the story only.
- `LoadActiveProfile()`, `WatchOptionsFromProfile()` and `GetProjectWatch()`
  expose the active profile and the editor-owned watch, for example to refresh
  presentation from `UProsettaWatchSubsystem::OnGenerationAccepted`.

## Scripts

`Plugins/Inkpot/Scripts` contains:

| File | Purpose |
| --- | --- |
| `prosetta.py` | The wrapper around `prosetta-sync`. It isolates credentials (`.prosetta-sync/config.json`, `PROSETTA_PROJECT_TOKEN`, the home config) and pins the mirror to `<Project>/Saved/Prosetta`. It also hands completed watch generations to the editor as receipts in `Saved/ProsettaSync/Watch`. |
| `ffmpeg.py`, `ffmpeg-lock.json`, `FFmpeg-NOTICES.md` | Fetches and verifies the pinned FFmpeg into `Saved/Tools/FFmpeg`. |
| `tests/test_tools.py` | Unit tests for both scripts (`python3 -m unittest tests.test_tools` in `Scripts`). |

The wrapper serves the project named by `PROSETTA_PROJECT_ROOT`, which the
editor exports. Without it, the wrapper uses the closest folder above the script
that contains a `.uproject`. From a terminal in the project folder:

```bash
python3 Plugins/Inkpot/Scripts/prosetta.py credentials
```

```bash
python3 Plugins/Inkpot/Scripts/prosetta.py sync
```

```bash
python3 Plugins/Inkpot/Scripts/prosetta.py watch
```

`native-bindings` (also run after `sync`) copies Prosetta's generated C++ into
`Source/<first runtime .uproject module>/Prosetta/NativeBindings`. Set
`PROSETTA_NATIVE_BINDINGS_DIR` to use another folder. `import`, `sync-import`,
`watch-import` and `relay` need project helpers (`Scripts/import-prosetta.py`
with a story import commandlet, `Scripts/prosetta-relay.py`). The plugin does not
ship these helpers, and the commands say so when a helper is missing.

## Security

- Tokens live only in ignored config (`.prosetta-sync/config.json`,
  `Saved/ProsettaSync/prosetta-sync.config.json`, the CLI user config) or in
  `PROSETTA_PROJECT_TOKEN`. They never go in `.uproject`, `Config/`, profiles,
  editor settings or logs. A profile that contains a token-like key is refused
  on load.
- C++ never reads a credential file. Credential presence comes from
  `prosetta.py credentials --json`.
- The dashboard never writes to Prosetta: no `POST`/`PATCH`/`PUT`/`DELETE`, no
  side-effecting `watch-import`, no release builds.

## Tests

The editor automation tests are `Inkpot.Prosetta.Dashboard.*` (model, profiles,
CLI runner, widgets, tab, re-point, shipped scripts) and
`Inkpot.Prosetta.ImportCache.Watch*` (project watch and decoder fetch).
`Inkpot.Prosetta.StorySource.*` covers manifest-driven story paths and the host
interface. Run them headless:

```bash
UnrealEditor-Cmd <Project>.uproject -ExecCmds="Automation RunTests Inkpot.Prosetta" -TestExit="Automation Test Queue Empty" -unattended -nullrhi -nosplash -stdout
```
