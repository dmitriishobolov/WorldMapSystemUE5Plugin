#include "WorldMapTypes.h"

bool FWorldMapElement::SamePresentation(const FWorldMapElement& Other) const
{
	return Id == Other.Id && Map == Other.Map && Floor == Other.Floor && Definition == Other.Definition
		&& State == Other.State && bLastKnown == Other.bLastKnown && Transform.Equals(Other.Transform, 0.01);
}

bool WorldMap::HasAudienceAccess(const UWorldMapVisibilityData* Policy, const FWorldMapViewerContext& Context, const UWorldMapTeamData* OwningTeam)
{
	return Policy && (Policy->AllowedTeams.IsEmpty() || (Context.Team && Policy->AllowedTeams.Contains(Context.Team)))
		&& (Policy->AllowedRoles.IsEmpty() || (Context.Role && Policy->AllowedRoles.Contains(Context.Role)))
		&& (!Policy->bOnlyOwningTeam || (OwningTeam && Context.Team == OwningTeam));
}

void WorldMap::GetLocalPoints(const UWorldMapShapeData* Shape, TArray<FVector2D>& Out)
{
	Out.Reset();
	if (!Shape) return;
	if (Shape->Shape == EWorldMapShape::Rectangle)
	{
		const FVector2D E = Shape->HalfExtent;
		Out = { {-E.X, -E.Y}, {E.X, -E.Y}, {E.X, E.Y}, {-E.X, E.Y} };
	}
	else Out = Shape->Points;
}

namespace
{
	double Cross(FVector2D A, FVector2D B, FVector2D C) { return FVector2D::CrossProduct(B - A, C - A); }
	bool InTriangle(FVector2D P, FVector2D A, FVector2D B, FVector2D C)
	{
		return Cross(A, B, P) >= -UE_DOUBLE_SMALL_NUMBER && Cross(B, C, P) >= -UE_DOUBLE_SMALL_NUMBER && Cross(C, A, P) >= -UE_DOUBLE_SMALL_NUMBER;
	}
	bool OnSegment(FVector2D A, FVector2D B, FVector2D P)
	{
		return FMath::Abs(Cross(A, B, P)) < UE_DOUBLE_SMALL_NUMBER && P.X >= FMath::Min(A.X, B.X) && P.X <= FMath::Max(A.X, B.X) && P.Y >= FMath::Min(A.Y, B.Y) && P.Y <= FMath::Max(A.Y, B.Y);
	}
	bool SegmentsIntersect(FVector2D A, FVector2D B, FVector2D C, FVector2D D)
	{
		return (Cross(A, B, C) * Cross(A, B, D) < 0 && Cross(C, D, A) * Cross(C, D, B) < 0)
			|| OnSegment(A, B, C) || OnSegment(A, B, D) || OnSegment(C, D, A) || OnSegment(C, D, B);
	}
}

bool WorldMap::Triangulate(const TArray<FVector2D>& Points, TArray<int32>& OutIndices)
{
	OutIndices.Reset();
	if (Points.Num() < 3 || Points.Num() > 256) return false;
	double Area = 0;
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		if (Points[I].ContainsNaN()) return false;
		Area += FVector2D::CrossProduct(Points[I], Points[(I + 1) % Points.Num()]);
	}
	if (FMath::Abs(Area) < UE_DOUBLE_SMALL_NUMBER) return false;
	for (int32 I = 0; I < Points.Num(); ++I)
	{
		const int32 NextI = (I + 1) % Points.Num();
		if (Points[I].Equals(Points[NextI], UE_DOUBLE_SMALL_NUMBER)) return false;
		for (int32 J = I + 1; J < Points.Num(); ++J)
		{
			const int32 NextJ = (J + 1) % Points.Num();
			if (NextI != J && NextJ != I && SegmentsIntersect(Points[I], Points[NextI], Points[J], Points[NextJ])) return false;
		}
	}
	TArray<int32> Remaining;
	for (int32 I = 0; I < Points.Num(); ++I) Remaining.Add(Area > 0 ? I : Points.Num() - 1 - I);
	while (Remaining.Num() > 3)
	{
		bool bClipped = false;
		for (int32 I = 0; I < Remaining.Num(); ++I)
		{
			const int32 A = Remaining[(I + Remaining.Num() - 1) % Remaining.Num()], B = Remaining[I], C = Remaining[(I + 1) % Remaining.Num()];
			if (Cross(Points[A], Points[B], Points[C]) <= UE_DOUBLE_SMALL_NUMBER) continue;
			bool bOccupied = false;
			for (int32 V : Remaining) if (V != A && V != B && V != C && InTriangle(Points[V], Points[A], Points[B], Points[C])) { bOccupied = true; break; }
			if (bOccupied) continue;
			OutIndices.Append({ A, B, C });
			Remaining.RemoveAt(I);
			bClipped = true;
			break;
		}
		if (!bClipped) { OutIndices.Reset(); return false; }
	}
	OutIndices.Append(Remaining);
	return true;
}

bool WorldMap::IsValidElement(const FWorldMapElement& Element)
{
	if (!Element.Id.IsValid() || !Element.Map || !Element.Floor || !Element.Map->Floors.Contains(Element.Floor)
		|| !Element.Definition || !Element.Definition->Visibility || !Element.Definition->Shape || !Element.Definition->Style
		|| !Element.Transform.IsValid() || Element.Transform.GetScale3D().GetAbsMin() < UE_SMALL_NUMBER) return false;
	const UWorldMapShapeData* Shape = Element.Definition->Shape;
	if (Shape->Shape == EWorldMapShape::Point) return true;
	if (Shape->Shape == EWorldMapShape::Rectangle) return !Shape->HalfExtent.ContainsNaN() && Shape->HalfExtent.X > 0 && Shape->HalfExtent.Y > 0;
	if (Shape->Points.Num() > 256) return false;
	for (const FVector2D& P : Shape->Points) if (P.ContainsNaN()) return false;
	if (Shape->Shape == EWorldMapShape::Polyline) return Shape->Points.Num() >= 2;
	TArray<int32> Indices;
	return Triangulate(Shape->Points, Indices);
}

bool WorldMap::ContainsPoint(const TArray<FVector2D>& Polygon, FVector2D P)
{
	bool bInside = false;
	for (int32 I = 0, J = Polygon.Num() - 1; I < Polygon.Num(); J = I++)
	{
		const FVector2D A = Polygon[I], B = Polygon[J];
		if ((A.Y > P.Y) != (B.Y > P.Y) && P.X < (B.X - A.X) * (P.Y - A.Y) / (B.Y - A.Y) + A.X) bInside = !bInside;
	}
	return bInside;
}

bool WorldMap::ContainsWorldXY(const FWorldMapElement& Element, FVector2D Point)
{
	if (!Element.Definition || !Element.Definition->Shape) return false;
	const EWorldMapShape Shape = Element.Definition->Shape->Shape;
	if (Shape == EWorldMapShape::Point || Shape == EWorldMapShape::Polyline) return false;
	TArray<FVector2D> Points;
	GetLocalPoints(Element.Definition->Shape, Points);
	for (FVector2D& P : Points) P = FVector2D(Element.Transform.TransformPosition(FVector(P, 0)));
	return ContainsPoint(Points, Point);
}

FVector2D WorldMap::WorldToMap(FVector2D World, FVector2D Center, FVector2D Size, double Zoom, double Rotation)
{
	const FVector2D P = (World - Center).GetRotated(-Rotation);
	return Size * 0.5 + FVector2D(P.Y, -P.X) * Zoom;
}

FVector2D WorldMap::MapToWorld(FVector2D Local, FVector2D Center, FVector2D Size, double Zoom, double Rotation)
{
	const FVector2D P = (Local - Size * 0.5) / FMath::Max(Zoom, 0.000001);
	return Center + FVector2D(-P.Y, P.X).GetRotated(Rotation);
}
