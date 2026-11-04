#include "Inkpot/Prosetta/ProsettaSpeakerRegistry.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

const FProsettaSpeakerIdentity* UProsettaSpeakerRegistry::FindById(int32 Id) const
{
 return Id > 0 ? Characters.FindByPredicate([Id](const auto& C) { return C.CharacterId == Id; }) : nullptr;
}
const FProsettaSpeakerIdentity* UProsettaSpeakerRegistry::FindByShortname(const FString& Name) const
{
 return Characters.FindByPredicate([&Name](const auto& C) { return C.Shortname.Equals(Name, ESearchCase::IgnoreCase); });
}
bool UProsettaSpeakerRegistry::LoadCharactersJSON(const FString& JSON, FString& OutError)
{
 TSharedPtr<FJsonObject> Root;
 const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
 int32 Id = 0;
 if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JSON), Root) || !Root ||
     !Root->TryGetNumberField(TEXT("projectId"), Id) || Id <= 0 || !Root->TryGetArrayField(TEXT("characters"), Rows))
 { OutError = TEXT("Character registry requires a projectId and characters array."); return false; }
 int32 Schema = 1;
 if (Root->HasField(TEXT("schemaVersion")) && (!Root->TryGetNumberField(TEXT("schemaVersion"), Schema) || Schema != 1))
 { OutError = TEXT("Unsupported character registry schemaVersion."); return false; }
 TArray<FProsettaSpeakerIdentity> Next;
 TSet<int32> Ids;
 TSet<FString> Names;
 for (const auto& Row : *Rows)
 {
  const auto C = Row->AsObject();
  FProsettaSpeakerIdentity Speaker;
  if (!C || !C->TryGetNumberField(TEXT("id"), Speaker.CharacterId) || Speaker.CharacterId <= 0 ||
      !C->TryGetStringField(TEXT("shortname"), Speaker.Shortname) || Speaker.Shortname.TrimStartAndEnd().IsEmpty() ||
      Ids.Contains(Speaker.CharacterId) || Names.Contains(Speaker.Shortname.ToUpper()))
  { OutError = TEXT("Character registry contains an invalid or duplicate identity."); return false; }
  C->TryGetStringField(TEXT("longName"), Speaker.DisplayName);
  if (Speaker.DisplayName.IsEmpty()) { Speaker.DisplayName = Speaker.Shortname; }
  Ids.Add(Speaker.CharacterId); Names.Add(Speaker.Shortname.ToUpper()); Next.Add(MoveTemp(Speaker));
 }
 ProjectId = Id; Characters = MoveTemp(Next); OutError.Reset(); return true;
}
