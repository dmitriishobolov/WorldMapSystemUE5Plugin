#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldMapTypes.h"
#include "WorldMapViewer.generated.h"

class AWorldMapManager;
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FWorldMapViewChanged);

/** One server-spawned actor per PlayerController. Never contains another viewer's private data. */
UCLASS(BlueprintType, Blueprintable, NotPlaceable)
class WORLDMAPSYSTEM_API AWorldMapViewer : public AActor
{
	GENERATED_BODY()
public:
	AWorldMapViewer();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	virtual bool IsNetRelevantFor(const AActor* RealViewer, const AActor* ViewTarget, const FVector& SrcLocation) const override;

	UPROPERTY(BlueprintAssignable, Category="World Map") FWorldMapViewChanged OnMapChanged;
	UFUNCTION(BlueprintPure, Category="World Map") TArray<FWorldMapElement> GetVisibleElements() const;
	UFUNCTION(BlueprintPure, Category="World Map") TArray<FWorldMapElement> FindElementsByDefinition(UWorldMapElementData* Definition) const;
	UFUNCTION(BlueprintPure, Category="World Map") TArray<FWorldMapElement> FindElementsByMapAndFloor(UWorldMapDefinition* Map, UWorldMapFloorData* Floor) const;
	UFUNCTION(BlueprintPure, Category="World Map") bool FindElementById(FGuid Id, FWorldMapElement& Element) const;
	UFUNCTION(BlueprintPure, Category="World Map") FWorldMapViewerContext GetViewerContext() const { return Context; }
	UFUNCTION(BlueprintPure, Category="World Map") UWorldMapDefinition* GetCurrentMap() const { return CurrentMap; }
	UFUNCTION(BlueprintPure, Category="World Map") UWorldMapFloorData* GetCurrentFloor() const { return CurrentFloor; }
	UFUNCTION(BlueprintPure, Category="World Map") AWorldMapManager* GetManager() const { return Manager; }

	/** The only client-to-server gameplay request. Team, lifetime and permissions are never client inputs. */
	UFUNCTION(BlueprintCallable, Server, Unreliable, Category="World Map|Pings")
	void ServerRequestPing(UWorldMapPingData* Definition, UWorldMapDefinition* Map, UWorldMapFloorData* Floor, FVector2D WorldXY);
	UFUNCTION(BlueprintCallable, Category="World Map|Personal Markers") FGuid AddPersonalMarker(UWorldMapElementData* Definition, UWorldMapDefinition* Map, UWorldMapFloorData* Floor, FVector Location);
	UFUNCTION(BlueprintCallable, Category="World Map|Personal Markers") bool RemovePersonalMarker(FGuid Id);

	void InitializeViewer(AWorldMapManager* InManager, const FWorldMapViewerContext& InContext);
	/** False requests actor replacement for large revocations instead of overflowing a Fast Array delta. */
	bool ApplyVisibleElements(const TArray<FWorldMapElement>& Elements, int32 Budget);
	void SetCurrentLocation(UWorldMapDefinition* Map, UWorldMapFloorData* Floor);

protected:
	UPROPERTY(ReplicatedUsing=OnRep_Map, BlueprintReadOnly, Category="World Map") TObjectPtr<AWorldMapManager> Manager;
	UPROPERTY(ReplicatedUsing=OnRep_Map, BlueprintReadOnly, Category="World Map") FWorldMapViewerContext Context;
	UPROPERTY(ReplicatedUsing=OnRep_Map, BlueprintReadOnly, Category="World Map") TObjectPtr<UWorldMapDefinition> CurrentMap;
	UPROPERTY(ReplicatedUsing=OnRep_Map, BlueprintReadOnly, Category="World Map") TObjectPtr<UWorldMapFloorData> CurrentFloor;
	UPROPERTY(ReplicatedUsing=OnRep_Map) FWorldMapReplicatedList Visible;
	UPROPERTY(Transient) TArray<FWorldMapElement> PersonalMarkers;
	UFUNCTION() void OnRep_Map();
	int32 UpdateCursor = 0;
};
