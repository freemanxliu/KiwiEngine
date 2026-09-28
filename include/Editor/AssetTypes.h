#pragma once

#include <string>

namespace Kiwi
{

// Image extensions the texture loader accepts (as shown in the content browser and texture picker).
inline bool IsTextureExtension(const std::string& ext)
{
    static const char* const kTextureExts[] = {
        ".png", ".jpg", ".jpeg", ".bmp", ".tga",
        ".PNG", ".JPG", ".BMP", ".TGA",
    };
    for (const char* texExt : kTextureExts)
    {
        if (ext == texExt)
            return true;
    }
    return false;
}

} // namespace Kiwi
