#include "Inkpot/Prosetta/ProsettaStorySource.h"
#include "Inkpot/Prosetta/ProsettaSyncInspector.h"
#include "Asset/InkpotStoryAsset.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/Paths.h"
#include <initializer_list>

DEFINE_LOG_CATEGORY_STATIC(LogProsettaStorySource, Log, All);

FString FProsettaStorySource::Describe() const
{
	if (!StoryAssetOverride.IsNull()) { return FString::Printf(TEXT("asset %s"), *StoryAssetOverride.ToString()); }
	if (VolumeSlug.IsEmpty()) { return TEXT("canonical"); }
	return VolumeTag.IsEmpty() ? FString::Printf(TEXT("volume %s"), *VolumeSlug) : FString::Printf(TEXT("volume %s@%s"), *VolumeSlug, *VolumeTag);
}

bool FProsettaStorySource::operator==(const FProsettaStorySource& Other) const
{
	return VolumeSlug == Other.VolumeSlug && VolumeTag == Other.VolumeTag && StoryAssetOverride == Other.StoryAssetOverride && bPreferSyncedMirror == Other.bPreferSyncedMirror;
}

bool FProsettaResolvedStoryPaths::IsComplete() const
{
	return !CompiledStoryPath.IsEmpty() && FPaths::FileExists(CompiledStoryPath) && !LinesPath.IsEmpty() && FPaths::FileExists(LinesPath);
}

namespace ProsettaStorySource
{
	FString MirrorRoot()
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Prosetta"));
	}

	FString ManifestPath(const FString& InMirrorRoot)
	{
		return FPaths::Combine(InMirrorRoot, TEXT("manifest.json"));
	}

	FProsettaResolvedStoryPaths CanonicalPaths(const FString& InMirrorRoot)
	{
		FProsettaResolvedStoryPaths Paths;
		Paths.MirrorRoot = InMirrorRoot;
		Paths.ManifestPath = ManifestPath(InMirrorRoot);
		Paths.CompiledStoryPath = FPaths::Combine(InMirrorRoot, TEXT("compiled"), TEXT("story.json"));
		Paths.LinesPath = FPaths::Combine(InMirrorRoot, TEXT("lines"), TEXT("lines.json"));
		Paths.LocalizationFolder = FPaths::Combine(InMirrorRoot, TEXT("localization"));
		return Paths;
	}

	namespace
	{
		/** Absolute path of an artifact, or empty when the manifest lacks it or its relative path. */
		FString ArtifactPath(const FProsettaManifestSummary& Manifest, const FString& InMirrorRoot, const FString& Key)
		{
			if (Key.IsEmpty()) { return FString(); }
			const FProsettaArtifactSummary* Artifact = Manifest.FindArtifact(Key);
			if (!Artifact || Artifact->RelativePath.IsEmpty()) { return FString(); }
			return FPaths::Combine(InMirrorRoot, Artifact->RelativePath);
		}

		FString FirstArtifactPath(const FProsettaManifestSummary& Manifest, const FString& InMirrorRoot, std::initializer_list<FString> Keys)
		{
			for (const FString& Key : Keys)
			{
				const FString Path = ArtifactPath(Manifest, InMirrorRoot, Key);
				if (!Path.IsEmpty()) { return Path; }
			}
			return FString();
		}
	}

	bool ResolveStoryPathsFromManifest(const FProsettaManifestSummary& Manifest, const FString& InMirrorRoot, const FString& VolumeSlug, FProsettaResolvedStoryPaths& Out)
	{
		Out = CanonicalPaths(InMirrorRoot);
		Out.bFromManifest = true;
		// The canonical artifacts may live somewhere other than the default layout; trust the manifest.
		const FString CanonicalStory = ArtifactPath(Manifest, InMirrorRoot, TEXT("compiled-story"));
		const FString CanonicalLines = ArtifactPath(Manifest, InMirrorRoot, TEXT("lines"));
		const FString CanonicalLocalization = ArtifactPath(Manifest, InMirrorRoot, TEXT("localization"));
		if (!CanonicalStory.IsEmpty()) { Out.CompiledStoryPath = CanonicalStory; }
		if (!CanonicalLines.IsEmpty()) { Out.LinesPath = CanonicalLines; }
		if (!CanonicalLocalization.IsEmpty()) { Out.LocalizationFolder = FPaths::GetPath(CanonicalLocalization); }

		if (VolumeSlug.IsEmpty())
		{
			// Canonical request: a single-volume scope still names the volume for provenance.
			if (Manifest.ScopeDetail.Volumes.Num() == 1)
			{
				Out.VolumeSlug = Manifest.ScopeDetail.Volumes[0].Slug;
				Out.VolumeTag = Manifest.ScopeDetail.Volumes[0].Tag;
			}
			return true;
		}

		const FProsettaScopeVolumeEntry* Entry = Manifest.ScopeDetail.FindVolume(VolumeSlug);
		const FString VolumeStory = FirstArtifactPath(Manifest, InMirrorRoot, { Entry ? Entry->CompiledStoryArtifactKey : FString(), TEXT("compiled-story:") + VolumeSlug });
		if (!VolumeStory.IsEmpty())
		{
			Out.CompiledStoryPath = VolumeStory;
			const FString VolumeLines = FirstArtifactPath(Manifest, InMirrorRoot, { Entry ? Entry->LinesArtifactKey : FString(), TEXT("lines:") + VolumeSlug });
			if (!VolumeLines.IsEmpty()) { Out.LinesPath = VolumeLines; }
			const FString VolumeLocalization = FirstArtifactPath(Manifest, InMirrorRoot, { Entry ? Entry->LocalizationArtifactKey : FString(), TEXT("localization:") + VolumeSlug });
			if (!VolumeLocalization.IsEmpty()) { Out.LocalizationFolder = FPaths::GetPath(VolumeLocalization); }
			Out.VolumeSlug = VolumeSlug;
			Out.VolumeTag = Entry ? Entry->Tag : FString();
			return true;
		}

		// A scope of exactly this one volume syncs to the canonical files.
		if (Entry && Manifest.ScopeDetail.Volumes.Num() == 1)
		{
			Out.VolumeSlug = VolumeSlug;
			Out.VolumeTag = Entry->Tag;
			return true;
		}

		Out.bFellBackToCanonical = true;
		Out.Note = Entry
			? FString::Printf(TEXT("volume '%s' is in the sync scope but the mirror has no compiled story artifact for it; playing the canonical story"), *VolumeSlug)
			: FString::Printf(TEXT("volume '%s' is not in the synced scope (%s); playing the canonical story. Sync the volume from the Prosetta Dashboard or re-point the level."), *VolumeSlug,
				Manifest.ScopeDetail.Volumes.IsEmpty() ? TEXT("live project") : *Manifest.ScopeDetail.Describe());
		return false;
	}

	bool ResolveStoryPaths(const FString& InMirrorRoot, const FString& VolumeSlug, FProsettaResolvedStoryPaths& Out)
	{
		FProsettaManifestSummary Manifest;
		bool bResolved;
		if (UProsettaSyncInspector::LoadManifestFile(ManifestPath(InMirrorRoot), Manifest))
		{
			bResolved = ResolveStoryPathsFromManifest(Manifest, InMirrorRoot, VolumeSlug, Out);
		}
		else
		{
			Out = CanonicalPaths(InMirrorRoot);
			bResolved = VolumeSlug.IsEmpty();
			if (!bResolved)
			{
				Out.bFellBackToCanonical = true;
				Out.Note = FString::Printf(TEXT("volume '%s' requested but %s has no readable manifest.json; playing the canonical story"), *VolumeSlug, *InMirrorRoot);
			}
		}
		if (Out.bFellBackToCanonical)
		{
			UE_LOG(LogProsettaStorySource, Warning, TEXT("Prosetta story source: %s"), *Out.Note);
		}
		return bResolved;
	}

	AActor* FindHost(const UWorld* World)
	{
		if (!World) { return nullptr; }
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (IsValid(*It) && It->Implements<UProsettaStorySourceHost>()) { return *It; }
		}
		return nullptr;
	}

	FProsettaStorySource ForWorld(const UWorld* World)
	{
		if (const IProsettaStorySourceHost* Host = Cast<IProsettaStorySourceHost>(FindHost(World)))
		{
			return Host->GetProsettaStorySource();
		}
		return FProsettaStorySource();
	}

	FString CompiledStoryPathForWorld(const UWorld* World)
	{
		FProsettaResolvedStoryPaths Paths;
		ResolveStoryPaths(MirrorRoot(), ForWorld(World).VolumeSlug, Paths);
		return Paths.CompiledStoryPath;
	}
}
