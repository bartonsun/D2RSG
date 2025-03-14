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

#pragma once

#include "decoration.h"
#include "gameinfo.h"
#include "position.h"
#include "scenario/bag.h"
#include "scenario/crystal.h"
#include "scenario/fortification.h"
#include "scenario/landmark.h"
#include "scenario/ruin.h"
#include "scenario/site.h"
#include "scenario/stack.h"
#include "vposition.h"
#include "zoneoptions.h"
#include <memory>
#include <queue>

namespace rsg {

class MapGenerator;
class UnitInfo;

enum class ObjectPlacingResult
{
    Success,
    CannotFit,
    SealedOff,
};

// A* priority queue
using Distance = std::pair<Position, float>;
struct NodeComparer
{
    bool operator()(const Distance& a, const Distance& b) const
    {
        return b.second < a.second;
    }
};

using PriorityQueue = std::priority_queue<Distance, std::vector<Distance>, NodeComparer>;

struct RoadInfo
{
    PriorityQueue path; // Road tiles
    Position source;
    Position destination;
};

// Describes zone in a template
struct TemplateZone : public ZoneOptions
{
    TemplateZone(MapGenerator* mapGenerator)
        : mapGenerator{mapGenerator}
    { }

    const VPosition& getCenter() const
    {
        return center;
    }

    void setCenter(const VPosition& value);

    void setPosition(const Position& position)
    {
        pos = position;
    }

    const Position& getPosition() const
    {
        return pos;
    }

    void setOptions(const ZoneOptions& options)
    {
        ZoneOptions::operator=(options);
    }

    void addTile(const Position& position)
    {
        tileInfo.insert(position);
    }

    void removeTile(const Position& position)
    {
        tileInfo.erase(position);
        possibleTiles.erase(position);
    }

    void clearTiles()
    {
        tileInfo.clear();
    }

    const std::set<Position>& getTileInfo() const
    {
        return tileInfo;
    }

    const CMidgardID& getOwner() const
    {
        return ownerId;
    }

    void setOwner(const CMidgardID& id)
    {
        ownerId = id;
    }

    void clearEntrance(const Fortification& fort);

    void initTowns();
    void initFreeTiles();
    void createBorder();
    void fill();
    void createObstacles();
    void connectRoads();

    ObjectPlacingResult tryToPlaceObjectAndConnectToPath(MapElement& mapElement,
                                                         const Position& position);

    void addRequiredObject(ScenarioObjectPtr&& object,
                           DecorationPtr&& decoration = nullptr,
                           int guardStrength = 500,
                           const Position& objectSize = Position{-1, -1});
    void addCloseObject(ScenarioObjectPtr&& object,
                        DecorationPtr&& decoration = nullptr,
                        int guardStrength = 500,
                        const Position& objectSize = Position{-1, -1});

    void placeScenarioObject(ScenarioObjectPtr&& object, const Position& position);

    void placeObject(std::unique_ptr<Fortification>&& fortification,
                     const Position& position,
                     TerrainType terrain = TerrainType::Neutral,
                     bool updateDistance = true);
    void placeObject(std::unique_ptr<Stack>&& stack,
                     const Position& position,
                     bool updateDistance = true);
    void placeObject(std::unique_ptr<Crystal>&& crystal,
                     const Position& position,
                     bool updateDistance = true);
    void placeObject(std::unique_ptr<Ruin>&& ruin,
                     const Position& position,
                     bool updateDistance = true);
    void placeObject(std::unique_ptr<Site>&& site,
                     const Position& position,
                     bool updateDistance = true);
    void placeObject(std::unique_ptr<Bag>&& bag,
                     const Position& position,
                     bool updateDistance = true);
    void placeObject(std::unique_ptr<Landmark>&& landmark,
                     const Position& position,
                     bool updateDistance = true);

    void placeMountain(const Position& position, const Position& size, int image);

    bool guardObject(const MapElement& mapElement, const GroupInfo& guardInfo);

    void updateDistances(const Position& position);

    void addRoadNode(const Position& position);

    void addFreePath(const Position& position);

    // Connect current tile to any other free tile within zone
    bool connectWithCenter(const Position& position,
                           bool onlyStraight,
                           bool passThroughBlocked = false);

    // Make shortest path with free tiles, reaching destination or closest already free tile.
    // Avoid blocks. Do not leave zone border
    bool crunchPath(const Position& source,
                    const Position& destination,
                    bool onlyStraight,
                    std::set<Position>* clearedTiles = nullptr);

    // Connect specified 'source' tile to nearest free tile with zone
    bool connectPath(const Position& source, bool onlyStraight);

    // Creates stack with loot from specified group information
    std::unique_ptr<Stack> createStack(const GroupInfo& stackInfo, bool neutralOwner);

    // Creates stack with specified leader and soldier units
    std::unique_ptr<Stack> createStack(const UnitInfo& leaderInfo,
                                       std::size_t leaderPosition,
                                       const GroupUnits& groupUnits,
                                       bool neutralOwner);

    // Picks stack leader using ids
    const UnitInfo* pickStackLeader(std::size_t& unusedValue,
                                      std::size_t& valuesConsumed,
                                      const std::vector<std::size_t>& unitValues,
                                      std::set<CMidgardID> leaderIds);

    // Picks stack leader using stack unit values
    const UnitInfo* createStackLeader(std::size_t& unusedValue,
                                      std::size_t& valuesConsumed,
                                      const std::vector<std::size_t>& unitValues,
                                      const std::set<SubRaceType>& allowedSubraces);

    // Picks soldiers using specified values
    void createGroup(std::size_t& unusedValue,
                     std::set<int>& positions,
                     GroupUnits& groupUnits,
                     const std::vector<std::size_t>& unitValues,
                     const std::set<SubRaceType>& allowedSubraces);

    // Tightens group by rolling additional soldier units
    void tightenGroup(std::size_t& unusedValue,
                      std::set<int>& positions,
                      GroupUnits& groupUnits,
                      const std::set<SubRaceType>& allowedSubraces);

    void createGroupUnits(Group& group, const GroupUnits& groupUnits);

    Village* placeCity(const Position& position, const CityInfo& cityInfo);

    Site* placeMerchant(const Position& position, const MerchantInfo& merchantInfo);
    Site* placeMage(const Position& position, const MageInfo& mageInfo);
    Site* placeMercenary(const Position& position, const MercenaryInfo& mercInfo);
    Site* placeTrainer(const Position& position, const TrainerInfo& trainerInfo);
    Site* placeMarket(const Position& position, const ResourceMarketInfo& marketInfo);
    Ruin* placeRuin(const Position& position, const RuinInfo& ruinInfo);
    Stack* placeZoneGuard(const Position& position, const GroupInfo& guardInfo);
    Bag* placeBag(const Position& position);

    std::vector<std::pair<CMidgardID, int>> createLoot(const LootInfo& loot,
                                                       bool forMerchant = false);
    CMidgardID createRuinLoot(const LootInfo& loot);

    void initTerrain();
    void fractalize();
    void placeCapital();
    void placeCities();
    void placeMerchants();
    void placeMages();
    void placeMercenaries();
    void placeTrainers();
    void placeMarkets();
    void placeRuins();
    bool placeMines();
    void placeStacks();
    void placeBags();
    bool createRequiredObjects();

    bool findPlaceForObject(const MapElement& mapElement, int minDistance, Position& position);
    bool findPlaceForObject(const std::set<Position>& area,
                            const MapElement& mapElement,
                            int minDistance,
                            Position& position,
                            bool findAccessible = true);
    bool isAccessibleFromSomewhere(const MapElement& mapElement, const Position& position) const;
    bool isEntranceAccessible(const MapElement& mapElement, const Position& position) const;
    Position getAccessibleOffset(const MapElement& mapElement, const Position& position) const;
    // Returns all tiles from which specified map element can be accessed
    std::vector<Position> getAccessibleTiles(const MapElement& mapElement) const;
    bool areAllTilesAvailable(const MapElement& mapElement,
                              const Position& position,
                              const std::set<Position>& blockedOffsets) const;
    bool canObstacleBePlacedHere(const MapElement& mapElement, const Position& position) const;

    void paintZoneTerrain(TerrainType terrain, GroundType ground);

    const std::vector<RoadInfo>& getRoads() const;

    // Returns true if tile with specified position belongs to zone
    bool isInTheZone(const Position& position) const;

private:
    bool createRoad(const Position& source, const Position& destination);

    MapGenerator* mapGenerator{};

    // Template info
    TerrainType terrainType{TerrainType::Neutral};

    struct ObjectPlacement
    {
        ScenarioObjectPtr object;
        DecorationPtr decoration;
        Position objectSize{-1, -1};
        int guardStrength{};
    };

    std::vector<ObjectPlacement> requiredObjects;
    std::vector<ObjectPlacement> closeObjects;
    std::vector<DecorationPtr> decorations;

    std::map<ScenarioObject*, Position> requestedPositions;
    int minGuardedValue{0};

    // Placement info
    Position pos;
    VPosition center;
    std::set<Position> tileInfo;      // Area assigned to zone
    std::set<Position> possibleTiles; // For treasure generation
    std::set<Position> freePaths;     // Paths of free tiles that all objects will be linked to
    std::set<Position> roadNodes;     // Tiles to be connected with roads

    std::vector<RoadInfo> roads; // All tiles with roads
    CMidgardID ownerId{emptyId}; // Player assigned to zone
};

} // namespace rsg
