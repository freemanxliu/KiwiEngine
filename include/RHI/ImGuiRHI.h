#pragma once

#include <imgui.h>

#include <memory>

namespace Kiwi
{
    // Runs a renderer backend's UpdateTexture for every texture request in DrawData.
    // Draw data snapshots reach the RHI thread up to two frames late, so a texture is only
    // destroyed once it has gone unused for longer than that.
    inline void UpdateImGuiTextures(ImDrawData* DrawData, void (*UpdateTexture)(ImTextureData*))
    {
        if (!DrawData || !DrawData->Textures)
            return;
        for (ImTextureData* Tex : *DrawData->Textures)
        {
            if (Tex->Status == ImTextureStatus_OK)
                continue;
            if (Tex->Status == ImTextureStatus_WantDestroy && Tex->UnusedFrames < 3)
                continue;
            UpdateTexture(Tex);
        }
    }

    // Deep copy of this frame's draw lists, so the next ImGui frame can be built while this one is drawn.
    // Textures is cleared: texture requests are handled on the game thread before the copy is made.
    inline std::shared_ptr<ImDrawData> CloneImDrawData(const ImDrawData* Source)
    {
        if (!Source || !Source->Valid)
            return nullptr;
        auto Deleter = [](ImDrawData* Data) {
            for (ImDrawList* List : Data->CmdLists)
                IM_DELETE(List);
            delete Data;
        };
        std::shared_ptr<ImDrawData> Copy(new ImDrawData(*Source), Deleter);
        for (ImDrawList*& List : Copy->CmdLists)
            List = List->CloneOutput();
        Copy->Textures = nullptr;
        return Copy;
    }
}
