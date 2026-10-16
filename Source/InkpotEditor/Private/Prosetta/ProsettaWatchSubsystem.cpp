#include "Prosetta/ProsettaWatchSubsystem.h"
#include "NativeBindings/InkpotNativeBindingGenerator.h"
#include "Inkpot/Prosetta/ProsettaNativeBindingsManifest.h"
#include "Prosetta/ProsettaChecksum.h"
#include "Asset/InkpotStoryAssetFactory.h"
#include "Inkpot/Prosetta/ProsettaSyncedArtifact.h"
#include "Containers/Ticker.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Editor.h"
#include "EditorFramework/AssetImportData.h"
#include "Factories/SoundFactory.h"
#include "AssetDeleteModel.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "Serialization/ObjectWriter.h"
#include "Serialization/ObjectReader.h"

namespace
{
 // Prosetta Sync CLI 0.10.0 is the first release with the native watch command.
 constexpr int32 MinimumCLIVersion[3] = { 0, 10, 0 };
 const TCHAR* MinimumCLIVersionText = TEXT("0.10.0");
 FString Hash(const FString& Value)
 {
  FTCHARToUTF8 utf8(*Value);
  return ProsettaChecksum::SHA256(utf8.Get(), utf8.Length());
 }
 bool Parse(const FString& JSON, TSharedPtr<FJsonObject>& Out)
 {
  return FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JSON), Out) && Out.IsValid();
 }
 FString Serialize(const TSharedPtr<FJsonObject>& Object)
 {
  FString result;
  FJsonSerializer::Serialize(Object.ToSharedRef(), TJsonWriterFactory<>::Create(&result));
  return result;
 }
 bool EventNamespace(const TSharedPtr<FJsonObject>& Event, FString& Out)
 {
  int32 project = 0;
  FString scope, channel, generation, path;
  if (!Event->TryGetNumberField(TEXT("projectId"), project) || project <= 0 || !Event->TryGetStringField(TEXT("scope"), scope) || scope.IsEmpty() ||
      !Event->TryGetStringField(TEXT("channel"), channel) || (channel != TEXT("live") && channel != TEXT("latest-release")) ||
      !Event->TryGetStringField(TEXT("generationId"), generation) || generation.Len() != 64 ||
      !Event->TryGetStringField(TEXT("generationPath"), path) || FPaths::IsRelative(path)) return false;
  for (const TCHAR c : generation) if (!FChar::IsHexDigit(c)) return false;
  Out = FString::Printf(TEXT("Project%d/%s_%s"), project, channel == TEXT("live") ? TEXT("Live") : TEXT("Release"), *Hash(scope));
  return true;
 }
 bool ContainedPath(const FString& Root, const FString& Relative, FString& Out)
 {
  if (Relative.IsEmpty() || FPaths::IsRelative(Root) || !FPaths::IsRelative(Relative) || Relative.Contains(TEXT("\\")) || Relative.Contains(TEXT(":")) || Relative.Contains(TEXT("\"")) || Relative.Contains(TEXT("\n")) || Relative.Contains(TEXT("\r"))) return false;
  FString root = FPaths::ConvertRelativePathToFull(Root);
  FPaths::NormalizeDirectoryName(root);
  Out = FPaths::ConvertRelativePathToFull(root / Relative);
  FPaths::NormalizeFilename(Out);
  TArray<FString> segments;
  Relative.ParseIntoArray(segments, TEXT("/"), false);
  return Out.StartsWith(root + TEXT("/")) && !segments.Contains(TEXT(".."));
 }
 bool SaveAsset(UObject* Asset)
 {
  UPackage* package = Asset->GetOutermost();
  const FString path = FPackageName::LongPackageNameToFilename(package->GetName(), FPackageName::GetAssetPackageExtension());
  FSavePackageArgs args;
  args.TopLevelFlags = RF_Public | RF_Standalone;
  args.SaveFlags = SAVE_NoError;
  return UPackage::SavePackage(package, Asset, *path, args);
 }
 bool OlderRevision(const TSharedPtr<FJsonObject>& Next, const TSharedPtr<FJsonObject>& Prior)
 {
  if (Next->GetStringField(TEXT("channel")) != TEXT("live"))
  {
   const TSharedPtr<FJsonObject>* nextRelease;
   const TSharedPtr<FJsonObject>* priorRelease;
   FString nextTime, priorTime;
   return Next->TryGetObjectField(TEXT("release"), nextRelease) && Prior->TryGetObjectField(TEXT("release"), priorRelease) &&
    (*nextRelease)->TryGetStringField(TEXT("publishedAt"), nextTime) && (*priorRelease)->TryGetStringField(TEXT("publishedAt"), priorTime) && nextTime < priorTime;
  }
  const TSharedPtr<FJsonObject>* nextRevision;
  const TSharedPtr<FJsonObject>* priorRevision;
  if (!Next->TryGetObjectField(TEXT("revision"), nextRevision) || !Prior->TryGetObjectField(TEXT("revision"), priorRevision)) return false;
  for (const auto& pair : (*priorRevision)->Values)
  {
   FString oldValue, newValue;
   if (!pair.Value->TryGetString(oldValue) || !(*nextRevision)->TryGetStringField(FString(*pair.Key), newValue)) continue;
   if (newValue.Len() < oldValue.Len() || (newValue.Len() == oldValue.Len() && newValue < oldValue)) return true;
  }
  return false;
 }
 struct FCandidate
 {
  FString Key, Path, Checksum, Text;
  bool bStory = false;
  TStrongObjectPtr<USoundWave> Audio;
  TMap<FString, FString> Translations;
 };
}

FOnProsettaGenerationAccepted UProsettaWatchSubsystem::OnGenerationAccepted;

void UProsettaWatchSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
 Super::Initialize(Collection);
 Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UProsettaWatchSubsystem::Tick), 0.25f);
}
void UProsettaWatchSubsystem::Deinitialize()
{
 StopWatch();
 DecoderResolver = nullptr;
 FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
 Super::Deinitialize();
}

bool UProsettaWatchSubsystem::StartWatch(const FString& CLIExecutable, const FString& ConfigFile, const FString& AudioDecoder, const FString& Channel, const FString& Volumes, bool bMirror)
{
 StopWatch();
 // The plugin owns the transport again, whether or not this launch succeeds.
 DecoderResolver = nullptr;
 if (FPaths::IsRelative(CLIExecutable) || FPaths::IsRelative(ConfigFile) || !FPaths::FileExists(CLIExecutable) || !FPaths::FileExists(ConfigFile))
 { Status = TEXT("Watch requires absolute CLI and configuration file paths."); return false; }
 FString arguments, error;
 if (!BuildWatchArguments(ConfigFile, Channel, Volumes, bMirror, arguments, error)) { Status = error; return false; }
 // `--version` runs alongside the editor: a cold .NET start must not freeze the game thread.
 LaunchExecutable = CLIExecutable;
 LaunchArguments = arguments;
 LaunchChannel = Channel;
 LaunchDecoder = AudioDecoder;
 FPlatformProcess::CreatePipe(VersionReadPipe, VersionWritePipe);
 // stdout only: stderr notices ( .NET first-run, runtime warnings ) must not precede the version line.
 VersionProcess = FPlatformProcess::CreateProc(*CLIExecutable, TEXT("--version"), false, true, true, nullptr, 0, nullptr, VersionWritePipe, nullptr, nullptr);
 if (!VersionProcess.IsValid()) { CancelVersionCheck(); Status = TEXT("Unable to run the CLI version check."); return false; }
 VersionStarted = FPlatformTime::Seconds();
 Status = TEXT("Checking Prosetta Sync CLI version.");
 return true;
}
void UProsettaWatchSubsystem::PollVersionCheck()
{
 // Observe the exit before draining so the final output is never lost.
 const bool bExited = !FPlatformProcess::IsProcRunning(VersionProcess);
 VersionOutput += FPlatformProcess::ReadPipe(VersionReadPipe);
 if (VersionOutput.Len() > 4096) VersionOutput.LeftInline(4096);
 if (!bExited)
 {
  if (FPlatformTime::Seconds() - VersionStarted < VersionCheckTimeoutSeconds) return;
  CancelVersionCheck();
  Status = FString::Printf(TEXT("Prosetta Sync CLI did not answer --version within %.0f s; watch not started."), VersionCheckTimeoutSeconds);
 }
 else
 {
  int32 exitCode = -1;
  FPlatformProcess::GetProcReturnCode(VersionProcess, &exitCode);
  // IsSupportedCLIVersion reads the first line, so that is the version reported.
  FString version = VersionOutput.TrimStartAndEnd();
  int32 newline;
  if (version.FindChar(TEXT('\n'), newline)) version = version.Left(newline).TrimEnd();
  CancelVersionCheck();
  // A failed `--version` ( missing .NET runtime, crash ) is not an old CLI; say so rather than "found ''".
  if (exitCode != 0)
   Status = FString::Printf(TEXT("Watch requires Prosetta Sync CLI %s or newer; `--version` exited with code %d."), MinimumCLIVersionText, exitCode);
  else if (!IsSupportedCLIVersion(version))
   Status = FString::Printf(TEXT("Watch requires Prosetta Sync CLI %s or newer; found '%s'."), MinimumCLIVersionText, *version.Left(64));
  else
   LaunchWatch(version);
 }
 // StartWatch returned before this outcome, so Prosetta.Watch.Start never logged it.
 UE_LOG(LogTemp, Display, TEXT("Prosetta: %s"), *Status);
}
void UProsettaWatchSubsystem::CancelVersionCheck()
{
 if (VersionProcess.IsValid())
 {
  if (FPlatformProcess::IsProcRunning(VersionProcess)) FPlatformProcess::TerminateProc(VersionProcess, true);
  FPlatformProcess::CloseProc(VersionProcess);
  VersionProcess.Reset();
 }
 FPlatformProcess::ClosePipe(VersionReadPipe, VersionWritePipe);
 VersionReadPipe = VersionWritePipe = nullptr;
 VersionOutput.Empty();
}
void UProsettaWatchSubsystem::LaunchWatch(const FString& Version)
{
 Decoder = LaunchDecoder;
 FPlatformProcess::CreatePipe(ReadPipe, WritePipe);
 Process = FPlatformProcess::CreateProc(*LaunchExecutable, *LaunchArguments, false, true, true, nullptr, 0, nullptr, WritePipe);
 if (!Process.IsValid()) { StopWatch(); Status = TEXT("Unable to launch CLI watch."); return; }
 Status = FString::Printf(TEXT("Watching %s (CLI %s); waiting for a complete generation."), *LaunchChannel, *Version);
}
bool UProsettaWatchSubsystem::BuildWatchArguments(const FString& ConfigFile, const FString& Channel, const FString& Volumes, bool bMirror, FString& OutArguments, FString& OutError)
{
 // Mirrors the CLI's own usage checks ( exit code 2 ) so mistakes surface before launch.
 if (ConfigFile.Contains(TEXT("\"")) || ConfigFile.Contains(TEXT("\n")) || ConfigFile.Contains(TEXT("\r")))
 { OutError = TEXT("Watch configuration path contains unsupported characters."); return false; }
 const bool bLive = Channel.Equals(TEXT("live"), ESearchCase::CaseSensitive);
 const bool bLatestRelease = Channel.Equals(TEXT("latest-release"), ESearchCase::CaseSensitive);
 if (!bLive && !bLatestRelease)
 { OutError = TEXT("Watch channel must be live or latest-release."); return false; }
 TArray<FString> volumes;
 if (!Volumes.IsEmpty()) Volumes.ParseIntoArray(volumes, TEXT(","), false);
 for (const FString& volume : volumes)
 {
  int32 at = INDEX_NONE;
  const FString slug = volume.FindChar(TEXT('@'), at) ? volume.Left(at) : volume;
  if (at != INDEX_NONE && !volume.Mid(at).Equals(TEXT("@latest"), ESearchCase::CaseSensitive)) { OutError = TEXT("Fixed tags use one-shot sync, not watch."); return false; }
  bool bValid = !slug.IsEmpty();
  for (const TCHAR c : slug) bValid &= FChar::IsAlnum(c) || c == TEXT('-') || c == TEXT('_');
  if (!bValid) { OutError = FString::Printf(TEXT("Invalid watch volume '%s'."), *volume.Left(64)); return false; }
 }
 if (bLatestRelease && volumes.Num() > 1)
 { OutError = TEXT("Watch each latest-release volume with its own cache."); return false; }
 OutArguments = FString::Printf(TEXT("watch --json --config \"%s\" --channel %s"), *ConfigFile, *Channel);
 if (!Volumes.IsEmpty()) OutArguments += TEXT(" --volumes ") + Volumes;
 if (!bMirror) OutArguments += TEXT(" --no-mirror");
 return true;
}
bool UProsettaWatchSubsystem::IsSupportedCLIVersion(const FString& VersionOutput)
{
 FString line = VersionOutput.TrimStartAndEnd();
 int32 end = INDEX_NONE;
 if (line.FindChar(TEXT('\n'), end)) line.LeftInline(end);
 line.RemoveFromStart(TEXT("v"));
 FString core = line, prerelease;
 if (line.FindChar(TEXT('+'), end)) core.LeftInline(end);
 if (core.FindChar(TEXT('-'), end)) { prerelease = core.Mid(end + 1); core.LeftInline(end); }
 TArray<FString> parts;
 core.TrimStartAndEnd().ParseIntoArray(parts, TEXT("."), false);
 if (parts.Num() != 3) return false;
 for (int32 i = 0; i < 3; ++i)
 {
  if (parts[i].IsEmpty() || !parts[i].IsNumeric() || parts[i].Contains(TEXT("."))) return false;
  const int32 value = FCString::Atoi(*parts[i]);
  if (value != MinimumCLIVersion[i]) return value > MinimumCLIVersion[i];
 }
 // A prerelease of the minimum ( 0.10.0-rc.1 ) predates the release that shipped watch.
 return prerelease.IsEmpty();
}
FString UProsettaWatchSubsystem::SelectDiagnostic(const FString& Current, const FString& RawLine)
{
 const FString line = RawLine.TrimStartAndEnd();
 if (line.IsEmpty() || line.StartsWith(TEXT("Run prosetta-sync --help"))) return Current;
 // .NET stack frames ( always indented, "   at X.Y()" ) and trace markers ( "--- End of stack trace ---" ) are never
 // the message; an unindented "At least one volume ..." is.
 const bool bIndented = FChar::IsWhitespace(RawLine[0]);
 if ((bIndented && line.StartsWith(TEXT("at "), ESearchCase::CaseSensitive)) || line.StartsWith(TEXT("--- End of"), ESearchCase::CaseSensitive)) return Current;
 // Other indented lines continue the block whose first line is already kept ( e.g. " ---> inner" ).
 if (!Current.IsEmpty() && bIndented) return Current;
 return line.Left(512);
}
void UProsettaWatchSubsystem::StopWatch()
{
 CancelVersionCheck();
 if (Process.IsValid()) { FPlatformProcess::TerminateProc(Process, true); FPlatformProcess::CloseProc(Process); Process.Reset(); }
 FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
 ReadPipe = WritePipe = nullptr;
 Buffer.Empty();
 LastDiagnostic.Empty();
 Status = TEXT("Stopped; queued generations retained.");
}
void UProsettaWatchSubsystem::ConsumeOutput(bool bFlush)
{
 Buffer += FPlatformProcess::ReadPipe(ReadPipe);
 if (bFlush && !Buffer.IsEmpty() && !Buffer.EndsWith(TEXT("\n"))) Buffer += TEXT('\n');
 int32 newline;
 while (Buffer.FindChar(TEXT('\n'), newline))
 {
  const FString raw = Buffer.Left(newline);
  const FString line = raw.TrimStartAndEnd();
  Buffer.RightChopInline(newline + 1);
  if (line.IsEmpty()) continue;
  // stderr shares the pipe: usage errors and fatal messages are plain text, not NDJSON.
  if (line.StartsWith(TEXT("{"))) ConsumeEvent(line);
  else LastDiagnostic = SelectDiagnostic(LastDiagnostic, raw);
 }
 if (Buffer.Len() > 4 * 1024 * 1024) { Buffer.Empty(); Status = TEXT("Rejected oversized sidecar event."); }
}
bool UProsettaWatchSubsystem::Tick(float DeltaTime)
{
 if (VersionProcess.IsValid()) PollVersionCheck();
 if (ReadPipe)
 {
  const bool bExited = Process.IsValid() && !FPlatformProcess::IsProcRunning(Process);
  // Drain after observing the exit so the final lines are never lost.
  ConsumeOutput(bExited);
  if (bExited)
  {
   int32 exitCode = -1;
   FPlatformProcess::GetProcReturnCode(Process, &exitCode);
   const FString diagnostic = LastDiagnostic.IsEmpty() ? FString() : TEXT(": ") + LastDiagnostic;
   const FString prior = Status;
   StopWatch();
   if (exitCode == 2) Status = TEXT("CLI rejected the watch arguments") + diagnostic;
   else if (exitCode != 0 && prior.StartsWith(TEXT("CLI error: "))) Status = prior;
   else Status = FString::Printf(TEXT("CLI exited with code %d%s; last imported assets retained."), exitCode, *diagnostic);
  }
 }
 TryApplyPending();
 return true;
}
bool UProsettaWatchSubsystem::IsSafeToApply()
{
 return IsInGameThread() && GEditor && !GEditor->GetPlaySessionRequest().IsSet() && !GEditor->IsSettingUpPlayWorld() && !GEditor->PlayWorld && !GEditor->bIsSimulatingInEditor && !UE::IsSavingPackage() && !IsAsyncLoading() && !IsGarbageCollecting() && !GEditor->IsTransactionActive();
}
void UProsettaWatchSubsystem::ConfigureExternalWatch(FProsettaDecoderResolver ResolveDecoder)
{
 StopWatch();
 Decoder.Empty();
 DecoderResolver = MoveTemp(ResolveDecoder);
 Status = TEXT("External CLI watch connected; waiting for a completed generation.");
}

bool UProsettaWatchSubsystem::ConsumeEvent(const FString& EventJSON)
{
 // Pending and Status are shared with the ticker: external transports must marshal here.
 if (!ensureMsgf(IsInGameThread(), TEXT("UProsettaWatchSubsystem::ConsumeEvent must be called on the game thread."))) return false;
 TSharedPtr<FJsonObject> event;
 FString type, name;
 int32 version;
 if (!Parse(EventJSON, event)) { Status = TEXT("Rejected malformed CLI event."); return false; }
 // A fatal CLI error under --json is a one-off sync result, not an NDJSON event.
 FString result, message;
 if (!event->HasField(TEXT("schemaVersion")) && event->TryGetStringField(TEXT("status"), result) && result == TEXT("error"))
 {
  event->TryGetStringField(TEXT("error"), message);
  Status = TEXT("CLI error: ") + message.Left(512);
  return true;
 }
 if (!event->TryGetNumberField(TEXT("schemaVersion"), version) || version != 1 || !event->TryGetStringField(TEXT("type"), type))
 { Status = TEXT("Rejected malformed CLI event."); return false; }
 if (type != TEXT("generation-completed"))
 {
  FString code, scope;
  event->TryGetStringField(TEXT("code"), code);
  event->TryGetStringField(TEXT("scope"), scope);
  const FString detail = (code.IsEmpty() ? FString() : TEXT(" ") + code.Left(64)) + (scope.IsEmpty() ? FString() : TEXT(" for ") + scope.Left(128));
  if (type == TEXT("blocked")) Status = TEXT("Watch blocked") + detail + TEXT("; last assets retained.");
  else if (code == TEXT("STREAM_RECONNECTING")) Status = TEXT("Watch reconnecting to Prosetta; last assets retained.");
  else if (code == TEXT("GENERATION_RETRY")) Status = TEXT("Generation failed; CLI will retry") + detail + TEXT(".");
  else if (type == TEXT("error")) Status = TEXT("Watch error") + detail + TEXT("; last assets retained.");
  else Status = TEXT("Watch transport status: ") + type.Left(64) + detail;
  return true;
 }
 if (!EventNamespace(event, name)) { Status = TEXT("Rejected invalid completed generation identity."); return false; }
 if (const FString* queued = Pending.Find(name))
 {
  TSharedPtr<FJsonObject> prior;
  if (Parse(*queued, prior) && OlderRevision(event, prior)) return true;
 }
 Pending.Add(name, EventJSON);
 Status = TEXT("Generation queued until Play stops and editor is idle.");
 return true;
}
bool UProsettaWatchSubsystem::TryApplyPending()
{
 if (Pending.IsEmpty() || !IsSafeToApply()) return false;
 TStrongObjectPtr<UProsettaWatchSubsystem> keepAlive(this);
 TArray<FString> names;
 Pending.GetKeys(names);
 for (const FString& name : names)
 {
  // The decoder resolver and accepted listeners may re-enter: skip what they applied, keep what they queued.
  const FString* queued = Pending.Find(name);
  if (!queued) continue;
  const FString event = *queued;
  FString error;
  UProsettaGenerationAsset* accepted = nullptr;
  const bool bApplied = ApplyGeneration(event, error, accepted);
  const TStrongObjectPtr<UProsettaGenerationAsset> announce(accepted);
  const FString* current = Pending.Find(name);
  if (current && *current == event) Pending.Remove(name);
  if (!bApplied) { Status = TEXT("Import failed; previous generation retained: ") + error; return false; }
  CleanupRetiredAssets(name);
  Status = TEXT("Applied generation: ") + name + TEXT(". ") + Status;
  if (announce.IsValid()) OnGenerationAccepted.Broadcast(*announce.Get());
 }
 return true;
}
bool UProsettaWatchSubsystem::ApplyGeneration(const FString& EventJSON, FString& OutError, UProsettaGenerationAsset*& OutAccepted)
{
 OutAccepted = nullptr;
 TSharedPtr<FJsonObject> event, manifest;
 FString name;
 if (!Parse(EventJSON, event) || !EventNamespace(event, name)) { OutError = TEXT("Invalid event."); return false; }
 const FString root = event->GetStringField(TEXT("generationPath"));
 const FString id = event->GetStringField(TEXT("generationId"));
 FString manifestPath, declaredManifest, manifestText;
 if (!ContainedPath(root, TEXT("manifest.json"), manifestPath) || !event->TryGetStringField(TEXT("manifestPath"), declaredManifest) ||
     FPaths::ConvertRelativePathToFull(declaredManifest) != manifestPath || !FFileHelper::LoadFileToString(manifestText, *manifestPath) || !Parse(manifestText, manifest))
 { OutError = TEXT("Missing generation manifest."); return false; }
 int32 schema = 0, project = 0;
 const TSharedPtr<FJsonObject>* provenance = nullptr;
 const TArray<TSharedPtr<FJsonValue>>* artifacts = nullptr;
 FString manifestId;
 if (!manifest->TryGetNumberField(TEXT("schemaVersion"), schema) || schema != 2 ||
     !manifest->TryGetNumberField(TEXT("projectId"), project) || project != event->GetIntegerField(TEXT("projectId")) ||
     !manifest->TryGetObjectField(TEXT("generation"), provenance) || !(*provenance)->TryGetStringField(TEXT("id"), manifestId) || manifestId != id ||
     !manifest->TryGetArrayField(TEXT("artifacts"), artifacts))
 { OutError = TEXT("Manifest identity/schema mismatch."); return false; }
 const FString base = TEXT("/Game/Prosetta/") + name;
 const FString statePath = base / TEXT("Generation");
 UProsettaGenerationAsset* state = LoadObject<UProsettaGenerationAsset>(nullptr, *(statePath + TEXT(".Generation")), nullptr, LOAD_NoWarn);
 if (state && state->Namespace != name) { OutError = TEXT("Generation registry ownership mismatch."); return false; }
 if (state && state->GenerationId == id) return true;
 if (state)
 {
  TSharedPtr<FJsonObject> prior;
  if (Parse(state->Provenance, prior) && OlderRevision(event, prior)) return true;
 }
 if (!state && (FPackageName::DoesPackageExist(statePath) || FindObject<UObject>(nullptr, *(statePath + TEXT(".Generation"))))) { OutError = TEXT("User-owned package occupies generation registry path."); return false; }
 TArray<FCandidate> candidates;
 TSet<FString> keys;
 FString metadata;

 for (const auto& value : *artifacts)
 {
  const TSharedPtr<FJsonObject>* artifact = nullptr;
  FCandidate candidate;
  FString relative, algorithm, kind;
  double size;
  if (!value->TryGetObject(artifact) || !(*artifact)->TryGetStringField(TEXT("key"), candidate.Key) || candidate.Key.IsEmpty() || keys.Contains(candidate.Key) ||
      !(*artifact)->TryGetStringField(TEXT("kind"), kind) || !(*artifact)->TryGetStringField(TEXT("relativePath"), relative) ||
      !(*artifact)->TryGetStringField(TEXT("checksum"), candidate.Checksum) || !(*artifact)->TryGetStringField(TEXT("checksumAlgorithm"), algorithm) || algorithm != TEXT("sha256") ||
      !(*artifact)->TryGetNumberField(TEXT("byteSize"), size) || !ContainedPath(root, relative, candidate.Path))
  { OutError = TEXT("Invalid artifact descriptor or unsafe path."); return false; }
  keys.Add(candidate.Key);
  TArray<uint8> bytes;
  if (!FFileHelper::LoadFileToArray(bytes, *candidate.Path) || bytes.Num() != size ||
      !ProsettaChecksum::SHA256(bytes.GetData(), bytes.Num()).Equals(candidate.Checksum, ESearchCase::IgnoreCase))
  { OutError = TEXT("Artifact checksum/size mismatch: ") + candidate.Key; return false; }
  if (relative.StartsWith(TEXT("native-bindings/")) && relative != TEXT("native-bindings/manifest.json"))
  { continue; }
  candidate.bStory = relative.StartsWith(TEXT("compiled/")) && relative.EndsWith(TEXT("/story.json"));
  if (!candidate.bStory && state && state->Checksums.FindRef(candidate.Key) == candidate.Checksum && state->Assets.FindRef(candidate.Key))
  {
   if (candidate.Key == TEXT("lines") && !FFileHelper::LoadFileToString(metadata, *candidate.Path)) { OutError = TEXT("Cannot read unchanged line metadata."); return false; }
   continue; // Verified unchanged bytes do not decode/reimport unchanged media or localization.
  }
  if (kind == TEXT("json") || relative.EndsWith(TEXT(".csv")) || relative.EndsWith(TEXT(".ink")))
  {
   if (!FFileHelper::LoadFileToString(candidate.Text, *candidate.Path)) { OutError = TEXT("Cannot read artifact text."); return false; }
   if (candidate.bStory)
   {
    FString decoded;
    if (!UInkpotStoryAssetFactory::DecodeCompiledJSON(candidate.Text, decoded, OutError)) return false;
    candidate.Text = decoded;
   }
   else if (kind == TEXT("json"))
   {
    TSharedPtr<FJsonObject> json;
    if (!Parse(candidate.Text, json)) { OutError = TEXT("Malformed supporting JSON: ") + candidate.Key; return false; }
    if (candidate.Key == TEXT("lines"))
    {
     const TSharedPtr<FJsonValue> lines = json->TryGetField(TEXT("lines"));
     if (!lines.IsValid() || (lines->Type != EJson::Array && lines->Type != EJson::Object)) { OutError = TEXT("Invalid Prosetta line metadata."); return false; }
     metadata = candidate.Text;
    }
    if (candidate.Key == TEXT("localization"))
    {
     const TArray<TSharedPtr<FJsonValue>>* containers;
     if (!json->TryGetArrayField(TEXT("containers"), containers)) { OutError = TEXT("Invalid localization payload."); return false; }
     for (const auto& containerValue : *containers)
     {
      const TSharedPtr<FJsonObject>* container;
      const TArray<TSharedPtr<FJsonValue>>* translations;
      FString lineId;
      if (!containerValue->TryGetObject(container) || !(*container)->TryGetStringField(TEXT("lineId"), lineId) || !(*container)->TryGetArrayField(TEXT("localizedContainers"), translations))
      { OutError = TEXT("Invalid localization container."); return false; }
      for (const auto& translationValue : *translations)
      {
       const TSharedPtr<FJsonObject>* translation;
       FString language, localized;
       if (!translationValue->TryGetObject(translation) || !(*translation)->TryGetStringField(TEXT("languageCode"), language) || !(*translation)->TryGetStringField(TEXT("localizedString"), localized))
       { OutError = TEXT("Invalid translation."); return false; }
       candidate.Translations.Add(language + TEXT("/") + lineId, localized);
      }
     }
    }
   }
  }
  else if (candidate.Key.StartsWith(TEXT("audio-file:")))
  {
   // Decode/import a transient candidate first. No published asset changes on failure.
   FString wav = candidate.Path;
   if (!relative.EndsWith(TEXT(".wav"), ESearchCase::IgnoreCase))
   {
    if (DecoderResolver && (Decoder.IsEmpty() || !FPaths::FileExists(Decoder)))
    {
     // Re-resolved whenever the cached decoder is missing; only an existing absolute path is kept.
     Decoder.Empty();
     FString resolved, resolveError;
     if (!DecoderResolver(resolved, resolveError) || FPaths::IsRelative(resolved) || !FPaths::FileExists(resolved))
     {
      OutError = TEXT("Audio decoder unavailable: ") + (resolveError.IsEmpty() ? FString(TEXT("the resolver found no absolute FFmpeg executable.")) : resolveError.Left(512));
      return false;
     }
     Decoder = resolved;
    }
    if (Decoder.IsEmpty() || !FPaths::FileExists(Decoder)) { OutError = TEXT("Compressed audio needs an absolute FFmpeg executable ( StartWatch's AudioDecoder or ConfigureExternalWatch's resolver )."); return false; }
    const FString stage = FPaths::ProjectSavedDir() / TEXT("ProsettaAudioStage");
    IFileManager::Get().MakeDirectory(*stage, true);
    wav = stage / (candidate.Checksum + TEXT(".wav"));
    const FString arguments = FString::Printf(TEXT("-nostdin -v error -y -i \"%s\" -vn -acodec pcm_s16le \"%s\""), *candidate.Path, *wav);
    int32 exitCode = -1;
    if (!FPlatformProcess::ExecProcess(*Decoder, *arguments, &exitCode, nullptr, nullptr) || exitCode != 0)
    { OutError = TEXT("Selected audio decode failed."); return false; }
   }
   TArray<uint8> pcm;
   FWaveModInfo info;
   FString waveError;
   if (!FFileHelper::LoadFileToArray(pcm, *wav) || !info.ReadWaveInfo(pcm.GetData(), pcm.Num(), &waveError))
   { OutError = TEXT("Invalid decoded WAV: ") + waveError; return false; }
   USoundFactory* factory = NewObject<USoundFactory>();
   factory->SuppressImportDialogs();
   const uint8* begin = pcm.GetData();
   candidate.Audio.Reset(Cast<USoundWave>(factory->FactoryCreateBinary(USoundWave::StaticClass(), GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), USoundWave::StaticClass()), RF_Transient, nullptr, TEXT("wav"), begin, begin + pcm.Num(), GWarn)));
   if (!candidate.Audio.IsValid()) { OutError = TEXT("Selected audio import failed."); return false; }
  }
  else
  { continue; } // Unrecognized binary stays immutable in the completed generation.
  const FString package = base / (TEXT("Artifact_") + Hash(candidate.Key));
  UObject* existing = state ? (state->Assets.Contains(candidate.Key) ? state->Assets.FindRef(candidate.Key).Get() : state->RetiredAssets.FindRef(candidate.Key).Get()) : nullptr;
  if (existing && (existing->GetOutermost()->GetName() != package ||
      (candidate.bStory ? !existing->IsA<UInkpotStoryAsset>() : !existing->IsA<UProsettaSyncedArtifact>())))
  { OutError = TEXT("Owned asset identity/type mismatch."); return false; }
  if (!existing && (FPackageName::DoesPackageExist(package) || FindObject<UObject>(nullptr, *(package + TEXT(".") + FPaths::GetBaseFilename(package))))) { OutError = TEXT("User-owned package occupies artifact path."); return false; }
  candidates.Add(MoveTemp(candidate));
 }
 // Keep memory and package-file snapshots until the ownership registry is saved.
 struct FBackup { TObjectPtr<UObject> Asset; TArray<uint8> Memory, File; FString Filename; bool bHadFile = false; TStrongObjectPtr<USoundWave> Audio; };
 TArray<FBackup> backups;
 TArray<UObject*> created;
 auto backup = [&backups](UObject* Asset)
 {
  FBackup item;
  item.Asset = Asset;
  FObjectWriter writer(Asset, item.Memory);
  item.Filename = FPackageName::LongPackageNameToFilename(Asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
  item.bHadFile = FFileHelper::LoadFileToArray(item.File, *item.Filename);
  if (const UProsettaSyncedArtifact* supporting = Cast<UProsettaSyncedArtifact>(Asset)) item.Audio.Reset(supporting->Audio);
  backups.Add(MoveTemp(item));
 };
 if (state)
 {
  backup(state);
  for (const auto& pair : state->Assets)
  {
   if (!pair.Value) continue;
   backup(pair.Value);
   if (UInkpotStoryAsset* story = Cast<UInkpotStoryAsset>(pair.Value)) if (story->GetAssetImportData()) backup(story->GetAssetImportData());
  }
  for (const auto& pair : state->RetiredAssets)
  {
   if (!pair.Value) continue;
   backup(pair.Value);
   if (UInkpotStoryAsset* story = Cast<UInkpotStoryAsset>(pair.Value)) if (story->GetAssetImportData()) backup(story->GetAssetImportData());
  }
 }
 auto rollback = [&backups, &created, &OutError]()
 {
  for (FBackup& item : backups)
  {
   FObjectReader reader(item.Asset, item.Memory);
   if (item.bHadFile && !FFileHelper::SaveArrayToFile(item.File, *item.Filename)) OutError += TEXT(" Restore of a package file failed; keep the last completed CLI generation for repair.");
   else if (!item.bHadFile) IFileManager::Get().Delete(*item.Filename);
  }
  for (UObject* asset : created)
  {
   const FString filename = FPackageName::LongPackageNameToFilename(asset->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
   IFileManager::Get().Delete(*filename);
   FAssetRegistryModule::AssetDeleted(asset);
   asset->ClearFlags(RF_Public | RF_Standalone);
   asset->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
  }
 };
 // Every file and prospective asset is validated before the first published mutation.
 if (!state)
 {
  UPackage* package = CreatePackage(*statePath);
  state = NewObject<UProsettaGenerationAsset>(package, TEXT("Generation"), RF_Public | RF_Standalone);
  state->Namespace = name;
  FAssetRegistryModule::AssetCreated(state);
  created.Add(state);
 }
 TArray<UObject*> toSave;
 for (const FCandidate& candidate : candidates)
 {
  UObject* asset = state->Assets.Contains(candidate.Key) ? state->Assets.FindRef(candidate.Key).Get() : state->RetiredAssets.FindRef(candidate.Key).Get();
  if (!asset)
  {
   const FString packageName = base / (TEXT("Artifact_") + Hash(candidate.Key));
   UPackage* package = CreatePackage(*packageName);
   asset = NewObject<UObject>(package, candidate.bStory ? UInkpotStoryAsset::StaticClass() : UProsettaSyncedArtifact::StaticClass(), *FPaths::GetBaseFilename(packageName), RF_Public | RF_Standalone);
   FAssetRegistryModule::AssetCreated(asset);
   created.Add(asset);
  }
  asset->Modify();
  if (UInkpotStoryAsset* story = Cast<UInkpotStoryAsset>(asset))
  {
   story->SetCompiledJSON(candidate.Text);
   story->SetProsettaJSON(metadata);
   // Provenance: project / channel / volume / generation from the event and manifest, build id from the artifact checksum.
   FProsettaStoryProvenance provenanceInfo;
   FProsettaStoryProvenance::FromSyncManifest(manifest, event, candidate.Key.StartsWith(TEXT("compiled-story:")) ? candidate.Key.RightChop(15) : FString(), provenanceInfo);
   provenanceInfo.Namespace = name;
   provenanceInfo.BuildId = candidate.Checksum.ToLower();
   story->SetProsettaProvenance(provenanceInfo);
   story->UpdateAssetInfo(candidate.Path);
  }
  else if (UProsettaSyncedArtifact* supporting = Cast<UProsettaSyncedArtifact>(asset))
  {
   supporting->Key = candidate.Key;
   supporting->Namespace = name;
   supporting->Checksum = candidate.Checksum;
   supporting->Text = candidate.Text;
   supporting->Translations = candidate.Translations;
   if (candidate.Audio.IsValid())
   {
    const FString soundName = TEXT("Take_") + candidate.Checksum;
    USoundWave* wave = FindObject<USoundWave>(supporting, *soundName);
    supporting->Audio = wave ? wave : DuplicateObject<USoundWave>(candidate.Audio.Get(), supporting, *soundName);
    supporting->Audio->ClearFlags(RF_Transient);
    supporting->Audio->SetFlags(RF_Public);
   }
   else supporting->Audio = nullptr;
  }
  asset->MarkPackageDirty();
  state->Assets.Add(candidate.Key, asset);
  state->RetiredAssets.Remove(candidate.Key);
  state->Checksums.Add(candidate.Key, candidate.Checksum);
  toSave.Add(asset);
 }
 // Reconcile against the full authoritative manifest; never trust removedKeys to own an asset.
 TArray<FString> oldKeys;
 state->Assets.GetKeys(oldKeys);
 TArray<UObject*> removed;
 for (const FString& key : oldKeys)
 {
  if (!keys.Contains(key))
  {
   UObject* asset = state->Assets.FindChecked(key);
   if (asset && asset->GetOutermost()->GetName() == base / (TEXT("Artifact_") + Hash(key)))
   {
    removed.Add(asset);
    state->RetiredAssets.Add(key, asset);
   }
   state->Assets.Remove(key);
   state->Checksums.Remove(key);
  }
 }
 for (UObject* asset : toSave)
  if (!SaveAsset(asset)) { OutError = TEXT("Unable to save asset package; generation not accepted."); rollback(); return false; }
 state->GenerationId = id;
 state->Provenance = EventJSON;
 state->MarkPackageDirty();
 if (!SaveAsset(state)) { OutError = TEXT("Unable to save generation ownership registry."); rollback(); return false; }
 backups.Empty();
 // Obsolete assets leave the active registry immediately. Referenced objects remain
 // in an editor-only retirement map, preserving user references until explicit cleanup.
 NativeGenerationPaths.Add(name, root);
 OutAccepted = state;
 return true;
}

int32 UProsettaWatchSubsystem::CleanupRetiredAssets(const FString& Namespace)
{
 if (!IsSafeToApply()) { Status = TEXT("Owned cleanup waits for Play stop/editor idle."); return 0; }
 TStrongObjectPtr<UProsettaWatchSubsystem> keepAlive(this);
 const FString base = TEXT("/Game/Prosetta/") + Namespace;
 const FString path = base / TEXT("Generation.Generation");
 TStrongObjectPtr<UProsettaGenerationAsset> state(LoadObject<UProsettaGenerationAsset>(nullptr, *path, nullptr, LOAD_NoWarn));
 if (!state.IsValid() || state->Namespace != Namespace) { Status = TEXT("Owned cleanup requires a matching generation registry."); return 0; }
 TArray<FString> keys;
 state->RetiredAssets.GetKeys(keys);
 int32 deleted = 0, retained = 0;
 for (const FString& key : keys)
 {
  UObject* asset = state->RetiredAssets.FindRef(key);
  const FString package = base / (TEXT("Artifact_") + Hash(key));
  if (!asset || state->Assets.Contains(key) || asset->GetOutermost()->GetName() != package)
  { ++retained; continue; }
  if (const UProsettaSyncedArtifact* supporting = Cast<UProsettaSyncedArtifact>(asset))
  {
   if (supporting->Namespace != Namespace || supporting->Key != key) { ++retained; continue; }
  }
  else if (!asset->IsA<UInkpotStoryAsset>()) { ++retained; continue; }

  // Detach only our own reference and persist it before the engine's disk+memory scan.
  // Never root the target: a strong reference here would make every scan retain it.
  const TWeakObjectPtr<UObject> target(asset);
  state->RetiredAssets.Remove(key);
  state->MarkPackageDirty();
  if (!SaveAsset(state.Get()))
  {
   state->RetiredAssets.Add(key, asset);
   Status = TEXT("Owned cleanup retained assets because the registry could not be saved.");
   return deleted;
  }
  const FString registryFile = FPackageName::LongPackageNameToFilename(state->GetOutermost()->GetName(), FPackageName::GetAssetPackageExtension());
  FAssetRegistryModule::GetRegistry().ScanModifiedAssetFiles({registryFile});
  FAssetDeleteModel model({asset});
  for (int32 step = 0; model.GetState() != FAssetDeleteModel::Finished && step < 1024; ++step) model.Tick(0.01f);
  const bool safe = model.GetState() == FAssetDeleteModel::Finished && model.CanDelete() && !model.IsAnythingReferencedInMemoryByUndo();
  // DoDelete performs Unreal's checked deletion. Never force-delete, replace references,
  // or delete source content. Referenced assets remain available for user content/undo.
  const bool removed = safe && model.DoDelete() && model.GetDeletedObjectCount() == 1;
  const FString assetFile = FPackageName::LongPackageNameToFilename(package, FPackageName::GetAssetPackageExtension());
  if (removed && !FPaths::FileExists(assetFile)) ++deleted;
  else
  {
   if (target.IsValid()) state->RetiredAssets.Add(key, target.Get());
   ++retained;
  }
  state->MarkPackageDirty();
  if (!SaveAsset(state.Get()))
  {
   Status = TEXT("Owned cleanup could not save its final registry; completed generation remains available for repair.");
   return deleted;
  }
 }
 Status = FString::Printf(TEXT("Owned cleanup deleted %d unused assets; retained %d referenced/unconfirmed assets."), deleted, retained);
 return deleted;
}

bool UProsettaWatchSubsystem::ImportNativeSources(const FString& Namespace, const FString& Destination, bool bGenerateLoaders)
{
 if (!IsSafeToApply()) { Status = TEXT("Native source import waits for Play stop/editor idle."); return false; }
 FString rootValue;
 if (const FString* cached = NativeGenerationPaths.Find(Namespace)) rootValue = *cached;
 else
 {
  const FString path = TEXT("/Game/Prosetta/") + Namespace + TEXT("/Generation.Generation");
  if (const UProsettaGenerationAsset* state = LoadObject<UProsettaGenerationAsset>(nullptr, *path, nullptr, LOAD_NoWarn))
  {
   TSharedPtr<FJsonObject> prior;
   if (state->Namespace == Namespace && Parse(state->Provenance, prior)) prior->TryGetStringField(TEXT("generationPath"), rootValue);
  }
 }
 const FString* root = rootValue.IsEmpty() ? nullptr : &rootValue;
 if (!root || FPaths::IsRelative(Destination)) { Status = TEXT("Choose an absolute native source destination and apply a generation first."); return false; }
 FString text;
 TSharedPtr<FJsonObject> manifest;
 const TArray<TSharedPtr<FJsonValue>>* artifacts;
 if (!FFileHelper::LoadFileToString(text, *(*root / TEXT("manifest.json"))) || !Parse(text, manifest) || !manifest->TryGetArrayField(TEXT("artifacts"), artifacts)) return false;
 struct FCopy { FString Source, Target; };
 TArray<FCopy> copies;
 for (const auto& value : *artifacts)
 {
  const TSharedPtr<FJsonObject>* artifact;
  FString relative;
  if (!value->TryGetObject(artifact) || !(*artifact)->TryGetStringField(TEXT("relativePath"), relative)) return false;
  if (!relative.StartsWith(TEXT("native-bindings/cpp/"))) continue;
  FCopy copy;
  if (!ContainedPath(*root, relative, copy.Source) || !ContainedPath(Destination, relative.RightChop(20), copy.Target)) return false;
  TArray<uint8> bytes;
  FString expected;
  if (!(*artifact)->TryGetStringField(TEXT("checksum"), expected) || !FFileHelper::LoadFileToArray(bytes, *copy.Source) || !ProsettaChecksum::SHA256(bytes.GetData(), bytes.Num()).Equals(expected, ESearchCase::IgnoreCase))
  { Status = TEXT("Native staged source checksum mismatch."); return false; }
  copies.Add(MoveTemp(copy));
 }
 for (const auto& copy : copies)
 {
  IFileManager::Get().MakeDirectory(*FPaths::GetPath(copy.Target), true);
  if (IFileManager::Get().Copy(*copy.Target, *copy.Source, true, true) != COPY_OK) { Status = TEXT("Native source copy failed."); return false; }
 }
 if (!bGenerateLoaders) { Status = TEXT("Native C++ sources explicitly imported; rebuild required."); return true; }
 // Validate the imported modules against the generation's manifest and write their Inkpot loaders.
 FProsettaNativeBindingsManifest nativeManifest;
 FString manifestError;
 const FString manifestPath = *root / FProsettaNativeBindingsManifest::RelativePath();
 const bool bManifest = FPaths::FileExists(manifestPath) && FProsettaNativeBindingsManifest::LoadFile(manifestPath, nativeManifest, manifestError);
 if (!bManifest && !manifestError.IsEmpty()) { Status = TEXT("Native bindings manifest rejected: ") + manifestError; return false; }
 const FInkpotNativeBindingGenerationReport report = FInkpotNativeBindingGenerator::Run(Destination, bManifest ? &nativeManifest : nullptr, true);
 UE_LOG(LogTemp, Display, TEXT("%s"), *report.ToString());
 if (!report.bOk) { Status = FString::Printf(TEXT("Native C++ sources imported but %d loader validation error(s) were found; see the log."), report.Errors.Num()); return false; }
 Status = FString::Printf(TEXT("Native C++ sources imported and %d loader(s) generated ( %d written ); rebuild required."), report.Loaders.Num(), report.NumWritten());
 return true;
}
