#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldMapRegion.generated.h"

class UBoxComponent;
class UWorldMapElementComponent;
class UWorldMapRegionData;
class AWorldMapManager;

/** Authoring actor. The server tests the Pawn's position; clients cannot claim an overlap. */
UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API AWorldMapRegion : public AActor
{
	GENERATED_BODY()
public:
	AWorldMapRegion();
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="World Map") TObjectPtr<UBoxComponent> Volume;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="World Map") TObjectPtr<UWorldMapElementComponent> MapElement;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World Map") TObjectPtr<UWorldMapRegionData> RegionSettings;
	UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category="World Map") bool ContainsLocation(FVector WorldLocation) const;
	UFUNCTION(BlueprintPure, Category="World Map") int32 GetRegionPriority() const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map") void RegisterRegionWithManager(AWorldMapManager* Manager);
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
protected:
	UPROPERTY(Transient) TObjectPtr<AWorldMapManager> RegisteredManager;
};
