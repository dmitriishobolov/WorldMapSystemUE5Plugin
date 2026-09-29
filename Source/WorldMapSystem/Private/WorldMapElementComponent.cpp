#include "WorldMapElementComponent.h"
#include "WorldMapManager.h"

UWorldMapElementComponent::UWorldMapElementComponent() { PrimaryComponentTick.bCanEverTick = false; }

void UWorldMapElementComponent::OnComponentCreated()
{
	Super::OnComponentCreated();
	if (!LocalElementId.IsValid() && !IsTemplate()) LocalElementId = FGuid::NewGuid();
}

#if WITH_EDITOR
void UWorldMapElementComponent::PostEditImport()
{
	Super::PostEditImport();
	LocalElementId = FGuid::NewGuid();
}
#endif

void UWorldMapElementComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetOwner()->HasAuthority() && bAutoRegister) RegisterWithManager(AWorldMapManager::FindMapManager(this));
}

FGuid UWorldMapElementComponent::GetResolvedExplorationId() const
{
	const FGuid Local = ExplorationId.IsValid() ? ExplorationId : LocalElementId;
	return InstanceNamespace.IsValid() ? FGuid::Combine(InstanceNamespace, Local) : Local;
}

FGuid UWorldMapElementComponent::RegisterWithManager(AWorldMapManager* Manager)
{
	if (!GetOwner() || !GetOwner()->HasAuthority() || !IsValid(Manager) || Manager->GetWorld() != GetWorld()) return FGuid();
	if (RegisteredManager && RegisteredId.IsValid()) return RegisteredId;
	if (!LocalElementId.IsValid()) LocalElementId = FGuid::NewGuid();
	FWorldMapElement Element;
	Element.Id = InstanceNamespace.IsValid() ? FGuid::Combine(InstanceNamespace, LocalElementId) : LocalElementId;
	Element.Map = Map; Element.Floor = Floor; Element.Definition = Definition; Element.State = InitialState; Element.Transform = GetComponentTransform();
	RegisteredId = Manager->RegisterElement(Element, GetResolvedExplorationId(), OwningTeam, this);
	if (RegisteredId.IsValid()) RegisteredManager = Manager;
	return RegisteredId;
}

void UWorldMapElementComponent::UnregisterFromManager(bool bKeepSnapshot)
{
	if (!GetOwner() || !GetOwner()->HasAuthority()) return;
	if (IsValid(RegisteredManager)) RegisteredManager->UnregisterElement(RegisteredId, bKeepSnapshot);
	RegisteredId.Invalidate(); RegisteredManager = nullptr;
}

void UWorldMapElementComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	UnregisterFromManager(bRememberAfterStreamingUnload && Reason == EEndPlayReason::RemovedFromWorld);
	Super::EndPlay(Reason);
}
