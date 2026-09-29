#include "WorldMapManager.h"
#include "WorldMapViewer.h"
#include "WorldMapRegion.h"
#include "WorldMapElementComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldMap, Log, All);

AWorldMapManager::AWorldMapManager()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AWorldMapManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWorldMapManager, Settings);
}

AWorldMapManager* AWorldMapManager::FindMapManager(const UObject* Context)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (World) for (TActorIterator<AWorldMapManager> It(World); It; ++It) if (IsValid(*It)) return *It;
	return nullptr;
}

AWorldMapViewer* AWorldMapManager::FindLocalViewer(const UObject* Context, APlayerController* PC)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(Context, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World || !PC || !PC->IsLocalController()) return nullptr;
	for (TActorIterator<AWorldMapViewer> It(World); It; ++It) if (IsValid(*It) && It->GetOwner() == PC) return *It;
	return nullptr;
}

void AWorldMapManager::BeginPlay()
{
	Super::BeginPlay();
	if (!HasAuthority()) { SetActorTickEnabled(false); return; }
	// Registration works regardless of whether components or manager begin play first.
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		TInlineComponentArray<UWorldMapElementComponent*> Components(*It);
		for (UWorldMapElementComponent* Component : Components) if (Component->bAutoRegister) Component->RegisterWithManager(this);
		if (AWorldMapRegion* Region = Cast<AWorldMapRegion>(*It)) if (Region->MapElement->bAutoRegister) Region->RegisterRegionWithManager(this);
	}
}

void AWorldMapManager::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) return;
	RefreshAccumulator += DeltaSeconds;
	if (RefreshAccumulator >= FMath::Max(0.05f, Settings ? Settings->RefreshInterval : 0.1f)) { RefreshAccumulator = 0; RefreshViewers(); }
}

void AWorldMapManager::EndPlay(const EEndPlayReason::Type Reason)
{
	if (HasAuthority()) for (auto& Pair : Sessions) if (IsValid(Pair.Value.Viewer)) Pair.Value.Viewer->Destroy();
	Sessions.Reset(); Registry.Reset(); Observations.Reset(); Knowledge.Reset(); Regions.Reset();
	Super::EndPlay(Reason);
}

AWorldMapViewer* AWorldMapManager::AssignViewer(APlayerController* PC, UWorldMapTeamData* Team, UWorldMapRoleData* ViewerRole)
{
	if (!HasAuthority() || !IsValid(PC) || PC->GetWorld() != GetWorld() || !Team) return nullptr;
	FWorldMapPlayerSession* Existing = Sessions.Find(PC);
	if (Existing && Existing->Context.Team == Team && Existing->Context.Role == ViewerRole && IsValid(Existing->Viewer)) return Existing->Viewer;
	if (Existing && (Existing->Context.Team != Team || Existing->Context.Role != ViewerRole)) RemoveViewer(PC);
	FActorSpawnParameters Params;
	Params.Owner = PC;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	UClass* Class = Settings && Settings->ViewerClass ? Settings->ViewerClass.Get() : AWorldMapViewer::StaticClass();
	AWorldMapViewer* Viewer = GetWorld()->SpawnActor<AWorldMapViewer>(Class, FTransform::Identity, Params);
	if (!Viewer) return nullptr;
	Viewer->SetNetUpdateFrequency(FMath::Clamp(Settings ? Settings->ViewerNetUpdateFrequency : 10.f, 1.f, 60.f));
	FWorldMapPlayerSession& Session = Sessions.FindOrAdd(PC);
	Session.Context.Team = Team; Session.Context.Role = ViewerRole; Session.Viewer = Viewer;
	Viewer->InitializeViewer(this, Session.Context);
	return Viewer;
}

void AWorldMapManager::RemoveViewer(APlayerController* PC)
{
	if (!HasAuthority()) return;
	if (FWorldMapPlayerSession* Session = Sessions.Find(PC)) if (IsValid(Session->Viewer)) Session->Viewer->Destroy();
	Sessions.Remove(PC);
	for (auto It = Registry.CreateIterator(); It; ++It) if (It.Value().bTeamPing && It.Value().PingOwner.Get() == PC) It.RemoveCurrent();
}

FGuid AWorldMapManager::RegisterElement(FWorldMapElement Element, FGuid ExplorationId, UWorldMapTeamData* OwningTeam, USceneComponent* TransformSource)
{
	if (!HasAuthority() || (TransformSource && TransformSource->GetWorld() != GetWorld())) return FGuid();
	if (!Element.Id.IsValid()) Element.Id = FGuid::NewGuid();
	Element.bLastKnown = false;
	if (!WorldMap::IsValidElement(Element)) { UE_LOG(LogWorldMap, Warning, TEXT("Rejected incomplete or invalid map element %s"), *Element.Id.ToString()); return FGuid(); }
	if (const FWorldMapServerEntry* Existing = Registry.Find(Element.Id))
	{
		if (Existing->TransformSource.IsValid() && Existing->TransformSource.Get() != TransformSource)
		{
			UE_LOG(LogWorldMap, Warning, TEXT("Duplicate map Id %s. Assign a unique InstanceNamespace to each generated room."), *Element.Id.ToString());
			return FGuid();
		}
	}
	FWorldMapServerEntry& Entry = Registry.FindOrAdd(Element.Id);
	Entry.Element = Element; Entry.ExplorationId = ExplorationId.IsValid() ? ExplorationId : Element.Id;
	Entry.OwningTeam = OwningTeam; Entry.TransformSource = TransformSource;
	return Element.Id;
}

bool AWorldMapManager::UnregisterElement(FGuid Id, bool bKeepSnapshot)
{
	if (!HasAuthority()) return false;
	FWorldMapServerEntry* Entry = Registry.Find(Id);
	if (!Entry) return false;
	if (bKeepSnapshot) { Entry->Element = ResolveElement(*Entry); Entry->TransformSource.Reset(); }
	else { Registry.Remove(Id); Observations.RemoveAll([Id](const FWorldMapObservation& O) { return O.Snapshot.Id == Id; }); }
	return true;
}

bool AWorldMapManager::SetElementState(FGuid Id, UWorldMapStateData* State)
{
	FWorldMapServerEntry* Entry = HasAuthority() ? Registry.Find(Id) : nullptr;
	if (!Entry) return false;
	Entry->Element.State = State;
	return true;
}

bool AWorldMapManager::SetElementPlacement(FGuid Id, UWorldMapDefinition* Map, UWorldMapFloorData* Floor, FTransform Transform)
{
	FWorldMapServerEntry* Entry = HasAuthority() ? Registry.Find(Id) : nullptr;
	if (!Entry || !Map || !Floor || !Map->Floors.Contains(Floor) || !Transform.IsValid() || Transform.GetScale3D().GetAbsMin() < UE_SMALL_NUMBER) return false;
	Entry->Element.Map = Map; Entry->Element.Floor = Floor; Entry->Element.Transform = Transform;
	if (Entry->TransformSource.IsValid()) Entry->TransformSource->SetWorldTransform(Transform);
	return true;
}

bool AWorldMapManager::ExploreElementForTeam(UWorldMapTeamData* Team, FGuid Id)
{
	const FWorldMapServerEntry* Entry = HasAuthority() ? Registry.Find(Id) : nullptr;
	if (!Team || !Entry) return false;
	Knowledge.FindOrAdd(Team).Explored.Add(Entry->ExplorationId);
	return true;
}

int32 AWorldMapManager::ExploreElementsByDefinition(UWorldMapTeamData* Team, UWorldMapElementData* Definition)
{
	if (!HasAuthority() || !Team || !Definition) return 0;
	int32 Count = 0;
	for (const auto& Pair : Registry) if (Pair.Value.Element.Definition == Definition) { ExploreElementForTeam(Team, Pair.Key); ++Count; }
	return Count;
}

TArray<FGuid> AWorldMapManager::GetExploredAreaIds(UWorldMapTeamData* Team) const
{
	const FWorldMapTeamKnowledge* Found = HasAuthority() && Team ? Knowledge.Find(Team) : nullptr;
	return Found ? Found->Explored.Array() : TArray<FGuid>();
}

void AWorldMapManager::RestoreExploredAreaIds(UWorldMapTeamData* Team, const TArray<FGuid>& Ids)
{
	if (!HasAuthority() || !Team) return;
	for (const FGuid& Id : Ids) if (Id.IsValid()) Knowledge.FindOrAdd(Team).Explored.Add(Id);
}

bool AWorldMapManager::ReportObservation(UWorldMapTeamData* Team, FGuid Id, UWorldMapObservationData* Source, AActor* Observer)
{
	const FWorldMapServerEntry* Entry = HasAuthority() ? Registry.Find(Id) : nullptr;
	if (!Team || !Entry || !Source || !IsValid(Observer) || Observer->GetWorld() != GetWorld()) return false;
	FWorldMapObservation* Record = Observations.FindByPredicate([=](const FWorldMapObservation& O) { return O.Team == Team && O.Source == Source && O.Observer == Observer && O.Snapshot.Id == Id; });
	if (!Record) Record = &Observations.AddDefaulted_GetRef();
	Record->Team = Team; Record->Source = Source; Record->Observer = Observer; Record->Snapshot = ResolveElement(*Entry);
	Record->ReportedAt = GetWorld()->GetTimeSeconds();
	Record->FreshUntil = Record->ReportedAt + FMath::Clamp(Source->FreshSeconds, 0.01f, 60.f);
	Record->ExpiresAt = Record->FreshUntil + FMath::Clamp(Source->MemorySeconds, 0.f, 300.f);
	return true;
}

void AWorldMapManager::EndObservation(UWorldMapTeamData* Team, FGuid Id, UWorldMapObservationData* Source, AActor* Observer, bool bKeepMemory)
{
	if (!HasAuthority()) return;
	const double Now = GetWorld()->GetTimeSeconds();
	for (FWorldMapObservation& O : Observations)
	{
		if (O.Team != Team || O.Source != Source || O.Observer != Observer || O.Snapshot.Id != Id) continue;
		O.FreshUntil = FMath::Min(O.FreshUntil, Now);
		O.ExpiresAt = bKeepMemory && Source ? O.FreshUntil + FMath::Clamp(Source->MemorySeconds, 0.f, 300.f) : Now;
	}
}

const FWorldMapObservation* AWorldMapManager::FindObservation(UWorldMapTeamData* Team, FGuid Id, double Now) const
{
	const FWorldMapObservation* Best = nullptr;
	for (const FWorldMapObservation& O : Observations)
	{
		if (O.Team != Team || O.Snapshot.Id != Id || O.ExpiresAt <= Now) continue;
		const bool bFresh = O.FreshUntil > Now, bBestFresh = Best && Best->FreshUntil > Now;
		if (!Best || (bFresh && !bBestFresh) || (bFresh == bBestFresh && O.ReportedAt > Best->ReportedAt)) Best = &O;
	}
	return Best;
}

FWorldMapElement AWorldMapManager::ResolveElement(const FWorldMapServerEntry& Entry) const
{
	FWorldMapElement Result = Entry.Element;
	if (Entry.TransformSource.IsValid()) Result.Transform = Entry.TransformSource->GetComponentTransform();
	if (Result.Definition && Result.Definition->bResolveFloorFromRegions)
	{
		if (AWorldMapRegion* Region = FindRegion(Result.Transform.GetLocation(), Result.Map))
			if (const FWorldMapServerEntry* RegionEntry = Registry.Find(Region->MapElement->GetElementId())) Result.Floor = RegionEntry->Element.Floor;
	}
	return Result;
}

bool AWorldMapManager::BuildVisibleElement(const FWorldMapServerEntry& Entry, const FWorldMapViewerContext& Context, double Now, FWorldMapElement& Out) const
{
	if (!HasAuthority() || !Context.Team || !Entry.Element.Definition) return false;
	const UWorldMapVisibilityData* Policy = Entry.Element.Definition->Visibility;
	if (!WorldMap::HasAudienceAccess(Policy, Context, Entry.OwningTeam) || (Entry.bTeamPing && Context.Team != Entry.OwningTeam)) return false;
	const FWorldMapTeamKnowledge* TeamKnowledge = Knowledge.Find(Context.Team);
	const bool bExplored = TeamKnowledge && TeamKnowledge->Explored.Contains(Entry.ExplorationId);
	const bool bBypass = Context.Role && Context.Role->bBypassExploration;
	const FWorldMapObservation* Observation = FindObservation(Context.Team, Entry.Element.Id, Now);
	if (!Entry.bTeamPing && Policy->RevealRule == EWorldMapRevealRule::Explored && !bExplored && !bBypass) return false;
	if (!Entry.bTeamPing && Policy->RevealRule == EWorldMapRevealRule::Observed)
	{
		if (!Observation) return false;
		Out = Observation->Snapshot;
		Out.bLastKnown = Observation->FreshUntil <= Now;
	}
	else Out = ResolveElement(Entry);
	if (Out.State && Out.State->Visibility)
	{
		if (Out.State->Visibility->RevealRule == EWorldMapRevealRule::Observed) Out.State = Observation ? Observation->Snapshot.State : nullptr;
	}
	if (Out.State && Out.State->Visibility)
	{
		const UWorldMapVisibilityData* StatePolicy = Out.State->Visibility;
		if (!WorldMap::HasAudienceAccess(StatePolicy, Context, Entry.OwningTeam)
			|| (StatePolicy->RevealRule == EWorldMapRevealRule::Explored && !bExplored && !bBypass)
			|| (StatePolicy->RevealRule == EWorldMapRevealRule::Observed && !Observation)) Out.State = nullptr;
	}
	return CanViewerReceiveElement(Context, Out);
}

TArray<FWorldMapElement> AWorldMapManager::FindRegisteredElementsByDefinition(UWorldMapElementData* Definition) const
{
	TArray<FWorldMapElement> Result;
	if (HasAuthority() && Definition) for (const auto& Pair : Registry) if (Pair.Value.Element.Definition == Definition) Result.Add(ResolveElement(Pair.Value));
	return Result;
}

void AWorldMapManager::RegisterRegion(AWorldMapRegion* Region) { if (HasAuthority() && IsValid(Region) && Region->GetWorld() == GetWorld()) Regions.AddUnique(Region); }
void AWorldMapManager::UnregisterRegion(AWorldMapRegion* Region) { if (HasAuthority()) Regions.Remove(Region); }

AWorldMapRegion* AWorldMapManager::FindRegion(FVector Location, UWorldMapDefinition* Map, bool bRequireFloor) const
{
	AWorldMapRegion* Best = nullptr;
	for (const auto& Weak : Regions)
	{
		AWorldMapRegion* Region = Weak.Get();
		if (!Region || (bRequireFloor && Region->RegionSettings && !Region->RegionSettings->bSetsPlayerFloor)) continue;
		const FWorldMapServerEntry* Entry = Registry.Find(Region->MapElement->GetElementId());
		if (!Entry || (Map && Entry->Element.Map != Map) || !Region->ContainsLocation(Location)) continue;
		if (!Best || Region->GetRegionPriority() > Best->GetRegionPriority()
			|| (Region->GetRegionPriority() == Best->GetRegionPriority() && Region->Volume->GetScaledBoxExtent().SizeSquared() < Best->Volume->GetScaledBoxExtent().SizeSquared())) Best = Region;
	}
	return Best;
}

void AWorldMapManager::UpdatePlayerRegion(APlayerController* PC, FWorldMapPlayerSession& Session, double Now)
{
	if (!PC->GetPawn() || !IsValid(Session.Viewer)) return;
	const FVector Location = PC->GetPawn()->GetActorLocation();
	if (AWorldMapRegion* ExplorationRegion = FindRegion(Location, nullptr, false))
		if (!ExplorationRegion->RegionSettings || ExplorationRegion->RegionSettings->bExploreOnEntry) ExploreElementForTeam(Session.Context.Team, ExplorationRegion->MapElement->GetElementId());
	AWorldMapRegion* Region = FindRegion(Location);
	if (!Region) { Session.PendingFloor = nullptr; Session.PendingMap = nullptr; return; }
	const FWorldMapServerEntry* Entry = Registry.Find(Region->MapElement->GetElementId());
	if (!Entry) return;
	if (Session.PendingMap != Entry->Element.Map || Session.PendingFloor != Entry->Element.Floor)
	{
		Session.PendingMap = Entry->Element.Map; Session.PendingFloor = Entry->Element.Floor; Session.PendingSince = Now;
	}
	if (Now - Session.PendingSince >= FMath::Max(0.f, Settings ? Settings->FloorSwitchDelay : 0.15f)) Session.Viewer->SetCurrentLocation(Session.PendingMap, Session.PendingFloor);
}

void AWorldMapManager::RefreshViewers()
{
	if (!HasAuthority() || !GetWorld()) return;
	const double Now = GetWorld()->GetTimeSeconds();
	Observations.RemoveAll([Now](const FWorldMapObservation& O) { return O.ExpiresAt <= Now; });
	for (auto It = Registry.CreateIterator(); It; ++It) if (It.Value().ExpiresAt > 0 && It.Value().ExpiresAt <= Now) It.RemoveCurrent();
	Regions.RemoveAll([](const auto& Region) { return !Region.IsValid(); });
	TArray<APlayerController*> Gone;
	TArray<TObjectPtr<APlayerController>> Players;
	Sessions.GetKeys(Players);
	for (APlayerController* PC : Players)
	{
		if (!IsValid(PC)) { Gone.Add(PC); continue; }
		if (FWorldMapPlayerSession* Session = Sessions.Find(PC)) UpdatePlayerRegion(PC, *Session, Now);
	}
	for (APlayerController* PC : Gone) RemoveViewer(PC);
	for (APlayerController* PC : Players)
	{
		FWorldMapPlayerSession* Found = Sessions.Find(PC);
		if (!Found) continue;
		FWorldMapPlayerSession& Session = *Found;
		if (!IsValid(Session.Viewer)) continue;
		TArray<FWorldMapElement> Allowed;
		for (const auto& Element : Registry)
		{
			FWorldMapElement View;
			if (BuildVisibleElement(Element.Value, Session.Context, Now, View)) Allowed.Add(View);
		}
		Allowed.Sort([](const FWorldMapElement& A, const FWorldMapElement& B) { return A.Definition->DrawOrder > B.Definition->DrawOrder; });
		const int32 Budget = Settings ? Settings->ChangesPerRefresh : 128;
		if (!Session.Viewer->ApplyVisibleElements(Allowed, Budget))
		{
			UWorldMapDefinition* Map = Session.Viewer->GetCurrentMap(); UWorldMapFloorData* Floor = Session.Viewer->GetCurrentFloor();
			Session.Viewer->Destroy();
			if (AWorldMapViewer* Replacement = AssignViewer(PC, Session.Context.Team, Session.Context.Role)) { Replacement->SetCurrentLocation(Map, Floor); Replacement->ApplyVisibleElements(Allowed, Budget); }
		}
	}
}

bool AWorldMapManager::CanViewerReceiveElement_Implementation(const FWorldMapViewerContext&, const FWorldMapElement&) const { return HasAuthority(); }
bool AWorldMapManager::CanCreatePing_Implementation(APlayerController*, UWorldMapPingData*, FVector) const { return HasAuthority(); }

void AWorldMapManager::HandlePing(AWorldMapViewer* Viewer, UWorldMapPingData* Definition, UWorldMapDefinition* Map, UWorldMapFloorData* Floor, FVector2D WorldXY)
{
	if (!HasAuthority() || !IsValid(Viewer)) return;
	APlayerController* PC = Cast<APlayerController>(Viewer->GetOwner());
	FWorldMapPlayerSession* Session = Sessions.Find(PC);
	if (!Session || Session->Viewer != Viewer) return;
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now < Session->NextPingAt) return;
	// Invalid requests also consume the rate limit, before any expensive lookup.
	Session->NextPingAt = Now + FMath::Max(0.1f, Settings ? Settings->MinimumPingInterval : 0.5f);
	if (!Settings || !Definition || !Settings->AllowedPings.Contains(Definition) || !Map || !Floor || !Map->Floors.Contains(Floor)
		|| WorldXY.ContainsNaN() || !PC || !PC->GetPawn() || !Session->Context.Team || !Definition->Shape || Definition->Shape->Shape != EWorldMapShape::Point) return;
	int32 ActivePings = 0;
	for (const auto& Pair : Registry) if (Pair.Value.bTeamPing && Pair.Value.PingOwner == PC && Pair.Value.ExpiresAt > Now) ++ActivePings;
	if (ActivePings >= FMath::Clamp(Settings->MaximumPingsPerPlayer, 1, 32)) return;
	bool bAllowedLocation = false;
	FVector ServerLocation(WorldXY, Floor->ReferenceWorldZ);
	bool bKnowsMap = false;
	for (const auto& Pair : Registry)
	{
		FWorldMapElement View;
		if (!BuildVisibleElement(Pair.Value, Session->Context, Now, View) || View.Map != Map || View.Floor != Floor || View.bLastKnown) continue;
		bKnowsMap = true;
		if (WorldMap::ContainsWorldXY(View, WorldXY)) { bAllowedLocation = true; ServerLocation.Z = View.Transform.GetLocation().Z; break; }
	}
	if (!bAllowedLocation && !(bKnowsMap && Map->bAllowPublicOutdoorPings)) return;
	if (ServerLocation.ContainsNaN() || !FMath::IsFinite(Definition->MaximumDistance) || Definition->MaximumDistance <= 0
		|| FVector::DistSquared(PC->GetPawn()->GetActorLocation(), ServerLocation) > FMath::Square(static_cast<double>(Definition->MaximumDistance))
		|| !CanCreatePing(PC, Definition, ServerLocation)) return;
	FWorldMapElement Element;
	Element.Id = FGuid::NewGuid(); Element.Map = Map; Element.Floor = Floor; Element.Definition = Definition; Element.Transform.SetLocation(ServerLocation);
	if (!WorldMap::HasAudienceAccess(Definition->Visibility, Session->Context, Session->Context.Team)) return;
	const FGuid Id = RegisterElement(Element, FGuid(), Session->Context.Team, nullptr);
	if (FWorldMapServerEntry* Entry = Registry.Find(Id))
	{
		Entry->bTeamPing = true; Entry->PingOwner = PC;
		Entry->ExpiresAt = Now + FMath::Clamp(Definition->Lifetime, 0.1f, 300.f);
	}
}
