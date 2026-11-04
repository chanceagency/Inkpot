#include "Inkpot/Prosetta/ProsettaSpeakerRegistry.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

namespace
{
 /** Only a JSON number holding a positive whole int32 is an identity: strings, booleans and fractions are rejected, not coerced. */
 bool TryGetPositiveId(const FJsonObject& Object, const TCHAR* Field, int32& OutId)
 {
  const TSharedPtr<FJsonValue> Value = Object.TryGetField(Field);
  double Number = 0.0;
  if (!Value || Value->Type != EJson::Number || !Value->TryGetNumber(Number) || Number < 1.0 ||
      Number > static_cast<double>(TNumericLimits<int32>::Max()) || Number != FMath::FloorToDouble(Number)) { return false; }
  OutId = static_cast<int32>(Number);
  return true;
 }
}

const FProsettaSpeakerIdentity* UProsettaSpeakerRegistry::FindById(int32 Id) const
{
 return Id > 0 ? Characters.FindByPredicate([Id](const auto& C) { return C.CharacterId == Id; }) : nullptr;
}
const FProsettaSpeakerIdentity* UProsettaSpeakerRegistry::FindByShortname(const FString& Name) const
{
 const FString Wanted = Name.TrimStartAndEnd();
 return Characters.FindByPredicate([&Wanted](const auto& C) { return C.Shortname.Equals(Wanted, ESearchCase::IgnoreCase); });
}
bool UProsettaSpeakerRegistry::LoadCharactersJSON(const FString& JSON, FString& OutError)
{
 TSharedPtr<FJsonObject> Root;
 const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
 int32 Id = 0;
 if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JSON), Root) || !Root ||
     !TryGetPositiveId(*Root, TEXT("projectId"), Id) || !Root->TryGetArrayField(TEXT("characters"), Rows))
 { OutError = TEXT("Character registry requires a projectId and characters array."); return false; }
 int32 Schema = 1;
 if (Root->HasField(TEXT("schemaVersion")) && (!TryGetPositiveId(*Root, TEXT("schemaVersion"), Schema) || Schema != 1))
 { OutError = TEXT("Unsupported character registry schemaVersion."); return false; }
 TArray<FProsettaSpeakerIdentity> Next;
 TSet<int32> Ids;
 TSet<FString> Names;
 for (const auto& Row : *Rows)
 {
  const auto C = Row->AsObject();
  FProsettaSpeakerIdentity Speaker;
  if (C && C->TryGetStringField(TEXT("shortname"), Speaker.Shortname)) { Speaker.Shortname.TrimStartAndEndInline(); }
  if (!C || !TryGetPositiveId(*C, TEXT("id"), Speaker.CharacterId) || Speaker.Shortname.IsEmpty() ||
      Ids.Contains(Speaker.CharacterId) || Names.Contains(Speaker.Shortname.ToUpper()))
  { OutError = TEXT("Character registry contains an invalid or duplicate identity."); return false; }
  C->TryGetStringField(TEXT("longName"), Speaker.DisplayName);
  if (Speaker.DisplayName.IsEmpty()) { Speaker.DisplayName = Speaker.Shortname; }
  Ids.Add(Speaker.CharacterId); Names.Add(Speaker.Shortname.ToUpper()); Next.Add(MoveTemp(Speaker));
 }
 ProjectId = Id; Characters = MoveTemp(Next); OutError.Reset(); return true;
}
