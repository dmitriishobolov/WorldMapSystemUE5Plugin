#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Styling/SlateBrush.h"
#include "WorldMapDataAssets.generated.h"

class UWorldMapTooltipWidget;
class AWorldMapViewer;

UENUM(BlueprintType)
enum class EWorldMapRevealRule : uint8 { Always, Explored, Observed };

UENUM(BlueprintType)
enum class EWorldMapShape : uint8 { Rectangle, Polygon, Polyline, Point };

/** Asset identity is the lookup key. Runtime copies of the same room additionally have a GUID. */
UCLASS(Abstract, BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapDescriptor : public UPrimaryDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Map") FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Map", meta=(MultiLine=true)) FText Description;
};

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapTeamData : public UWorldMapDescriptor
{
	GENERATED_BODY()
};

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapRoleData : public UWorldMapDescriptor
{
	GENERATED_BODY()
public:
	/** Does not bypass team/role audience restrictions or Observed visibility. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Access") bool bBypassExploration = false;
};

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapFloorData : public UWorldMapDescriptor
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Floor") int32 SortOrder = 0;
	/** Used for public outdoor pings when no region contains the location. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Floor") double ReferenceWorldZ = 0;
};

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapDefinition : public UWorldMapDescriptor
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Map") TArray<TObjectPtr<UWorldMapFloorData>> Floors;
	/** Explicitly permits pings in this public map outside authored regions. Default: deny. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Map") bool bAllowPublicOutdoorPings = false;
};

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapVisibilityData : public UWorldMapDescriptor
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Access") EWorldMapRevealRule RevealRule = EWorldMapRevealRule::Explored;
	/** Empty means no team restriction. Team and role restrictions are ANDed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Access") TArray<TObjectPtr<UWorldMapTeamData>> AllowedTeams;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Access") TArray<TObjectPtr<UWorldMapRoleData>> AllowedRoles;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Access") bool bOnlyOwningTeam = false;
};

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapShapeData : public UWorldMapDescriptor
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Geometry") EWorldMapShape Shape = EWorldMapShape::Rectangle;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Geometry", meta=(ClampMin="1")) FVector2D HalfExtent = FVector2D(500, 500);
	/** Simple polygon without holes, or polyline. Local XY coordinates, no repeated final point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Geometry") TArray<FVector2D> Points;
};

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapStyleData : public UWorldMapDescriptor
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Style") FLinearColor FillColor = FLinearColor(0.08f, 0.15f, 0.18f, 0.8f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Style") FLinearColor OutlineColor = FLinearColor(0.25f, 0.8f, 0.85f, 1);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Style", meta=(ClampMin="0")) float LineThickness = 2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Style", meta=(ClampMin="1")) float MarkerSize = 14;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Style") FSlateBrush Icon;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Style", meta=(ClampMin="0", ClampMax="1")) float LastKnownOpacity = 0.4f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Style", meta=(ClampMin="0")) float MinimumZoom = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Style", meta=(ClampMin="0")) float MaximumZoom = 1000;
};

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapStateData : public UWorldMapDescriptor
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="State") TObjectPtr<UWorldMapStyleData> StyleOverride;
	/** Optional additional restriction on the state; the object's geometry may remain visible. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="State") TObjectPtr<UWorldMapVisibilityData> Visibility;
};

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapElementData : public UWorldMapDescriptor
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Element") TObjectPtr<UWorldMapShapeData> Shape;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Element") TObjectPtr<UWorldMapStyleData> Style;
	/** Missing policy is fail-closed: the element is not published. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Element") TObjectPtr<UWorldMapVisibilityData> Visibility;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Element") int32 DrawOrder = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Element") bool bInteractive = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Element") bool bResolveFloorFromRegions = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Element") TSubclassOf<UWorldMapTooltipWidget> TooltipClass;
};

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapPingData : public UWorldMapElementData
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ping", meta=(ClampMin="0.1", ClampMax="300")) float Lifetime = 8;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Ping", meta=(ClampMin="1")) float MaximumDistance = 10000;
};

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapObservationData : public UWorldMapDescriptor
{
	GENERATED_BODY()
public:
	/** Report again before this timeout to maintain live tracking. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Observation", meta=(ClampMin="0.01", ClampMax="60")) float FreshSeconds = 0.3f;
	/** Memory starts after FreshSeconds and retains the recorded position, floor and state. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Observation", meta=(ClampMin="0", ClampMax="300")) float MemorySeconds = 5;
};

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapRegionData : public UWorldMapDescriptor
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Region") FVector VolumeHalfExtent = FVector(500, 500, 200);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Region") int32 Priority = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Region") bool bExploreOnEntry = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Region") bool bSetsPlayerFloor = true;
};

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapSettings : public UWorldMapDescriptor
{
	GENERATED_BODY()
public:
	/** Only these assets can be selected by client ping RPCs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Security") TArray<TObjectPtr<UWorldMapPingData>> AllowedPings;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Security", meta=(ClampMin="0.1")) float MinimumPingInterval = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Security", meta=(ClampMin="1", ClampMax="32")) int32 MaximumPingsPerPlayer = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Network", meta=(ClampMin="0.05")) float RefreshInterval = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Network", meta=(ClampMin="1", ClampMax="60")) float ViewerNetUpdateFrequency = 10;
	/** Caps additions/changes per refresh to avoid Fast Array per-update limits. Revocations take priority. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Network", meta=(ClampMin="1", ClampMax="512")) int32 ChangesPerRefresh = 128;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Floors", meta=(ClampMin="0")) float FloorSwitchDelay = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Network") TSubclassOf<AWorldMapViewer> ViewerClass;
};

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapViewData : public UWorldMapDescriptor
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View", meta=(ClampMin="0.0001")) float InitialZoom = 0.1f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View", meta=(ClampMin="0.0001")) float MinimumZoom = 0.005f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View", meta=(ClampMin="0.0001")) float MaximumZoom = 2;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View", meta=(ClampMin="1.01")) float WheelZoomFactor = 1.2f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View") bool bInteractive = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View") bool bFollowPlayer = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View") bool bRotateWithPlayer = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View") FLinearColor BackgroundColor = FLinearColor(0.015f, 0.025f, 0.04f, 0.95f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View") FLinearColor PlayerColor = FLinearColor::White;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View", meta=(ClampMin="1")) float PlayerSize = 10;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View", meta=(ClampMin="1")) float HitTolerance = 6;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View") FVector2D DefaultSize = FVector2D(640, 640);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View", meta=(ClampMin="0")) float DragThreshold = 4;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View", meta=(ClampMin="0.05")) float ViewerRetryInterval = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="View", meta=(ClampMin="0.1")) float PlayerLineThickness = 2;
};
