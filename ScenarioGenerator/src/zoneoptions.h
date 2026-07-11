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

#include "aipriority.h"
#include "currency.h"
#include "enums.h"
#include "randomgenerator.h"
#include "rsgid.h"
#include "zoneid.h"
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <vector>

namespace rsg {

struct RequiredItemInfo
{
    CMidgardID itemId;
    RandomValue<std::uint8_t> amount;
};

struct LocationInfo
{
    // Unique location identifier
    std::string uid;
    // Locations size id 0-3 -> 1x1-7x7 square
    LocationSize size{LocationSize::x3};
    // Init position
    Position position{-1, -1};
};

struct OrderInfo
{
    OrderType type{OrderType::Stand};
    std::string targetUid;
};

struct LootInfo
{
    // Types of items that are allowed
    std::set<ItemType> itemTypes;
    // Items that must be generated
    std::vector<RequiredItemInfo> requiredItems;
    // Total loot value, excluding required items
    RandomValue<std::uint32_t> value{};
    // Single loot item value, excluding required items
    RandomValue<std::uint32_t> itemValue{};
    // List of restricted item ids for this loot, excluding required items
    std::set<CMidgardID> forbiddenIds;
};

struct GuardianInfo
{
    bool create{true};
    std::vector<CMidgardID> modifierIds;
};

struct GroupInfo
{
    // Subraces of units allowed in group
    std::set<SubRaceType> subraceTypes;
    std::set<std::string> customSubraceUids;
    // Group loot
    LootInfo loot;
    // Group units value
    RandomValue<std::uint32_t> value{};
    // List of restricted units id for this stack
    std::set<CMidgardID> forbiddenIds;
};

struct StackInfo
{
    // Stack index for uid
    int groupIndex = 0;
    // GroupInfo used to describe value and loot of entire group
    GroupInfo groupInfo;
    // Number of stacks to create
    std::uint32_t count{};
    // Race that controls the stacks
    RaceType owner{RaceType::Neutral};
    // Owner subrace
    SubRaceType subrace{SubRaceType::Neutral};
    // Custom subrace uid
    std::string customSubraceUid;
    // Stack order
    OrderInfo order;
    // Custom leader name
    std::string name;
    // AI-priority
    AiPriority aiPriority;
    // Required leaders ids
    std::set<CMidgardID> leaderIds;
    // Required leaders modifiers
    std::vector<CMidgardID> leaderModifiers;
    // Leader equipment
    CMidgardID bannerId;
    CMidgardID tomeId;
    CMidgardID battle1Id;
    CMidgardID battle2Id;
    CMidgardID artifact1Id;
    CMidgardID artifact2Id;
    CMidgardID bootsId;
    // Location
    LocationInfo location;
};

struct CityInfo
{
    // City garrison defenders and items
    GroupInfo garrison;
    // Stack that is visiting the city
    StackInfo stack;
    // Custom city name
    std::string name;
    // Race that controls the city
    RaceType owner{RaceType::Neutral};
    // Owner subrace
    SubRaceType subrace{SubRaceType::Neutral};
    // Custom subrace uid
    std::string customSubraceUid;
    // AI-priority
    AiPriority aiPriority;
    // City tier
    std::uint8_t tier{1};
    // Change the base city's regeneration by the specified percentage
    std::int8_t regen;
    // Blocks city upgrades until the specified turn ends
    std::int16_t growthTurn;
    // Riot until specified turn
    std::int16_t riotTurn;
    // Modifier for all defenders units
    CMidgardID protectionId;
    //
    int gapMask{0};
    // Location
    LocationInfo location;
};

struct CapitalInfo
{
    // Capital garrison defenders and items
    GroupInfo garrison;
    // Stack that is visiting the capital
    StackInfo stack;
    // Bonus gold and mana for player
    Currency bonusResources;
    // Spells the player knowns from the start
    std::set<CMidgardID> spells;
    // Buildings that must be present in capital
    std::set<CMidgardID> buildings;
    // Custom capital name
    std::string name;
    // AI-priority
    AiPriority aiPriority;
    int gapMask{0};
    // Generate capital guardian
    GuardianInfo guardian;
    // Generate start stack in capital
    bool startingStack{true};
    // Location
    LocationInfo location;
};

struct RuinInfo
{
    // Group inside the ruin, group loot ignored
    GroupInfo guard;
    // Item reward. If specified, first required item is picked
    LootInfo loot;
    // Custom ruin name
    std::string name;
    // Reward in gold
    RandomValue<std::uint16_t> gold{};
    // AI-priority
    AiPriority aiPriority;
    // Location
    LocationInfo location;
};

struct MerchantInfo
{
    // Stack that is guarding the merchant
    StackInfo guard;
    // Merchant items
    LootInfo items;
    // Custom merchant name and description
    std::string name;
    std::string description;
    // AI-priority
    AiPriority aiPriority;
    // Location
    LocationInfo location;
};

struct MageInfo
{
    // Stack that is guarding the mage
    StackInfo guard;
    // Types of spells merchant is allowed to sell
    std::set<SpellType> spellTypes;
    // Spells that merchant must sell, regardless of spellTypes and value
    std::set<CMidgardID> requiredSpells;
    // Custom mage name and description
    std::string name;
    std::string description;
    // Total value of merchant tradable spells, excluding requiredSpells
    RandomValue<std::uint32_t> value{};
    // Spell levels that merchant is allowed to sell.
    RandomValue<std::uint8_t> spellLevels{};
    // AI-priority
    AiPriority aiPriority;
    // List of restricted spell ids for this mage, excluding requiredSpells
    std::set<CMidgardID> forbiddenIds;
    // Location
    LocationInfo location;
};

struct MercenaryUnitInfo
{
    CMidgardID unitId;
    int level{};
    bool unique{};
};

struct MercenaryInfo
{
    // Stack that is guarding the mercenary camp
    StackInfo guard;
    // Subraces of units allowed for hire
    std::set<SubRaceType> subraceTypes;
    std::set<std::string> customSubraceUids;
    // Units that must be generated
    std::vector<MercenaryUnitInfo> requiredUnits;
    // Custom mercenary name and description
    std::string name;
    std::string description;
    // Total value of units, excluding requiredUnits
    RandomValue<std::uint32_t> value{};
    // Single unit enroll cost, excluding requiredUnits
    RandomValue<std::uint32_t> enrollValue{};
    // AI-priority
    AiPriority aiPriority;
    // List of restricted unit ids for this mercenary camp, excluding requiredUnits
    std::set<CMidgardID> forbiddenIds;
    // All randomly generated units will be marked as unique
    bool unique{true};
    // Allow duplicate units in random generation (if false, no repeats)
    bool duplicate{true};
    // Location
    LocationInfo location;
};

struct StacksInfo
{
    // Several neutral stack groups each with the same generation parameters
    std::vector<StackInfo> stackGroups;
};

struct BagInfo
{
    // Bags index for uid
    int groupIndex = 0;
    LootInfo loot;
    std::uint32_t count{};
    // AI-priority
    AiPriority aiPriority;
    // Location
    LocationInfo location;
};

struct TrainerInfo
{
    StackInfo guard;
    // Custom trainer name and description
    std::string name;
    std::string description;
    // AI-priority
    AiPriority aiPriority;
    // Location
    LocationInfo location;
};

struct ResourceMarketStock
{
    // Amount of resource on the market
    RandomValue<std::uint32_t> amount;
    // Whether resource is infinite or not
    bool infinite{};
};

struct ResourceMarketInfo
{
    // Stack that is guarding resource market
    StackInfo guard;
    // Custom exchange rates, if specified
    std::string exchangeRates;
    // Market resources
    std::map<ResourceType, ResourceMarketStock> stock;
    // Custom name and description
    std::string name;
    std::string description;
    // AI-priority
    AiPriority aiPriority;
    // Location
    LocationInfo location;
};

// Connection between two zones in template
struct ZoneConnection
{
    StackInfo guard;
    TemplateZoneId zoneFrom{0};
    TemplateZoneId zoneTo{0};
    int size{1};
    int distance{0};
    bool required{true};
};

// Template zone settings
struct ZoneOptions
{
    std::set<TerrainType> terrainTypes;         // Terrain types allowed in zone
    std::set<GroundType> groundTypes;           // Ground types allowed in zone
    std::map<ResourceType, std::uint8_t> mines; // Mines and their count in zone
    std::vector<TemplateZoneId> connections;    // Adjacent zones
    std::vector<CityInfo> neutralCities;        // Neutral cities
    std::vector<RuinInfo> ruins;                // Ruins in the zone
    std::vector<MerchantInfo> merchants;        // Merchants
    std::vector<MageInfo> mages;                // Mage towers
    std::vector<MercenaryInfo> mercenaries;     // Mercenary camps
    std::vector<TrainerInfo> trainers;          // Trainers
    std::vector<ResourceMarketInfo> markets;    // Resource markets
    std::vector<BagInfo> bagGroups;             // Bags with treasures
    StacksInfo stacks;                          // Neutral stacks
    CapitalInfo capital;                        // Capital, in case of starting zone
    TemplateZoneId id{0};
    TemplateZoneType type{TemplateZoneType::PlayerStart};
    TemplateZoneFillType fillType{TemplateZoneFillType::None};
    RaceType playerRace{RaceType::Neutral};
    ZoneBorderType borderType{ZoneBorderType::Closed};
    int gapChance{50}; // Chance border tile will become gap in case of SemiOpen borders
    int size{1};       // Zone size
    int waterPrc{0};      // Percent of free tiles converted to water
    int forestPrc{-1};     // Percent of forest tiles. -1 = use global
    int roadsPrc{-1};      // Percent of tiles with roads. -1 = use global
    char label{};
    float pathWidth{75.f};
    std::map<std::pair<TemplateZoneId, TemplateZoneId>, int> guardCounters;
};

} // namespace rsg
