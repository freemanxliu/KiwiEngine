#pragma once

#include "RHI/RHI.h"

namespace Kiwi
{

    void CreateMetalRHI(
        const RHIInitParams& params,
        std::unique_ptr<RHIDevice>& outDevice,
        std::unique_ptr<RHICommandContext>& outContext);

}
