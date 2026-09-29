#include "WorldMapWidget.h"
#include "WorldMapViewer.h"
#include "WorldMapManager.h"
#include "Blueprint/WidgetTree.h"
#include "Components/SizeBox.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"

void UWorldMapTooltipWidget::SetMapElement(const FWorldMapElement& Element)
{
	MapElement = Element;
	UpdateFromMapElement(Element);
}

UWorldMapWidget::UWorldMapWidget(const FObjectInitializer& Initializer) : Super(Initializer)
{
	SetClipping(EWidgetClipping::ClipToBoundsAlways);
	SetVisibility(ESlateVisibility::Visible);
}

UWorldMapMinimapWidget::UWorldMapMinimapWidget(const FObjectInitializer& Initializer) : Super(Initializer) { bMinimapDefaults = true; }

TSharedRef<SWidget> UWorldMapWidget::RebuildWidget()
{
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		USizeBox* Root = WidgetTree->ConstructWidget<USizeBox>();
		const FVector2D DefaultSize = ViewSettings ? ViewSettings->DefaultSize : FVector2D(bMinimapDefaults ? 240 : 640);
		Root->SetWidthOverride(DefaultSize.X); Root->SetHeightOverride(DefaultSize.Y);
		Root->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = Root;
	}
	return Super::RebuildWidget();
}

const UWorldMapViewData* UWorldMapWidget::GetViewConfig() const { return ViewSettings ? ViewSettings.Get() : GetDefault<UWorldMapViewData>(); }
bool UWorldMapWidget::IsInteractiveMap() const { return ViewSettings ? ViewSettings->bInteractive : !bMinimapDefaults; }

void UWorldMapWidget::NativeConstruct()
{
	Super::NativeConstruct();
	bFollowPlayer = ViewSettings ? ViewSettings->bFollowPlayer : bMinimapDefaults;
	bFollowFloor = true;
	SetZoom(GetViewConfig()->InitialZoom);
	BindViewer(AWorldMapManager::FindLocalViewer(this, GetOwningPlayer()));
}

void UWorldMapWidget::NativeDestruct()
{
	BindViewer(nullptr);
	Super::NativeDestruct();
}

void UWorldMapWidget::BindViewer(AWorldMapViewer* Viewer)
{
	if (Viewer && (!IsValid(Viewer) || Viewer->GetOwner() != GetOwningPlayer())) return;
	if (IsValid(BoundViewer)) BoundViewer->OnMapChanged.RemoveDynamic(this, &UWorldMapWidget::HandleMapChanged);
	BoundViewer = Viewer;
	SetHoveredElement(nullptr);
	if (BoundViewer) BoundViewer->OnMapChanged.AddDynamic(this, &UWorldMapWidget::HandleMapChanged);
	HandleMapChanged();
}

void UWorldMapWidget::HandleMapChanged()
{
	CachedElements = IsValid(BoundViewer) ? BoundViewer->GetVisibleElements() : TArray<FWorldMapElement>();
	CachedElements.Sort([](const FWorldMapElement& A, const FWorldMapElement& B)
	{
		const int32 OrderA = A.Definition ? A.Definition->DrawOrder : 0, OrderB = B.Definition ? B.Definition->DrawOrder : 0;
		return OrderA == OrderB ? A.Id < B.Id : OrderA < OrderB;
	});
	Triangles.Reset();
	for (const FWorldMapElement& Element : CachedElements)
	{
		if (!Element.Definition || !Element.Definition->Shape) continue;
		const UWorldMapShapeData* Shape = Element.Definition->Shape;
		if (!Triangles.Contains(Shape) && (Shape->Shape == EWorldMapShape::Rectangle || Shape->Shape == EWorldMapShape::Polygon))
		{
			TArray<FVector2D> Points; WorldMap::GetLocalPoints(Shape, Points);
			WorldMap::Triangulate(Points, Triangles.Add(Shape));
		}
	}
	if (!DisplayedMap && !CachedElements.IsEmpty()) { DisplayedMap = CachedElements[0].Map; DisplayedFloor = CachedElements[0].Floor; MapCenter = FVector2D(CachedElements[0].Transform.GetLocation()); }
	if (HoveredId.IsValid())
	{
		const FWorldMapElement* Hovered = CachedElements.FindByPredicate([this](const FWorldMapElement& E) { return E.Id == HoveredId && ShouldDisplay(E); });
		if (!Hovered) SetHoveredElement(nullptr);
		else if (ActiveTooltip) ActiveTooltip->SetMapElement(*Hovered);
	}
	InvalidateLayoutAndVolatility();
}

bool UWorldMapWidget::SetDisplayedFloor(UWorldMapDefinition* Map, UWorldMapFloorData* Floor)
{
	if (!Map || !Floor || !Map->Floors.Contains(Floor)) return false;
	DisplayedMap = Map; DisplayedFloor = Floor; bFollowFloor = false;
	SetHoveredElement(nullptr);
	return true;
}

void UWorldMapWidget::CenterOnPlayer()
{
	bFollowPlayer = true; bFollowFloor = true;
	if (APawn* Pawn = GetOwningPlayerPawn()) MapCenter = FVector2D(Pawn->GetActorLocation());
	if (IsValid(BoundViewer)) { DisplayedMap = BoundViewer->GetCurrentMap(); DisplayedFloor = BoundViewer->GetCurrentFloor(); }
}

void UWorldMapWidget::SetFollowPlayer(bool bFollow) { bFollowPlayer = bFollow; bFollowFloor = bFollow; }
void UWorldMapWidget::SetMapCenter(FVector2D WorldXY) { if (!WorldXY.ContainsNaN()) { MapCenter = WorldXY; bFollowPlayer = false; } }

void UWorldMapWidget::SetZoom(float NewZoom)
{
	if (!FMath::IsFinite(NewZoom)) return;
	const UWorldMapViewData* Config = GetViewConfig();
	Zoom = FMath::Clamp(NewZoom, FMath::Max(0.0001f, Config->MinimumZoom), FMath::Max(Config->MinimumZoom, Config->MaximumZoom));
}

void UWorldMapWidget::ZoomAtLocalPosition(float NewZoom, FVector2D LocalPosition)
{
	if (LocalPosition.ContainsNaN()) return;
	const FVector2D Before = FVector2D(WidgetToWorldPosition(LocalPosition));
	SetZoom(NewZoom);
	if (!bFollowPlayer) MapCenter += Before - FVector2D(WidgetToWorldPosition(LocalPosition));
}

void UWorldMapWidget::SetDefinitionFilter(const TArray<UWorldMapElementData*>& Definitions)
{
	DefinitionFilter.Reset();
	for (UWorldMapElementData* Definition : Definitions) if (Definition) DefinitionFilter.AddUnique(Definition);
	SetHoveredElement(nullptr);
}

FVector2D UWorldMapWidget::WorldToWidgetPosition(FVector Location) const { return WorldMap::WorldToMap(FVector2D(Location), MapCenter, GetCachedGeometry().GetLocalSize(), Zoom, Rotation); }
FVector UWorldMapWidget::WidgetToWorldPosition(FVector2D Position) const { return FVector(WorldMap::MapToWorld(Position, MapCenter, GetCachedGeometry().GetLocalSize(), Zoom, Rotation), DisplayedFloor ? DisplayedFloor->ReferenceWorldZ : 0); }

const UWorldMapStyleData* UWorldMapWidget::GetElementStyle(const FWorldMapElement& E) const { return E.State && E.State->StyleOverride ? E.State->StyleOverride.Get() : (E.Definition ? E.Definition->Style.Get() : nullptr); }

bool UWorldMapWidget::ShouldDisplay(const FWorldMapElement& E) const
{
	const UWorldMapStyleData* Style = GetElementStyle(E);
	return E.Map == DisplayedMap && E.Floor == DisplayedFloor && E.Definition && E.Definition->Shape && Style
		&& Zoom >= Style->MinimumZoom && Zoom <= Style->MaximumZoom && (DefinitionFilter.IsEmpty() || DefinitionFilter.Contains(E.Definition));
}

void UWorldMapWidget::ProjectElement(const FWorldMapElement& E, FVector2D Size, TArray<FVector2D>& Points) const
{
	WorldMap::GetLocalPoints(E.Definition->Shape, Points);
	for (FVector2D& P : Points) P = WorldMap::WorldToMap(FVector2D(E.Transform.TransformPosition(FVector(P, 0))), MapCenter, Size, Zoom, Rotation);
}

namespace
{
	double SegmentDistanceSquared(FVector2D P, FVector2D A, FVector2D B)
	{
		const FVector2D D = B - A;
		const double T = D.SizeSquared() > UE_DOUBLE_SMALL_NUMBER ? FMath::Clamp(FVector2D::DotProduct(P - A, D) / D.SizeSquared(), 0., 1.) : 0.;
		return (P - A - D * T).SizeSquared();
	}
}

bool UWorldMapWidget::FindElementAtLocalPosition(FVector2D P, FWorldMapElement& Element) const
{
	Element = FWorldMapElement();
	const FVector2D Size = GetCachedGeometry().GetLocalSize();
	if (P.ContainsNaN() || P.X < 0 || P.Y < 0 || P.X > Size.X || P.Y > Size.Y) return false;
	for (int32 I = CachedElements.Num() - 1; I >= 0; --I)
	{
		const FWorldMapElement& E = CachedElements[I];
		if (!ShouldDisplay(E) || !E.Definition->bInteractive) continue;
		const UWorldMapStyleData* Style = GetElementStyle(E);
		bool bHit = false;
		if (E.Definition->Shape->Shape == EWorldMapShape::Point)
		{
			const FVector2D Delta = (P - WorldToWidgetPosition(E.Transform.GetLocation())).GetAbs();
			const double Radius = FMath::Max(Style->MarkerSize * 0.5f, GetViewConfig()->HitTolerance);
			bHit = Delta.X <= Radius && Delta.Y <= Radius;
		}
		else
		{
			TArray<FVector2D> Points; ProjectElement(E, Size, Points);
			if (E.Definition->Shape->Shape == EWorldMapShape::Polyline)
			{
				const double Radius = FMath::Max(Style->LineThickness * 0.5f, GetViewConfig()->HitTolerance);
				for (int32 J = 1; J < Points.Num(); ++J) if (SegmentDistanceSquared(P, Points[J - 1], Points[J]) <= Radius * Radius) { bHit = true; break; }
			}
			else bHit = WorldMap::ContainsPoint(Points, P);
		}
		if (bHit) { Element = E; return true; }
	}
	return false;
}

void UWorldMapWidget::NativeTick(const FGeometry& Geometry, float DeltaSeconds)
{
	Super::NativeTick(Geometry, DeltaSeconds);
	if (!IsValid(BoundViewer))
	{
		if (!CachedElements.IsEmpty()) BindViewer(nullptr);
		ViewerRetry -= DeltaSeconds;
		if (ViewerRetry <= 0) { ViewerRetry = FMath::Max(0.05f, GetViewConfig()->ViewerRetryInterval); BindViewer(AWorldMapManager::FindLocalViewer(this, GetOwningPlayer())); }
	}
	if (bFollowPlayer) if (APawn* Pawn = GetOwningPlayerPawn()) MapCenter = FVector2D(Pawn->GetActorLocation());
	if (bFollowFloor && IsValid(BoundViewer) && BoundViewer->GetCurrentMap() && BoundViewer->GetCurrentFloor()) { DisplayedMap = BoundViewer->GetCurrentMap(); DisplayedFloor = BoundViewer->GetCurrentFloor(); }
	Rotation = GetViewConfig()->bRotateWithPlayer && GetOwningPlayerPawn() ? GetOwningPlayerPawn()->GetActorRotation().Yaw : 0;
	if (bPointerInside && !bDragging && IsInteractiveMap())
	{
		FWorldMapElement Hovered; const bool bFound = FindElementAtLocalPosition(LastPointerPosition, Hovered); SetHoveredElement(bFound ? &Hovered : nullptr);
	}
}

int32 UWorldMapWidget::NativePaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& Draw, int32 LayerId, const FWidgetStyle& WidgetStyle, bool bParentEnabled) const
{
	const FSlateBrush* White = FCoreStyle::Get().GetBrush(TEXT("WhiteBrush"));
	FSlateDrawElement::MakeBox(Draw, LayerId, Geometry.ToPaintGeometry(), White, ESlateDrawEffect::None, GetViewConfig()->BackgroundColor * WidgetStyle.GetColorAndOpacityTint());
	const FVector2D Size = Geometry.GetLocalSize();
	int32 Layer = LayerId + 1;
	for (const FWorldMapElement& E : CachedElements)
	{
		if (!ShouldDisplay(E)) continue;
		const UWorldMapStyleData* Style = GetElementStyle(E);
		FLinearColor Tint = WidgetStyle.GetColorAndOpacityTint();
		if (E.bLastKnown) Tint.A *= Style->LastKnownOpacity;
		const FSlateBrush* Brush = Style->Icon.GetResourceObject() ? &Style->Icon : White;
		if (E.Definition->Shape->Shape == EWorldMapShape::Point)
		{
			const FVector2D P = WorldMap::WorldToMap(FVector2D(E.Transform.GetLocation()), MapCenter, Size, Zoom, Rotation);
			const float Extent = FMath::Max(1.f, Style->MarkerSize);
			if (P.X < -Extent || P.Y < -Extent || P.X > Size.X + Extent || P.Y > Size.Y + Extent) continue;
			FSlateDrawElement::MakeBox(Draw, Layer++, Geometry.ToPaintGeometry(FVector2f(Extent, Extent), FSlateLayoutTransform(FVector2f(P - FVector2D(Extent * 0.5)))), Brush, ESlateDrawEffect::None, Style->OutlineColor * Tint);
			continue;
		}
		TArray<FVector2D> Points; ProjectElement(E, Size, Points);
		if (Points.IsEmpty()) continue;
		FBox2D Bounds(Points);
		if (!Bounds.Intersect(FBox2D(FVector2D(-Style->LineThickness), Size + FVector2D(Style->LineThickness)))) continue;
		if (const TArray<int32>* Indices = Triangles.Find(E.Definition->Shape))
		{
			TArray<FSlateVertex> Vertices;
			TArray<SlateIndex> SlateIndices;
			TArray<FVector2D> LocalPoints; WorldMap::GetLocalPoints(E.Definition->Shape, LocalPoints);
			const FBox2D LocalBounds(LocalPoints);
			const FVector2D UVSize = LocalBounds.GetSize().ComponentMax(FVector2D(0.001));
			for (int32 I = 0; I < Points.Num(); ++I)
			{
				const FVector2D UV = (LocalPoints[I] - LocalBounds.Min) / UVSize;
				Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Geometry.GetAccumulatedRenderTransform(), FVector2f(Points[I]), FVector2f(UV), (Style->FillColor * Tint).ToFColor(true)));
			}
			for (int32 Index : *Indices) SlateIndices.Add(static_cast<SlateIndex>(Index));
			FSlateDrawElement::MakeCustomVerts(Draw, Layer++, FSlateApplication::Get().GetRenderer()->GetResourceHandle(*Brush), Vertices, SlateIndices, nullptr, 0, 0);
		}
		if (E.Definition->Shape->Shape != EWorldMapShape::Polyline) Points.Add(Points[0]);
		if (Style->LineThickness > 0) FSlateDrawElement::MakeLines(Draw, Layer++, Geometry.ToPaintGeometry(), Points, ESlateDrawEffect::None, Style->OutlineColor * Tint, true, Style->LineThickness);
	}
	if (IsValid(BoundViewer) && DisplayedMap == BoundViewer->GetCurrentMap() && DisplayedFloor == BoundViewer->GetCurrentFloor())
	{
		if (APawn* Pawn = GetOwningPlayerPawn())
		{
			const FVector2D P = WorldMap::WorldToMap(FVector2D(Pawn->GetActorLocation()), MapCenter, Size, Zoom, Rotation);
			const double Heading = Pawn->GetActorRotation().Yaw - Rotation;
			const double Radius = GetViewConfig()->PlayerSize;
			TArray<FVector2D> Triangle = { P + FVector2D(0, -Radius).GetRotated(Heading), P + FVector2D(-Radius * 0.65, Radius * 0.65).GetRotated(Heading), P + FVector2D(Radius * 0.65, Radius * 0.65).GetRotated(Heading) };
			Triangle.Add(Triangle[0]);
			FSlateDrawElement::MakeLines(Draw, Layer++, Geometry.ToPaintGeometry(), Triangle, ESlateDrawEffect::None, GetViewConfig()->PlayerColor * WidgetStyle.GetColorAndOpacityTint(), true, GetViewConfig()->PlayerLineThickness);
		}
	}
	return Super::NativePaint(Args, Geometry, CullingRect, Draw, Layer, WidgetStyle, bParentEnabled);
}

void UWorldMapWidget::SetHoveredElement(const FWorldMapElement* Element)
{
	const FGuid Id = Element ? Element->Id : FGuid();
	if (Id == HoveredId) return;
	HoveredId = Id;
	SetToolTip(nullptr); SetToolTipText(FText::GetEmpty()); ActiveTooltip = nullptr;
	if (Element && Element->Definition)
	{
		if (Element->Definition->TooltipClass)
		{
			ActiveTooltip = CreateWidget<UWorldMapTooltipWidget>(GetOwningPlayer(), Element->Definition->TooltipClass);
			if (ActiveTooltip) { ActiveTooltip->SetMapElement(*Element); SetToolTip(ActiveTooltip); }
		}
		else SetToolTipText(Element->Definition->DisplayName);
	}
	OnHoveredElementChanged.Broadcast(Element != nullptr, Element ? *Element : FWorldMapElement());
}

FReply UWorldMapWidget::NativeOnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!IsInteractiveMap() || Event.GetEffectingButton() != EKeys::LeftMouseButton) return Super::NativeOnMouseButtonDown(Geometry, Event);
	bPointerDown = true; bDragging = false; DragStart = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()); DragCenter = MapCenter;
	return FReply::Handled().CaptureMouse(TakeWidget());
}

FReply UWorldMapWidget::NativeOnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!bPointerDown || Event.GetEffectingButton() != EKeys::LeftMouseButton) return Super::NativeOnMouseButtonUp(Geometry, Event);
	bPointerDown = false;
	if (!bDragging)
	{
		const FVector2D P = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
		FWorldMapElement Element;
		if (FindElementAtLocalPosition(P, Element)) OnElementClicked.Broadcast(Element); else OnMapClicked.Broadcast(WidgetToWorldPosition(P));
	}
	bDragging = false;
	return FReply::Handled().ReleaseMouseCapture();
}

FReply UWorldMapWidget::NativeOnMouseMove(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!IsInteractiveMap()) return Super::NativeOnMouseMove(Geometry, Event);
	LastPointerPosition = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()); bPointerInside = true;
	if (bPointerDown && (bDragging || FVector2D::Distance(DragStart, LastPointerPosition) > GetViewConfig()->DragThreshold))
	{
		bDragging = true; bFollowPlayer = false; bFollowFloor = false;
		MapCenter = DragCenter + WorldMap::MapToWorld(DragStart, FVector2D::ZeroVector, Geometry.GetLocalSize(), Zoom, Rotation)
			- WorldMap::MapToWorld(LastPointerPosition, FVector2D::ZeroVector, Geometry.GetLocalSize(), Zoom, Rotation);
		SetHoveredElement(nullptr);
	}
	return FReply::Handled();
}

FReply UWorldMapWidget::NativeOnMouseWheel(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (!IsInteractiveMap()) return Super::NativeOnMouseWheel(Geometry, Event);
	ZoomAtLocalPosition(Zoom * FMath::Pow(FMath::Max(1.01f, GetViewConfig()->WheelZoomFactor), Event.GetWheelDelta()), Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition()));
	return FReply::Handled();
}

void UWorldMapWidget::NativeOnMouseLeave(const FPointerEvent& Event) { bPointerInside = false; SetHoveredElement(nullptr); Super::NativeOnMouseLeave(Event); }
void UWorldMapWidget::NativeOnMouseCaptureLost(const FCaptureLostEvent& Event) { bPointerDown = false; bDragging = false; Super::NativeOnMouseCaptureLost(Event); }

void UWorldMapWidget::RequestPingAtLocalPosition(UWorldMapPingData* Definition, FVector2D Position)
{
	if (IsValid(BoundViewer) && DisplayedMap && DisplayedFloor) BoundViewer->ServerRequestPing(Definition, DisplayedMap, DisplayedFloor, FVector2D(WidgetToWorldPosition(Position)));
}
