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

#include "unit.h"
#include "serializer.h"

namespace rsg {

void Unit::serialize(Serializer& serializer, const Map& scenario) const
{
    serializer.enterRecord();
    serializer.serialize("UNIT_ID", objectId);
    serializer.serialize("TYPE", implId);
    serializer.serialize("LEVEL", level);

    CMidgardID::String idString{};
    objectId.toString(idString);

    serializer.serialize(idString.data(), static_cast<std::uint32_t>(modifiers.size()));

    for (const auto& id : modifiers) {
        serializer.serialize("MODIF_ID", id);
    }

    serializer.serialize("CREATION", creation);
    serializer.serialize("NAME_TXT", name.c_str());
    serializer.serialize("TRANSF", transformed);
    serializer.serialize("DYNLEVEL", dynlevel);
    serializer.serialize("HP", hp);
    serializer.serialize("XP", xp);
    serializer.leaveRecord();
}

} // namespace rsg