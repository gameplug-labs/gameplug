#include "dispatch.h"

namespace GamePlug {
    DispatchManager& DispatchManager::Get() {
        static DispatchManager instance;
        return instance;
    }
}
