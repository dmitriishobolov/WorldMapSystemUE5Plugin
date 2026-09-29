#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WorldMapTypes.h"
#include "WorldMapManager.generated.h"

class AWorldMapViewer;
class AWorldMapRegion;
class USceneComponent;

USTRUCT()
struct FWorldMapServerEntry
{
	GENERATED_BODY()
	UPROPERTY() FWorldMapElement Element;
	UPROPERTY() FGuid ExplorationId;
	UPROPERTY() TObjectPtr<UWorldMapTeamData> OwningTeam;
	UPROPERTY() TWeakObjectPtr<USceneComponent> TransformSource;
	UPROPERTY() TWeakObjectPtr<APlayerController> PingOwner;
	UPROPERTY() double ExpiresAt = 0;
	UPROPERTY() bool bTeamPing = false;
};

USTRUCT()
struct FWorldMapTeamKnowledge
{
	GENERATED_BODY()
	UPROPERTY() TSet<FGuid> Explored;
};

USTRUCT()
struct FWorldMapObservation
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<UWorldMapTeamData> Team;
	UPROPERTY() TObjectPtr<UWorldMapObservationData> Source;
	UPROPERTY() TWeakObjectPtr<AActor> Observer;
	UPROPERTY() FWorldMapElement Snapshot;
	UPROPERTY() double ReportedAt = 0;
	UPROPERTY() double FreshUntil = 0;
	UPROPERTY() double ExpiresAt = 0;
};

USTRUCT()
struct FWorldMapPlayerSession
{
	GENERATED_BODY()
	UPROPERTY() FWorldMapViewerContext Context;
	UPROPERTY() TObjectPtr<AWorldMapViewer> Viewer;
	UPROPERTY() TObjectPtr<UWorldMapDefinition> PendingMap;
	UPROPERTY() TObjectPtr<UWorldMapFloorData> PendingFloor;
	UPROPERTY() double PendingSince = 0;
	UPROPERTY() double NextPingAt = 0;
};

/** Public replicated manager; the registry, exploration and observations are SERVER-ONLY properties. */
UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API AWorldMapManager : public AActor
{
	GENERATED_BODY()
public:
	AWorldMapManager();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& Out) const override;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="World Map") TObjectPtr<UWorldMapSettings> Settings;

	UFUNCTION(BlueprintPure, Category="World Map", meta=(WorldContext="WorldContextObject")) static AWorldMapManager* FindMapManager(const UObject* WorldContextObject);
	UFUNCTION(BlueprintPure, Category="World Map", meta=(WorldContext="WorldContextObject")) static AWorldMapViewer* FindLocalViewer(const UObject* WorldContextObject, APlayerController* PlayerController);
	/** Call only from trusted server logic, e.g. GameMode PostLogin / team assignment. Null team is rejected. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map|Authority") AWorldMapViewer* AssignViewer(APlayerController* PlayerController, UWorldMapTeamData* Team, UWorldMapRoleData* ViewerRole);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map|Authority") void RemoveViewer(APlayerController* PlayerController);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map|Authority") FGuid RegisterElement(FWorldMapElement Element, FGuid ExplorationId, UWorldMapTeamData* OwningTeam, USceneComponent* TransformSource);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map|Authority") bool UnregisterElement(FGuid Id, bool bKeepSnapshot = false);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map|Authority") bool SetElementState(FGuid Id, UWorldMapStateData* State);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map|Authority") bool SetElementPlacement(FGuid Id, UWorldMapDefinition* Map, UWorldMapFloorData* Floor, FTransform Transform);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map|Exploration") bool ExploreElementForTeam(UWorldMapTeamData* Team, FGuid ElementId);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map|Exploration") int32 ExploreElementsByDefinition(UWorldMapTeamData* Team, UWorldMapElementData* Definition);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map|Exploration") TArray<FGuid> GetExploredAreaIds(UWorldMapTeamData* Team) const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map|Exploration") void RestoreExploredAreaIds(UWorldMapTeamData* Team, const TArray<FGuid>& AreaIds);
	/** Repeated reports update a sample; the manager never follows an unseen target between reports. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map|Observation") bool ReportObservation(UWorldMapTeamData* Team, FGuid ElementId, UWorldMapObservationData* Source, AActor* Observer);
	/** Immediately ends this source's live phase; optionally keeps its configured memory. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map|Observation") void EndObservation(UWorldMapTeamData* Team, FGuid ElementId, UWorldMapObservationData* Source, AActor* Observer, bool bKeepMemory = true);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map|Authority") TArray<FWorldMapElement> FindRegisteredElementsByDefinition(UWorldMapElementData* Definition) const;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map|Authority") void RefreshViewers();

	/** Additional restrictions only. Built-in audience and knowledge checks run first. */
	UFUNCTION(BlueprintNativeEvent, Category="World Map|Authority") bool CanViewerReceiveElement(const FWorldMapViewerContext& Context, const FWorldMapElement& Element) const;
	UFUNCTION(BlueprintNativeEvent, Category="World Map|Authority") bool CanCreatePing(APlayerController* PlayerController, UWorldMapPingData* Definition, FVector ServerLocation) const;

	void RegisterRegion(AWorldMapRegion* Region);
	void UnregisterRegion(AWorldMapRegion* Region);
	void HandlePing(AWorldMapViewer* Viewer, UWorldMapPingData* Definition, UWorldMapDefinition* Map, UWorldMapFloorData* Floor, FVector2D WorldXY);
	bool BuildVisibleElement(const FWorldMapServerEntry& Entry, const FWorldMapViewerContext& Context, double Now, FWorldMapElement& Out) const;

protected:
	UPROPERTY(Transient) TMap<FGuid, FWorldMapServerEntry> Registry;
	UPROPERTY(Transient) TMap<TObjectPtr<UWorldMapTeamData>, FWorldMapTeamKnowledge> Knowledge;
	UPROPERTY(Transient) TArray<FWorldMapObservation> Observations;
	UPROPERTY(Transient) TMap<TObjectPtr<APlayerController>, FWorldMapPlayerSession> Sessions;
	UPROPERTY(Transient) TArray<TWeakObjectPtr<AWorldMapRegion>> Regions;
	FWorldMapElement ResolveElement(const FWorldMapServerEntry& Entry) const;
	AWorldMapRegion* FindRegion(FVector Location, UWorldMapDefinition* Map = nullptr, bool bRequireFloor = true) const;
	const FWorldMapObservation* FindObservation(UWorldMapTeamData* Team, FGuid Id, double Now) const;
	void UpdatePlayerRegion(APlayerController* PlayerController, FWorldMapPlayerSession& Session, double Now);
	double RefreshAccumulator = 0;
};
