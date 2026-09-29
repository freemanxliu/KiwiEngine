#pragma once

#include "RHI/RHI.h"
#include "RHI/RHICommandList.h"

#include <memory>
#include <type_traits>

namespace Kiwi
{
    // Mirrors UE5 EUniformBufferUsage. Every backend renames the GPU copy on write
    // (see ConstantBufferVersioning.h), so all usages may be updated freely within a frame.
    enum class EUniformBufferUsage : uint8_t
    {
        SingleDraw,   // Filled and bound for one draw, then overwritten.
        SingleFrame,  // Filled once, bound by many draws within the current frame.
        MultiFrame,   // Kept across frames, updated only when its contents change.
    };

    // The struct must match the HLSL cbuffer layout byte for byte.
    template<typename TBufferStruct>
    class TUniformBufferRef
    {
        static_assert(std::is_trivially_copyable_v<TBufferStruct>, "Uniform buffer structs are uploaded with memcpy and must be trivially copyable");
        static_assert(sizeof(TBufferStruct) % 16 == 0, "Uniform buffer structs must be padded to a multiple of 16 bytes (HLSL cbuffer packing)");

    public:
        TUniformBufferRef() = default;

        static TUniformBufferRef CreateUniformBufferImmediate(RHIDevice* device, const TBufferStruct& value, EUniformBufferUsage usage, const char* debugName = nullptr)
        {
            return Create(device, &value, usage, debugName);
        }

        static TUniformBufferRef CreateEmptyUniformBufferImmediate(RHIDevice* device, EUniformBufferUsage usage, const char* debugName = nullptr)
        {
            return Create(device, nullptr, usage, debugName);
        }

        void UpdateUniformBufferImmediate(const TBufferStruct& value) const
        {
            if (!Buffer) return;
            RHIUpdateBuffer(Buffer.get(), &value, sizeof(TBufferStruct), 0);
        }

        RHIBuffer* GetReference() const { return Buffer.get(); }
        EUniformBufferUsage GetUsage() const { return Usage; }
        bool IsValid() const { return Buffer != nullptr; }
        explicit operator bool() const { return IsValid(); }
        void SafeRelease() { Buffer.reset(); }

    private:
        static TUniformBufferRef Create(RHIDevice* device, const TBufferStruct* value, EUniformBufferUsage usage, const char* debugName)
        {
            TUniformBufferRef ref;
            if (!device) return ref;

            BufferDesc desc;
            desc.SizeInBytes = sizeof(TBufferStruct);
            desc.BindFlags = BUFFER_USAGE_CONSTANT;
            desc.Usage = EResourceUsage::Dynamic;
            desc.DebugName = debugName;
            ref.Buffer = std::shared_ptr<RHIBuffer>(device->CreateBuffer(desc, value));
            ref.Usage = usage;
            return ref;
        }

        std::shared_ptr<RHIBuffer> Buffer;
        EUniformBufferUsage Usage = EUniformBufferUsage::SingleFrame;
    };
}
