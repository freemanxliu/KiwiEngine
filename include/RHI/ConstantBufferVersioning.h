#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace Kiwi
{
    // Per-frame linear allocator that backs constant buffer versions (UE5 FMetalTempAllocator /
    // D3D11 WRITE_DISCARD renaming). Pages are persistently mapped, CPU-visible GPU memory.
    // BeginFrame() may only be called once the GPU has finished the previous frame.
    class ConstantUploadAllocator
    {
    public:
        static constexpr uint32_t kAlignment = 256;
        static constexpr uint32_t kDefaultPageSize = 2 * 1024 * 1024;

        struct Page
        {
            uint8_t* CpuBase = nullptr;
            uint64_t GpuBase = 0;
            void* NativeHandle = nullptr;
            uint32_t Size = 0;
            std::shared_ptr<void> Owner;
        };

        struct Allocation
        {
            uint8_t* Cpu = nullptr;
            uint64_t GpuAddress = 0;
            void* NativeHandle = nullptr;
            uint32_t Offset = 0;
        };

        using PageFactory = std::function<Page(uint32_t size)>;

        explicit ConstantUploadAllocator(PageFactory factory, uint32_t pageSize = kDefaultPageSize)
            : Factory(std::move(factory)), PageSize(pageSize) {}

        void BeginFrame()
        {
            ++Frame;
            PageIndex = 0;
            Cursor = 0;
        }

        uint64_t GetFrame() const { return Frame; }

        bool Allocate(uint32_t size, Allocation& out)
        {
            const uint32_t aligned = (size + kAlignment - 1) & ~(kAlignment - 1);
            while (PageIndex < Pages.size() && Cursor + aligned > Pages[PageIndex].Size)
            {
                ++PageIndex;
                Cursor = 0;
            }
            if (PageIndex == Pages.size())
            {
                Page page = Factory(aligned > PageSize ? aligned : PageSize);
                if (!page.CpuBase)
                    return false;
                Pages.push_back(std::move(page));
                Cursor = 0;
            }

            const Page& page = Pages[PageIndex];
            out.Cpu = page.CpuBase + Cursor;
            out.GpuAddress = page.GpuBase + Cursor;
            out.NativeHandle = page.NativeHandle;
            out.Offset = Cursor;
            std::memset(out.Cpu + size, 0, aligned - size);
            Cursor += aligned;
            return true;
        }

    private:
        PageFactory Factory;
        uint32_t PageSize;
        std::vector<Page> Pages;
        size_t PageIndex = 0;
        uint32_t Cursor = 0;
        uint64_t Frame = 0;
    };

    // Constant buffer whose GPU copy is renamed on every write: Map() hands out a CPU shadow,
    // Unmap()/UpdateData() push it into a fresh allocator slot, and binding only references the
    // current slot. Draws recorded earlier keep reading the version they were bound with.
    class VersionedConstantBuffer
    {
    public:
        VersionedConstantBuffer(ConstantUploadAllocator* allocator, uint32_t size, const void* initialData)
            : Allocator(allocator), Shadow(size, 0)
        {
            if (initialData)
                std::memcpy(Shadow.data(), initialData, size);
        }

        uint32_t GetSize() const { return (uint32_t)Shadow.size(); }

        void* Map() { return Shadow.data(); }
        void Unmap() { Commit(); }

        void UpdateData(const void* data, uint32_t size, uint32_t offset)
        {
            if (!data || offset + size > Shadow.size())
                return;
            std::memcpy(Shadow.data() + offset, data, size);
            Commit();
        }

        // Current GPU version. The allocator is reset every frame, so a version left over from an
        // earlier frame is re-uploaded once on first use.
        const ConstantUploadAllocator::Allocation& GetCurrent()
        {
            if (!Valid || Frame != Allocator->GetFrame())
                Commit();
            return Current;
        }

    private:
        void Commit()
        {
            ConstantUploadAllocator::Allocation allocation;
            if (!Allocator->Allocate(GetSize(), allocation))
                return;
            std::memcpy(allocation.Cpu, Shadow.data(), Shadow.size());
            Current = allocation;
            Frame = Allocator->GetFrame();
            Valid = true;
        }

        ConstantUploadAllocator* Allocator;
        std::vector<uint8_t> Shadow;
        ConstantUploadAllocator::Allocation Current;
        uint64_t Frame = 0;
        bool Valid = false;
    };
}
