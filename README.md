# Inkpot for Prosetta
**Inkpot** - A container for **Ink** within the Unreal Engine developed by [The Chinese Room](https://www.thechineseroom.co.uk/), with surgical changes to facilitate running Ink stories authored with [Prosetta](https://prosetta.studio) <br><br>

This is a plugin for Unreal Engine 5.7 or later.<br>
This is version **1.40.23** of the plugin.</br>
By parsing out unique ProseLine IDs from embedded <prosetta> tags in your Ink source, we can seamlessly load associated artifacts— voiceover files, localized line text, and more.

## Prosetta line tags

Inkpot recognizes inline `<prosetta>` elements in story text to associate a piece of dialogue with a Prosetta ProseLine ID and optional data attributes. A tag can contain fallback text:

```ink
The driver says <prosetta id="023A" animate="wave" sfx="carHorn.mp3">Honk honk!</prosetta>
```

The `id` attribute identifies the Prosetta line. Prosetta Line IDs use the [Crockford Base32](https://www.crockford.com/base32.html) notation— case-insensitive, fixed-length, alphanumeric IDs which omit a few commonly-confused characters. Prosetta API routes always return IDs in all-caps, e.g. `5A0Z`; Inkpot consumes them as `FName` values without changing their spelling. Other attributes, such as `animate` and `sfx`, are preserved as name/value pairs for gameplay code.

During playback, Inkpot scans the emitted text for `<prosetta>` tags, removes the tag markup, and returns the remaining player-facing text. It resolves IDs against the story's imported Prosetta sidecar when one is available, using the authored ProseLine string in place of the text inside the tag. If no authored string is available, the tag's inner text is used. Each parsed segment also reports its ID, attributes, chosen text, and that text's offset and length in the cleaned result.

### Inline Ink expressions in sidecar strings

ProseLine strings may contain inline Ink logic, exactly as authored in Prosetta: `I'm a math wiz, {2+2} equals four!`. The Ink runtime has already evaluated that logic in the text it emits (`I'm a math wiz, 4 equals four!`), so Inkpot mirrors the Prosetta web app's Inline Ink Player rather than showing the raw template:

1. The default-language template from the sidecar is aligned against the runtime text inside the `<prosetta>` tag, recovering the value each `{expression}` produced at that moment. Any Ink expression works — arithmetic, variables, function calls, conditionals, sequences such as `{&tick|tock}` — because the values come from the story's own evaluation.
2. Those values are injected into the chosen template. Translations may reorder, repeat, or omit placeholders; a value is matched by the expression's text (whitespace-normalized), so `{gold * 2}` in Italian receives the same value as `{gold * 2}` in English.
3. A placeholder that was not recovered (for example a translation that references a variable the default string does not) falls back to live evaluation of literals and story variables.

If a default-language line cannot be fully resolved — the sidecar is stale and no longer matches the compiled story, or two placeholders sit side by side with no literal text between them — the runtime text is shown unchanged, so players never see raw `{...}` template text. `UProsettaLine::GetLocalizedText(LanguageCode, OutText)` renders a translation the same way and falls back to the default-language text when a translation is missing or cannot be resolved. `GetRuntimeText`, `GetTemplateValue`, and `RenderTemplate` expose the raw pieces for custom UI. Templates are parsed once when the sidecar loads; lines without `{` skip all template work.

The parser accepts single-quoted, double-quoted, and unquoted attribute values, as well as self-closing tags. Tag names and the `id` attribute name are matched without regard to case. Nested Prosetta elements are handled by matching their depth. If an opening tag is malformed or has no matching close tag, the unmatched remainder is kept as literal text.

Inkpot is a wrapper for the wonderful narrative scripting language **Ink** developed by [Inkle Studios](https://www.inklestudios.com/ink/).<br>

For a demo of how this integrates with an Unreal project (& a load more documentation) see [the inkpot demo](https://github.com/The-Chinese-Room/InkpotDemo/).<br>
More docs can be found [here](https://docs.google.com/document/d/1P5p0KLc7EfqqNNqzM0VHyllNZhtMQy7Bw_Y4_0Rcpu8/edit?tab=t.0), courtesy of Milo Duclayan,

For general support and chat with other users, check out [Inkle's discord](https://discord.com/invite/inkle#unreal-projects) <br>
(You'll find Inkpot chat in #unreal-projects)

## Changelog  

### Prosetta-specific changes (unreleased)
Inline Ink expressions in sidecar strings (`{2+2}`, `{gold}`, `{double(gold)}`…) now display their evaluated values in the default and translated languages, matching Prosetta's Inline Ink Player.<br>
Engine Sync line records are joined by `lineId` (falling back to `id`), fixing lookups for volume-qualified record IDs such as `23-0008`.<br>
Editor watch targets Prosetta Sync CLI 0.10.0's native `watch` command: version check before launch, `--channel`, `--volumes` and `--no-mirror` options, event codes and CLI usage errors surfaced through `Prosetta.Watch.Status`.<br>

### Prosetta-specific changes (1.40.23)
These changes apply to Inkpot's Prosetta integration, not to Inkpot's core Ink runtime.<br>
Prosetta Line IDs now use `FName` throughout import and runtime handling, avoiding repeated string comparisons.<br>
Sidecar metadata parsing supports Unreal's shared-string JSON keys and reserves storage to reduce allocations.<br>
Documented Prosetta tag parsing and ID requirements above.<br>

### Inkpot core compatibility (1.40.23)
Read the first Ink list entry directly in `ToGameplayTag`, avoiding a Clang 21 loop warning without changing its result.<br>

### Changes in 1.40.21
Updated to build with Unreal 5.8 whilst maintaining compatibility with 5.7.<br>
Continue blocked whilst rendering lines are in flight.<br>
Fixed SwitchPathToFlow incorrectly consuming lines. ( Internal continue is incorrect )<br>
FJsonObject keys are now FSharedString for 5.8.<br>
Linux build fixed for capitalisation on includes.<br>


### Changes in 1.32.21
Fixed naming of paths in debug dump.<br>

### Changes in 1.31.21
Changed flow not found from assert to error, fixes reported crash during serlialisation.<br>
List add item now does correct name resolution for item names containing an origin prefix.<br>

### Changes in 1.30.21
Gameplay tag tables now automatically created on ink source import. Conditional on bAutogenerateGameplayTags.<br>
Gameplay tag tables automatically added to gameplay tags list with generated & save to ini file. Conditional on bAutoUpdateGameplayTagsList<br>
Added ContinueIfYouCan, combines CanContinue and Continue to simplify blueprint code.<br>
Added SwitchFlowToPath reimplemented in the story class from the demo, added GameplayTag variant, SwitchFlowToPathGT.<br>
InpotList library now has functions to add individual items to lists, AddItem & AddItemGT.<br>
Inkpotwatch updated to work with gameplay tags.<br>
Implemented scriptable import pipeline, Ink source import can now be fully customised.<br>
Can now query inkpot for the story asset for a runtime story.<br>
Custom meta data can now be added to the story asset using the custom import pipeline.<br>

### Changes in 1.24.21
Fixed bad resolution of gameplay tags for getting variables. 

### Changes in 1.23.21
Fixed compilation errors when target cs has TargetRules.NativePointerMemberBehaviorOverride set to PointerMemberBehavior.Disallow (LyraStarterGame)<br>

### Changes in 1.22.21
Ink lists now report truthyness correctly.<br>

### Changes in 1.21.21
Module loading phase now set to PreDefault to resolve some blueprint function resolution errors.<br>
Fixed compilation errors on latest MSVC tool chain 14.44.35207.<br>
Fixed bad test condition parsing that was falsely failing some InkCPP unit tests.<br>
Merged support for Mac builds ( see [pull request](https://github.com/The-Chinese-Room/Inkpot/pull/118#issue-3442796120) )<br>

### Changes in 1.20.21
Variables can now be filtered in the blotter view.<br>
Variables can now be pinned in the blotter view.<br>
List entries can now be set in drop down menu in blotter view.<br>
New outliner tool, only shows story structure at the moment.<br>
Simplified version of Evaluate function, Eval, for when you have no parameters.<br>
Inkpot settings now split between project settings and editor preferences. <br>
Story factory class support, allowing projects to define their own subclass of the Inkpot story.<br>
Gameplay tag support, to streamline workflows in Blueprints. Gameplay tags can now be used instead of strings for variables and paths etc. <br>
Fixed late binding notification, which caused a few timing bugs especially for the Inkpot watch component.<br>

### Changes in 1.11.21
Added tools menu section for inkpot, for easier launching of the blotter.<br>
Blotter QoL, list value update in blotter now more streamlined, items now have check box entries.<br>
Exposed VisitCountAtPathString to Inkpot Story.<br>
Added library functions to test the type of an Inkpot value.<br>
Added delegate to allow notify when a line has finished being displayed.<br>
Line render notification, for when multiple systems are rendering your line of text.<br>
Fixed bad plugin declarations, no more dependency warnings.<br>
Refined blotter update, list values are now updated rather than being replaced for each update.<br>
Set is now SetText on blotter strings.<br>

### Changes in 1.10.21
Introducing the Blotter! or Inkpot Debugger, an Unreal editor utility widget that allows the viewing of and setting of Ink variables at runtime.<br>
Full support for Ink List creation & manipulation in blueprints.<br>
Added documentation to the headers for most blueprint facing functions, story, values & lists.<br>
Ink function evaluation now supported from Blueprints.<br> 
Added variable existence checks. All setter or getter functions from story now report success, plus generic test function IsVariableDefined.<br>
Fixed crash when calling begin story with a null asset.<br> 
Fixed crash when calling function with empty variable declarations.<br>

### Changes in 1.03.21
Added new abstract factory creation for stories, class UInkpotStory can now be subclassed on a per project basis.<br>
Switched settings back to regular UDeveloperSettings as backed by CVAR version did not seem to work.<br>
Fixed for 5.4 compilation error, template instantiation context error in InkPlusPlusTest.cpp(738).<br>
Fixed compiler warnings in 5.4.<br>
Fixed assertion when attempting to access no existent variable in story.<br>
Fixed typo in headers that would fail on certain compilers ( #include "Coreminimal.h" should have been #include "CoreMinimal.h" )<br>
FIxed Deprecation warning for GetAssetRegistryTags.<br>
Updated serialize function to match parent in UInkpotStoryAsset.<br>

### Changes in 1.02.21
Fixed assertion when accessing variables that do not exist in the ink script.<br>
Fixed relative path resolution bug that caused Ink source files to not import.<br>
Improved compilation logging.<br>

### Changes in 1.01.21
Added tag accessor methods to InkpotLine & InkpotChoice.<br>

### Changes in 1.00.21
Inkplusplus port now matches version 21 of the Ink runtime engine, Ink version 1.1.1.<br>
.net version updated to 5.0<br>

### Changes in 0.4.20
Settings backed by CVars added.<br>
First pass on auto reload and replay of ink source.<br>

### Changes in 0.3.20
JSON serialisation now functional.<br>

### Changes in 0.2.20
External functions are now implemented, along with functional tests.<br>

### Changes in 0.1.20
Initial release.<br>

## Requirements 
Inkpot works with version 5.7 of Unreal.<br>
Inkpot includes a C++ port of Ink-engine-runtime version 21, which can be found in the InkPlusPlus module.<br>

### .Net framework 5.0
InkleCate requires the .net framework 5.0 which you can download from here.<br>
https://dotnet.microsoft.com/en-us/download/dotnet/thank-you/runtime-5.0.17-windows-x64-installer<br>
(Without this you will not be able to import Ink source)<br>

### Visual Studio 2022
This plugin is distributed as source code which needs compiling.<br>
You will need to install Visual Studio 2022.<br>
https://visualstudio.microsoft.com/vs/<br>

>[!TIP]<br>
> You'll need the "Game Development with C++" workload in Visual Studio to compile Unreal projects.<br>
> To install it, go to `Tools > Get Tools and Features...` and select "Game Development with C++" under "Gaming."<br>

## Installing the plugin

Make sure you have **.Net framework 5.0** and **Visual Studio 2022** installed.<br>

In your **project folder**, create a folder named **Plugins** and copy the Inkpot plugin there.<br>
Make sure the inkpot folder is called simply '**Inkpot**'.<br>

LIke this:<br>

    [Project Root Directory]/Plugins/Inkpot/

For an example of how this plugin should sit in your project see the InkpotDemo.<br>
https://github.com/The-Chinese-Room/InkpotDemo

Once you have it installed, you'll need to enable it for your project. Open the project and go to `edit > plugins`. Search for "Inkpot."
If everything is set up correctly, it should appear in the plugins interface. Tick the box to enable the plugin.
It will prompt you to restart the editor. When you do, the plugin should compile.

## FAQ

### I cannot compile the plugin.
* Inkpot is distributed as C++ source code and will need compiling. <br>
Make sure you have **Visual Studio 2022** installed.<br>

* If you get this error _VisualStudioUnsupported x64 must be installed in order to build this target_.<br> 
For Unreal Engine 5.6, you need to be on v17.8 of the MSCV v143 toolchain. See here for more details.<br>
https://forums.unrealengine.com/t/visualstudiounsupported-x64-must-be-installed-trying-to-update-a-project-from-5-3-2-to-5-4-1/1835116/5

* If you get this error _[YourProjectName] could not be compiled. Try rebuilding from source manually_.<br>
Make sure you have the "Game Development with C++" workload installed in Visual Studio.<br>

### My Ink files will not import.

* Make sure you have **.Net framework 5.0** installed.<br>
Inkpot uses Inklecate to compile the Ink files and it needs .net 5.0.<br>
Without this the Ink files will not import.<br>

* Inkpot has only been tested as an application plugin and not an engine plugin.<br>
It needs to be located in the project's plugins folder and not the engine plugin folder.<br>

* The plugin folder name should be simply 'inkpot',<br>
If you unzipped it directly to your project, it will likely be called 'inkpot-release'.<br>
eg <your project root>\plugins\inkpot<br>

* See the inkpotdemo for how it should look in your project.<br>
https://github.com/The-Chinese-Room/InkpotDemo<br>


### My Ink file changes will not update on reimport.
* Inky, the Ink script editor, will silently fail when saving to read only files.<br>
Some version control systems will make the file read only when you check them out, eg perforce.<br>
This can lead to some confusion when running Inky alongside Unreal & Inkpot.<br>


## Module Structure
Inkpot is made from three modules. <br>

### InkPlusPlus
InkPlusPlus is the name of the module that contains the C++ port of the Ink Engine Runtime which can be found here</br>
https://github.com/inkle/ink/tree/master/ink-engine-runtime

When porting the code from C# to C++ we came to the conclusion that we should keep the source as close to the original C# as possible to make updates easier. </br>
This has now been updated to Inkversion 21, Ink v1.1.1. </br>

### Inkpot 
A set of wrapper classes for InkPlusPlus to simplify Blueprint coding.<br>
Manages the lifetime of the Ink story and provides the interface for Ink variable setting, getting and change notification.<br>

### InkpotEditor 
This is the unreal editor facing part of Ink pot.</br>
This manages the defintion of data types in Unreal for inkpot and the compilation of new Ink scripts as they are added to the project.</br>
It also contains the unit tests for InkPlusPlus.</br>

## Other folders
There area couple of supporting folders that make up the Inkpot distribution.<br>

### TestInkSource
All the Unit tests for Ink.</br> 
Currently 180 tests.

### Third Party 
This contains a copy of InkleCate and is used by the script importer to compile the scripts.<br>

## Thanks to 
**[Inkle](https://www.inklestudios.com/)** </br>
for creating Ink. We've used this for much more than narrative.</br></br>
**[UnrealInk](https://github.com/DavidColson/UnrealInk)**</br>
for giving us a good basis for creating InkPlusPlus.</br></br>
**Colin, Seb & Canute at TCR**</br>
for the brilliant job in porting the Ink Engine Runtime to C++, along with the editor asset handling, unit tests and Unreal integration, most of the work here in fact.<br>

## Compiled Prosetta stories

Import a Prosetta compiled story as `<name>.ink.json`. The editor imports the
compiled Ink directly and never invokes inklecate or the custom source compiler
pipeline for JSON. Raw Ink JSON is retained byte-for-byte; a Prosetta `story`
object/string wrapper is unwrapped without recompilation. Place optional line
metadata beside it as `<name>.ink.prosetta.json`.

Reimport updates the existing story asset in place. Malformed/truncated JSON,
unsupported Ink format versions, and malformed metadata leave the last imported
story intact and report an error. Existing `.ink` imports keep their standard or
custom source pipeline.

Maintainers can run `python3 Scripts/agent-verify.py` against UE 5.8 (set `UE_ROOT`
for another installation). It creates an ignored minimal verification project
using this shipping plugin, builds it, and rejects missing, skipped, or failing
Unreal automation reports. Use `--commit-message-file /absolute/message.txt` to
commit staged changes only after the verification succeeds. Evidence is saved
under `Saved/Automation/agent-verify`.

## Watch completed CLI generations in the editor

The editor watch drives the native `watch` command of **Prosetta Sync CLI 0.10.0
or newer**; older CLIs are refused before launch. Install or pin it with:

```bash
curl -fsSL https://downloads.prosetta.studio/cli/install.sh | bash -s -- --version 0.10.0
```

Run `prosetta-sync init` once so the CLI configuration holds the project token.
Keep credentials in the CLI configuration; they are never put in Unreal assets.
In the Unreal Output Log console, start the sidecar with quoted absolute paths:

```text
Prosetta.Watch.Start "/absolute/prosetta-sync" "/absolute/prosetta-sync.config.json" "/absolute/ffmpeg"
Prosetta.Watch.Status
Prosetta.Watch.Stop
```

The default CLI configuration lives at
`~/Library/Application Support/Prosetta/SyncTool/config.json` on macOS and
`%APPDATA%\Prosetta\SyncTool\config.json` on Windows. Optional arguments follow
the paths and map directly to `prosetta-sync watch` options:

| Console option | CLI option | Effect |
| --- | --- | --- |
| `--channel=live` (default) | `--channel live` | Import every completed live revision. |
| `--channel=latest-release` | `--channel latest-release` | Track the newest published release; one volume per watch. |
| `--volumes=core` or `--volumes=core,side` | `--volumes …` | Scope the watch to volumes. Fixed tags such as `core@1.0` use one-shot `prosetta-sync sync` instead. |
| `--no-mirror` | `--no-mirror` | Skip the CLI's engine mirror; imports still read the immutable generation. |

`StartWatch(CLIExecutable, ConfigFile, AudioDecoder, Channel, Volumes, bMirror)`
takes the same options from Blueprints or C++. Invalid options are rejected before
launch with the CLI's own wording. While watching, `Prosetta.Watch.Status` reports
the latest CLI event code — `STREAM_RECONNECTING` while the CLI reconnects,
`GENERATION_RETRY` or `REST_<status>` (for example `REST_401` for a bad token) when
a generation must be retried, and `blocked` for an unavailable release. If the CLI
exits, the status keeps its exit code and last diagnostic line; exit code 2 means
the CLI rejected the watch arguments.

The FFmpeg argument is optional for WAV-only projects. For compressed selected
voice takes, install FFmpeg for your editor platform and pass its executable
path; a missing decoder or decode failure preserves the last imported generation
and reports an actionable error. The plugin runs that executable directly.

The editor consumes only `generation-completed` events. It checks the immutable
manifest's generation/project identity and every artifact's SHA-256/size before
changing published assets. Downloads continue during Play. Imports wait for
Play/simulation and queued Play setup to finish, asynchronous loading/package
saving to finish, and active editor transactions to end. New events replace an
older queued live revision; repeated/older generations cannot roll back imported
assets. Active Ink story instances are never hot reloaded.

Assets live under `/Game/Prosetta/Project<id>/<Live|Release>_<scope-hash>/`.
The `Generation` asset maps artifact keys to stable story/supporting identities.
Compiled stories use the downloaded Ink JSON. Supporting metadata and localization
use `UProsettaSyncedArtifact`; `ResolveLocalizedText(lineId, languageCode)` reads
its baked lookup map. Selected audio artifacts use the same stable wrapper and a
persistent nested `Audio` SoundWave, held by a hard reference. Reference the
wrapper from your game, then read its current Audio property for each playback;
selected take replacement does not change the wrapper's identity. Unchanged media
is verified but not decoded/reimported. Localization/source data and audio remain
owned by the selected generation. Other binary formats stay in the immutable CLI
cache for game-specific adapters.

A removed key leaves the active Generation map. The watcher persists and rescans
its ownership registry, then uses Unreal's checked deletion model to remove unused
owned assets and their packages. External memory/disk references, undo references,
unconfirmed scans, or failed deletion preserve the identity in an editor-only
retirement map; reappearing referenced keys restore that same identity. The
retirement map is omitted from cooking, while an explicit external game reference
can still cook an old asset. The watcher never force-deletes objects or source
files. After removing user references, run `Prosetta.Watch.Cleanup "namespace"` to
recheck retirement; each accepted generation also performs this checked pass.
Cleanup failures report pending repair after generation acceptance; they do not
roll back the accepted generation. User-created packages at a generated path cause an import error instead of an
overwrite.

Live and latest-release watches have separate scope namespaces. A blocked or
missing release retains the last imported assets. Native sources are staged in
the completed CLI generation and are never copied automatically. For an explicit
C++ import, take the applied namespace from `Prosetta.Watch.Status` and run:

```text
Prosetta.Watch.ImportNative "Project42/Live_<scope-hash>" "/absolute/game/Source/MyModule/Prosetta"
```

This checks staged source hashes, preserves relative C++ module paths, and requires
the same editor idle conditions. Review the copied sources and rebuild explicitly.
C# remains staged for the Unity integration.

The subsystem also exposes these commands to editor Blueprints and C++ through
`UProsettaWatchSubsystem`. Shipping player builds contain cooked content only;
CLI watch and credentials exist exclusively in the editor module.

The local verification workflow exercises compiled import, source import control,
Play/stop deferral, out-of-order supersession, stable identities, changed localization
and WAV import, corrupt-generation preservation, checked owned package removal,
referenced retirement/resurrection, unrelated user-package preservation, frozen
isolation, native staging/explicit import, and a real process/ticker sidecar fixture.
It then launches a fresh editor process to reload saved localization/audio and
verify unreferenced packages remain absent and saved user references remain intact.
This is editor fixture evidence; a human project pilot, Windows validation, and a
cooked game/audio acceptance run are still required before milestone acceptance.
