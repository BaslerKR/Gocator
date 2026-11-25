#pragma once

#include <GoPxLSdk/Def.h>
#include <GoPxLSdk/GoSystem.h>

#include <memory>

namespace gocator {

class GocatorSystemControl
{
public:
    virtual ~GocatorSystemControl() = default;
    [[nodiscard]] virtual GoPxLSdk::GoSystem::State runningState() = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
};

}
