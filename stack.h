#pragma once

#include "aipriority.h"
#include "group.h"
#include "inventory.h"
#include "mapelement.h"
#include "scenarioobject.h"

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

    bool addLeader(const CMidgardID& leaderId, std::size_t position)
    {
        if (group.addLeader(leaderId, position)) {
            this->leaderId = leaderId;
            return true;
        }

        return false;
    }

    bool addUnit(const CMidgardID& unitId, std::size_t position)
    {
        return group.addUnit(unitId, position);
    }

    bool removeUnit(const CMidgardID& unitId)
    {
        return group.removeUnit(unitId);
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

    void setFacing(int value)
    {
        facing = value;
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
    int facing{};
    int upgradeCount{};
    int order{1};
    int aiOrder{1};
    int creatureLevel{1};
    int nbBattle{};
    bool aiIgnore{};
    bool invisible{};
    bool leaderAlive{true};
};