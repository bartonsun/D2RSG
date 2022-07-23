#include "decoration.h"
#include "capital.h"
#include "containers.h"
#include "crystal.h"
#include "gameinfo.h"
#include "landmark.h"
#include "map.h"
#include "mapgenerator.h"
#include "player.h"
#include "ruin.h"
#include "site.h"
#include "templatezone.h"
#include "village.h"

bool Decoration::decorate(TemplateZone& zone, MapGenerator& mapGenerator, Map& map, Rng& rand)
{
    auto decorationsArea{getArea(zone, mapGenerator, map, rand)};
    if (decorationsArea.empty()) {
        // No place for landmarks or forests
        return true;
    }

    if (!placeLandmarks(decorationsArea, zone, mapGenerator, map, rand)) {
        return false;
    }

    if (decorationsArea.empty()) {
        // No place for forests
        return true;
    }

    return placeForests(decorationsArea, zone, mapGenerator, map, rand);
}

std::set<Position> Decoration::getMapElementArea(const MapElement& mapElement,
                                                 int gapSizeX,
                                                 int gapSizeY,
                                                 TemplateZone&,
                                                 MapGenerator& mapGenerator,
                                                 Map& map,
                                                 Rng&)
{
    const auto& startPos{mapElement.getPosition()};
    const auto& size{mapElement.getSize()};
    const auto entrance{mapElement.getEntrance()};
    const auto endPos{startPos + size};

    auto blocked{mapElement.getBlockedPositions()};
    blocked.insert(entrance);

    const auto& nearEntrance{mapElement.getEntranceOffsets()};
    for (const auto& offset : nearEntrance) {
        blocked.insert(entrance + offset);
    }

    std::set<Position> decorationsArea;
    for (int x = startPos.x - gapSizeX; x < endPos.x + gapSizeX; ++x) {
        for (int y = startPos.y - gapSizeY; y < endPos.y + gapSizeY; ++y) {
            Position tile{x, y};
            if (contains(blocked, tile)) {
                continue;
            }

            if (!map.isInTheMap(tile) || !mapGenerator.isPossible(tile)) {
                continue;
            }

            decorationsArea.insert(tile);
        }
    }

    return decorationsArea;
}

const LandmarkFilterList& Decoration::getLandmarkFilters()
{
    static const LandmarkFilterList filters{};
    return filters;
}

std::set<Position> Decoration::getArea(TemplateZone&, MapGenerator&, Map&, Rng&)
{
    return {};
}

int Decoration::getMinLandmarkDistance(const LandmarkInfo& info) const
{
    return info.size.x * 2;
}

RaceType Decoration::getLandmarksRace(TemplateZone&, MapGenerator&, Map&, Rng&)
{
    return RaceType::Neutral;
}

TerrainType Decoration::getLandmarksTerrain(TemplateZone&, MapGenerator&, Map&, Rng&)
{
    return TerrainType::Neutral;
}

TerrainType Decoration::getForestsTerrain(TemplateZone&, MapGenerator&, Map&, Rng&)
{
    return TerrainType::Neutral;
}

bool Decoration::placeLandmarks(std::set<Position>& area,
                                TemplateZone& zone,
                                MapGenerator& mapGenerator,
                                Map& map,
                                Rng& rand)
{
    const auto landmarksTotal{(std::size_t)rand.getInt64Range(landmarks.min, landmarks.max)()};
    const auto landmarkFilters{getLandmarkFilters()};

    for (std::size_t i = 0; i < landmarksTotal; ++i) {
        const auto landmarkRace{getLandmarksRace(zone, mapGenerator, map, rand)};

        const auto* info{pickLandmark(landmarkRace, rand, landmarkFilters)};
        if (!info) {
            break;
        }

        const auto midDistance{getMinLandmarkDistance(*info)};
        MapElement tmpLandmark{info->size};
        Position position;
        if (!zone.findPlaceForObject(area, tmpLandmark, midDistance, position, false)) {
            // TODO: pick smaller and try to place ?
            continue;
        }

        // Create landmark object
        const auto landmarkId{mapGenerator.createId(CMidgardID::Type::Landmark)};
        auto landmark{std::make_unique<Landmark>(landmarkId, info->size)};
        landmark->setTypeId(info->landmarkId);

        auto landmarkPtr{landmark.get()};
        // Place created landmark
        zone.placeObject(std::move(landmark), position);

        auto landmarkTiles{landmarkPtr->getBlockedPositions()};
        landmarkTiles.insert(landmarkPtr->getEntrance());
        // Remove blocked tiles from area
        // Change tiles to specified terrain for better visuals
        const auto landmarkTerrain{getLandmarksTerrain(zone, mapGenerator, map, rand)};
        for (const auto& tile : landmarkTiles) {
            map.getTile(tile).setTerrainGround(landmarkTerrain, GroundType::Plain);
            area.erase(tile);
        }
    }

    return true;
}

bool Decoration::placeForests(std::set<Position>& area,
                              TemplateZone& zone,
                              MapGenerator& mapGenerator,
                              Map& map,
                              Rng& rand)
{
    // Pick number of forests
    const auto forestsTotal{(std::size_t)rand.getInt64Range(forests.min, forests.max)()};

    std::vector<Position> forestTiles(area.begin(), area.end());
    randomShuffle(forestTiles, rand);

    const auto forestTerrain{getForestsTerrain(zone, mapGenerator, map, rand)};

    for (std::size_t i = 0; i < forestsTotal && i < forestTiles.size(); ++i) {
        const auto& forestTile{forestTiles[i]};

        auto& tile{map.getTile(forestTile)};
        tile.setTerrainGround(forestTerrain, GroundType::Forest);
        tile.treeImage = (std::uint8_t)rand.getInt64Range(0, 19)();

        mapGenerator.setOccupied(forestTile, TileType::Used);
    }

    return true;
}

const LandmarkFilterList& CapitalDecoration::getLandmarkFilters()
{
    // clang-format off
    static const LandmarkFilterList filters{
        // Pick landmarks that are smaller than capital
        [size = capital->getSize().x]( const LandmarkInfo* info) {
            return info->size.x >= size;
        },
        // Pick landmarks that allow terrain spread
        [](const LandmarkInfo* info) {
            return info->mountain == true;
        }
    };
    // clang-format on

    return filters;
}

std::set<Position> CapitalDecoration::getArea(TemplateZone& zone,
                                              MapGenerator& mapGenerator,
                                              Map& map,
                                              Rng& rand)
{
    return getMapElementArea(*capital, 3, 3, zone, mapGenerator, map, rand);
}

RaceType CapitalDecoration::getLandmarksRace(TemplateZone& zone,
                                             MapGenerator& mapGenerator,
                                             Map& map,
                                             Rng& rand)
{
    auto player = map.find<Player>(capital->getOwner());
    assert(player != nullptr);

    return map.getRaceType(player->getRace());
}

TerrainType CapitalDecoration::getLandmarksTerrain(TemplateZone& zone,
                                                   MapGenerator& mapGenerator,
                                                   Map& map,
                                                   Rng& rand)
{
    return map.getRaceTerrain(getLandmarksRace(zone, mapGenerator, map, rand));
}

TerrainType CapitalDecoration::getForestsTerrain(TemplateZone& zone,
                                                 MapGenerator& mapGenerator,
                                                 Map& map,
                                                 Rng& rand)
{
    return getLandmarksTerrain(zone, mapGenerator, map, rand);
}

const LandmarkFilterList& VillageDecoration::getLandmarkFilters()
{
    // clang-format off
    static const LandmarkFilterList filters{
        // Pick landmarks that are smaller than village
        [size = village->getSize().x] (const LandmarkInfo* info) {
            return info->size.x > size;
        },
        // Pick landmarks that allow terrain spread, necessary for the villages
        [](const LandmarkInfo* info) {
            return info->mountain == true;
        },
        // Don't pick cemeteries and skeletons for high tier cities
        // I think they look ugly, especially in high quantities
        [tier = village->getTier()](const LandmarkInfo* info) {
            return info->landmarkType == LandmarkType::Misc && tier >= 3;
        }
    };
    // clang-format on

    return filters;
}

std::set<Position> VillageDecoration::getArea(TemplateZone& zone,
                                              MapGenerator& mapGenerator,
                                              Map& map,
                                              Rng& rand)
{
    return getMapElementArea(*village, 4, 4, zone, mapGenerator, map, rand);
}

int VillageDecoration::getMinLandmarkDistance(const LandmarkInfo& info) const
{
    return info.size.x * 3;
}

RaceType VillageDecoration::getLandmarksRace(TemplateZone&, MapGenerator&, Map&, Rng&)
{
    return RaceType::Neutral;
}

TerrainType VillageDecoration::getLandmarksTerrain(TemplateZone&, MapGenerator&, Map&, Rng&)
{
    return TerrainType::Neutral;
}

TerrainType VillageDecoration::getForestsTerrain(TemplateZone&, MapGenerator&, Map&, Rng&)
{
    return TerrainType::Neutral;
}

bool CrystalDecoration::decorate(TemplateZone& zone,
                                 MapGenerator& mapGenerator,
                                 Map& map,
                                 Rng& rand)
{
    auto decorationsArea{getArea(zone, mapGenerator, map, rand)};
    if (decorationsArea.empty()) {
        // No place for landmarks or forests
        return true;
    }

    // Place forests first to allow them randomly occupy some tiles
    // This should prevent landmarks spawn at the top tile almost all the time
    if (!placeForests(decorationsArea, zone, mapGenerator, map, rand)) {
        return false;
    }

    if (decorationsArea.empty()) {
        // No place for landmarks
        return true;
    }

    return placeLandmarks(decorationsArea, zone, mapGenerator, map, rand);
}

const LandmarkFilterList& CrystalDecoration::getLandmarkFilters()
{
    // clang-format off
    static const LandmarkFilterList filters{
        // Pick landmarks that are smaller than crystal
        [size = crystal->getSize().x] (const LandmarkInfo* info) {
            return info->size.x > size;
        },
        // Pick landmarks that allow terrain spread
        [](const LandmarkInfo* info) {
            return info->mountain == true;
        }
    };
    // clang-format on

    return filters;
}

std::set<Position> CrystalDecoration::getArea(TemplateZone& zone,
                                              MapGenerator& mapGenerator,
                                              Map& map,
                                              Rng& rand)
{
    return getMapElementArea(*crystal, 1, 1, zone, mapGenerator, map, rand);
}

RaceType CrystalDecoration::getLandmarksRace(TemplateZone&, MapGenerator&, Map&, Rng&)
{
    return RaceType::Neutral;
}

TerrainType CrystalDecoration::getLandmarksTerrain(TemplateZone&, MapGenerator&, Map&, Rng&)
{
    return TerrainType::Neutral;
}

TerrainType CrystalDecoration::getForestsTerrain(TemplateZone&, MapGenerator&, Map&, Rng&)
{
    return TerrainType::Neutral;
}

bool CrystalDecoration::placeForests(std::set<Position>& area,
                                     TemplateZone& zone,
                                     MapGenerator& mapGenerator,
                                     Map& map,
                                     Rng& rand)
{
    // Pick number of forests
    const auto forestsTotal{(std::size_t)rand.getInt64Range(forests.min, forests.max)()};

    std::vector<Position> forestTiles(area.begin(), area.end());
    randomShuffle(forestTiles, rand);

    const auto forestTerrain{getForestsTerrain(zone, mapGenerator, map, rand)};

    for (std::size_t i = 0; i < forestsTotal && i < forestTiles.size(); ++i) {
        const auto& forestTile{forestTiles[i]};

        auto& tile{map.getTile(forestTile)};
        tile.setTerrainGround(forestTerrain, GroundType::Forest);
        tile.treeImage = (std::uint8_t)rand.getInt64Range(0, 19)();

        mapGenerator.setOccupied(forestTile, TileType::Used);

        area.erase(forestTile);
    }

    return true;
}

const LandmarkFilterList& SiteDecoration::getLandmarkFilters()
{
    // clang-format off
    static const LandmarkFilterList filters{
        // Pick landmarks that are smaller than site
        [size = site->getSize().x] (const LandmarkInfo* info) {
        return info->size.x > size;
        }
    };
    // clang-format on

    return filters;
}

std::set<Position> SiteDecoration::getArea(TemplateZone& zone,
                                           MapGenerator& mapGenerator,
                                           Map& map,
                                           Rng& rand)
{
    return getMapElementArea(*site, 3, 3, zone, mapGenerator, map, rand);
}

int SiteDecoration::getMinLandmarkDistance(const LandmarkInfo& info) const
{
    return info.size.x * 3;
}

RaceType SiteDecoration::getLandmarksRace(TemplateZone&, MapGenerator&, Map&, Rng&)
{
    return RaceType::Neutral;
}

TerrainType SiteDecoration::getLandmarksTerrain(TemplateZone&, MapGenerator&, Map&, Rng&)
{
    return TerrainType::Neutral;
}

TerrainType SiteDecoration::getForestsTerrain(TemplateZone&, MapGenerator&, Map&, Rng&)
{
    return TerrainType::Neutral;
}

std::set<Position> RuinDecoration::getArea(TemplateZone& zone,
                                           MapGenerator& mapGenerator,
                                           Map& map,
                                           Rng& rand)
{
    return getMapElementArea(*ruin, 4, 4, zone, mapGenerator, map, rand);
}

int RuinDecoration::getMinLandmarkDistance(const LandmarkInfo& info) const
{
    return info.size.x * 3;
}

RaceType RuinDecoration::getLandmarksRace(TemplateZone&, MapGenerator&, Map&, Rng&)
{
    return RaceType::Neutral;
}

TerrainType RuinDecoration::getLandmarksTerrain(TemplateZone&, MapGenerator&, Map&, Rng&)
{
    return TerrainType::Neutral;
}

TerrainType RuinDecoration::getForestsTerrain(TemplateZone&, MapGenerator&, Map&, Rng&)
{
    return TerrainType::Neutral;
}