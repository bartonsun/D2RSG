/*
 * This file is part of the random scenario generator for Disciples 2.
 * (https://github.com/VladimirMakeev/D2RSG)
 * Copyright (C) 2023 Vladimir Makeev.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "templatezone.h"
#include "blueprint.h"
#include "capital.h"
#include "containers.h"
#include "crystal.h"
#include "exceptions.h"
#include "generatorsettings.h"
#include "item.h"
#include "itempicker.h"
#include "knownspells.h"
#include "playerbuildings.h"
#include "landmarkpicker.h"
#include "mage.h"
#include "mapgenerator.h"
#include "maptemplate.h"
#include "mercenary.h"
#include "merchant.h"
#include "player.h"
#include "resourcemarket.h"
#include "spellpicker.h"
#include "subrace.h"
#include "texts.h"
#include "trainer.h"
#include "unit.h"
#include "unitpicker.h"
#include "village.h"
#include <cassert>
#include <iostream>
#include <iterator>
#include <sstream>


namespace rsg {

static Facing getRandomFacing(RandomGenerator& rand)
{
    const int minFacing{static_cast<int>(Facing::Southwest)};
    const int maxFacing{static_cast<int>(Facing::South)};

    return static_cast<Facing>(rand.nextInteger(minFacing, maxFacing));
}

// Returns true if all tiles near mapElement entrance are blocked or used not by forest
static bool isEntranceBlocked(const MapElement& mapElement, const MapGenerator& mapGenerator)
{
    const Position entrance{mapElement.getEntrance()};
    const auto& offsets{mapElement.getEntranceOffsets()};

    auto isOffsetBlocked = [&entrance, &mapGenerator](const Position& offset) {
        const Position pos{entrance + offset};

        // Explicitly check blocked state
        if (mapGenerator.shouldBeBlocked(pos)) {
            return true;
        }

        if (mapGenerator.isRoad(pos)) {
            // Road means entrance isn't blocked at all
            return false;
        }

        if (mapGenerator.isUsed(pos)) {
            const Tile& tile{mapGenerator.map->getTile(pos)};

            if (tile.ground != GroundType::Forest) {
                // Used and not a forest? Stack, landmark, other object?
                // Currently, we don't care.
                // We should not end up with situation where entrance
                // is completely blocked with stacks or bags
                return true;
            }
        }

        // Assume not blocked
        return false;
    };

    return std::all_of(offsets.begin(), offsets.end(), isOffsetBlocked);
}

static void checkObjectsAccess(const MapGenerator& mapGenerator, const Map& map)
{
    // Check all cities
    map.visit(CMidgardID::Type::Fortification, [&mapGenerator](const ScenarioObject* object) {
        const Fortification* fort{dynamic_cast<const Fortification*>(object)};

        if (isEntranceBlocked(*fort, mapGenerator)) {
            std::stringstream msg;
            msg << "City at " << fort->getPosition()
                << " has its entrance blocked! Map seed: " << (std::uint32_t)mapGenerator.randomSeed
                << '\n';
            throw std::runtime_error(msg.str());
        }
    });

    // Check all ruins
    map.visit(CMidgardID::Type::Ruin, [&mapGenerator](const ScenarioObject* object) {
        const Ruin* ruin{dynamic_cast<const Ruin*>(object)};

        if (isEntranceBlocked(*ruin, mapGenerator)) {
            std::stringstream msg;
            msg << "Ruin at " << ruin->getPosition()
                << " has its entrance blocked! Map seed: " << (std::uint32_t)mapGenerator.randomSeed
                << '\n';
            throw std::runtime_error(msg.str());
        }
    });

    // Check all sites
    map.visit(CMidgardID::Type::Site, [&mapGenerator](const ScenarioObject* object) {
        const Site* site{dynamic_cast<const Site*>(object)};

        if (isEntranceBlocked(*site, mapGenerator)) {
            std::stringstream msg;
            msg << "Site at " << site->getPosition()
                << " has its entrance blocked! Map seed: " << (std::uint32_t)mapGenerator.randomSeed
                << '\n';
            throw std::runtime_error(msg.str());
        }
    });
}

void TemplateZone::setCenter(const VPosition& value)
{
    // Wrap zone around (0, 1) square.
    // If it doesn't fit on one side, will come out on the opposite side.
    center = value;

    center.x = static_cast<float>(std::fmod(center.x, 1));
    center.y = static_cast<float>(std::fmod(center.y, 1));

    if (center.x < 0.f) {
        center.x = 1.f - std::abs(center.x);
    }

    if (center.y < 0.f) {
        center.y = 1.f - std::abs(center.y);
    }
}

void TemplateZone::clearEntrance(const Fortification& fort)
{
    auto clearPosition = [this](const Position& position) {
        if (mapGenerator->isPossible(position)) {
            mapGenerator->setOccupied(position, TileType::Free);
        }
    };

    mapGenerator->foreachNeighbor(fort.getEntrance() + Position(1, 1), clearPosition);
}

void TemplateZone::initTowns()
{
    if (type == TemplateZoneType::Water) {
        return;
    }

    // Create first neutral city or player capital at the center of the zone.
    // Rest of neutral cities will be created later
    if (type == TemplateZoneType::PlayerStart || type == TemplateZoneType::AiStart) {
        if (mapGenerator->isDebugMode()) {
            std::cout << "Preparing player zone\n";
        }

        placeCapital();
        return;
    }

    if (!neutralCities.empty()) {
        auto village = placeCity(pos - Position(2, 2), neutralCities[0]);
        // All roads lead to tile near central village entrance
        setPosition(village->getEntrance() + Position(1, 1));

        mapGenerator->registerZone(RaceType::Neutral);
    }
}

void TemplateZone::initFreeTiles()
{
    std::copy_if(tileInfo.begin(), tileInfo.end(),
                 std::inserter(possibleTiles, possibleTiles.end()),
                 [this](const Position& position) {
                     return this->mapGenerator->isPossible(position);
                 });

    // Zone must have at least one free tile where other paths go - for instance in the center
    if (freePaths.empty()) {
        addFreePath(getPosition());
    }
}

void TemplateZone::createBorder()
{
    std::size_t borderTiles{};
    std::size_t openBorders{};
    std::size_t closedBorders{};

    for (auto& tile : tileInfo) {
        bool border{};

        mapGenerator->foreachNeighbor(tile, [this, &border, &borderTiles, &openBorders,
                                             &closedBorders](const Position& position) {
            border = border || mapGenerator->getZoneId(position) != id;
        });

        if (border) {
            ++borderTiles;

            if (mapGenerator->isPossible(tile)) {
                switch (borderType) {
                case ZoneBorderType::Water: {
                    Tile& mapTile = mapGenerator->map->getTile(tile);
                    mapTile.setTerrainGround(TerrainType::Neutral, GroundType::Water);
                    mapGenerator->setOccupied(tile, TileType::Free);
                    ++openBorders;
                    break;
                }
                case ZoneBorderType::Open:
                    mapGenerator->setOccupied(tile, TileType::Free);
                    ++openBorders;
                    break;

                case ZoneBorderType::Closed:
                    mapGenerator->setOccupied(tile, TileType::Blocked);
                    ++closedBorders;
                    break;

                case ZoneBorderType::SemiOpen: {
                    const bool gap{mapGenerator->randomGenerator.chance(gapChance)};

                    mapGenerator->setOccupied(tile, gap ? TileType::Free : TileType::Blocked);
                    if (gap) {
                        ++openBorders;
                    } else {
                        ++closedBorders;
                    }

                    break;
                }
                }
            }
        }
    }

    if (mapGenerator->isDebugMode()) {
        const double bordersTotal{static_cast<double>(borderTiles)};
        const float openPercent = static_cast<double>(openBorders) / bordersTotal * 100.0f;
        const float closedPercent = static_cast<double>(closedBorders) / bordersTotal * 100.0f;

        std::cout << "Zone id " << id << ", border tiles " << borderTiles << ", open "
                  << openBorders << " (" << openPercent << " %)"
                  << ", closed " << closedBorders << " (" << closedPercent << " %). Gap chance "
                  << gapChance << " %\n";
    }
}

void TemplateZone::fill()
{
    initTerrain();

    // Zone center should be always clear to allow other tiles to connect
    initFreeTiles();
    fractalize();
    placeCities();
    placeMerchants();
    placeMages();
    placeMercenaries();
    placeTrainers();
    placeMarkets();
    placeRuins();
    placeMines();
    createRequiredObjects();
    placeStacks();
    placeBags();

    if (mapGenerator->isDebugMode()) {
        std::cout << "Zone " << id << " filled successfully\n";
    }
}

void TemplateZone::createObstacles()
{
    if (mapGenerator->isDebugMode()) {
        std::cout << "Place decorations\n";
        checkObjectsAccess(*mapGenerator, *mapGenerator->map);
    }

    // Place decorations first
    for (const auto& decoration : decorations) {
        decoration->decorate(*this, *mapGenerator, *mapGenerator->map,
                             mapGenerator->randomGenerator);
    }

    decorations.clear();

    if (mapGenerator->isDebugMode()) {
        std::cout << "Decorations placed\n";
        checkObjectsAccess(*mapGenerator, *mapGenerator->map);

        std::cout << "Place mountains\n";
    }

    using MountainsVector = std::vector<GeneratorSettings::Mountain>;
    using MountainPair = std::pair<int /* mountain size */, MountainsVector>;

    std::map<int /* mountain size */, MountainsVector> obstaclesBySize;
    std::vector<MountainPair> possibleObstacles;

    const auto& knownMountains = getGeneratorSettings().mountains;
    for (const auto& mountain : knownMountains) {
        obstaclesBySize[mountain.size].push_back(mountain);
    }

    for (const auto& [size, vector] : obstaclesBySize) {
        possibleObstacles.push_back({size, vector});
    }

    std::sort(possibleObstacles.begin(), possibleObstacles.end(),
              [](const MountainPair& a, const MountainPair& b) {
                  // Bigger mountains first
                  return a.first > b.first;
              });

    auto tryPlaceMountainHere = [this, &possibleObstacles](const Position& tile, int index) {
        auto& rand{mapGenerator->randomGenerator};

        const auto it{getRandomElement(possibleObstacles[index].second, rand)};

        const MapElement mountainElement({it->size, it->size});
        if (!canObstacleBePlacedHere(mountainElement, tile)) {
            return false;
        }

        // If size is 3 or 5, roll 10% chance to spawn mountain landmark
        // TODO: remove hardcoded values
        if ((it->size == 3 || it->size == 5) && rand.chance(10)) {
            auto noWrongSize = [size = it->size](const LandmarkInfo* info) {
                return info->getSize().x != size || info->getSize().y != size;
            };

            auto info{pickMountainLandmark(rand, {noWrongSize})};
            assert(info != nullptr);

            auto landmarkId{mapGenerator->createId(CMidgardID::Type::Landmark)};
            auto landmark{std::make_unique<Landmark>(landmarkId, info->getSize())};
            landmark->setTypeId(info->getLandmarkId());

            placeObject(std::move(landmark), tile);
        } else {
            placeMountain(tile, mountainElement.getSize(), it->image);
        }

        return true;
    };

    for (const auto& tile : tileInfo) {
        // Fill tiles that should be blocked with obstacles
        if (mapGenerator->shouldBeBlocked(tile)) {

            // Start from biggets obstacles
            for (int i = 0; i < (int)possibleObstacles.size(); ++i) {
                if (tryPlaceMountainHere(tile, i)) {
                    break;
                }
            }
        }
    }

    if (mapGenerator->isDebugMode()) {
        std::cout << "Mountains placed\n";
        checkObjectsAccess(*mapGenerator, *mapGenerator->map);
    }

    // TODO: this step can be changed, we can place forests here, for example
    // Roads already have clear free paths for them,
    // we can convert remaining possible tiles to forests

    // TODO: I have tested filling remaining possible tiles completely with water and with
    // forests Both results loogs good, but: in case of water there are some crystals that
    // became stranded on an island without single free tile for a rod. this can be fixed by
    // marking some tiles as free during decoration placement there are also cases of single
    // tile waters: this can be fixed by checking neigbour tiles the same way as we do in
    // MapGenerator::createObstacles()

    // In case water and forest settings are become part of template we need to generate both of
    // them here For water we can randomly pick several possible tiles (pick them the same way
    // we find place for objects) and create small lakes (depending on water template setting).
    // The rest for the forests, check mountains in the zone, place near them. Also pick random
    // tiles and place around

    // Place forests
    const int forests = mapGenerator->mapGenOptions.mapTemplate->settings.forest;

    if (forests == 0) {
        // Cleanup, remove unused possible tiles to make space for roads
        for (auto& tile : tileInfo) {
            if (mapGenerator->isPossible(tile)) {
                mapGenerator->setOccupied(tile, TileType::Free);
            }
        }

        return;
    }

    auto& rand{mapGenerator->randomGenerator};

    for (auto& tile : tileInfo) {
        if (mapGenerator->isPossible(tile)) {
            if (mapGenerator->isRoad(tile)) {
                mapGenerator->setOccupied(tile, TileType::Free);
                continue;
            }

            // Can place forests here
            const bool shouldPlace = forests == 100 ? true : rand.chance(forests);
            if (!shouldPlace) {
                mapGenerator->setOccupied(tile, TileType::Free);
                continue;
            }

            mapGenerator->setOccupied(tile, TileType::Used);

            auto& mapTile = mapGenerator->map->getTile(tile);

            mapTile.setTerrainGround(TerrainType::Neutral, GroundType::Forest);
            mapTile.treeImage = getRandomTreeImageIndex(rand);
        }
    }
}

void TemplateZone::connectRoads()
{
    if (mapGenerator->isDebugMode()) {
        std::cout << "Started building roads\n";
    }

    std::set<Position> roadNodesCopy{roadNodes};
    std::set<Position> processed;

    while (!roadNodesCopy.empty()) {
        auto node{*roadNodesCopy.begin()};
        roadNodesCopy.erase(node);

        Position cross{-1, -1};

        auto comparator = [&node](const Position& a, const Position& b) {
            return node.distanceSquared(a) < node.distanceSquared(b);
        };

        if (!processed.empty()) {
            // Connect with existing network
            cross = *std::min_element(processed.begin(), processed.end(), comparator);
        } else if (!roadNodesCopy.empty()) {
            // Connect with any other unconnected node
            cross = *std::min_element(roadNodesCopy.begin(), roadNodesCopy.end(), comparator);
        } else {
            // No other nodes left, for example single road node in this zone
            break;
        }

        if (mapGenerator->isDebugMode()) {
            std::cout << "Building road from " << node << " to " << cross << '\n';
        }

        if (createRoad(node, cross)) {
            // Don't draw road starting at end point which is already connected
            processed.insert(cross);

            eraseIfPresent(roadNodesCopy, cross);
        }

        processed.insert(node);
    }

    if (mapGenerator->isDebugMode()) {
        std::cout << "Finished building roads\n";
    }
}

ObjectPlacingResult TemplateZone::tryToPlaceObjectAndConnectToPath(MapElement& mapElement,
                                                                   const Position& position)
{
    mapElement.setPosition(position);

    const auto tiles{getAccessibleTiles(mapElement)};
    if (tiles.empty()) {
        if (mapGenerator->isDebugMode()) {
            std::cout << "Can not access required object at position " << position
                      << ", retrying\n";
        }

        return ObjectPlacingResult::CannotFit;
    }

    const auto accessibleTile{getAccessibleOffset(mapElement, position)};
    if (!accessibleTile.isValid()) {
        if (mapGenerator->isDebugMode()) {
            std::cout << "Can not access required object at position " << position
                      << ", retrying\n";
        }

        return ObjectPlacingResult::CannotFit;
    }

    {
        Blueprint blueprint{*mapGenerator, position, mapElement.getSize()};

        if (!connectPath(accessibleTile, true)) {
            if (mapGenerator->isDebugMode()) {
                std::cout << "Failed to create path to required object at position " << position
                          << ", retrying\n";
            }

            return ObjectPlacingResult::SealedOff;
        }
    }

    mapGenerator->setOccupied(mapElement.getEntrance(), TileType::Blocked);

    for (const auto& tile : mapElement.getBlockedPositions()) {
        if (mapGenerator->map->isInTheMap(tile)) {
            mapGenerator->setOccupied(tile, TileType::Blocked);
        }
    }

    return ObjectPlacingResult::Success;
}

void TemplateZone::addRequiredObject(ScenarioObjectPtr&& object,
                                     DecorationPtr&& decoration,
                                     int guardStrength,
                                     const Position& objectSize)
{
    requiredObjects.push_back(
        ObjectPlacement{std::move(object), std::move(decoration), objectSize, guardStrength});
}

void TemplateZone::addCloseObject(ScenarioObjectPtr&& object,
                                  DecorationPtr&& decoration,
                                  int guardStrength,
                                  const Position& objectSize)
{
    closeObjects.push_back(
        ObjectPlacement{std::move(object), std::move(decoration), objectSize, guardStrength});
}

// See:
// https://stackoverflow.com/questions/26377430/how-to-perform-a-dynamic-cast-with-a-unique-ptr/26377517
template <typename To, typename From>
std::unique_ptr<To> dynamic_unique_cast(std::unique_ptr<From>&& p)
{
    if (To* cast = dynamic_cast<To*>(p.get())) {
        std::unique_ptr<To> result(dynamic_cast<To*>(p.release()));
        return result;
    }

    throw std::bad_cast();
}

void TemplateZone::placeScenarioObject(ScenarioObjectPtr&& object, const Position& position)
{
    switch (object->getId().getType()) {
    case CMidgardID::Type::Fortification: {
        auto fort{dynamic_unique_cast<Fortification>(std::move(object))};
        placeObject(std::move(fort), position);
        break;
    }

    case CMidgardID::Type::Stack: {
        auto stack{dynamic_unique_cast<Stack>(std::move(object))};
        placeObject(std::move(stack), position);
        break;
    }

    case CMidgardID::Type::Crystal: {
        auto crystal{dynamic_unique_cast<Crystal>(std::move(object))};
        placeObject(std::move(crystal), position);
        break;
    }

    case CMidgardID::Type::Ruin: {
        auto ruin{dynamic_unique_cast<Ruin>(std::move(object))};
        placeObject(std::move(ruin), position);
        break;
    }

    case CMidgardID::Type::Site: {
        auto site{dynamic_unique_cast<Site>(std::move(object))};
        placeObject(std::move(site), position);
        break;
    }

    case CMidgardID::Type::Bag: {
        auto bag{dynamic_unique_cast<Bag>(std::move(object))};
        placeObject(std::move(bag), position);
        break;
    }
    }
}

void TemplateZone::placeObject(std::unique_ptr<Fortification>&& fortification,
                               const Position& position,
                               TerrainType terrain,
                               bool updateDistance)
{
    // Check position
    if (!mapGenerator->map->isInTheMap(position)) {
        CMidgardID::String fortId{};
        fortification->getId().toString(fortId);

        std::stringstream stream;
        stream << "Position of fort " << fortId.data() << " at " << position
               << " is outside of the map\n";
        throw std::runtime_error(stream.str());
    }

    fortification->setPosition(position);

    // Check entrance
    // Since position and entrance form rectangle we don't need to check other tiles
    if (!mapGenerator->map->isInTheMap(fortification->getEntrance())) {
        CMidgardID::String fortId{};
        fortification->getId().toString(fortId);

        std::stringstream stream;
        stream << "Entrance " << fortification->getEntrance() << " of fort " << fortId.data()
               << " at " << position << " is outside of the map\n";
        throw std::runtime_error(stream.str());
    }

    // Mark fort tiles and entrance as used
    auto blocked{fortification->getBlockedPositions()};
    blocked.insert(fortification->getEntrance());

    for (auto& tile : blocked) {
        mapGenerator->setOccupied(tile, TileType::Used);
        // Change terrain under city to race specific
        mapGenerator->paintTerrain(tile, terrain, GroundType::Plain);
    }

    if (fortification->getGapMask() > 0) {
        std::set<Position> tiles = fortification->getTilesByMask(
            fortification->getGapMask());
        for (auto& tile : tiles) {
            if (blocked.find(tile) != blocked.end()) {
                continue;
            } else if (!mapGenerator->map->isInTheMap(tile)) {
                continue;
            }
            mapGenerator->setOccupied(tile, TileType::Free);
        }
    }

    // Update distances
    if (updateDistance) {
        updateDistances(position);
    }

    // Add road node using entrance point
    addRoadNode(fortification->getEntrance());

    mapGenerator->map->insertMapElement(*fortification.get(), fortification->getId());
    // Store object in scenario map
    mapGenerator->insertObject(std::move(fortification));
}

void TemplateZone::placeObject(std::unique_ptr<Stack>&& stack,
                               const Position& position,
                               bool updateDistance)
{
    // Check position
    if (!mapGenerator->map->isInTheMap(position)) {
        CMidgardID::String stackId{};
        stack->getId().toString(stackId);

        std::stringstream stream;
        stream << "Position of stack " << stackId.data() << " at " << position
               << " is outside of the map\n";
        throw std::runtime_error(stream.str());
    }

    stack->setPosition(position);

    // Mark stack tiles as used
    auto blocked{stack->getBlockedPositions()};
    blocked.insert(stack->getEntrance());

    for (auto& tile : blocked) {
        mapGenerator->setOccupied(tile, TileType::Used);
    }

    // Update distances
    if (updateDistance) {
        updateDistances(position);
    }

    mapGenerator->map->insertMapElement(*stack.get(), stack->getId());
    // Store object in scenario map
    mapGenerator->insertObject(std::move(stack));
}

void TemplateZone::placeObject(std::unique_ptr<Crystal>&& crystal,
                               const Position& position,
                               bool updateDistance)
{
    // Check position
    if (!mapGenerator->map->isInTheMap(position)) {
        CMidgardID::String crystalId{};
        crystal->getId().toString(crystalId);

        std::stringstream stream;
        stream << "Position of crystal " << crystalId.data() << " at " << position
               << " is outside of the map\n";
        throw std::runtime_error(stream.str());
    }

    crystal->setPosition(position);

    // Mark crystal tiles as used
    auto blocked{crystal->getBlockedPositions()};
    blocked.insert(crystal->getEntrance());

    for (auto& tile : blocked) {
        mapGenerator->setOccupied(tile, TileType::Used);
    }

    // Update distances
    if (updateDistance) {
        updateDistances(position);
    }

    mapGenerator->map->insertMapElement(*crystal.get(), crystal->getId());
    // Store object in scenario map
    mapGenerator->insertObject(std::move(crystal));
}

void TemplateZone::placeObject(std::unique_ptr<Ruin>&& ruin,
                               const Position& position,
                               bool updateDistance)
{
    // Check position
    if (!mapGenerator->map->isInTheMap(position)) {
        CMidgardID::String ruinId{};
        ruin->getId().toString(ruinId);

        std::stringstream stream;
        stream << "Position of ruin " << ruinId.data() << " at " << position
               << " is outside of the map\n";
        throw std::runtime_error(stream.str());
    }

    ruin->setPosition(position);

    // Check entrance
    // Since position and entrance form rectangle we don't need to check other tiles
    if (!mapGenerator->map->isInTheMap(ruin->getEntrance())) {
        CMidgardID::String ruinId{};
        ruin->getId().toString(ruinId);

        std::stringstream stream;
        stream << "Entrance " << ruin->getEntrance() << " of ruin " << ruinId.data() << " at "
               << position << " is outside of the map\n";
        throw std::runtime_error(stream.str());
    }

    // Mark ruin tiles and entrance as used
    auto blocked{ruin->getBlockedPositions()};
    blocked.insert(ruin->getEntrance());

    for (auto& tile : blocked) {
        mapGenerator->setOccupied(tile, TileType::Used);
    }

    // Update distances
    if (updateDistance) {
        updateDistances(position);
    }

    mapGenerator->map->insertMapElement(*ruin.get(), ruin->getId());
    // Store object in scenario map
    mapGenerator->insertObject(std::move(ruin));
}

void TemplateZone::placeObject(std::unique_ptr<Site>&& site,
                               const Position& position,
                               bool updateDistance)
{
    // Check position
    if (!mapGenerator->map->isInTheMap(position)) {
        CMidgardID::String siteId{};
        site->getId().toString(siteId);

        std::stringstream stream;
        stream << "Position of site " << siteId.data() << " at " << position
               << " is outside of the map\n";
        throw std::runtime_error(stream.str());
    }

    site->setPosition(position);

    // Check entrance
    // Since position and entrance form rectangle we don't need to check other tiles
    if (!mapGenerator->map->isInTheMap(site->getEntrance())) {
        CMidgardID::String siteId{};
        site->getId().toString(siteId);

        std::stringstream stream;
        stream << "Entrance " << site->getEntrance() << " of site " << siteId.data() << " at "
               << position << " is outside of the map\n";
        throw std::runtime_error(stream.str());
    }

    // Mark site tiles and entrance as used
    auto blocked{site->getBlockedPositions()};
    blocked.insert(site->getEntrance());

    for (auto& tile : blocked) {
        mapGenerator->setOccupied(tile, TileType::Used);
    }

    // Update distances
    if (updateDistance) {
        updateDistances(position);
    }

    // Add road node using entrance point
    addRoadNode(site->getEntrance());

    mapGenerator->map->insertMapElement(*site.get(), site->getId());
    // Store object in scenario map
    mapGenerator->insertObject(std::move(site));
}

void TemplateZone::placeObject(std::unique_ptr<Bag>&& bag,
                               const Position& position,
                               bool updateDistance)
{
    // Check position
    if (!mapGenerator->map->isInTheMap(position)) {
        CMidgardID::String bagId{};
        bag->getId().toString(bagId);

        std::stringstream stream;
        stream << "Position of bag " << bagId.data() << " at " << position
               << " is outside of the map\n";
        throw std::runtime_error(stream.str());
    }

    bag->setPosition(position);

    // Mark bag tiles as used
    auto blocked{bag->getBlockedPositions()};
    blocked.insert(bag->getEntrance());

    for (auto& tile : blocked) {
        mapGenerator->setOccupied(tile, TileType::Used);
    }

    // Update distances
    if (updateDistance) {
        updateDistances(position);
    }

    mapGenerator->map->insertMapElement(*bag.get(), bag->getId());
    // Store object in scenario map
    mapGenerator->insertObject(std::move(bag));
}

void TemplateZone::placeObject(std::unique_ptr<Landmark>&& landmark,
                               const Position& position,
                               bool updateDistance)
{
    // Check position
    if (!mapGenerator->map->isInTheMap(position)) {
        CMidgardID::String landmarkId{};
        landmark->getId().toString(landmarkId);

        std::stringstream stream;
        stream << "Position of landmark " << landmarkId.data() << " at " << position
               << " is outside of the map\n";
        throw std::runtime_error(stream.str());
    }

    landmark->setPosition(position);

    // Use entrance as bottom-right point of landmark area
    // Since position and entrance form rectangle we don't need to check other tiles
    if (!mapGenerator->map->isInTheMap(landmark->getEntrance())) {
        CMidgardID::String landmarkId{};
        landmark->getId().toString(landmarkId);

        std::stringstream stream;
        stream << "Bottom-right point " << landmark->getEntrance() << " of landmark "
               << landmarkId.data() << " at " << position << " is outside of the map\n";
        throw std::runtime_error(stream.str());
    }

    // Mark landmark tiles as used
    auto blocked{landmark->getBlockedPositions()};
    // Landmarks does not have entrance, but we use it to block all positions
    blocked.insert(landmark->getEntrance());

    for (auto& tile : blocked) {
        mapGenerator->setOccupied(tile, TileType::Used);
    }

    // Update distances
    if (updateDistance) {
        updateDistances(position);
    }

    mapGenerator->map->insertMapElement(*landmark.get(), landmark->getId());
    // Store object in scenario map
    mapGenerator->insertObject(std::move(landmark));
}

void TemplateZone::placeMountain(const Position& position, const Position& size, int image)
{
    for (int x = 0; x < size.x; ++x) {
        for (int y = 0; y < size.y; ++y) {
            const auto pos{position + Position{x, y}};

            if (!mapGenerator->map->isInTheMap(position)) {
                std::stringstream stream;
                stream << "Position of mountain at " << pos << " is outside of the map\n";
                throw std::runtime_error(stream.str());
            }

            mapGenerator->setOccupied(pos, TileType::Used);
        }
    }

    mapGenerator->map->addMountain(position, size, image);
}

bool TemplateZone::guardObject(const MapElement& mapElement, const GroupInfo& guardInfo)
{
    const auto tiles{getAccessibleTiles(mapElement)};
    Position guardTile{-1, -1};

    if (!tiles.empty()) {
        guardTile = getAccessibleOffset(mapElement, mapElement.getPosition());

        // std::cout << "Guard object at " << mapElement.getPosition() << '\n';
    } else {
        std::cerr << "Failed to guard object at " << mapElement.getPosition() << '\n';
        return false;
    }

    auto stack{createStack(guardInfo, true)};
    if (!stack) {
        // Allow no guard or other object in front of this object
        for (const auto& tile : tiles) {
            if (mapGenerator->isPossible(tile)) {
                mapGenerator->setOccupied(tile, TileType::Free);
            }
        }

        return true;
    }

    CMidgardID ownerId{mapGenerator->getPlayerId(guardInfo.owner)};
    CMidgardID subraceId{mapGenerator->getSubraceId(guardInfo.owner)};

    if (ownerId == emptyId || subraceId == emptyId) {
        ownerId = mapGenerator->getNeutralPlayerId();
        subraceId = mapGenerator->getNeutralSubraceId();
    }

    stack->setOwner(ownerId);
    stack->setSubrace(subraceId);

    if (!guardInfo.name.empty()) {
        Unit* leader{mapGenerator->map->find<Unit>(stack->getLeader())};
        if (leader) {
            leader->setName(guardInfo.name);
        }
    }

    if (!guardInfo.leaderModifiers.empty()) {
        Unit* leader{mapGenerator->map->find<Unit>(stack->getLeader())};
        if (leader) {
            for (auto modifierId : guardInfo.leaderModifiers) {
                leader->addModifier(modifierId);
            }
        }
    }

    stack->setAiPriority(guardInfo.aiPriority);
    stack->setOrder(guardInfo.order);

    placeObject(std::move(stack), guardTile);

    return true;
}

void TemplateZone::updateDistances(const Position& position)
{
    for (auto& tile : possibleTiles) {
        const auto distance{static_cast<float>(position.distanceSquared(tile))};
        const auto currentDistance{mapGenerator->getNearestObjectDistance(tile)};

        mapGenerator->setNearestObjectDistance(tile, std::min(distance, currentDistance));
    }
}

void TemplateZone::addRoadNode(const Position& position)
{
    roadNodes.insert(position);
}

void TemplateZone::addFreePath(const Position& position)
{
    mapGenerator->setOccupied(position, TileType::Free);
    freePaths.insert(position);
}

bool TemplateZone::connectWithCenter(const Position& position,
                                     bool onlyStraight,
                                     bool passThroughBlocked)
{
    // A* algorithm

    // Nodes that are already evaluated
    std::set<Position> closed;
    // The set of tentative nodes to be evaluated, initially containing the start node
    PriorityQueue queue;
    // Map of navigated nodes
    std::map<Position, Position> cameFrom;
    std::map<Position, float> distances;

    // First node points to finish condition.
    // Invalid position of (-1 -1) used as stop element
    cameFrom[position] = Position(-1, -1);
    queue.push(std::make_pair(position, 0.f));
    distances[position] = 0.f;

    while (!queue.empty()) {
        auto node = queue.top();
        // Remove top element
        queue.pop();

        const auto& currentNode{node.first};
        closed.insert(currentNode);

        // Reached center of the zone, stop
        if (currentNode == pos) {
            // Trace the path using the saved parent information and return path
            Position backTracking{currentNode};
            while (cameFrom[backTracking].isValid()) {
                mapGenerator->setOccupied(backTracking, TileType::Free);
                backTracking = cameFrom[backTracking];
            }

            return true;
        } else {
            auto functor = [this, &queue, &closed, &cameFrom, &currentNode, &distances,
                            passThroughBlocked](Position& p) {
                if (contains(closed, p)) {
                    return;
                }

                if (mapGenerator->getZoneId(p) != id) {
                    return;
                }

                float movementCost{};
                if (mapGenerator->isFree(p)) {
                    movementCost = 1.f;
                } else if (mapGenerator->isPossible(p)) {
                    movementCost = 2.f;
                } else if (passThroughBlocked && mapGenerator->shouldBeBlocked(p)) {
                    movementCost = 3.f;
                } else {
                    return;
                }

                // We prefer to use already free paths
                const float distance{distances[currentNode] + movementCost};
                auto bestDistanceSoFar{std::numeric_limits<int>::max()};

                auto it{distances.find(p)};
                if (it != distances.end()) {
                    bestDistanceSoFar = static_cast<int>(it->second);
                }

                if (distance < bestDistanceSoFar) {
                    cameFrom[p] = currentNode;
                    queue.push(std::make_pair(p, distance));
                    distances[p] = distance;
                }
            };

            if (onlyStraight) {
                mapGenerator->foreachDirectNeighbor(currentNode, functor);
            } else {
                mapGenerator->foreachNeighbor(currentNode, functor);
            }
        }
    }

    return false;
}

bool TemplateZone::crunchPath(const Position& source,
                              const Position& destination,
                              bool onlyStraight,
                              std::set<Position>* clearedTiles)
{
    bool result{};
    bool end{};

    Position currentPosition{source};
    auto distance{static_cast<float>(currentPosition.distanceSquared(destination))};

    while (!end) {
        if (currentPosition == destination) {
            result = true;
            break;
        }

        auto lastDistance{distance};

        auto processNeighbors = [this, &currentPosition, &destination, &distance, &result, &end,
                                 clearedTiles](Position& position) {
            if (result) {
                return;
            }

            if (position == destination) {
                result = true;
                end = true;
            }

            if (position.distanceSquared(destination) >= distance) {
                return;
            }

            if (mapGenerator->isBlocked(position)) {
                return;
            }

            if (mapGenerator->getZoneId(position) != id) {
                return;
            }

            if (mapGenerator->isPossible(position)) {
                mapGenerator->setOccupied(position, TileType::Free);
                if (clearedTiles) {
                    clearedTiles->insert(position);
                }

                currentPosition = position;
                distance = static_cast<float>(currentPosition.distanceSquared(destination));
            } else if (mapGenerator->isFree(position)) {
                end = true;
                result = true;
            }
        };

        if (onlyStraight) {
            mapGenerator->foreachDirectNeighbor(currentPosition, processNeighbors);
        } else {
            mapGenerator->foreachNeighbor(currentPosition, processNeighbors);
        }

        Position anotherPosition{-1, -1};

        // We do not advance, use more advanced pathfinding algorithm?
        if (!(result || distance < lastDistance)) {
            // Try any nearby tiles, even if its not closer than current
            // Start with significantly larger value
            float lastDistance{2 * distance};

            auto functor = [this, &currentPosition, &destination, &lastDistance, &anotherPosition,
                            clearedTiles](Position& position) {
                // Try closest tiles from all surrounding unused tiles
                if (currentPosition.distanceSquared(destination) >= lastDistance) {
                    return;
                }

                if (mapGenerator->getZoneId(position) != id) {
                    return;
                }

                if (!mapGenerator->isPossible(position)) {
                    return;
                }

                if (clearedTiles) {
                    clearedTiles->insert(position);
                }

                anotherPosition = position;
                lastDistance = static_cast<float>(currentPosition.distanceSquared(destination));
            };

            if (onlyStraight) {
                mapGenerator->foreachDirectNeighbor(currentPosition, functor);
            } else {
                mapGenerator->foreachNeighbor(currentPosition, functor);
            }

            if (anotherPosition.isValid()) {
                if (clearedTiles) {
                    clearedTiles->insert(anotherPosition);
                }

                mapGenerator->setOccupied(anotherPosition, TileType::Free);
                currentPosition = anotherPosition;
            }
        }

        if (!(result || distance < lastDistance || anotherPosition.isValid())) {
            if (mapGenerator->isDebugMode()) {
                std::cout << "No tile closer than " << currentPosition << " found on path from "
                          << source << " to " << destination << '\n';
            }

            break;
        }
    }

    return result;
}

bool TemplateZone::connectPath(const Position& source, bool onlyStraight)
{
    // A* algorithm

    // The set of nodes already evaluated
    std::set<Position> closed;
    // The set of tentative nodes to be evaluated, initially containing the start node
    PriorityQueue open;
    // The map of navigated nodes
    std::map<Position, Position> cameFrom;
    std::map<Position, float> distances;

    // First node points to finish condition
    cameFrom[source] = Position{-1, -1};
    distances[source] = 0.f;
    open.push({source, 0.f});

    // Cost from start along best known path.
    // Estimated total cost from start to goal through y.

    while (!open.empty()) {
        auto node{open.top()};
        open.pop();
        const auto currentNode{node.first};

        closed.insert(currentNode);

        // We reached free paths, stop
        if (mapGenerator->isFree(currentNode)) {
            // Trace the path using the saved parent information and return path
            auto backTracking{currentNode};
            while (cameFrom[backTracking].isValid()) {
                mapGenerator->setOccupied(backTracking, TileType::Free);
                backTracking = cameFrom[backTracking];
            }

            mapGenerator->setOccupied(backTracking, TileType::Free);
            return true;
        }

        auto functor = [this, &open, &closed, &cameFrom, &currentNode, &distances](Position& pos) {
            if (contains(closed, pos)) {
                return;
            }

            // No paths through blocked or occupied tiles, stay within zone
            if (mapGenerator->isBlocked(pos) || mapGenerator->getZoneId(pos) != id) {
                return;
            }

            const auto distance{static_cast<int>(distances[currentNode]) + 1};
            int bestDistanceSoFar{std::numeric_limits<int>::max()};

            auto it{distances.find(pos)};
            if (it != distances.end()) {
                bestDistanceSoFar = static_cast<int>(it->second);
            }

            if (distance < bestDistanceSoFar) {
                cameFrom[pos] = currentNode;
                open.push({pos, static_cast<float>(distance)});
                distances[pos] = static_cast<float>(distance);
            }
        };

        if (onlyStraight) {
            mapGenerator->foreachDirectNeighbor(currentNode, functor);
        } else {
            mapGenerator->foreachNeighbor(currentNode, functor);
        }
    }

    // These tiles are sealed off and can't be connected anymore
    for (const auto& tile : closed) {
        if (mapGenerator->isPossible(tile)) {
            mapGenerator->setOccupied(tile, TileType::Blocked);
        }

        eraseIfPresent(possibleTiles, tile);
    }

    return false;
}

std::unique_ptr<Stack> TemplateZone::createStack(const GroupInfo& stackInfo, bool neutralOwner)
{
    const auto& stackValue{stackInfo.value};
    if (!stackValue) {
        return nullptr;
    }

    auto& rand{mapGenerator->randomGenerator};

    int strength = static_cast<int>(rand.pickValue(stackValue));

    // Roll number of units
    int soldiersStrength{strength - getGameInfo()->getMinLeaderValue()};

    // Determine maximum possible soldier units in stack.
    // Make sure we do not roll too many soldier units for stack with low strength
    const int maxUnitsPossible = std::min(5,
                                          soldiersStrength / getGameInfo()->getMinSoldierValue());
    // Pick how many soldier units will be in stack along with leader.
    // This will affect leader pick and resulting stack contents
    int soldiersTotal{rand.nextInteger(0, maxUnitsPossible)};
    // +1 because of leader
    int unitsTotal = soldiersTotal + 1;

    // do constrained sum to get unit values
    auto unitValues = constrainedSum(unitsTotal, strength, rand);

    std::size_t unusedValue{};
    std::size_t valuesConsumed{};

    // Pick leader
    const UnitInfo* leaderInfo{};
    const auto& leaders{getGameInfo()->getLeaders()};

    if (!stackInfo.leaderIds.empty()) {
        leaderInfo = pickStackLeader(unusedValue, valuesConsumed, unitValues, 
                                       stackInfo.leaderIds);
    }

    if (!leaderInfo) {
        leaderInfo = createStackLeader(unusedValue, valuesConsumed, unitValues,
                                       stackInfo.subraceTypes);
    }

    if (!leaderInfo) {
        std::string msg{"Could not pick stack leader. Stack value: "};
        msg += std::to_string(strength);
        msg += ". Units total: ";
        msg += std::to_string(unitsTotal);

        throw std::runtime_error(msg);
    }

    // Positions in group that are free
    std::set<int> positions{0, 1, 2, 3, 4, 5};
    // Default leader position
    std::size_t leaderPosition{2};

    // Find place in group for leader
    if (leaderInfo->isBig()) {
        // Big units always at front line
        positions.erase(leaderPosition);
        positions.erase(leaderPosition + 1);
    } else if (isSupport(*leaderInfo)) {
        // Supports are always at back line
        leaderPosition = 3;
        positions.erase(leaderPosition);
    } else if (leaderInfo->getAttackReach() != ReachType::Adjacent) {
        // Ranged units at back line
        leaderPosition = 3;
        positions.erase(leaderPosition);
    } else {
        // Averyone else at front line
        positions.erase(leaderPosition);
    }

    GroupUnits soldiers = {{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

    // Pick soldier units 1 by 1, starting from value that was not used for leader
    if (valuesConsumed < unitValues.size()) {
        std::vector<std::size_t> soldierValues(unitValues.begin() + valuesConsumed,
                                               unitValues.end());

        createGroup(unusedValue, positions, soldiers, soldierValues, stackInfo.subraceTypes);
    }

    // Check if we still have unused value and free positions in group.
    // This should help with better stack value usage
    // and reduce number of stacks with single ranged or support leader
    tightenGroup(unusedValue, positions, soldiers, stackInfo.subraceTypes);

    if (mapGenerator->isDebugMode()) {
        // +1 because of leader
        int unitsCreated{1};
        int createdValue = leaderInfo->getValue();

        for (std::size_t position = 0; position < soldiers.size(); ++position) {
            const auto* unitInfo{soldiers[position]};
            if (!unitInfo) {
                continue;
            }

            ++unitsCreated;
            createdValue += unitInfo->getValue();

            if (unitInfo->isBig()) {
                // Skip second part of big unit
                ++position;
            }
        }

        std::cout << "Stack value " << strength << ", created " << createdValue << ", unused "
                  << strength - createdValue << ". Units " << unitsTotal << ", created "
                  << unitsCreated << '\n';
    }

    auto stack{createStack(*leaderInfo, leaderPosition, soldiers, neutralOwner)};

    // Make sure we create leader with correct leadership value
    int leadershipRequired = leaderInfo->isBig() ? 2 : 1;

    for (std::size_t position = 0; position < soldiers.size(); ++position) {
        const auto* unitInfo{soldiers[position]};
        if (!unitInfo) {
            continue;
        }

        ++leadershipRequired;

        if (unitInfo->isBig()) {
            ++leadershipRequired;
            // Skip second part of big unit
            ++position;
        }
    }

    if (leaderInfo->getLeadership() < leadershipRequired) {
        const int diff = leadershipRequired - leaderInfo->getLeadership();
        Unit* leaderUnit = mapGenerator->map->find<Unit>(stack->getLeader());

        for (int i = 0; i < diff; ++i) {
            leaderUnit->addModifier(CMidgardID("G000UM9031")); // +1 Leadership
        }
    }

    auto stackLoot{createLoot(stackInfo.loot)};
    auto& stackInventory{stack->getInventory()};

    for (const auto& [id, amount] : stackLoot) {
        for (int i = 0; i < amount; ++i) {
            auto itemId{mapGenerator->createId(CMidgardID::Type::Item)};
            auto item{std::make_unique<Item>(itemId)};
            item->setItemType(id);

            mapGenerator->insertObject(std::move(item));
            stackInventory.add(itemId);
        }
    }

    return stack;
}

std::unique_ptr<Stack> TemplateZone::createStack(const UnitInfo& leaderInfo,
                                                 std::size_t leaderPosition,
                                                 const GroupUnits& groupUnits,
                                                 bool neutralOwner)
{
    auto& rand{mapGenerator->randomGenerator};

    // Create stack
    auto stackId{mapGenerator->createId(CMidgardID::Type::Stack)};
    auto stack{std::make_unique<Stack>(stackId)};

    stack->setMove(leaderInfo.getMove());
    stack->setFacing(getRandomFacing(rand));

    // Create leader unit
    auto leaderId{mapGenerator->createId(CMidgardID::Type::Unit)};
    auto leader{std::make_unique<Unit>(leaderId)};

    leader->setImplId(leaderInfo.getUnitId());
    leader->setHp(leaderInfo.getHp());
    leader->setName(getUnitName(leaderInfo, rand, neutralOwner));

    mapGenerator->insertObject(std::move(leader));

    const auto leaderAdded = stack->addLeader(leaderId, leaderPosition, leaderInfo.isBig());
    assert(leaderAdded);

    createGroupUnits(stack->getGroup(), groupUnits);

    return std::move(stack);
}

const UnitInfo* TemplateZone::pickStackLeader(std::size_t& unusedValue,
                                              std::size_t& valuesConsumed,
                                              const std::vector<std::size_t>& unitValues,
                                              std::set<CMidgardID> leaderIds)
{
    auto& rand{mapGenerator->randomGenerator};

    auto leadersRequired = [leaderIds](const UnitInfo* info) {
        return !contains(leaderIds, info->getUnitId());
    };

    std::size_t unused = unusedValue;
    const UnitInfo* leaderInfo{pickLeader(rand, {leadersRequired})};

    if (leaderInfo) {
        for (std::size_t i = 0; i < unitValues.size(); ++i) {
            unused += unitValues[i];
            valuesConsumed = i + 1;
            if (i == 0 and leaderInfo->isBig()) {
                continue;
            }
            if (unused > leaderInfo->getValue()) {
                break;
            }
        }
        if (unused < leaderInfo->getValue()) {
            unusedValue = 0;
        } else {
            unusedValue = unused - leaderInfo->getValue();
        }
        return leaderInfo;
    }

    return nullptr;
}

const UnitInfo* TemplateZone::createStackLeader(std::size_t& unusedValue,
                                                std::size_t& valuesConsumed,
                                                const std::vector<std::size_t>& unitValues,
                                                const std::set<SubRaceType>& allowedSubraces)
{
    auto& rand{mapGenerator->randomGenerator};

    // How many failed attempts considered as a stop condition
    constexpr std::size_t totalFails{5};
    // How fast minValue coefficient decreases after each unsuccessfull attempt
    constexpr float minValueCoeffDecrease{0.15f};
    // Coefficient that determines minimum leader value for pick.
    // Gradually decreased if we struggle to pick leader
    float minValueCoeff{0.65f};
    // How many times we failed to pick a leader
    std::size_t failedAttempts{0};

    while (failedAttempts < totalFails) {
        std::size_t unused = unusedValue;

        for (std::size_t i = 0; i < unitValues.size(); ++i) {
            const std::size_t value = unitValues[i] + unused;

            const float minValue = value * minValueCoeff;
            // We can't choose a large squad if the experience is divided into 6 parts
            const bool canPlaceBig = unitValues.size() < 6;

            auto filter = [allowedSubraces, minValue, value, canPlaceBig](const UnitInfo* info) {
                if (!allowedSubraces.empty()) {
                    if (!contains(allowedSubraces, info->getSubrace())) {
                        return true;
                    }
                }
                if (!canPlaceBig && info->isBig()) {
                    // Remove big units if we can not place them
                    return true;
                }
                return static_cast<float>(info->getValue()) < minValue || info->getValue() > value;
            };

            auto noForbiddenOnTemplate = [this](const UnitInfo* info) {
                return contains(mapGenerator->mapGenOptions.mapTemplate->settings.forbiddenUnits,
                                info->getUnitId());
            };

            const UnitInfo* leaderInfo{
                pickLeader(rand, {filter, noForbiddenOnTemplate, noForbiddenUnit})};
            if (leaderInfo) {
                // Accumulate unused value after picking a leader
                unusedValue = value - leaderInfo->getValue();

                valuesConsumed = i + 1;

                return leaderInfo;
            }

            // Could not pick leader.
            // Consume next unit value for leader pick and remember all unused
            unused = value;
        }

        // Consumed all unit values and still could not pick leader.
        // Decrease minValue range, count how many times we failed
        minValueCoeff = std::max(0.f, minValueCoeff - minValueCoeffDecrease);
        ++failedAttempts;
    }

    // Could not pick any leader.
    // Either constraints are too tight, or something wrong with value (or unit values)
    // Pick weakest one just to create the stack and do not lose its value
    const auto& leaders{getGameInfo()->getLeaders()};
    auto it = std::find_if(leaders.begin(), leaders.end(), [](const UnitInfo* info) {
        return info->getValue() == getGameInfo()->getMinLeaderValue();
    });
    if (it != leaders.end()) {
        std::cerr << "Could not pick leader, place weakest\n";
        unusedValue = 0;
        valuesConsumed = 0;
        return *it;
    }

    return nullptr;
}

void TemplateZone::createGroup(std::size_t& unusedValue,
                               std::set<int>& positions,
                               GroupUnits& groupUnits,
                               const std::vector<std::size_t>& unitValues,
                               const std::set<SubRaceType>& allowedSubraces)
{
    auto& rand{mapGenerator->randomGenerator};

    // Pick soldier units 1 by 1, starting from value that was not used for leader
    for (std::size_t i = 0; i < unitValues.size() && !positions.empty(); ++i) {
        auto value = unitValues[i] + unusedValue;
        float minValueCoeff = 0.95f - positions.size() * 0.05f;
        auto minValue = value * minValueCoeff;

        auto noWrongValue = [minValue, value](const UnitInfo* info) {
            return info->getValue() < minValue || info->getValue() > value;
        };

        // Pick random position in group
        int position = *getRandomElement(positions, rand);

        // If front line, pick melee only
        const auto frontline = position % 2 == 0;
        // Second position in case of big unit
        const auto secondPosition = frontline ? position + 1 : position - 1;
        // We can place big unit if front and back line positions are free
        const auto canPlaceBig = positions.count(position) && positions.count(secondPosition) && positions.size() > unitValues.size();

        auto filter = [allowedSubraces, canPlaceBig, frontline](const UnitInfo* info) {
            if (!allowedSubraces.empty()) {
                if (allowedSubraces.find(info->getSubrace()) == allowedSubraces.end()) {
                    // Remove units of subraces that are not allowed
                    return true;
                }
            }

            if (!canPlaceBig && info->isBig()) {
                // Remove big units if we can not place them
                return true;
            }

            // We don't care about front or back line and unit attack reach in case of big unit
            if (canPlaceBig) {
                return false;
            }

            if (frontline && info->getAttackReach() != ReachType::Adjacent) {
                // Remove ranged units from frontline
                return true;
            }

            if (!frontline && info->getAttackReach() == ReachType::Adjacent) {
                // Remove melee units from backline
                return true;
            }

            return false;
        };

        auto noForbiddenOnTemplate = [this](const UnitInfo* info) {
            return contains(mapGenerator->mapGenOptions.mapTemplate->settings.forbiddenUnits,
                            info->getUnitId());
        };

        const UnitInfo* info = pickUnit(rand, {filter, noWrongValue, noForbiddenOnTemplate,
                                               noForbiddenUnit});
        if (info) {
            // We picked a unit, update unused value
            unusedValue = value - info->getValue();

            if (info->isBig()) {
                positions.erase(position);
                groupUnits[position] = info;

                positions.erase(secondPosition);
                groupUnits[secondPosition] = info;
            } else {
                // We could have picked big unit, but got a small one.
                // Check if unit's attack range is optimal for its placement.
                // We can adjust unit position since second position is empty
                if (canPlaceBig && frontline && info->getAttackReach() != ReachType::Adjacent) {
                    // Small soldier unit at frontline.
                    position = secondPosition;
                } else if (canPlaceBig && !frontline
                           && info->getAttackReach() == ReachType::Adjacent) {
                    // Small melee unit at backline.
                    position = secondPosition;
                }

                positions.erase(position);
                groupUnits[position] = info;
            }
        } else {
            // Accumulate unused value
            unusedValue += unitValues[i];
        }
    }
}

void TemplateZone::tightenGroup(std::size_t& unusedValue,
                                std::set<int>& positions,
                                GroupUnits& groupUnits,
                                const std::set<SubRaceType>& allowedSubraces)
{
    auto& rand{mapGenerator->randomGenerator};

    // Start with somewhat relaxed minimum value.
    // Gradually decrease min value expectation as we struggle to pick units
    float minValueCoeff = 1.f - positions.size() * 0.05f;
    // How many times we failed to pick a unit
    int failedAttempts{0};
    // How many failed attemts considered as a stop
    const int totalFails{200};

    while (failedAttempts < totalFails && !positions.empty()
           && unusedValue >= getGameInfo()->getMinSoldierValue()) {
        auto value = unusedValue;
        auto minValue = value * minValueCoeff;

        auto noWrongValue = [minValue, value](const UnitInfo* info) {
            return info->getValue() < minValue || info->getValue() > value;
        };

        int position = *getRandomElement(positions, rand);

        const auto frontline = position % 2 == 0;
        // Second position in case of big unit
        const auto secondPosition = frontline ? position + 1 : position - 1;
        // We can place big unit if front and back line positions are free
        const auto canPlaceBig = positions.count(position) && positions.count(secondPosition);

        auto filter = [allowedSubraces, canPlaceBig, frontline](const UnitInfo* info) {
            if (!allowedSubraces.empty()) {
                if (allowedSubraces.find(info->getSubrace()) == allowedSubraces.end()) {
                    // Remove units of subraces that are not allowed
                    return true;
                }
            }

            if (!canPlaceBig && info->isBig()) {
                // Remove big units if we can not place them
                return true;
            }

            // We don't care about front or back line and unit attack reach in case of big unit
            if (canPlaceBig) {
                return false;
            }

            if (frontline && info->getAttackReach() != ReachType::Adjacent) {
                // Remove ranged units from frontline
                return true;
            }

            if (!frontline && info->getAttackReach() == ReachType::Adjacent) {
                // Remove melee units from backline
                return true;
            }

            return false;
        };

        auto noForbiddenOnTemplate = [this](const UnitInfo* info) {
            return contains(mapGenerator->mapGenOptions.mapTemplate->settings.forbiddenUnits,
                            info->getUnitId());
        };

        const UnitInfo* info = pickUnit(rand, {filter, noWrongValue, noForbiddenOnTemplate,
                                               noForbiddenUnit});
        if (info) {
            // We picked a unit, update unused value
            unusedValue = value - info->getValue();
            // Reset failed attempts counter
            failedAttempts = 0;

            if (info->isBig()) {
                positions.erase(position);
                groupUnits[position] = info;

                positions.erase(secondPosition);
                groupUnits[secondPosition] = info;
            } else {
                // We could have picked big unit, but got a small one.
                // Check if unit's attack range is optimal for its placement.
                // We can adjust unit position since second position is empty
                if (canPlaceBig && frontline && info->getAttackReach() != ReachType::Adjacent) {
                    // Small soldier unit at frontline.
                    position = secondPosition;
                } else if (canPlaceBig && !frontline
                           && info->getAttackReach() == ReachType::Adjacent) {
                    // Small melee unit at backline.
                    position = secondPosition;
                }

                positions.erase(position);
                groupUnits[position] = info;
            }
            minValueCoeff = 1.f - positions.size() * 0.05f;

        } else {
            // Could not pick a unit, unused value remains the same
            // Decrease minValue range, count how many times we failed to pick
            minValueCoeff = std::max(0.f, minValueCoeff - 0.05f);
            ++failedAttempts;
        }
    }
}

void TemplateZone::createGroupUnits(Group& group, const GroupUnits& groupUnits)
{
    for (std::size_t position = 0; position < groupUnits.size(); ++position) {
        const auto* unitInfo{groupUnits[position]};
        if (!unitInfo) {
            continue;
        }

        // Create unit
        auto unitId{mapGenerator->createId(CMidgardID::Type::Unit)};
        auto unit{std::make_unique<Unit>(unitId)};
        unit->setImplId(unitInfo->getUnitId());
        unit->setLevel(unitInfo->getLevel());
        unit->setHp(unitInfo->getHp());

        // Add it to scenario
        mapGenerator->insertObject(std::move(unit));

        // Add it to group
        auto unitAdded{group.addUnit(unitId, position, unitInfo->isBig())};
        assert(unitAdded);

        if (unitInfo->isBig()) {
            // Skip second part of big unit
            ++position;
        }
    }
}

Village* TemplateZone::placeCity(const Position& position, const CityInfo& cityInfo)
{
    auto& rand{mapGenerator->randomGenerator};

    // Create city of specified tier, assign position, owner, subrace
    auto villageId{mapGenerator->createId(CMidgardID::Type::Fortification)};
    auto village{std::make_unique<Village>(villageId)};

    CMidgardID ownerId{mapGenerator->getPlayerId(cityInfo.owner)};
    CMidgardID subraceId{mapGenerator->getSubraceId(cityInfo.owner)};

    if (ownerId == emptyId || subraceId == emptyId) {
        ownerId = mapGenerator->getNeutralPlayerId();
        subraceId = mapGenerator->getNeutralSubraceId();
    }

    village->setOwner(ownerId);
    village->setSubrace(subraceId);
    village->setTier(cityInfo.tier);

    if (cityInfo.name.empty()) {
        village->setName(*getRandomElement(getGameInfo()->getCityNames(), rand));
    } else {
        village->setName(cityInfo.name);
    }

    village->setAiPriority(cityInfo.aiPriority);
    village->setGapMask(cityInfo.gapMask);

    auto villagePtr{village.get()};

    decorations.push_back(std::make_unique<VillageDecoration>(villagePtr));
    placeObject(std::move(village), position);
    clearEntrance(*villagePtr);

    // Create garrison and loot
    const auto& garrisonValue{cityInfo.garrison.value};
    if (garrisonValue) {
        std::size_t unusedValue{};
        std::set<int> positions;
        GroupUnits units = {{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

        const std::size_t value{rand.pickValue(garrisonValue)};
        auto values{constrainedSum(cityInfo.tier, value, rand)};

        switch (cityInfo.tier) {
        case 1:
            // Tier 1 city will always have melee defender in center
            positions.insert(2);
            break;

        case 2: {
            // Tier 2 city will have center frontline position and one random
            positions.insert(2);

            const std::set<int> possiblePositions = {0, 1, 3, 4, 5};
            positions.insert(*getRandomElement(possiblePositions, rand));
            break;
        }
        case 3: {
            // Tier 3 city will have center frontline position and two random
            positions.insert(2);

            std::set<int> possiblePositions = {0, 1, 3, 4, 5};
            const int pos = *getRandomElement(possiblePositions, rand);
            possiblePositions.erase(pos);

            positions.insert(pos);
            positions.insert(*getRandomElement(possiblePositions, rand));
            break;
        }
        default: {
            // Tier 4 and 5 cities will have random positions excluded
            std::set<int> possiblePositions = {0, 1, 2, 3, 4, 5};
            for (std::uint8_t i = cityInfo.tier; i < 6; ++i) {
                auto pos = getRandomElement(possiblePositions, rand);
                possiblePositions.erase(*pos);
            }

            positions.swap(possiblePositions);
            break;
        }
        }

        createGroup(unusedValue, positions, units, values, cityInfo.garrison.subraceTypes);
        tightenGroup(unusedValue, positions, units, cityInfo.garrison.subraceTypes);
        createGroupUnits(villagePtr->getGroup(), units);
    }

    auto loot{createLoot(cityInfo.garrison.loot)};
    auto& inventory{villagePtr->getInventory()};

    for (const auto& [id, amount] : loot) {
        for (int i = 0; i < amount; ++i) {
            auto itemId{mapGenerator->createId(CMidgardID::Type::Item)};
            auto item{std::make_unique<Item>(itemId)};
            item->setItemType(id);

            mapGenerator->insertObject(std::move(item));
            inventory.add(itemId);
        }
    }

    const bool neutralOwner{ownerId == mapGenerator->getNeutralPlayerId()};
    // Create visitor stack and its loot
    auto stack{createStack(cityInfo.stack, neutralOwner)};
    if (stack) {
        // Make sure visitor stack is inside the city
        villagePtr->setStack(stack->getId());
        stack->setInside(villageId);

        stack->setOwner(ownerId);
        stack->setSubrace(subraceId);

        if (!cityInfo.stack.name.empty()) {
            Unit* leader{mapGenerator->map->find<Unit>(stack->getLeader())};
            if (leader) {
                leader->setName(cityInfo.stack.name);
            }
        }

        if (!cityInfo.stack.leaderModifiers.empty()) {
            Unit* leader{mapGenerator->map->find<Unit>(stack->getLeader())};
            if (leader) {
                for (auto modifierId : cityInfo.stack.leaderModifiers) {
                    leader->addModifier(modifierId);
                }
            }
        }

        stack->setOrder(cityInfo.stack.order);
        stack->setAiPriority(cityInfo.stack.aiPriority);

        placeObject(std::move(stack), position);
    }

    return villagePtr;
}

Site* TemplateZone::placeMerchant(const Position& position, const MerchantInfo& merchantInfo)
{
    auto& rand{mapGenerator->randomGenerator};

    auto merchantId{mapGenerator->createId(CMidgardID::Type::Site)};
    auto merchant{std::make_unique<Merchant>(merchantId)};

    const SiteText& text = *getRandomElement(getGameInfo()->getMerchantTexts(), rand);
    if (merchantInfo.name.empty()) {
        merchant->setTitle(text.name);
    } else {
        merchant->setTitle(merchantInfo.name);
    }

    if (merchantInfo.description.empty()) {
        merchant->setDescription(text.description);
    } else {
        merchant->setDescription(merchantInfo.description);
    }

    merchant->setImgIso(*getRandomElement(getGeneratorSettings().merchants.images, rand));
    merchant->setAiPriority(merchantInfo.aiPriority);

    // Create merchant items
    const auto items{createLoot(merchantInfo.items, true)};
    for (const auto& [id, amount] : items) {
        merchant->addItem(id, amount);
    }

    auto merchantPtr{merchant.get()};
    placeObject(std::move(merchant), position);
    guardObject(*merchantPtr, merchantInfo.guard);

    return merchantPtr;
}

Site* TemplateZone::placeMage(const Position& position, const MageInfo& mageInfo)
{
    auto& rand{mapGenerator->randomGenerator};

    auto mageId{mapGenerator->createId(CMidgardID::Type::Site)};
    auto mage{std::make_unique<Mage>(mageId)};

    const SiteText& text = *getRandomElement(getGameInfo()->getMageTexts(), rand);

    if (mageInfo.name.empty()) {
        mage->setTitle(text.name);
    } else {
        mage->setTitle(mageInfo.name);
    }

    if (mageInfo.description.empty()) {
        mage->setDescription(text.description);
    } else {
        mage->setDescription(mageInfo.description);
    }

    mage->setImgIso(*getRandomElement(getGeneratorSettings().mages.images, rand));
    mage->setAiPriority(mageInfo.aiPriority);

    // Generate random spells of specified types
    if (mageInfo.value) {
        const auto& value{mageInfo.value};
        const int desiredValue{static_cast<int>(rand.pickValue(value))};
        int currentValue{};

        std::set<CMidgardID> pickedSpells;

        auto noDuplicates = [&pickedSpells](const SpellInfo* info) {
            // Make sure we don't pick duplicates
            return contains(pickedSpells, info->getSpellId());
        };

        auto noWrongType = [types = &mageInfo.spellTypes](const SpellInfo* info) {
            if (types->empty()) {
                // No types specified, allow all spells
                return false;
            }

            // Remove spells of types that mage is not allowed to sell
            return !contains(*types, info->getSpellType());
        };

        auto noWrongLevel = [&level = mageInfo.spellLevels](const SpellInfo* info) {
            if (!level) {
                // No level range specified, allow all spell levels
                return false;
            }

            return info->getLevel() < level.min || info->getLevel() > level.max;
        };

        auto noForbiddenOnTemplate = [this](const SpellInfo* info) {
            return contains(mapGenerator->mapGenOptions.mapTemplate->settings.forbiddenSpells,
                            info->getSpellId());
        };

        while (currentValue <= desiredValue) {
            const int remainingValue = desiredValue - currentValue;

            auto noWrongValue = [remainingValue](const SpellInfo* info) {
                return info->getValue() > remainingValue;
            };

            auto spell{pickSpell(rand, {noWrongType, noWrongLevel, noWrongValue,
                                        noForbiddenOnTemplate, noForbiddenSpell, noDuplicates})};
            if (!spell) {
                // Could not pick anything, stop
                break;
            }

            currentValue += spell->getValue();
            mage->addSpell(spell->getSpellId());
            pickedSpells.insert(spell->getSpellId());
        }
    }

    for (const auto& spell : mageInfo.requiredSpells) {
        mage->addSpell(spell);
    }

    auto sitePtr{mage.get()};
    placeObject(std::move(mage), position);
    guardObject(*sitePtr, mageInfo.guard);

    return sitePtr;
}

Site* TemplateZone::placeMercenary(const Position& position, const MercenaryInfo& mercInfo)
{
    auto& rand{mapGenerator->randomGenerator};

    auto mercenaryId{mapGenerator->createId(CMidgardID::Type::Site)};
    auto mercenary{std::make_unique<Mercenary>(mercenaryId)};

    const SiteText& text = *getRandomElement(getGameInfo()->getMercenaryTexts(), rand);

    if (mercInfo.name.empty()) {
        mercenary->setTitle(text.name);
    } else {
        mercenary->setTitle(mercInfo.name);
    }

    if (mercInfo.description.empty()) {
        mercenary->setDescription(text.description);
    } else {
        mercenary->setDescription(mercInfo.description);
    }

    mercenary->setImgIso(*getRandomElement(getGeneratorSettings().mercenaries.images, rand));
    mercenary->setAiPriority(mercInfo.aiPriority);

    // Generate random mercenary units of specified subraces
    if (mercInfo.value) {
        const auto& value{mercInfo.value};
        const int desiredValue{static_cast<int>(rand.pickValue(value))};
        int currentValue{};

        auto noWrongType = [types = &mercInfo.subraceTypes](const UnitInfo* info) {
            if (types->empty()) {
                // No types specified, allow all units
                return false;
            }

            // Remove units of subraces that mercenary is not allowed to sell
            return types->find(info->getSubrace()) == types->end();
        };

        auto noForbiddenOnTemplate = [this](const UnitInfo* info) {
            return contains(mapGenerator->mapGenOptions.mapTemplate->settings.forbiddenUnits,
                            info->getUnitId());
        };

        const auto& enrollValue{mercInfo.enrollValue};

        while (currentValue <= desiredValue) {
            const int remainingValue = desiredValue - currentValue;

            auto noWrongValue = [remainingValue, enrollValue](const UnitInfo* info) {
                if (enrollValue && (info->getEnrollCost() < enrollValue.min ||
                    info->getEnrollCost() > enrollValue.max)) {
                    // If user specified single units value range,
                    // filter units that are outside of it
                    return true;
                }

                return info->getEnrollCost() > remainingValue;
            };

            auto unit{pickUnit(rand, {noWrongType, noWrongValue, noForbiddenOnTemplate,
                                      noForbiddenUnit})};
            if (!unit) {
                // Could not pick anything, stop
                break;
            }

            currentValue += unit->getEnrollCost();
            mercenary->addUnit(unit->getUnitId(), unit->getLevel(), true);
        }
    }

    // Add required units
    for (const auto& unit : mercInfo.requiredUnits) {
        mercenary->addUnit(unit.unitId, unit.level, unit.unique);
    }

    auto mercPtr{mercenary.get()};
    placeObject(std::move(mercenary), position);
    guardObject(*mercPtr, mercInfo.guard);

    return mercPtr;
}

Site* TemplateZone::placeTrainer(const Position& position, const TrainerInfo& trainerInfo)
{
    auto& rand{mapGenerator->randomGenerator};

    auto trainerId{mapGenerator->createId(CMidgardID::Type::Site)};
    auto trainer{std::make_unique<Trainer>(trainerId)};

    const SiteText& text = *getRandomElement(getGameInfo()->getTrainerTexts(), rand);

    if (trainerInfo.name.empty()) {
        trainer->setTitle(text.name);
    } else {
        trainer->setTitle(trainerInfo.name);
    }

    if (trainerInfo.description.empty()) {
        trainer->setDescription(text.description);
    } else {
        trainer->setDescription(trainerInfo.description);
    }

    trainer->setImgIso(*getRandomElement(getGeneratorSettings().trainers.images, rand));
    trainer->setAiPriority(trainerInfo.aiPriority);

    auto trainerPtr{trainer.get()};
    placeObject(std::move(trainer), position);
    guardObject(*trainerPtr, trainerInfo.guard);

    return trainerPtr;
}

Site* TemplateZone::placeMarket(const Position& position, const ResourceMarketInfo& marketInfo)
{
    auto& rand{mapGenerator->randomGenerator};

    auto marketId{mapGenerator->createId(CMidgardID::Type::Site)};
    auto market{std::make_unique<ResourceMarket>(marketId)};

    const SiteText& text = *getRandomElement(getGameInfo()->getMarketTexts(), rand);

    if (marketInfo.name.empty()) {
        market->setTitle(text.name);
    } else {
        market->setTitle(marketInfo.name);
    }

    if (marketInfo.description.empty()) {
        market->setDescription(text.description);
    } else {
        market->setDescription(marketInfo.description);
    }

    market->setImgIso(*getRandomElement(getGeneratorSettings().resourceMarkets.images, rand));
    market->setAiPriority(marketInfo.aiPriority);

    market->setExchangeRates(marketInfo.exchangeRates);
    Currency stock{};

    for (const auto& [resource, info] : marketInfo.stock) {
        if (info.infinite) {
            market->setInfiniteStock(resource, true);
        } else {
            stock.set(resource, static_cast<std::uint16_t>(rand.pickValue(info.amount)));
        }
    }

    market->setStock(stock);

    auto marketPtr{market.get()};
    placeObject(std::move(market), position);
    guardObject(*marketPtr, marketInfo.guard);

    return marketPtr;
}

Ruin* TemplateZone::placeRuin(const Position& position, const RuinInfo& ruinInfo)
{
    auto& rand{mapGenerator->randomGenerator};

    auto ruinId{mapGenerator->createId(CMidgardID::Type::Ruin)};
    auto ruin{std::make_unique<Ruin>(ruinId)};

    const SiteText& text = *getRandomElement(getGameInfo()->getRuinTexts(), rand);
    if (ruinInfo.name.empty()) {
        ruin->setTitle(text.name);
    } else {
        ruin->setTitle(ruinInfo.name);
    }

    ruin->setImage(*getRandomElement(getGeneratorSettings().ruins.images, rand));
    ruin->setAiPriority(ruinInfo.aiPriority);

    const auto& guardValue{ruinInfo.guard.value};
    if (guardValue) {
        constexpr std::size_t maxRuinUnits{6};

        std::size_t unusedValue{};
        std::set<int> positions = {0, 1, 2, 3, 4, 5};
        GroupUnits units = {{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};

        const std::size_t value{rand.pickValue(guardValue)};
        auto values{constrainedSum(maxRuinUnits, value, rand)};

        createGroup(unusedValue, positions, units, values, ruinInfo.guard.subraceTypes);
        tightenGroup(unusedValue, positions, units, ruinInfo.guard.subraceTypes);

        createGroupUnits(ruin->getGroup(), units);
    }

    const auto& gold{ruinInfo.gold};
    if (gold) {
        const std::uint16_t goldValue{rand.pickValue(gold)};

        Currency cash;
        cash.set(ResourceType::Gold, goldValue);
        ruin->setCash(cash);
    }

    ruin->setItem(createRuinLoot(ruinInfo.loot));

    auto ruinPtr{ruin.get()};
    placeObject(std::move(ruin), position);

    return ruinPtr;
}

Stack* TemplateZone::placeZoneGuard(const Position& position, const GroupInfo& guardInfo)
{
    if (!guardInfo.value) {
        // No guard at all
        return nullptr;
    }

    auto stack{createStack(guardInfo, true)};
    if (!stack) {
        return nullptr;
    }

    CMidgardID ownerId{mapGenerator->getPlayerId(guardInfo.owner)};
    CMidgardID subraceId{mapGenerator->getSubraceId(guardInfo.owner)};

    if (ownerId == emptyId || subraceId == emptyId) {
        ownerId = mapGenerator->getNeutralPlayerId();
        subraceId = mapGenerator->getNeutralSubraceId();
    }

    stack->setOwner(ownerId);
    stack->setSubrace(subraceId);

    if (!guardInfo.name.empty()) {
        Unit* leader{mapGenerator->map->find<Unit>(stack->getLeader())};
        if (leader) {
            leader->setName(guardInfo.name);
        }
    }

    if (!guardInfo.leaderModifiers.empty()) {
        Unit* leader{mapGenerator->map->find<Unit>(stack->getLeader())};
        if (leader) {
            for (auto modifierId : guardInfo.leaderModifiers) {
                leader->addModifier(modifierId);
            }
        }
    }

    stack->setAiPriority(guardInfo.aiPriority);
    stack->setOrder(guardInfo.order);

    Stack* stackPtr{stack.get()};
    placeObject(std::move(stack), position);

    return stackPtr;
}

Bag* TemplateZone::placeBag(const Position& position)
{
    auto bagId{mapGenerator->createId(CMidgardID::Type::Bag)};
    auto bag{std::make_unique<Bag>(bagId)};

    const auto& bags = getGeneratorSettings().bags;

    const auto& bagImages = mapGenerator->map->getTile(position).isWater() ? bags.waterImages
                                                                           : bags.images;

    auto& rand{mapGenerator->randomGenerator};
    // Pick random bag image with respect to ground type
    bag->setImage(*getRandomElement(bagImages, rand));

    Bag* bagPtr{bag.get()};
    placeObject(std::move(bag), position);

    return bagPtr;
}

std::vector<std::pair<CMidgardID, int>> TemplateZone::createLoot(const LootInfo& loot,
                                                                 bool forMerchant)
{
    auto& rand{mapGenerator->randomGenerator};

    std::vector<std::pair<CMidgardID, int>> items;

    // Create required items
    for (const auto& item : loot.requiredItems) {
        if (item.itemId == emptyId) {
            continue;
        }

        const int amount{static_cast<int>(rand.pickValue(item.amount))};
        if (amount > 0) {
            // User can specify [0 : 1+] amount of required items,
            // make sure we only add valid amount
            items.push_back({item.itemId, amount});
        }
    }

    // Create random items of specified types and value
    const auto& value{loot.value};
    if (value) {
        const int desiredValue{static_cast<int>(rand.pickValue(value))};
        int currentValue{};

        auto noWrongType = [types = &loot.itemTypes, forMerchant](const ItemInfo* info) {
            if (forMerchant && info->getItemType() == ItemType::Valuable) {
                // Do not generate valuables as merchant goods
                return true;
            }

            if (types->empty()) {
                return false;
            }

            // Remove items of types that is not allowed
            return types->find(info->getItemType()) == types->end();
        };

        auto noForbiddenOnTemplate = [this](const ItemInfo* info) {
            return contains(mapGenerator->mapGenOptions.mapTemplate->settings.forbiddenItems,
                            info->getItemId());
        };

        const auto& itemValue{loot.itemValue};

        int picked{};
        while (currentValue <= desiredValue) {
            const int remainingValue = desiredValue - currentValue;

            auto noWrongValue = [remainingValue, &itemValue](const ItemInfo* info) {
                if (itemValue
                    && (info->getValue() < itemValue.min || info->getValue() > itemValue.max)) {
                    // If user specified single item value range,
                    // filter items that are outside of it
                    return true;
                }

                return info->getValue() > static_cast<int>(remainingValue);
            };

            auto item{pickItem(rand, {noWrongType, noWrongValue, noSpecialItem,
                                      noForbiddenOnTemplate, noForbiddenItem})};
            if (!item) {
                // Could not pick anything, stop
                break;
            }

            ++picked;
            currentValue += item->getValue();
            items.push_back({item->getItemId(), 1});
        }

        if (mapGenerator->isDebugMode()) {
            std::cout << "Loot value " << desiredValue << ", created " << currentValue << ", "
                      << picked << " items\n";
        }
    }

    return items;
}

CMidgardID TemplateZone::createRuinLoot(const LootInfo& loot)
{
    auto lootItems = createLoot(loot);
    if (lootItems.empty()) {
        return emptyId;
    }

    return lootItems[0].first;
}

void TemplateZone::initTerrain()
{
    if (type == TemplateZoneType::Water) {
        paintZoneTerrain(TerrainType::Neutral, GroundType::Water);
        return;
    }

    // TODO: create random patches of race-specific terrains,
    // excluding playable races in scenario
    // paintZoneTerrain(TerrainType::Neutral, GroundType::Plain);
}

void TemplateZone::fractalize()
{
    for (const auto& tile : tileInfo) {
        if (mapGenerator->isFree(tile)) {
            freePaths.insert(tile);
        }
    }

    std::vector<Position> clearedTiles(freePaths.begin(), freePaths.end());
    std::set<Position> possibleTiles;
    std::set<Position> tilesToIgnore;

    // TODO: move this setting into template for better zone free space control
    // TODO: adjust this setting based on template value
    // and number of objects (and their average size?)
    const float minDistance{7.5 * 10};

    for (const auto& tile : tileInfo) {
        if (mapGenerator->isPossible(tile)) {
            possibleTiles.insert(tile);
        }
    }

    // This should come from zone connections
    assert(!clearedTiles.empty());
    // Connect them with a grid
    std::vector<Position> nodes;

    if (type != TemplateZoneType::Junction) {
        // Junction is not fractalized,
        // has only one straight path everything else remains blocked
        while (!possibleTiles.empty()) {
            // Link tiles in random order
            std::vector<Position> tilesToMakePath(possibleTiles.begin(), possibleTiles.end());
            randomShuffle(tilesToMakePath, mapGenerator->randomGenerator);

            Position nodeFound{-1, -1};

            for (const auto& tileToMakePath : tilesToMakePath) {
                // Find closest free tile
                float currentDistance{1e10};
                Position closestTile{-1, -1};

                for (const auto& clearTile : clearedTiles) {
                    auto distance{static_cast<float>(tileToMakePath.distanceSquared(clearTile))};
                    if (distance < currentDistance) {
                        currentDistance = distance;
                        closestTile = clearTile;
                    }

                    if (currentDistance <= minDistance) {
                        // This tile is close enough. Forget about it and check next one
                        tilesToIgnore.insert(tileToMakePath);
                        break;
                    }
                }

                // If tiles is not close enough, make path to it
                if (currentDistance > minDistance) {
                    nodeFound = tileToMakePath;
                    nodes.push_back(nodeFound);
                    // From now on nearby tiles will be considered handled
                    clearedTiles.push_back(nodeFound);
                    // Next iteration - use already cleared tiles
                    break;
                }
            }

            // These tiles are already connected, ignore them
            for (const auto& tileToClear : tilesToIgnore) {
                eraseIfPresent(possibleTiles, tileToClear);
            }

            // Nothing else can be done (?)
            if (!nodeFound.isValid()) {
                break;
            }

            tilesToIgnore.clear();
        }
    }

    // Cut straight paths towards the center
    for (const auto& node : nodes) {
        auto subnodes{nodes};

        std::sort(subnodes.begin(), subnodes.end(), [&node](const Position& a, const Position& b) {
            return node.distanceSquared(a) < node.distanceSquared(b);
        });

        std::vector<Position> nearbyNodes;
        if (subnodes.size() >= 2) {
            // node[0] is our node we want to connect
            nearbyNodes.push_back(subnodes[1]);
        }

        if (subnodes.size() >= 3) {
            nearbyNodes.push_back(subnodes[2]);
        }

        // Connect with all the paths
        crunchPath(node, findClosestTile(freePaths, node), true, &freePaths);
        // Connect with nearby nodes
        for (const auto& nearbyNode : nearbyNodes) {
            // Do not allow to make another path network
            crunchPath(node, nearbyNode, true, &freePaths);
        }
    }

    // Make sure they are clear
    for (const auto& node : nodes) {
        mapGenerator->setOccupied(node, TileType::Free);
    }

    // Now block most distant tiles away from passages
    const float blockDistance{minDistance * 0.25f};

    for (const auto& tile : tileInfo) {
        if (!mapGenerator->isPossible(tile)) {
            continue;
        }

        if (freePaths.count(tile)) {
            continue;
        }

        bool closeTileFound{};
        for (const auto& clearTile : freePaths) {
            const auto distance{static_cast<float>(tile.distanceSquared(clearTile))};

            if (distance < blockDistance) {
                closeTileFound = true;
                break;
            }
        }

        if (!closeTileFound) {
            // This tile is far enough from passages
            mapGenerator->setOccupied(tile, TileType::Blocked);
        }
    }

    constexpr bool debugFractalize{false};

    if constexpr (debugFractalize) {
        char name[100] = {0};
        std::snprintf(name, sizeof(name) - 1, "zone %d fractalize.png", id);

        mapGenerator->debugTiles(name);
    }
}

void TemplateZone::placeCapital()
{
    auto& rand{mapGenerator->randomGenerator};

    // Create capital id
    auto capitalId{mapGenerator->createId(CMidgardID::Type::Fortification)};
    // Create capital object
    auto capitalCity{std::make_unique<Capital>(capitalId)};
    auto fort{capitalCity.get()};

    assert(ownerId != emptyId);
    fort->setOwner(ownerId);

    if (capital.name.empty()) {
        fort->setName(*getRandomElement(getGameInfo()->getCityNames(), rand));
    } else {
        fort->setName(capital.name);
    }

    fort->setAiPriority(capital.aiPriority);
    fort->setGapMask(capital.gapMask);

    auto ownerPlayer{mapGenerator->map->find<Player>(ownerId)};
    assert(ownerPlayer != nullptr);

    auto playerRace{mapGenerator->getRaceType(ownerPlayer->getRace())};

    const auto& raceInfo{getGameInfo()->getRaceInfo(playerRace)};
    const auto& unitsInfo{getGameInfo()->getUnits()};

    const auto& garrison{capital.garrison};

    {
        const UnitInfo* guardianInfo{unitsInfo.find(raceInfo.getGuardianUnitId())->second.get()};
        assert(guardianInfo);

        // Create capital garrison
        std::size_t unusedValue{};
        // Capital can fit entire group in its garrison.
        std::set<int> positions{0, 1, 2, 3, 4, 5};
        GroupUnits units = {{nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}};
        if (capital.guardian == true) {
            // Slot 2 is reserved for a capital guardian
            positions.erase(2);
            units[2] = guardianInfo;
            if (guardianInfo->isBig()) {
                units[3] = guardianInfo;
                positions.erase(3);
            }
        }

        const auto& garrisonValue{garrison.value};
        const std::size_t value{rand.pickValue(garrisonValue)};
        const auto values{constrainedSum(Group::groupSize, value, rand)};

        createGroup(unusedValue, positions, units, values, garrison.subraceTypes);
        tightenGroup(unusedValue, positions, units, garrison.subraceTypes);
        createGroupUnits(capitalCity->getGroup(), units);
    }

    // Create capital starting items
    auto loot{createLoot(garrison.loot)};
    auto& inventory{capitalCity->getInventory()};

    for (const auto& [id, amount] : loot) {
        for (int i = 0; i < amount; ++i) {
            auto itemId{mapGenerator->createId(CMidgardID::Type::Item)};
            auto item{std::make_unique<Item>(itemId)};
            item->setItemType(id);

            mapGenerator->insertObject(std::move(item));
            inventory.add(itemId);
        }
    }

    const auto* leaderInfo{unitsInfo.find(raceInfo.getLeaderIds()[0])->second.get()};
    assert(leaderInfo);

    // Create starting leader unit
    auto leaderId{mapGenerator->createId(CMidgardID::Type::Unit)};
    auto leader{std::make_unique<Unit>(leaderId)};
    leader->setImplId(leaderInfo->getUnitId());
    leader->setHp(leaderInfo->getHp());
    leader->setName(getUnitName(*leaderInfo, rand, false));
    mapGenerator->insertObject(std::move(leader));

    // Create starting stack
    auto stackId{mapGenerator->createId(CMidgardID::Type::Stack)};
    auto stack{std::make_unique<Stack>(stackId)};
    auto leaderAdded{stack->addLeader(leaderId, 2, leaderInfo->isBig())};
    assert(leaderAdded);
    stack->setInside(capitalId);
    stack->setMove(leaderInfo->getMove());
    stack->setOwner(ownerId);
    stack->setOrder(OrderType::Normal);

    fort->setStack(stackId);

    auto subraceType{mapGenerator->map->getSubRaceType(playerRace)};

    CMidgardID subraceId;
    mapGenerator->map->visit(CMidgardID::Type::SubRace,
                             [this, subraceType, &subraceId](const ScenarioObject* object) {
                                 auto subrace{dynamic_cast<const SubRace*>(object)};

                                 if (subrace->getType() == subraceType) {
                                     assert(subrace->getPlayerId() == ownerId);
                                     subraceId = subrace->getId();
                                 }
                             });

    fort->setSubrace(subraceId);
    stack->setSubrace(subraceId);

    // Add capital decoration
    decorations.push_back(std::make_unique<CapitalDecoration>(capitalCity.get()));

    // Place capital at the center of the zone
    placeObject(std::move(capitalCity), pos - fort->getSize() / 2,
                mapGenerator->map->getRaceTerrain(playerRace));
    clearEntrance(*fort);
    // All roads lead to tile near capital entrance
    setPosition(fort->getEntrance() + Position(1, 1));

    mapGenerator->registerZone(playerRace);

    placeObject(std::move(stack), fort->getPosition());

    // If there are known spells specified for player, add them
    KnownSpells* knownSpells{mapGenerator->map->find<KnownSpells>(ownerPlayer->getSpellsId())};
    assert(knownSpells);

    for (const auto& spellId : capital.spells) {
        knownSpells->add(spellId);
    }

    // If there are buildings specified for player, add them
    PlayerBuildings* playerBuildings{
        mapGenerator->map->find<PlayerBuildings>(ownerPlayer->getBuildingsId())};
    assert(playerBuildings);

    for (const auto& buildId : capital.buildings) {
        playerBuildings->add(buildId);
    }
}

void TemplateZone::placeCities()
{
    if (mapGenerator->isDebugMode()) {
        std::cout << "Creating cities\n";
    }

    // Non-starting zones already have first city placed
    std::size_t i = (type == TemplateZoneType::PlayerStart || type == TemplateZoneType::AiStart)
                        ? 0
                        : 1;

    for (; i < neutralCities.size(); ++i) {
        MapElement mapElement{Position{4, 4}};
        Position position;

        const int minDistance{mapElement.getSize().x * 2};
        while (true) {
            if (!findPlaceForObject(mapElement, minDistance, position)) {
                throw LackOfSpaceException(std::string("Failed to place city in zone ")
                                           + std::to_string(id) + " due to lack of space");
            }

            if (tryToPlaceObjectAndConnectToPath(mapElement, position)
                == ObjectPlacingResult::Success) {
                if (mapGenerator->isDebugMode()) {
                    std::cout << "Create city at " << position << '\n';
                }

                auto city = placeCity(position, neutralCities[i]);
                decorations.push_back(std::make_unique<VillageDecoration>(city));
                break;
            }
        }
    }
}

void TemplateZone::placeMerchants()
{
    for (const auto& merchantInfo : merchants) {
        MapElement mapElement{Position{3, 3}};
        Position position;

        const int minDistance{mapElement.getSize().x * 2};
        while (true) {
            if (!findPlaceForObject(mapElement, minDistance, position)) {
                throw LackOfSpaceException(std::string("Failed to place merchant in zone ")
                                           + std::to_string(id) + " due to lack of space");
            }

            if (tryToPlaceObjectAndConnectToPath(mapElement, position)
                == ObjectPlacingResult::Success) {
                if (mapGenerator->isDebugMode()) {
                    std::cout << "Create merchant at " << position << '\n';
                }

                auto merchant = placeMerchant(position, merchantInfo);
                decorations.push_back(std::make_unique<SiteDecoration>(merchant));
                break;
            }
        }
    }
}

void TemplateZone::placeMages()
{
    for (const auto& mageInfo : mages) {
        MapElement mapElement{Position{3, 3}};
        Position position;

        const int minDistance{mapElement.getSize().x * 2};
        while (true) {
            if (!findPlaceForObject(mapElement, minDistance, position)) {
                throw LackOfSpaceException(std::string("Failed to place mage in zone ")
                                           + std::to_string(id) + " due to lack of space");
            }

            if (tryToPlaceObjectAndConnectToPath(mapElement, position)
                == ObjectPlacingResult::Success) {
                if (mapGenerator->isDebugMode()) {
                    std::cout << "Create mage at " << position << '\n';
                }

                auto mage = placeMage(position, mageInfo);
                decorations.push_back(std::make_unique<SiteDecoration>(mage));
                break;
            }
        }
    }
}

void TemplateZone::placeMercenaries()
{
    for (const auto& mercInfo : mercenaries) {
        MapElement mapElement{Position{3, 3}};
        Position position;

        const int minDistance{mapElement.getSize().x * 2};
        while (true) {
            if (!findPlaceForObject(mapElement, minDistance, position)) {
                throw LackOfSpaceException(std::string("Failed to place mercenary in zone ")
                                           + std::to_string(id) + " due to lack of space");
            }

            if (tryToPlaceObjectAndConnectToPath(mapElement, position)
                == ObjectPlacingResult::Success) {
                if (mapGenerator->isDebugMode()) {
                    std::cout << "Create mercenary at " << position << '\n';
                }

                auto merc = placeMercenary(position, mercInfo);
                decorations.push_back(std::make_unique<SiteDecoration>(merc));
                break;
            }
        }
    }
}

void TemplateZone::placeTrainers()
{
    for (const auto& trainerInfo : trainers) {
        MapElement mapElement{Position{3, 3}};
        Position position;

        const int minDistance{mapElement.getSize().x * 2};
        while (true) {
            if (!findPlaceForObject(mapElement, minDistance, position)) {
                throw LackOfSpaceException(std::string("Failed to place trainer in zone ")
                                           + std::to_string(id) + " due to lack of space");
            }

            if (tryToPlaceObjectAndConnectToPath(mapElement, position)
                == ObjectPlacingResult::Success) {
                if (mapGenerator->isDebugMode()) {
                    std::cout << "Create trainer at " << position << '\n';
                }

                auto trainer = placeTrainer(position, trainerInfo);
                decorations.push_back(std::make_unique<SiteDecoration>(trainer));
                break;
            }
        }
    }
}

void TemplateZone::placeMarkets()
{
    for (const auto& marketInfo : markets) {
        MapElement mapElement{Position{3, 3}};
        Position position;

        const int minDistance{mapElement.getSize().x * 2};
        while (true) {
            if (!findPlaceForObject(mapElement, minDistance, position)) {
                throw LackOfSpaceException(std::string("Failed to place resource market in zone ")
                                           + std::to_string(id) + " due to lack of space");
            }

            if (tryToPlaceObjectAndConnectToPath(mapElement, position)
                == ObjectPlacingResult::Success) {
                if (mapGenerator->isDebugMode()) {
                    std::cout << "Create resource market at " << position << '\n';
                }

                auto market = placeMarket(position, marketInfo);
                decorations.push_back(std::make_unique<SiteDecoration>(market));
                break;
            }
        }
    }
}

void TemplateZone::placeRuins()
{
    for (const auto& ruinInfo : ruins) {
        MapElement mapElement{Position{3, 3}};
        Position position;

        const int minDistance{mapElement.getSize().x * 2};
        while (true) {
            if (!findPlaceForObject(mapElement, minDistance, position)) {
                throw LackOfSpaceException(std::string("Failed to place ruin in zone ")
                                           + std::to_string(id) + " due to lack of space");
            }

            if (tryToPlaceObjectAndConnectToPath(mapElement, position)
                == ObjectPlacingResult::Success) {
                if (mapGenerator->isDebugMode()) {
                    std::cout << "Create ruin at " << position << '\n';
                }

                auto ruin = placeRuin(position, ruinInfo);
                decorations.push_back(std::make_unique<RuinDecoration>(ruin));
                break;
            }
        }
    }
}

bool TemplateZone::placeMines()
{
    const auto zoneHasOwner{ownerId != emptyId};
    auto& map{mapGenerator->map};
    auto nativeResource{map->getNativeResource(RaceType::Neutral)};

    TerrainType crystalTerrain{TerrainType::Neutral};

    if (zoneHasOwner) {
        auto player{map->find<Player>(ownerId)};
        assert(player != nullptr);

        const auto ownerRace{map->getRaceType(player->getRace())};
        nativeResource = map->getNativeResource(ownerRace);
        crystalTerrain = map->getRaceTerrain(ownerRace);
    }

    for (const auto& mineInfo : mines) {
        const auto resourceType{mineInfo.first};

        for (std::uint8_t i = 0; i < mineInfo.second; ++i) {
            auto crystalId{mapGenerator->createId(CMidgardID::Type::Crystal)};
            auto crystal{std::make_unique<Crystal>(crystalId)};

            crystal->setResourceType(resourceType);

            auto crystalPtr{crystal.get()};

            // Place crystals so they have at least 1 tile between them and nearby obstacle,
            // excluding decorations
            const Position crystalSize{3, 3};
            // Only first gold mine and mana crystal are placed close
            // They are not guarded in player owned zones
            if (i == 0 && (resourceType == nativeResource || resourceType == ResourceType::Gold)) {
                addCloseObject(std::move(crystal),
                               std::make_unique<CapturedCrystalDecoration>(crystalPtr,
                                                                           crystalTerrain),
                               zoneHasOwner ? 0 : 500, crystalSize);
            } else {
                addRequiredObject(std::move(crystal),
                                  std::make_unique<CrystalDecoration>(crystalPtr), 500,
                                  crystalSize);
            }
        }
    }

    return true;
}

void TemplateZone::placeStacks()
{
    // Compute how many stacks we have in total
    const std::size_t stacksTotal = std::accumulate(stacks.stackGroups.begin(),
                                                    stacks.stackGroups.end(), 0u,
                                                    [](std::uint32_t val,
                                                       const NeutralStacksInfo& info) {
                                                        return info.count + val;
                                                    });
    std::vector<Position> positions(stacksTotal);

    // Find position for each of them
    for (std::size_t i = 0; i < stacksTotal; ++i) {
        Position position;

        MapElement mapElement{Position{1, 1}};
        const int minDistance{1};

        while (true) {
            if (!findPlaceForObject(mapElement, minDistance, position)) {
                throw LackOfSpaceException(std::string("Failed to place stacks in zone ")
                                           + std::to_string(id) + " due to lack of space");
            }

            if (tryToPlaceObjectAndConnectToPath(mapElement, position)
                == ObjectPlacingResult::Success) {
                positions[i] = position;
                // We need to update distance now so findPlaceForObject could search properly
                // Actual stack placement will be done later
                updateDistances(position);
                break;
            }
        }
    }

    auto& rand{mapGenerator->randomGenerator};

    // Make sure stacks from different groups are mixed on the map
    randomShuffle(positions, rand);

    std::size_t positionIndex{};
    for (const auto& stackGroup : stacks.stackGroups) {
        if (!stackGroup.count) {
            continue;
        }

        CMidgardID ownerId{mapGenerator->getPlayerId(stackGroup.owner)};
        CMidgardID subraceId{mapGenerator->getSubraceId(stackGroup.owner)};

        if (ownerId == emptyId || subraceId == emptyId) {
            ownerId = mapGenerator->getNeutralPlayerId();
            subraceId = mapGenerator->getNeutralSubraceId();
        }

        const bool neutralOwner{ownerId == mapGenerator->getNeutralPlayerId()};

        std::vector<Stack*> randomStacks(stackGroup.count);
        std::size_t stackIndex{};
        // Generate and place all random stacks, value is split evenly
        GroupInfo randomStackInfo;
        randomStackInfo.value = stackGroup.stacks.value / stackGroup.count;
        randomStackInfo.subraceTypes = stackGroup.stacks.subraceTypes;
        randomStackInfo.leaderIds = stackGroup.stacks.leaderIds;

        for (; stackIndex < stackGroup.count; ++stackIndex) {
            auto stack{createStack(randomStackInfo, neutralOwner)};
            if (!stack) {
                continue;
            }

            stack->setOwner(ownerId);
            stack->setSubrace(subraceId);

            if (!stackGroup.name.empty()) {
                Unit* leader{mapGenerator->map->find<Unit>(stack->getLeader())};
                if (leader) {
                    leader->setName(stackGroup.name);
                }
            }

            if (!stackGroup.leaderModifiers.empty()) {
                Unit* leader{mapGenerator->map->find<Unit>(stack->getLeader())};
                if (leader) {
                    for (auto modifierId : stackGroup.leaderModifiers) {
                        leader->addModifier(modifierId);
                    }
                }
            }

            stack->setAiPriority(stackGroup.aiPriority);
            stack->setOrder(stackGroup.order);

            randomStacks[stackIndex] = stack.get();
            placeObject(std::move(stack), positions[positionIndex++]);
        }

        // Compute loot value for a single stack in group
        const auto& stackGroupLoot{stackGroup.stacks.loot};
        LootInfo stackLoot;
        stackLoot.value = stackGroupLoot.value / stackGroup.count;
        stackLoot.itemTypes = stackGroupLoot.itemTypes;
        stackLoot.itemValue = stackGroupLoot.itemValue;

        std::vector<std::vector<CMidgardID>> items(stackGroup.count);
        // Generate loot for each stack
        for (std::uint32_t i = 0; i < stackGroup.count; ++i) {
            const auto loot{createLoot(stackLoot)};

            for (const auto& [id, amount] : loot) {
                items[i].insert(items[i].end(), amount, id);
            }
        }

        // Generate required items
        LootInfo requiredLootInfo;
        requiredLootInfo.requiredItems = stackGroupLoot.requiredItems;
        const auto requiredLoot{createLoot(requiredLootInfo)};

        std::vector<CMidgardID> requiredItems;
        for (const auto& [id, amount] : requiredLoot) {
            requiredItems.insert(requiredItems.end(), amount, id);
        }

        // Place required items in stacks randomly
        for (const auto& id : requiredItems) {
            const std::size_t stackIndex{rand.nextInteger(std::size_t{0}, items.size() - 1)};

            items[stackIndex].push_back(id);
        }

        for (std::size_t i = 0; i < randomStacks.size(); ++i) {
            Stack* stack{randomStacks[i]};
            if (!stack) {
                continue;
            }

            Inventory& inventory{stack->getInventory()};

            const std::vector<CMidgardID>& loot{items[i]};
            for (const auto& itemType : loot) {
                auto itemId{mapGenerator->createId(CMidgardID::Type::Item)};
                auto item{std::make_unique<Item>(itemId)};
                item->setItemType(itemType);

                mapGenerator->insertObject(std::move(item));
                inventory.add(itemId);
            }
        }
    }
}

void TemplateZone::placeBags()
{
    if (!bags.count) {
        return;
    }

    // Compute single bag value
    const auto& bagsLoot = bags.loot;
    LootInfo bagLoot;
    bagLoot.value = bagsLoot.value / bags.count;
    bagLoot.itemTypes = bagsLoot.itemTypes;
    bagLoot.itemValue = bagsLoot.itemValue;

    std::vector<std::vector<CMidgardID>> items(bags.count);
    // Generate loot for each bag
    for (std::uint32_t i = 0; i < bags.count; ++i) {
        const auto loot{createLoot(bagLoot)};

        for (const auto& [id, amount] : loot) {
            items[i].insert(items[i].end(), amount, id);
        }
    }

    // Generate required items
    LootInfo requiredLootInfo;
    requiredLootInfo.requiredItems = bagsLoot.requiredItems;
    const auto requiredLoot{createLoot(requiredLootInfo)};

    std::vector<CMidgardID> requiredItems;
    for (const auto& [id, amount] : requiredLoot) {
        requiredItems.insert(requiredItems.end(), amount, id);
    }

    auto& rand{mapGenerator->randomGenerator};

    // Place required items in the bags randomly
    for (const auto& id : requiredItems) {
        const std::size_t bagIndex = rand.nextInteger(std::size_t{0}, items.size() - 1);

        items[bagIndex].push_back(id);
    }

    // Place bags
    std::vector<Bag*> placedBags;
    for (std::uint32_t i = 0; i < bags.count; ++i) {
        MapElement mapElement{Position{1, 1}};
        Position position;

        const int minDistance{mapElement.getSize().x * 2};
        while (true) {
            if (!findPlaceForObject(mapElement, minDistance, position)) {
                throw LackOfSpaceException(std::string("Failed to place bags in zone ")
                                           + std::to_string(id) + " due to lack of space");
            }

            if (tryToPlaceObjectAndConnectToPath(mapElement, position)
                == ObjectPlacingResult::Success) {
                if (mapGenerator->isDebugMode()) {
                    std::cout << "Create bag at " << position << '\n';
                }

                // Do not create decorations near bags
                auto bag{placeBag(position)};
                bag->setAiPriority(bags.aiPriority);

                placedBags.push_back(std::move(bag));
                break;
            }
        }
    }

    // Fill bags with actual items.
    // It is the template author's job to think about bags.count and item values distribution.
    // Generator won't care about dumb combinations that lead to empty bags.
    for (std::size_t i = 0; i < items.size() && i < placedBags.size(); ++i) {
        const auto& bagItems = items[i];
        for (const auto& bagItemId : bagItems) {
            auto itemId{mapGenerator->createId(CMidgardID::Type::Item)};
            auto item{std::make_unique<Item>(itemId)};
            item->setItemType(bagItemId);

            mapGenerator->insertObject(std::move(item));
            placedBags[i]->add(itemId);
        }
    }
}

bool TemplateZone::createRequiredObjects()
{
    if (mapGenerator->isDebugMode()) {
        std::cout << "Creating required objects\n";
    }

    for (auto& requiredObject : requiredObjects) {
        auto& object{requiredObject.object};
        Position position;

        auto mapElement{dynamic_cast<MapElement*>(object.get())};
        if (!mapElement) {
            throw std::runtime_error("Required object is not MapElement!");
        }

        while (true) {
            const auto elementSize{mapElement->getSize().x};
            const auto sizeSquared{elementSize * elementSize};
            // TODO: move this setting into template for better object placement ?
            const auto minDistance{elementSize * 2};
            // Find place for object using required object size
            const auto& objectSize{requiredObject.objectSize};

            if (!findPlaceForObject(objectSize.isValid() ? MapElement{objectSize} : *mapElement,
                                    minDistance, position)) {
                throw LackOfSpaceException(std::string("Failed to fill zone ") + std::to_string(id)
                                           + " due to lack of space");
            }

            // If specific size was requested, place object at the center of found area
            if (objectSize.isValid()) {
                position += objectSize / 2;
            }

            if (tryToPlaceObjectAndConnectToPath(*mapElement, position)
                == ObjectPlacingResult::Success) {
                placeScenarioObject(std::move(object), position);
                // guardObject(*mapElement, requiredObject.guardStrength);

                if (requiredObject.decoration) {
                    // If object has decoration, remember it
                    decorations.push_back(std::move(requiredObject.decoration));
                }

                break;
            } else {
                throw LackOfSpaceException(std::string("Failed to fill zone ") + std::to_string(id)
                                           + " due to lack of space");
            }
        }
    }

    for (auto& closeObject : closeObjects) {
        auto& object{closeObject.object};

        auto mapElement{dynamic_cast<MapElement*>(object.get())};
        if (!mapElement) {
            throw std::runtime_error("Required object is not MapElement!");
        }

        // Find place for object using required object size
        const auto& objectSize{closeObject.objectSize};
        const MapElement requiredMapElement = objectSize.isValid() ? MapElement{objectSize}
                                                                   : *mapElement;

        const auto tilesBlockedByObject{requiredMapElement.getBlockedOffsets()};

        bool objectPlaced{};
        bool finished{};
        bool attempt{true};
        while (!finished && attempt) {
            attempt = false;

            std::vector<Position> tiles(possibleTiles.begin(), possibleTiles.end());
            // New tiles vector after each object has been placed,
            // OR misplaced area has been sealed off

            auto eraseFunc = [this, &requiredMapElement](Position& tile) {
                // Object must be accessible from at least one surounding
                // tile and must not be at the border of the map
                return mapGenerator->map->isAtTheBorder(tile)
                       || mapGenerator->map->isAtTheBorder(requiredMapElement, tile)
                       || !isAccessibleFromSomewhere(requiredMapElement, tile);
            };

            tiles.erase(std::remove_if(tiles.begin(), tiles.end(), eraseFunc), tiles.end());

            auto targetPosition{requestedPositions.find(object.get()) != requestedPositions.end()
                                    ? requestedPositions[object.get()]
                                    : pos};
            // Smallest distance to zone center, greatest distance to nearest object
            auto isCloser = [this, &targetPosition, &tilesBlockedByObject](const Position& a,
                                                                           const Position& b) {
                float lDist{std::numeric_limits<float>::max()};
                float rDist{std::numeric_limits<float>::max()};

                for (auto t : tilesBlockedByObject) {
                    t += targetPosition;
                    lDist = fmin(lDist, static_cast<float>(t.distance(a)));
                    rDist = fmin(rDist, static_cast<float>(t.distance(b)));
                }

                // Objects within 12 tile radius are preferred
                // (smaller distance rating)
                lDist *= (lDist > 12) ? 10 : 1;
                rDist *= (rDist > 12) ? 10 : 1;

                return (lDist * 0.5f - std::sqrt(mapGenerator->getNearestObjectDistance(a)))
                       < (rDist * 0.5f - std::sqrt(mapGenerator->getNearestObjectDistance(b)));
            };

            std::sort(tiles.begin(), tiles.end(), isCloser);

            if (tiles.empty()) {
                throw LackOfSpaceException(std::string("Failed to fill zone ") + std::to_string(id)
                                           + " due to lack of space");
            }

            for (const auto& tile : tiles) {
                // Code partially adapted from findPlaceForObject()
                if (!areAllTilesAvailable(requiredMapElement, tile, tilesBlockedByObject)) {
                    continue;
                }

                attempt = true;

                Position position = tile;
                // If specific size was requested, place object at the center of found area
                if (objectSize.isValid()) {
                    position += objectSize / 2;
                }

                auto result{tryToPlaceObjectAndConnectToPath(*mapElement, position)};
                if (result == ObjectPlacingResult::Success) {
                    placeScenarioObject(std::move(object), position);
                    // guardObject(*mapElement, closeObject.guardStrength);

                    if (closeObject.decoration) {
                        decorations.push_back(std::move(closeObject.decoration));
                    }

                    objectPlaced = true;
                    finished = true;
                    break;
                }

                if (result == ObjectPlacingResult::CannotFit) {
                    // Next tile
                    continue;
                }

                // tiles expired, pick new ones
                if (result == ObjectPlacingResult::SealedOff) {
                    break;
                }

                throw std::runtime_error("Wrong result of tryToPlaceObjectAndConnectToPath()");
            }
        }

        if (!objectPlaced) {
            throw LackOfSpaceException(std::string("Failed to fill zone ") + std::to_string(id)
                                       + " due to lack of space");
        }
    }

    requiredObjects.clear();
    closeObjects.clear();

    return true;
}

bool TemplateZone::findPlaceForObject(const MapElement& mapElement,
                                      int minDistance,
                                      Position& position)
{
    return findPlaceForObject(tileInfo, mapElement, minDistance, position);
}

bool TemplateZone::findPlaceForObject(const std::set<Position>& area,
                                      const MapElement& mapElement,
                                      int minDistance,
                                      Position& position,
                                      bool findAccessible)
{
    float bestDistance{0.f};
    bool result{};

    auto blockedOffsets{mapElement.getBlockedOffsets()};

    for (const auto& tile : area) {
        // Avoid borders
        if (mapGenerator->map->isAtTheBorder(mapElement, tile)) {
            continue;
        }

        if (findAccessible) {
            if (!isAccessibleFromSomewhere(mapElement, tile)) {
                continue;
            }

            if (!isEntranceAccessible(mapElement, tile)) {
                continue;
            }
        }

        const bool isPossible{mapGenerator->isPossible(tile)};
        if (!isPossible) {
            continue;
        }

        const TileInfo& t = mapGenerator->getTile(tile);
        const float distance{t.getNearestObjectDistance()};

        const bool distanceMoreThanMin{distance >= minDistance};
        const bool distanceMoreThanBest{distance > bestDistance};

        if (distanceMoreThanMin && distanceMoreThanBest) {
            if (areAllTilesAvailable(mapElement, tile, blockedOffsets)) {
                bestDistance = distance;
                position = tile;
                result = true;
            }
        }
    }

    return result;
}

bool TemplateZone::isAccessibleFromSomewhere(const MapElement& mapElement,
                                             const Position& position) const
{
    return getAccessibleOffset(mapElement, position).isValid();
}

bool TemplateZone::isEntranceAccessible(const MapElement& mapElement,
                                        const Position& position) const
{
    const auto entrance{position + mapElement.getEntranceOffset()};

    // If at least one tile nearby entrance is inaccessible
    // assume whole map element is also inaccessible
    for (const auto& offset : mapElement.getEntranceOffsets()) {
        const auto entranceTile{entrance + offset};

        if (!mapGenerator->map->isInTheMap(entranceTile)) {
            return false;
        }

        if (mapGenerator->isBlocked(entranceTile)) {
            return false;
        }
    }

    return true;
}

Position TemplateZone::getAccessibleOffset(const MapElement& mapElement,
                                           const Position& position) const
{
    const auto blocked{mapElement.getBlockedOffsets()};
    Position result{-1, -1};

    // Check tiles around mapElement possible entrance in 1 tile radius
    for (int x = -1; x < 2; ++x) {
        for (int y = -1; y < 2; ++y) {
            // Check only if object is visitable from another tile
            if (x == 0 && y == 0) {
                continue;
            }

            const Position offset{Position{x, y} + mapElement.getEntranceOffset()};

            if (contains(blocked, offset)) {
                continue;
            }

            const Position nearbyPos{position + offset};
            if (!mapGenerator->map->isInTheMap(nearbyPos)) {
                continue;
            }

            if (mapElement.isVisitableFrom({x, y}) && !mapGenerator->isBlocked(nearbyPos)
                && isInTheZone(nearbyPos)) {
                result = nearbyPos;
            }
        }
    }

    return result;
}

std::vector<Position> TemplateZone::getAccessibleTiles(const MapElement& mapElement) const
{
    const auto entrance{mapElement.getEntrance()};
    std::vector<Position> tiles;

    const auto tilesBlockedByObject{mapElement.getBlockedPositions()};

    mapGenerator->foreachNeighbor(entrance, [this, mapElement, &entrance, &tilesBlockedByObject,
                                             &tiles](Position& position) {
        if (!(mapGenerator->isPossible(position) || mapGenerator->isFree(position))) {
            return;
        }

        if (contains(tilesBlockedByObject, position)) {
            return;
        }

        if (mapElement.isVisitableFrom(position - entrance) && !mapGenerator->isBlocked(position)) {
            tiles.push_back(position);
        }
    });

    return tiles;
}

bool TemplateZone::areAllTilesAvailable(const MapElement& mapElement,
                                        const Position& position,
                                        const std::set<Position>& blockedOffsets) const
{
    for (const auto& offset : blockedOffsets) {
        const auto t{position + offset};

        if (!mapGenerator->map->isInTheMap(t) || !mapGenerator->isPossible(t)
            || mapGenerator->getZoneId(t) != id) {
            // If at least one tile is not possible, object can't be placed here
            return false;
        }
    }

    return true;
}

bool TemplateZone::canObstacleBePlacedHere(const MapElement& mapElement,
                                           const Position& position) const
{
    // Blockmap may fit in the map, but botom-right corner does not
    if (!mapGenerator->map->isInTheMap(position)) {
        return false;
    }

    auto blockedOffsets{mapElement.getBlockedOffsets()};
    for (const auto& offset : blockedOffsets) {
        const Position t{position + offset};

        if (!mapGenerator->map->isInTheMap(t)) {
            return false;
        }

        if (!mapGenerator->shouldBeBlocked(t)) {
            return false;
        }

        /*const auto isPossible{mapGenerator->isPossible(t)};
        const auto shouldBeBlocked{mapGenerator->shouldBeBlocked(t)};

        if (!(isPossible || shouldBeBlocked)) {
            // If at least one tile is not possible, object can't be placed here
            return false;
        }*/
    }

    return true;
}

void TemplateZone::paintZoneTerrain(TerrainType terrain, GroundType ground)
{
    std::vector<Position> tiles(tileInfo.begin(), tileInfo.end());
    mapGenerator->paintTerrain(tiles, terrain, ground);
}

const std::vector<RoadInfo>& TemplateZone::getRoads() const
{
    return roads;
}

bool TemplateZone::isInTheZone(const Position& position) const
{
    return mapGenerator->getZoneId(position) == id;
}

bool TemplateZone::createRoad(const Position& source, const Position& destination)
{
    // A* algorithm

    // The set of nodes already evaluated
    std::set<Position> closed;
    // The set of tentative nodes to be evaluated, initially containing the start node
    PriorityQueue queue;
    // The map of navigated nodes
    std::map<Position, Position> cameFrom;
    std::map<Position, float> distances;

    // Just in case zone guard already has road under it
    // Road under nodes will be added at very end
    mapGenerator->setRoad(source, false);

    // First node points to finish condition
    cameFrom[source] = Position{-1, -1};
    queue.push({source, 0.f});
    distances[source] = 0.f;
    // Cost from start along best known path

    RoadInfo road;
    road.source = source;
    road.destination = destination;

    while (!queue.empty()) {
        auto node{queue.top()};
        queue.pop();

        auto& currentNode{node.first};
        closed.insert(currentNode);

        if (currentNode == destination || mapGenerator->isRoad(currentNode)) {
            // The goal node was reached.
            // Trace the path using the saved parent information and return path
            Position backtracking{currentNode};
            while (cameFrom[backtracking].isValid()) {
                // Add node to path
                road.path.push({backtracking, distances[backtracking]});
                mapGenerator->setRoad(backtracking, true);
                backtracking = cameFrom[backtracking];
            }

            roads.push_back(road);
            return true;
        }

        const auto& currentTile{mapGenerator->map->getTile(currentNode)};
        bool directNeighbourFound{false};
        float movementCost{1.f};

        auto functor = [this, &queue, &distances, &closed, &cameFrom, &currentNode, &currentTile,
                        &node, &destination, &directNeighbourFound, &movementCost](Position& p) {
            if (contains(closed, p)) {
                // We already visited that node
                return;
            }

            float distance{node.second + movementCost};
            float bestDistanceSoFar{std::numeric_limits<float>::max()};

            auto it{distances.find(p)};
            if (it != distances.end()) {
                bestDistanceSoFar = it->second;
            }

            if (distance >= bestDistanceSoFar) {
                return;
            }

            auto& tile{mapGenerator->map->getTile(p)};
            if (tile.isWater()) {
                return;
            }

            const auto canMoveBetween{mapGenerator->map->canMoveBetween(currentNode, p)};

            const auto emptyPath{mapGenerator->isFree(p) && mapGenerator->isFree(currentNode)};
            // Moving from or to visitable object
            const auto visitable{(tile.visitable || currentTile.visitable) && canMoveBetween};
            // Already completed the path
            const auto completed{p == destination};

            if (emptyPath || visitable || completed) {
                // Otherwise guard position may appear already connected to other zone.
                if (mapGenerator->getZoneId(p) == id || completed) {
                    cameFrom[p] = currentNode;
                    distances[p] = distance;
                    queue.push({p, distance});
                    directNeighbourFound = true;
                }
            }
        };

        // Roads cannot be placed diagonally
        mapGenerator->foreachDirectNeighbor(currentNode, functor);
        if (!directNeighbourFound) {
            // Moving diagonally is penalized over moving two tiles straight
            movementCost = 2.1f;
            mapGenerator->foreachDiagonalNeighbor(currentNode, functor);
        }
    }

    if (mapGenerator->isDebugMode()) {
        std::cout << "Failed create road from " << source << " to " << destination << '\n';
    }

    return false;
}

} // namespace rsg
