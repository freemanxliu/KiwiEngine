#pragma once

#include <string>

namespace Kiwi
{

    // Supported primitive mesh types
    enum class EPrimitiveType
    {
        Cube,
        Sphere,
        Cylinder,
        Floor,
    };

    inline const char* PrimitiveTypeToString(EPrimitiveType type)
    {
        switch (type)
        {
        case EPrimitiveType::Cube:     return "Cube";
        case EPrimitiveType::Sphere:   return "Sphere";
        case EPrimitiveType::Cylinder: return "Cylinder";
        case EPrimitiveType::Floor:    return "Floor";
        default:                       return "Unknown";
        }
    }

    inline EPrimitiveType StringToPrimitiveType(const std::string& str)
    {
        if (str == "Cube")     return EPrimitiveType::Cube;
        if (str == "Sphere")   return EPrimitiveType::Sphere;
        if (str == "Cylinder") return EPrimitiveType::Cylinder;
        if (str == "Floor")    return EPrimitiveType::Floor;
        return EPrimitiveType::Cube;
    }

} // namespace Kiwi
