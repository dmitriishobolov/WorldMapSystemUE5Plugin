"""Run inside Unreal Editor: Tools > Execute Python Script.

Creates missing starter assets in /Game/WorldMapStarter. Existing assets are
never modified. Requires Python Editor Script Plugin; the runtime plugin does not.
No level, GameMode or project configuration is changed.
"""
import unreal

ROOT = "/Game/WorldMapStarter"
tools = unreal.AssetToolsHelpers.get_asset_tools()
created = []


def data(name, cls, description, **properties):
    path = ROOT + "/Data/" + name
    existing = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if existing:
        return existing
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", cls)
    asset = tools.create_asset(name, ROOT + "/Data", cls, factory)
    if not asset:
        raise RuntimeError("Could not create " + path)
    asset.set_editor_property("display_name", name.removeprefix("DA_"))
    asset.set_editor_property("description", description)
    for key, value in properties.items():
        asset.set_editor_property(key, value)
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    created.append(path)
    return asset


def blueprint(name, parent, widget=False, configure=None):
    path = ROOT + "/Blueprints/" + name
    existing = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
    if existing:
        return existing
    factory = unreal.WidgetBlueprintFactory() if widget else unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent)
    asset_class = unreal.WidgetBlueprint if widget else unreal.Blueprint
    asset = tools.create_asset(name, ROOT + "/Blueprints", asset_class, factory)
    if not asset:
        raise RuntimeError("Could not create " + path)
    unreal.BlueprintEditorLibrary.compile_blueprint(asset)
    if configure:
        configure(unreal.get_default_object(asset.generated_class()))
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    created.append(path)
    return asset


humans = data("DA_Team_Humans", unreal.WorldMapTeamData, "Shared exploration and sightings for the human team.")
hunter = data("DA_Team_Hunter", unreal.WorldMapTeamData, "Separate information audience for the hunter.")
human_role = data("DA_Role_Human", unreal.WorldMapRoleData, "Rooms require exploration.")
hunter_role = data("DA_Role_Hunter", unreal.WorldMapRoleData, "Knows unexplored rooms, but does not bypass audience or detection rules.", bypass_exploration=True)
ground = data("DA_Floor_Ground", unreal.WorldMapFloorData, "Ground floor.", sort_order=0)
basement = data("DA_Floor_Basement", unreal.WorldMapFloorData, "Basement, 600 cm below ground.", sort_order=-1, reference_world_z=-600.0)
map_asset = data("DA_Map_Complex", unreal.WorldMapDefinition, "Example two-floor complex.", floors=[ground, basement])
public = data("DA_Visibility_Public", unreal.WorldMapVisibilityData, "Public geometry visible from the start.", reveal_rule=unreal.WorldMapRevealRule.ALWAYS)
explored = data("DA_Visibility_Explored", unreal.WorldMapVisibilityData, "Requires team exploration, or a role with an exploration bypass.", reveal_rule=unreal.WorldMapRevealRule.EXPLORED)
observed = data("DA_Visibility_Observed", unreal.WorldMapVisibilityData, "Only observation snapshots are sent to the detecting team.", reveal_rule=unreal.WorldMapRevealRule.OBSERVED)
room_shape = data("DA_Shape_Room", unreal.WorldMapShapeData, "1000 x 1000 cm local XY rectangle.", half_extent=unreal.Vector2D(500, 500))
point_shape = data("DA_Shape_Point", unreal.WorldMapShapeData, "Constant-size map marker.", shape=unreal.WorldMapShape.POINT)
room_style = data("DA_Style_Room", unreal.WorldMapStyleData, "Default cyan room footprint.")
enemy_style = data("DA_Style_Enemy", unreal.WorldMapStyleData, "Red enemy marker; dims when last-known.", outline_color=unreal.LinearColor(1.0, 0.15, 0.1, 1.0), marker_size=14.0)
ping_style = data("DA_Style_Ping", unreal.WorldMapStyleData, "Yellow team ping.", outline_color=unreal.LinearColor(1.0, 0.8, 0.1, 1.0), marker_size=18.0)
room = data("DA_Element_Room", unreal.WorldMapElementData, "Explorable room. Multiple instances use distinct runtime GUIDs.", shape=room_shape, style=room_style, visibility=explored)
city = data("DA_Element_PublicArea", unreal.WorldMapElementData, "Always-known public area. Scale its component to cover a larger area.", shape=room_shape, style=room_style, visibility=public, draw_order=-10)
enemy = data("DA_Element_Enemy", unreal.WorldMapElementData, "Server detection produces snapshots of this marker.", shape=point_shape, style=enemy_style, visibility=observed, draw_order=20, resolve_floor_from_regions=True)
ping = data("DA_Ping_Default", unreal.WorldMapPingData, "Server-validated team ping, 8 second lifetime.", shape=point_shape, style=ping_style, visibility=public, draw_order=30)
vision = data("DA_Observation_Vision", unreal.WorldMapObservationData, "Report while visible, at intervals shorter than 0.3 seconds. Memory lasts 5 seconds.")
region = data("DA_Region_Default", unreal.WorldMapRegionData, "Independent 1000 x 1000 x 400 cm membership volume.")
settings = data("DA_Settings", unreal.WorldMapSettings, "Server refresh, floor hysteresis and ping limits.", allowed_pings=[ping])
minimap = data("DA_View_Minimap", unreal.WorldMapViewData, "Following non-interactive minimap.", follow_player=True, interactive=False, default_size=unreal.Vector2D(240, 240))
fullmap = data("DA_View_FullMap", unreal.WorldMapViewData, "Interactive north-up map with drag and cursor-centered zoom.", follow_player=False, interactive=True)

blueprint("BP_MapManager", unreal.WorldMapManager, configure=lambda obj: obj.set_editor_property("settings", settings))


def configure_room(obj):
    obj.set_editor_property("region_settings", region)
    component = obj.get_editor_property("map_element")
    component.set_editor_property("map", map_asset)
    component.set_editor_property("floor", ground)
    component.set_editor_property("definition", room)


blueprint("BP_MapRegion", unreal.WorldMapRegion, configure=configure_room)
blueprint("BP_MapViewer", unreal.WorldMapViewer)
blueprint("BP_MapElementComponent", unreal.WorldMapElementComponent)
blueprint("BP_ElementDefinition", unreal.WorldMapElementData)
blueprint("WBP_Minimap", unreal.WorldMapMinimapWidget, True, lambda obj: obj.set_editor_property("view_settings", minimap))
blueprint("WBP_FullMap", unreal.WorldMapFullMapWidget, True, lambda obj: obj.set_editor_property("view_settings", fullmap))
blueprint("WBP_MapTooltip", unreal.WorldMapTooltipWidget, True)
unreal.log("WorldMap starter: created {} assets; existing assets preserved.".format(len(created)))
for path in created:
    unreal.log("  " + path)
