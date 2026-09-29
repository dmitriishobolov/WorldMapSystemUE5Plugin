#include "WorldMapViewer.h"
#include "WorldMapManager.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

AWorldMapViewer::AWorldMapViewer()
{
	bReplicates = true;
	bOnlyRelevantToOwner = true;
	bAlwaysRelevant = false;
	bNetLoadOnClient = false;
	SetReplicateMovement(false);
	SetNetUpdateFrequency(10);
}

bool AWorldMapViewer::IsNetRelevantFor(const AActor* RealViewer, const AActor*, const FVector&) const
{
	return GetOwner() && RealViewer == GetOwner();
}

void AWorldMapViewer::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION(AWorldMapViewer, Manager, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AWorldMapViewer, Context, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AWorldMapViewer, CurrentMap, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AWorldMapViewer, CurrentFloor, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(AWorldMapViewer, Visible, COND_OwnerOnly);
}

void AWorldMapViewer::InitializeViewer(AWorldMapManager* InManager, const FWorldMapViewerContext& InContext)
{
	if (!HasAuthority()) return;
	Manager = InManager;
	Context = InContext;
	OnRep_Map();
	ForceNetUpdate();
}

void AWorldMapViewer::SetCurrentLocation(UWorldMapDefinition* Map, UWorldMapFloorData* Floor)
{
	if (!HasAuthority() || (CurrentMap == Map && CurrentFloor == Floor)) return;
	CurrentMap = Map;
	CurrentFloor = Floor;
	OnRep_Map();
	ForceNetUpdate();
}

bool AWorldMapViewer::ApplyVisibleElements(const TArray<FWorldMapElement>& Elements, int32 Budget)
{
	if (!HasAuthority()) return false;
	Budget = FMath::Clamp(Budget, 1, 512);
	TMap<FGuid, const FWorldMapElement*> Desired;
	for (const FWorldMapElement& Element : Elements) Desired.Add(Element.Id, &Element);
	int32 Revoked = 0;
	for (const FWorldMapReplicatedItem& Item : Visible.Items) if (!Desired.Contains(Item.Element.Id)) ++Revoked;
	if (Revoked > Budget) return false;
	bool bChanged = Revoked > 0;
	if (Revoked)
	{
		Visible.Items.RemoveAll([&Desired](const FWorldMapReplicatedItem& Item) { return !Desired.Contains(Item.Element.Id); });
		Visible.MarkArrayDirty();
	}
	TSet<FGuid> Existing;
	for (const FWorldMapReplicatedItem& Item : Visible.Items) Existing.Add(Item.Element.Id);
	int32 Changes = 0;
	for (const FWorldMapElement& Element : Elements)
	{
		if (Changes >= Budget) break;
		if (Existing.Contains(Element.Id)) continue;
		FWorldMapReplicatedItem& Item = Visible.Items.AddDefaulted_GetRef();
		Item.Element = Element;
		Visible.MarkItemDirty(Item);
		++Changes;
	}
	const int32 Count = Visible.Items.Num();
	for (int32 Scanned = 0; Scanned < Count && Changes < Budget; ++Scanned)
	{
		UpdateCursor = (UpdateCursor + 1) % Count;
		FWorldMapReplicatedItem& Item = Visible.Items[UpdateCursor];
		const FWorldMapElement* const* Wanted = Desired.Find(Item.Element.Id);
		if (Wanted && !Item.Element.SamePresentation(**Wanted))
		{
			Item.Element = **Wanted;
			Visible.MarkItemDirty(Item);
			++Changes;
		}
	}
	if (bChanged || Changes) { OnRep_Map(); ForceNetUpdate(); }
	return true;
}

void AWorldMapViewer::OnRep_Map() { OnMapChanged.Broadcast(); }

TArray<FWorldMapElement> AWorldMapViewer::GetVisibleElements() const
{
	TArray<FWorldMapElement> Result;
	Result.Reserve(Visible.Items.Num() + PersonalMarkers.Num());
	for (const FWorldMapReplicatedItem& Item : Visible.Items) Result.Add(Item.Element);
	Result.Append(PersonalMarkers);
	return Result;
}

TArray<FWorldMapElement> AWorldMapViewer::FindElementsByDefinition(UWorldMapElementData* Definition) const
{
	TArray<FWorldMapElement> Result;
	if (Definition) for (const FWorldMapElement& Item : GetVisibleElements()) if (Item.Definition == Definition) Result.Add(Item);
	return Result;
}

TArray<FWorldMapElement> AWorldMapViewer::FindElementsByMapAndFloor(UWorldMapDefinition* Map, UWorldMapFloorData* Floor) const
{
	TArray<FWorldMapElement> Result;
	if (Map && Floor) for (const FWorldMapElement& Item : GetVisibleElements()) if (Item.Map == Map && Item.Floor == Floor) Result.Add(Item);
	return Result;
}

bool AWorldMapViewer::FindElementById(FGuid Id, FWorldMapElement& Element) const
{
	for (const FWorldMapElement& Item : GetVisibleElements()) if (Item.Id == Id) { Element = Item; return true; }
	Element = FWorldMapElement();
	return false;
}

void AWorldMapViewer::ServerRequestPing_Implementation(UWorldMapPingData* Definition, UWorldMapDefinition* Map, UWorldMapFloorData* Floor, FVector2D WorldXY)
{
	if (Manager) Manager->HandlePing(this, Definition, Map, Floor, WorldXY);
}

FGuid AWorldMapViewer::AddPersonalMarker(UWorldMapElementData* Definition, UWorldMapDefinition* Map, UWorldMapFloorData* Floor, FVector Location)
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!PC || !PC->IsLocalController() || Location.ContainsNaN()) return FGuid();
	FWorldMapElement Element;
	Element.Id = FGuid::NewGuid(); Element.Definition = Definition; Element.Map = Map; Element.Floor = Floor; Element.Transform.SetLocation(Location);
	if (!WorldMap::IsValidElement(Element)) return FGuid();
	PersonalMarkers.Add(Element);
	OnRep_Map();
	return Element.Id;
}

bool AWorldMapViewer::RemovePersonalMarker(FGuid Id)
{
	const bool bRemoved = PersonalMarkers.RemoveAll([Id](const FWorldMapElement& Item) { return Item.Id == Id; }) > 0;
	if (bRemoved) OnRep_Map();
	return bRemoved;
}
