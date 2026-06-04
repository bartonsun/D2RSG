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
#include "enums.h"
#include "group.h"
#include "inventory.h"
#include "mapelement.h"
#include "scenarioobject.h"

namespace rsg {

class Stack
    : public ScenarioObject
    , public MapElement
{
public:
    Stack(const CMidgardID& id)
        : ScenarioObject(id)
        , MapElement(Position{1, 1})
    { }

    ~Stack() override = default;

    const char* rawName() const override
    {
        return ".?AVCMidStack@@";
    }

    void serialize(Serializer& serializer, const Map& scenario) const override;

    bool addLeader(const CMidgardID& leaderId, std::size_t position, bool bigUnit = false)
    {
        if (group.addLeader(leaderId, position, bigUnit)) {
            this->leaderId = leaderId;
            return true;
        }

        return false;
    }

    bool addUnit(const CMidgardID& unitId, std::size_t position, bool bigUnit = false)
    {
        return group.addUnit(unitId, position, bigUnit);
    }

    bool removeUnit(const CMidgardID& unitId, bool bigUnit = false)
    {
        return group.removeUnit(unitId, bigUnit);
    }

    const CMidgardID& getLeader() const
    {
        return leaderId;
    }

    const CMidgardID& getOwner() const
    {
        return ownerId;
    }

    void setOwner(const CMidgardID& id)
    {
        ownerId = id;
    }

    const CMidgardID& getSubrace() const
    {
        return subraceId;
    }

    void setSubrace(const CMidgardID& id)
    {
        subraceId = id;
    }

    void setInside(const CMidgardID& id)
    {
        insideId = id;
    }

    void setMove(int value)
    {
        move = value;
    }

    void setFacing(Facing value)
    {
        facing = value;
    }

    void setOrder(OrderType value, const CMidgardID& targetId = emptyId)
    {
        order = value;
    }

    void setOrderTargetId(const CMidgardID& id)
    {
        orderTargetId = id;
    }

    CMidgardID getOrderTargetId() const
    {
        return orderTargetId;
    }

    const Group& getGroup() const
    {
        return group;
    }

    Group& getGroup()
    {
        return group;
    }

    const Inventory& getInventory() const
    {
        return inventory;
    }

    Inventory& getInventory()
    {
        return inventory;
    }

    void setBannerId(const CMidgardID& id)
    {
        bannerId = id;
    }

    void setTomeId(const CMidgardID& id)
    {
        tomeId = id;
    }

    void setBattle1Id(const CMidgardID& id)
    {
        battle1Id = id;
    }

    void setBattle2Id(const CMidgardID& id)
    {
        battle2Id = id;
    }

    void setArtifact1Id(const CMidgardID& id)
    {
        artifact1Id = id;
    }

    void setArtifact2Id(const CMidgardID& id)
    {
        artifact2Id = id;
    }

    void setBootsId(const CMidgardID& id)
    {
        bootsId = id;
    }

    void setAiPriority(AiPriority priority)
    {
        aiPriority = priority;
    }

    void setUid(const std::string& value)
    {
        uid = value;
    }
    const std::string& getUid() const
    {
        return uid;
    }

    void setTargetUid(const std::string& value)
    {
        targetUid = value;
    }
    const std::string& getTargetUid() const
    {
        return targetUid;
    }

private:
    Group group;
    Inventory inventory;
    CMidgardID leaderId;
    CMidgardID srcTemplateId;
    CMidgardID bannerId;
    CMidgardID tomeId;
    CMidgardID battle1Id;
    CMidgardID battle2Id;
    CMidgardID artifact1Id;
    CMidgardID artifact2Id;
    CMidgardID bootsId;
    CMidgardID ownerId;
    CMidgardID subraceId;
    CMidgardID insideId;
    CMidgardID orderTargetId;
    CMidgardID aiOrderTargetId;
    AiPriority aiPriority;
    int morale{};
    int move{};
    Facing facing{Facing::South};
    int upgradeCount{};
    OrderType order{OrderType::Stand};
    int aiOrder{1};
    int creatureLevel{1};
    int nbBattle{};
    bool aiIgnore{};
    bool invisible{};
    bool leaderAlive{true};
    // Internal use only
    std::string uid;
    std::string targetUid;
};

} // namespace rsg
