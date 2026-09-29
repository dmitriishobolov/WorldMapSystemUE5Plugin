#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "WorldMapTypes.h"
#include "WorldMapElementComponent.generated.h"

class AWorldMapManager;

UCLASS(BlueprintType, Blueprintable, ClassGroup=(WorldMap), meta=(BlueprintSpawnableComponent))
class WORLDMAPSYSTEM_API UWorldMapElementComponent : public USceneComponent
{
	GENERATED_BODY()
public:
	UWorldMapElementComponent();
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Map") TObjectPtr<UWorldMapDefinition> Map;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Map") TObjectPtr<UWorldMapFloorData> Floor;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Map") TObjectPtr<UWorldMapElementData> Definition;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Map") TObjectPtr<UWorldMapStateData> InitialState;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Map") TObjectPtr<UWorldMapTeamData> OwningTeam;
	/** Same ExplorationId for room geometry, doors and objects revealed together. Invalid uses own element Id. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Map|Identity") FGuid ExplorationId;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Map|Identity") FGuid LocalElementId;
	/** Set once per generated room instance BEFORE registration. Combined with local IDs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Map|Identity") FGuid InstanceNamespace;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Map") bool bAutoRegister = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World Map") bool bRememberAfterStreamingUnload = true;
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map") FGuid RegisterWithManager(AWorldMapManager* Manager);
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="World Map") void UnregisterFromManager(bool bKeepSnapshot = false);
	UFUNCTION(BlueprintPure, Category="World Map") FGuid GetElementId() const { return RegisteredId; }
	UFUNCTION(BlueprintPure, Category="World Map") FGuid GetResolvedExplorationId() const;
	virtual void OnComponentCreated() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
#if WITH_EDITOR
	virtual void PostEditImport() override;
#endif
protected:
	UPROPERTY(Transient) TObjectPtr<AWorldMapManager> RegisteredManager;
	UPROPERTY(Transient) FGuid RegisteredId;
};
