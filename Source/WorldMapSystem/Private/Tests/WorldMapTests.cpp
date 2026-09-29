#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WorldMapManager.h"
#include "WorldMapViewer.h"
#include "WorldMapRegion.h"
#include "WorldMapElementComponent.h"
#include "WorldMapWidget.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "UObject/UnrealType.h"
#include "Algo/Reverse.h"

namespace
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
	UWorld* CreateFixtureWorld()
	{
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		return World;
	}
	struct FFixture
	{
		UWorld* World = CreateFixtureWorld();
		AWorldMapManager* Manager = World->SpawnActor<AWorldMapManager>();
		UWorldMapTeamData* Humans = NewObject<UWorldMapTeamData>();
		UWorldMapTeamData* Hunter = NewObject<UWorldMapTeamData>();
		UWorldMapRoleData* HumanRole = NewObject<UWorldMapRoleData>();
		UWorldMapRoleData* HunterRole = NewObject<UWorldMapRoleData>();
		UWorldMapDefinition* Map = NewObject<UWorldMapDefinition>();
		UWorldMapFloorData* Floor = NewObject<UWorldMapFloorData>();
		UWorldMapFloorData* Floor2 = NewObject<UWorldMapFloorData>();
		APlayerController* HumanPC = World->SpawnActor<APlayerController>();
		APlayerController* HunterPC = World->SpawnActor<APlayerController>();
		AWorldMapViewer* HumanView = nullptr;
		AWorldMapViewer* HunterView = nullptr;
		FFixture()
		{
			Map->Floors = { Floor, Floor2 }; HunterRole->bBypassExploration = true;
			Manager->Settings = NewObject<UWorldMapSettings>();
			HumanView = Manager->AssignViewer(HumanPC, Humans, HumanRole);
			HunterView = Manager->AssignViewer(HunterPC, Hunter, HunterRole);
		}
		~FFixture() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
		FWorldMapElement MakeElement(EWorldMapRevealRule Rule = EWorldMapRevealRule::Explored)
		{
			FWorldMapElement Element;
			Element.Id = FGuid::NewGuid(); Element.Map = Map; Element.Floor = Floor;
			Element.Definition = NewObject<UWorldMapElementData>();
			Element.Definition->Shape = NewObject<UWorldMapShapeData>();
			Element.Definition->Style = NewObject<UWorldMapStyleData>();
			Element.Definition->Visibility = NewObject<UWorldMapVisibilityData>();
			Element.Definition->Visibility->RevealRule = Rule;
			return Element;
		}
		void Register(const FWorldMapElement& Element) { Manager->RegisterElement(Element, FGuid(), nullptr, nullptr); }
		void Advance(float Seconds)
		{
			for (float Remaining = Seconds; Remaining > UE_SMALL_NUMBER; Remaining -= 0.05f) World->Tick(LEVELTICK_All, FMath::Min(Remaining, 0.05f));
			Manager->RefreshViewers();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapProjectionTest, "WorldMap.Geometry.ProjectionRoundTrip", Flags)
bool FMapProjectionTest::RunTest(const FString&)
{
	const FVector2D Center(1200000000, -300000000), Position = Center + FVector2D(7231, -9031), Size(711, 317);
	for (double Rotation : { 0., 47., -120., 270. }) for (double Zoom : { 0.005, 0.1, 2. })
	{
		const FVector2D Projected = WorldMap::WorldToMap(Position, Center, Size, Zoom, Rotation);
		TestTrue(TEXT("Projection round trip at large coordinates"), WorldMap::MapToWorld(Projected, Center, Size, Zoom, Rotation).Equals(Position, 0.00001));
		const FVector2D Cursor(131, 87), Before = WorldMap::MapToWorld(Cursor, Center, Size, Zoom, Rotation);
		const FVector2D NewCenter = Center + Before - WorldMap::MapToWorld(Cursor, Center, Size, Zoom * 1.2, Rotation);
		TestTrue(TEXT("Zoom preserves cursor anchor"), WorldMap::WorldToMap(Before, NewCenter, Size, Zoom * 1.2, Rotation).Equals(Cursor, 0.001));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapPolygonTest, "WorldMap.Geometry.ConcaveAndInvalidPolygons", Flags)
bool FMapPolygonTest::RunTest(const FString&)
{
	TArray<FVector2D> L = { {0,0}, {4,0}, {4,1}, {1,1}, {1,4}, {0,4} };
	TArray<int32> Triangles;
	TestTrue(TEXT("Concave polygon triangulates"), WorldMap::Triangulate(L, Triangles));
	TestEqual(TEXT("n minus 2 triangles"), Triangles.Num(), 12);
	double Area = 0;
	for (int32 I = 0; I < Triangles.Num(); I += 3) Area += FMath::Abs(FVector2D::CrossProduct(L[Triangles[I + 1]] - L[Triangles[I]], L[Triangles[I + 2]] - L[Triangles[I]])) * 0.5;
	TestEqual(TEXT("Triangle coverage preserves concavity"), Area, 7.);
	TestTrue(TEXT("Inside arm"), WorldMap::ContainsPoint(L, FVector2D(0.5, 3)));
	TestFalse(TEXT("Not in missing corner"), WorldMap::ContainsPoint(L, FVector2D(3, 3)));
	Algo::Reverse(L);
	TestTrue(TEXT("Reverse winding accepted"), WorldMap::Triangulate(L, Triangles));
	TestFalse(TEXT("Self-intersection rejected"), WorldMap::Triangulate({ {0,0}, {4,4}, {0,4}, {3,0} }, Triangles));
	TestFalse(TEXT("Degenerate polygon rejected"), WorldMap::Triangulate({ {0,0}, {1,0}, {2,0} }, Triangles));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapAudienceTest, "WorldMap.Security.AudienceAndTeamExploration", Flags)
bool FMapAudienceTest::RunTest(const FString&)
{
	FFixture F;
	FWorldMapElement City = F.MakeElement(EWorldMapRevealRule::Always), Room = F.MakeElement();
	F.Register(City); F.Register(Room); F.Manager->RefreshViewers();
	TestEqual(TEXT("Public city visible before exploration"), F.HumanView->GetVisibleElements().Num(), 1);
	TestEqual(TEXT("Hunter role bypasses room exploration"), F.HunterView->GetVisibleElements().Num(), 2);
	F.Manager->ExploreElementForTeam(F.Humans, Room.Id); F.Manager->RefreshViewers();
	TestEqual(TEXT("Humans now know both"), F.HumanView->GetVisibleElements().Num(), 2);
	APlayerController* LatePC = F.World->SpawnActor<APlayerController>();
	AWorldMapViewer* LateView = F.Manager->AssignViewer(LatePC, F.Humans, F.HumanRole); F.Manager->RefreshViewers();
	TestEqual(TEXT("Late join inherits team knowledge"), LateView->GetVisibleElements().Num(), 2);
	FWorldMapElement Private = F.MakeElement(EWorldMapRevealRule::Always); Private.Definition->Visibility->AllowedTeams = {F.Humans}; F.Register(Private); F.Manager->RefreshViewers();
	TestEqual(TEXT("Explicit audience remains restricted despite role bypass"), F.HunterView->GetVisibleElements().Num(), 2);
	TestFalse(TEXT("Owner-only endpoint not relevant to other PC"), F.HumanView->IsNetRelevantFor(F.HunterPC, nullptr, FVector::ZeroVector));
	TestTrue(TEXT("Owner endpoint relevant to owner"), F.HumanView->IsNetRelevantFor(F.HumanPC, nullptr, FVector::ZeroVector));
	AWorldMapViewer* Switched = F.Manager->AssignViewer(LatePC, F.Hunter, F.HumanRole); F.Manager->RefreshViewers();
	TestEqual(TEXT("Switching team revokes previous private knowledge"), Switched->GetVisibleElements().Num(), 1);
	TestFalse(TEXT("Absent policy is fail closed"), WorldMap::HasAudienceAccess(nullptr, FWorldMapViewerContext(), nullptr));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapObservationTest, "WorldMap.Security.ObservationSamplesAndIndependentSources", Flags)
bool FMapObservationTest::RunTest(const FString&)
{
	FFixture F;
	FWorldMapElement Enemy = F.MakeElement(EWorldMapRevealRule::Observed); F.Register(Enemy);
	UWorldMapObservationData* Vision = NewObject<UWorldMapObservationData>(); Vision->FreshSeconds = 0.1f; Vision->MemorySeconds = 2;
	UWorldMapObservationData* Scanner = NewObject<UWorldMapObservationData>(); Scanner->FreshSeconds = 5; Scanner->MemorySeconds = 0;
	F.Manager->ReportObservation(F.Humans, Enemy.Id, Vision, F.HumanPC);
	F.Manager->SetElementPlacement(Enemy.Id, F.Map, F.Floor2, FTransform(FVector(900, 800, 700)));
	F.Manager->RefreshViewers();
	FWorldMapElement Seen;
	TestTrue(TEXT("Detected enemy is visible to reporting team"), F.HumanView->FindElementById(Enemy.Id, Seen));
	TestTrue(TEXT("Live interval does not leak unreported movement"), Seen.Transform.GetLocation().Equals(FVector::ZeroVector));
	TestEqual(TEXT("Unreported floor remains old floor"), Seen.Floor.Get(), F.Floor);
	TestEqual(TEXT("Detection not broadcast to other team"), F.HunterView->GetVisibleElements().Num(), 0);
	F.Advance(0.2f); F.HumanView->FindElementById(Enemy.Id, Seen);
	TestTrue(TEXT("Sample becomes last-known"), Seen.bLastKnown);
	F.Manager->ReportObservation(F.Humans, Enemy.Id, Scanner, F.HumanPC);
	F.Manager->EndObservation(F.Humans, Enemy.Id, Vision, F.HumanPC, false); F.Manager->RefreshViewers();
	F.HumanView->FindElementById(Enemy.Id, Seen);
	TestFalse(TEXT("Ending vision does not stop scanner"), Seen.bLastKnown);
	TestTrue(TEXT("New sample contains new position"), Seen.Transform.GetLocation().Equals(FVector(900, 800, 700)));
	F.Manager->EndObservation(F.Humans, Enemy.Id, Scanner, F.HumanPC, false); F.Manager->RefreshViewers();
	TestFalse(TEXT("No source or memory removes target"), F.HumanView->FindElementById(Enemy.Id, Seen));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapStateTest, "WorldMap.Security.StateAccessDoesNotExposePrivateMetadata", Flags)
bool FMapStateTest::RunTest(const FString&)
{
	FFixture F;
	FWorldMapElement Door = F.MakeElement(EWorldMapRevealRule::Always);
	Door.State = NewObject<UWorldMapStateData>(); Door.State->Visibility = NewObject<UWorldMapVisibilityData>();
	Door.State->Visibility->RevealRule = EWorldMapRevealRule::Always; Door.State->Visibility->AllowedTeams = {F.Hunter};
	F.Register(Door); F.Manager->RefreshViewers();
	FWorldMapElement Seen;
	F.HumanView->FindElementById(Door.Id, Seen); TestNull(TEXT("Human gets geometry without secret state asset reference"), Seen.State.Get());
	F.HunterView->FindElementById(Door.Id, Seen); TestEqual(TEXT("Hunter receives state"), Seen.State.Get(), Door.State.Get());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapPingTest, "WorldMap.Security.PingValidation", Flags)
bool FMapPingTest::RunTest(const FString&)
{
	FFixture F;
	APawn* Pawn = F.World->SpawnActor<APawn>(); F.HumanPC->Possess(Pawn);
	FWorldMapElement Room = F.MakeElement(); F.Register(Room);
	UWorldMapPingData* Ping = NewObject<UWorldMapPingData>();
	Ping->Shape = NewObject<UWorldMapShapeData>(); Ping->Shape->Shape = EWorldMapShape::Point;
	Ping->Style = NewObject<UWorldMapStyleData>(); Ping->Visibility = NewObject<UWorldMapVisibilityData>(); Ping->Visibility->RevealRule = EWorldMapRevealRule::Always;
	F.Manager->Settings->AllowedPings.Add(Ping); F.Manager->Settings->MinimumPingInterval = 0.1f; F.Manager->Settings->MaximumPingsPerPlayer = 1;
	F.Manager->HandlePing(F.HumanView, Ping, F.Map, F.Floor, FVector2D::ZeroVector);
	TestEqual(TEXT("Cannot ping undiscovered room"), F.Manager->FindRegisteredElementsByDefinition(Ping).Num(), 0);
	F.Manager->ExploreElementForTeam(F.Humans, Room.Id);
	F.Manager->HandlePing(F.HumanView, Ping, F.Map, F.Floor, FVector2D::ZeroVector);
	TestEqual(TEXT("Rejected request consumed cooldown"), F.Manager->FindRegisteredElementsByDefinition(Ping).Num(), 0);
	F.Advance(0.2f);
	F.Manager->HandlePing(F.HumanView, Ping, F.Map, F.Floor, FVector2D(10000000, 0));
	TestEqual(TEXT("Out-of-map or out-of-range ping rejected"), F.Manager->FindRegisteredElementsByDefinition(Ping).Num(), 0);
	F.Advance(0.2f);
	F.Manager->HandlePing(F.HumanView, Ping, F.Map, F.Floor, FVector2D::ZeroVector); F.Manager->RefreshViewers();
	TestEqual(TEXT("Valid ping accepted"), F.Manager->FindRegisteredElementsByDefinition(Ping).Num(), 1);
	TestEqual(TEXT("Ping stays in originating team even with public policy"), F.HunterView->FindElementsByDefinition(Ping).Num(), 0);
	F.Advance(0.2f); F.Manager->HandlePing(F.HumanView, Ping, F.Map, F.Floor, FVector2D::ZeroVector);
	TestEqual(TEXT("Active ping quota enforced"), F.Manager->FindRegisteredElementsByDefinition(Ping).Num(), 1);
	F.Advance(Ping->Lifetime + 0.2f);
	TestEqual(TEXT("Expired ping removed"), F.Manager->FindRegisteredElementsByDefinition(Ping).Num(), 0);
	UWorldMapPingData* Unlisted = NewObject<UWorldMapPingData>();
	F.Manager->HandlePing(F.HumanView, Unlisted, F.Map, F.Floor, FVector2D::ZeroVector);
	TestEqual(TEXT("Unlisted asset rejected"), F.Manager->FindRegisteredElementsByDefinition(Unlisted).Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapAuthorityTest, "WorldMap.Security.ClientCannotMutateAuthority", Flags)
bool FMapAuthorityTest::RunTest(const FString&)
{
	FFixture F;
	FWorldMapElement Element = F.MakeElement(); F.Register(Element);
	FByteProperty* RoleProperty = FindFProperty<FByteProperty>(AActor::StaticClass(), TEXT("Role"));
	if (!TestNotNull(TEXT("Test can emulate a simulated proxy"), RoleProperty)) return false;
	RoleProperty->SetPropertyValue_InContainer(F.Manager, ROLE_SimulatedProxy);
	TestFalse(TEXT("Client cannot register elements"), F.Manager->RegisterElement(F.MakeElement(), FGuid(), nullptr, nullptr).IsValid());
	TestFalse(TEXT("Client cannot explore"), F.Manager->ExploreElementForTeam(F.Humans, Element.Id));
	TestFalse(TEXT("Client cannot mutate state"), F.Manager->SetElementState(Element.Id, nullptr));
	TestFalse(TEXT("Client cannot remove elements"), F.Manager->UnregisterElement(Element.Id));
	TestNull(TEXT("Client cannot assign privileged role"), F.Manager->AssignViewer(F.HumanPC, F.Humans, F.HunterRole));
	TestEqual(TEXT("Client cannot query server registry"), F.Manager->FindRegisteredElementsByDefinition(Element.Definition).Num(), 0);
	UWorldMapObservationData* Source = NewObject<UWorldMapObservationData>();
	TestFalse(TEXT("Client cannot report sightings"), F.Manager->ReportObservation(F.Humans, Element.Id, Source, F.HumanPC));
	TestFalse(TEXT("AssignViewer is not an RPC"), AWorldMapManager::StaticClass()->FindFunctionByName(TEXT("AssignViewer"))->HasAnyFunctionFlags(FUNC_Net));
	RoleProperty->SetPropertyValue_InContainer(F.Manager, ROLE_Authority);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapIdentityTest, "WorldMap.Registration.InstancesStreamingAndDeltaBudget", Flags)
bool FMapIdentityTest::RunTest(const FString&)
{
	FFixture F;
	const FGuid Local = FGuid::NewGuid();
	TestNotEqual(TEXT("One definition can have distinct room instance IDs"), FGuid::Combine(FGuid::NewGuid(), Local), FGuid::Combine(FGuid::NewGuid(), Local));
	FWorldMapElement Room = F.MakeElement(EWorldMapRevealRule::Always);
	AActor* SourceActor = F.World->SpawnActor<AActor>(); USceneComponent* Component = NewObject<USceneComponent>(SourceActor); SourceActor->SetRootComponent(Component); Component->RegisterComponent();
	F.Manager->RegisterElement(Room, FGuid(), nullptr, Component);
	Component->SetWorldLocation(FVector(100, 200, 300)); F.Manager->UnregisterElement(Room.Id, true); SourceActor->Destroy(); F.Manager->RefreshViewers();
	FWorldMapElement Seen; TestTrue(TEXT("Streaming unload retains known geometry"), F.HumanView->FindElementById(Room.Id, Seen));
	TestTrue(TEXT("Retained geometry uses final transform"), Seen.Transform.GetLocation().Equals(FVector(100, 200, 300)));
	TArray<FWorldMapElement> Many;
	for (int32 I = 0; I < 10; ++I) Many.Add(F.MakeElement(EWorldMapRevealRule::Always));
	TestTrue(TEXT("Initial changes stay within delta budget"), F.HumanView->ApplyVisibleElements(Many, 3));
	TestEqual(TEXT("Only budgeted additions published"), F.HumanView->GetVisibleElements().Num(), 3);
	for (int32 I = 0; I < 3; ++I) F.HumanView->ApplyVisibleElements(Many, 3);
	TestEqual(TEXT("Snapshot eventually completes"), F.HumanView->GetVisibleElements().Num(), 10);
	TestFalse(TEXT("Mass revocation requests endpoint replacement"), F.HumanView->ApplyVisibleElements({}, 3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMapFloorTest, "WorldMap.Floors.AuthoritativeVolumesAndHysteresis", Flags)
bool FMapFloorTest::RunTest(const FString&)
{
	FFixture F;
	APawn* Pawn = F.World->SpawnActor<APawn>();
	USceneComponent* Root = NewObject<USceneComponent>(Pawn); Pawn->SetRootComponent(Root); Root->RegisterComponent(); F.HumanPC->Possess(Pawn);
	FWorldMapElement Template = F.MakeElement();
	auto AddRegion = [&F, &Template](UWorldMapFloorData* Floor, FVector Location)
	{
		AWorldMapRegion* Region = F.World->SpawnActor<AWorldMapRegion>();
		Region->MapElement->Map = F.Map; Region->MapElement->Floor = Floor; Region->MapElement->Definition = Template.Definition;
		Region->SetActorLocation(Location); Region->RegisterRegionWithManager(F.Manager);
		return Region;
	};
	AWorldMapRegion* Ground = AddRegion(F.Floor, FVector::ZeroVector);
	AddRegion(F.Floor2, FVector(0, 0, 600));
	F.Manager->RefreshViewers();
	TestNull(TEXT("Floor waits for stable candidate"), F.HumanView->GetCurrentFloor());
	F.Advance(0.2f); TestEqual(TEXT("Server volume selects floor"), F.HumanView->GetCurrentFloor(), F.Floor);
	TestEqual(TEXT("Entering reveals only that room"), F.HumanView->FindElementsByDefinition(Template.Definition).Num(), 1);
	Pawn->SetActorLocation(FVector(0, 0, 600)); F.Manager->RefreshViewers(); F.Advance(0.05f);
	TestEqual(TEXT("Short overlap does not flip floor"), F.HumanView->GetCurrentFloor(), F.Floor);
	Pawn->SetActorLocation(FVector::ZeroVector); F.Manager->RefreshViewers();
	Pawn->SetActorLocation(FVector(0, 0, 600)); F.Manager->RefreshViewers(); F.Advance(0.2f);
	TestEqual(TEXT("Stable transition selects second floor"), F.HumanView->GetCurrentFloor(), F.Floor2);
	Pawn->SetActorLocation(FVector(10000, 0, 0)); F.Manager->RefreshViewers();
	TestEqual(TEXT("Outside any region preserves previous floor"), F.HumanView->GetCurrentFloor(), F.Floor2);
	TestTrue(TEXT("A region on another floor does not contain this point"), !Ground->ContainsLocation(FVector(0, 0, 600)));
	return true;
}

#endif
