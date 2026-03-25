#pragma once

#include "RHI/DX11/DX11Headers.h"
#include <unordered_map>

namespace Kiwi
{

    // RHI Format -> DXGI Format 转换
    inline DXGI_FORMAT ToDXGIFormat(EFormat format)
    {
        switch (format)
        {
        case EFormat::R8G8B8A8_UNORM:     return DXGI_FORMAT_R8G8B8A8_UNORM;
        case EFormat::R16G16B16A16_FLOAT:  return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case EFormat::R16G16_FLOAT:        return DXGI_FORMAT_R16G16_FLOAT;
        case EFormat::R32G32B32A32_FLOAT:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case EFormat::R32G32B32_FLOAT:     return DXGI_FORMAT_R32G32B32_FLOAT;
        case EFormat::R32G32_FLOAT:        return DXGI_FORMAT_R32G32_FLOAT;
        case EFormat::R32_FLOAT:           return DXGI_FORMAT_R32_FLOAT;
        case EFormat::R32_UINT:            return DXGI_FORMAT_R32_UINT;
        case EFormat::R16_UINT:            return DXGI_FORMAT_R16_UINT;
        case EFormat::D24_UNORM_S8_UINT:   return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case EFormat::D32_FLOAT:           return DXGI_FORMAT_D32_FLOAT;
        default:                           return DXGI_FORMAT_UNKNOWN;
        }
    }

    // DXGI Format -> RHI Format 转换
    inline EFormat FromDXGIFormat(DXGI_FORMAT format)
    {
        switch (format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:     return EFormat::R8G8B8A8_UNORM;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:  return EFormat::R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R16G16_FLOAT:        return EFormat::R16G16_FLOAT;
        case DXGI_FORMAT_R32G32B32A32_FLOAT:  return EFormat::R32G32B32A32_FLOAT;
        case DXGI_FORMAT_R32G32B32_FLOAT:     return EFormat::R32G32B32_FLOAT;
        case DXGI_FORMAT_R32G32_FLOAT:        return EFormat::R32G32_FLOAT;
        case DXGI_FORMAT_R32_FLOAT:           return EFormat::R32_FLOAT;
        case DXGI_FORMAT_D24_UNORM_S8_UINT:   return EFormat::D24_UNORM_S8_UINT;
        case DXGI_FORMAT_D32_FLOAT:           return EFormat::D32_FLOAT;
        default:                              return EFormat::Unknown;
        }
    }

    inline D3D11_PRIMITIVE_TOPOLOGY ToDX11Topology(EPrimitiveTopology topology)
    {
        switch (topology)
        {
        case EPrimitiveTopology::TriangleList:  return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        case EPrimitiveTopology::TriangleStrip: return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case EPrimitiveTopology::LineList:      return D3D11_PRIMITIVE_TOPOLOGY_LINELIST;
        case EPrimitiveTopology::LineStrip:     return D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case EPrimitiveTopology::PointList:     return D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
        default:                                return D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        }
    }

    // D3D11 usage mapping
    inline D3D11_USAGE ToDX11Usage(EResourceUsage usage)
    {
        switch (usage)
        {
        case EResourceUsage::Default:    return D3D11_USAGE_DEFAULT;
        case EResourceUsage::Immutable:  return D3D11_USAGE_IMMUTABLE;
        case EResourceUsage::Dynamic:    return D3D11_USAGE_DYNAMIC;
        case EResourceUsage::Staging:    return D3D11_USAGE_STAGING;
        default:                         return D3D11_USAGE_DEFAULT;
        }
    }

} // namespace Kiwi
