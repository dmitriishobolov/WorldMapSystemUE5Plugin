#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WorldMapTypes.h"
#include "WorldMapWidget.generated.h"

class AWorldMapViewer;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWorldMapElementEvent, const FWorldMapElement&, Element);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FWorldMapLocationEvent, FVector, WorldLocation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FWorldMapHoverEvent, bool, bHasElement, const FWorldMapElement&, Element);

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapTooltipWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UPROPERTY(BlueprintReadOnly, Category="World Map") FWorldMapElement MapElement;
	UFUNCTION(BlueprintCallable, Category="World Map") void SetMapElement(const FWorldMapElement& Element);
	/** Override in the Widget Blueprint to fill labels/images from the permitted snapshot. */
	UFUNCTION(BlueprintImplementableEvent, Category="World Map") void UpdateFromMapElement(const FWorldMapElement& Element);
};

/** Shared CPU geometry renderer and input handling. Each widget owns its own camera and selected floor. */
UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UWorldMapWidget(const FObjectInitializer& Initializer);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World Map", meta=(ExposeOnSpawn=true)) TObjectPtr<UWorldMapViewData> ViewSettings;
	UPROPERTY(BlueprintAssignable, Category="World Map") FWorldMapElementEvent OnElementClicked;
	UPROPERTY(BlueprintAssignable, Category="World Map") FWorldMapLocationEvent OnMapClicked;
	UPROPERTY(BlueprintAssignable, Category="World Map") FWorldMapHoverEvent OnHoveredElementChanged;
	UFUNCTION(BlueprintCallable, Category="World Map") void BindViewer(AWorldMapViewer* Viewer);
	UFUNCTION(BlueprintPure, Category="World Map") AWorldMapViewer* GetViewer() const { return BoundViewer; }
	UFUNCTION(BlueprintCallable, Category="World Map") bool SetDisplayedFloor(UWorldMapDefinition* Map, UWorldMapFloorData* Floor);
	UFUNCTION(BlueprintPure, Category="World Map") UWorldMapDefinition* GetDisplayedMap() const { return DisplayedMap; }
	UFUNCTION(BlueprintPure, Category="World Map") UWorldMapFloorData* GetDisplayedFloor() const { return DisplayedFloor; }
	UFUNCTION(BlueprintCallable, Category="World Map") void CenterOnPlayer();
	UFUNCTION(BlueprintCallable, Category="World Map") void SetFollowPlayer(bool bFollow);
	UFUNCTION(BlueprintCallable, Category="World Map") void SetMapCenter(FVector2D WorldXY);
	UFUNCTION(BlueprintCallable, Category="World Map") void SetZoom(float NewZoom);
	UFUNCTION(BlueprintPure, Category="World Map") float GetZoom() const { return Zoom; }
	UFUNCTION(BlueprintCallable, Category="World Map") void ZoomAtLocalPosition(float NewZoom, FVector2D LocalPosition);
	UFUNCTION(BlueprintCallable, Category="World Map") void SetDefinitionFilter(const TArray<UWorldMapElementData*>& Definitions);
	UFUNCTION(BlueprintPure, Category="World Map") FVector2D WorldToWidgetPosition(FVector WorldLocation) const;
	UFUNCTION(BlueprintPure, Category="World Map") FVector WidgetToWorldPosition(FVector2D LocalPosition) const;
	UFUNCTION(BlueprintPure, Category="World Map") bool FindElementAtLocalPosition(FVector2D LocalPosition, FWorldMapElement& Element) const;
	UFUNCTION(BlueprintCallable, Category="World Map") void RequestPingAtLocalPosition(UWorldMapPingData* PingDefinition, FVector2D LocalPosition);

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& Geometry, float DeltaSeconds) override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& DrawElements, int32 LayerId, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& Event) override;
	virtual void NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) override;
	UFUNCTION() void HandleMapChanged();
	void SetHoveredElement(const FWorldMapElement* Element);
	void ProjectElement(const FWorldMapElement& Element, FVector2D Size, TArray<FVector2D>& Points) const;
	bool ShouldDisplay(const FWorldMapElement& Element) const;
	const UWorldMapViewData* GetViewConfig() const;
	const UWorldMapStyleData* GetElementStyle(const FWorldMapElement& Element) const;
	bool IsInteractiveMap() const;

	UPROPERTY(Transient) TObjectPtr<AWorldMapViewer> BoundViewer;
	UPROPERTY(Transient) TObjectPtr<UWorldMapDefinition> DisplayedMap;
	UPROPERTY(Transient) TObjectPtr<UWorldMapFloorData> DisplayedFloor;
	UPROPERTY(Transient) TArray<FWorldMapElement> CachedElements;
	UPROPERTY(Transient) TArray<TObjectPtr<UWorldMapElementData>> DefinitionFilter;
	UPROPERTY(Transient) TObjectPtr<UWorldMapTooltipWidget> ActiveTooltip;
	TMap<const UWorldMapShapeData*, TArray<int32>> Triangles;
	FVector2D MapCenter = FVector2D::ZeroVector;
	float Zoom = 0.1f;
	double Rotation = 0;
	bool bFollowPlayer = false;
	bool bFollowFloor = true;
	bool bMinimapDefaults = false;
	bool bPointerDown = false;
	bool bDragging = false;
	FVector2D DragStart;
	FVector2D DragCenter;
	FVector2D LastPointerPosition;
	bool bPointerInside = false;
	FGuid HoveredId;
	double ViewerRetry = 0;
};

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapMinimapWidget : public UWorldMapWidget
{
	GENERATED_BODY()
public:
	UWorldMapMinimapWidget(const FObjectInitializer& Initializer);
};

UCLASS(BlueprintType, Blueprintable)
class WORLDMAPSYSTEM_API UWorldMapFullMapWidget : public UWorldMapWidget
{
	GENERATED_BODY()
};
