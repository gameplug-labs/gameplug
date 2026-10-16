#pragma once

struct GamePlugD3D11HookInterface {
    void (*OnPresent)(void* swapChain, unsigned int syncInterval, unsigned int flags);
    void (*OnDrawIndexed)(void* context, unsigned int indexCount, unsigned int startIndexLocation, int baseVertexLocation);
};
