#include "WorldMapRegion.h"
#include "WorldMapElementComponent.h"
#include "WorldMapManager.h"
#include "Components/BoxComponent.h"

AWorldMapRegion::AWorldMapRegion()
{
	PrimaryActorTick.bCanEverTick = false;
	Volume = CreateDefaultSubobject<UBoxComponent>(TEXT("RegionVolume"));
	SetRootComponent(Volume);
	Volume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Volume->SetBoxExtent(FVector(500, 500, 200));
	MapElement = CreateDefaultSubobject<UWorldMapElementComponent>(TEXT("MapElement"));
	MapElement->SetupAttachment(Volume);
}

void AWorldMapRegion::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	if (RegionSettings) Volume->SetBoxExtent(RegionSettings->VolumeHalfExtent.GetAbs());
}

bool AWorldMapRegion::ContainsLocation_Implementation(FVector Location) const
{
	if (Location.ContainsNaN() || !Volume) return false;
	const FVector P = Volume->GetComponentTransform().InverseTransformPosition(Location).GetAbs();
	const FVector E = Volume->GetUnscaledBoxExtent();
	return P.X <= E.X && P.Y <= E.Y && P.Z <= E.Z;
}

int32 AWorldMapRegion::GetRegionPriority() const { return RegionSettings ? RegionSettings->Priority : 0; }

void AWorldMapRegion::RegisterRegionWithManager(AWorldMapManager* Manager)
{
	if (!HasAuthority() || !IsValid(Manager)) return;
	if (RegisteredManager && RegisteredManager != Manager) RegisteredManager->UnregisterRegion(this);
	if (MapElement->RegisterWithManager(Manager).IsValid()) { RegisteredManager = Manager; Manager->RegisterRegion(this); }
}

void AWorldMapRegion::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority() && MapElement->bAutoRegister) RegisterRegionWithManager(AWorldMapManager::FindMapManager(this));
}

void AWorldMapRegion::EndPlay(const EEndPlayReason::Type Reason)
{
	if (IsValid(RegisteredManager)) RegisteredManager->UnregisterRegion(this);
	Super::EndPlay(Reason);
}
