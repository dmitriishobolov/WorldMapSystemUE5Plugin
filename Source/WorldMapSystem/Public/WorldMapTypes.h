#pragma once

#include "CoreMinimal.h"
#include "WorldMapDataAssets.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "WorldMapTypes.generated.h"

/** Only this redacted representation goes over the network. No target Actor reference. */
USTRUCT(BlueprintType)
struct WORLDMAPSYSTEM_API FWorldMapElement
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Map") FGuid Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Map") TObjectPtr<UWorldMapDefinition> Map = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Map") TObjectPtr<UWorldMapFloorData> Floor = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Map") TObjectPtr<UWorldMapElementData> Definition = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Map") TObjectPtr<UWorldMapStateData> State = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Map") FTransform Transform = FTransform::Identity;
	UPROPERTY(BlueprintReadOnly, Category="Map") bool bLastKnown = false;
	bool SamePresentation(const FWorldMapElement& Other) const;
};

USTRUCT(BlueprintType)
struct WORLDMAPSYSTEM_API FWorldMapViewerContext
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Map") TObjectPtr<UWorldMapTeamData> Team = nullptr;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Map") TObjectPtr<UWorldMapRoleData> Role = nullptr;
};

USTRUCT()
struct FWorldMapReplicatedItem : public FFastArraySerializerItem
{
	GENERATED_BODY()
	UPROPERTY() FWorldMapElement Element;
};

USTRUCT()
struct FWorldMapReplicatedList : public FFastArraySerializer
{
	GENERATED_BODY()
	UPROPERTY() TArray<FWorldMapReplicatedItem> Items;
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& Params)
	{
		return FastArrayDeltaSerialize<FWorldMapReplicatedItem, FWorldMapReplicatedList>(Items, Params, *this);
	}
};

template<> struct TStructOpsTypeTraits<FWorldMapReplicatedList> : TStructOpsTypeTraitsBase2<FWorldMapReplicatedList>
{
	enum { WithNetDeltaSerializer = true };
};

/** Native helpers are also used by automated tests; BP queries live on manager/viewer/widgets. */
namespace WorldMap
{
	WORLDMAPSYSTEM_API bool HasAudienceAccess(const UWorldMapVisibilityData* Policy, const FWorldMapViewerContext& Context, const UWorldMapTeamData* OwningTeam);
	WORLDMAPSYSTEM_API bool IsValidElement(const FWorldMapElement& Element);
	WORLDMAPSYSTEM_API void GetLocalPoints(const UWorldMapShapeData* Shape, TArray<FVector2D>& Out);
	WORLDMAPSYSTEM_API bool Triangulate(const TArray<FVector2D>& Points, TArray<int32>& OutIndices);
	WORLDMAPSYSTEM_API bool ContainsPoint(const TArray<FVector2D>& Polygon, FVector2D Point);
	WORLDMAPSYSTEM_API bool ContainsWorldXY(const FWorldMapElement& Element, FVector2D Point);
	WORLDMAPSYSTEM_API FVector2D WorldToMap(FVector2D World, FVector2D Center, FVector2D Size, double Zoom, double Rotation);
	WORLDMAPSYSTEM_API FVector2D MapToWorld(FVector2D Local, FVector2D Center, FVector2D Size, double Zoom, double Rotation);
}
