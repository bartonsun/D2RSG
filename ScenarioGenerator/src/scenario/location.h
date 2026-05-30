/*
 * This file is part of the random scenario generator for Disciples 2.
 * (https://github.com/VladimirMakeev/D2RSG)
 * Copyright (C) 2026 Alexey Voskresensky.
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

#include "enums.h"
#include "position.h"
#include "scenarioobject.h"

namespace rsg {

class Location : public ScenarioObject
{
public:
    Location(const CMidgardID& locationId)
        : ScenarioObject(locationId)
    { }

    ~Location() override = default;

    const char* rawName() const override
    {
        return ".?AVCMidLocation@@";
    }

    void serialize(Serializer& serializer, const Map& scenario) const override;

    const std::string& getName() const
    {
        return name;
    }

    void setName(const std::string& value)
    {
        name = value;
    }
    void setPosition(const Position& value)
    {
        position = value;
    }
    void setSize(LocationSize value)
    {
        size = value;
    }

private:
    std::string name;
    Position position;
    LocationSize size{LocationSize::x3};
};

} // namespace rsg