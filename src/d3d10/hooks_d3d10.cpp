#include "hooks_common.h"

namespace GamePlug {

bool ShouldOverrideD3D10(const D3D10_TEXTURE2D_DESC& desc) {
    return false;
}

HRESULT STDMETHODCALLTYPE HookedCreateTexture2D(
    ID3D10Device* pDevice, const D3D10_TEXTURE2D_DESC* pDesc, const D3D10_SUBRESOURCE_DATA* pInitialData, ID3D10Texture2D** ppTexture2D) {
    if (g_InHook)
        return g_OriginalCreateTexture2D(pDevice, pDesc, pInitialData, ppTexture2D);
    ScopedRecursionGuard guard;

    D3D10_TEXTURE2D_DESC desc = *pDesc;
    if (ShouldOverrideD3D10(desc)) {
        uint32_t renderW = desc.Width;
        uint32_t renderH = desc.Height;

        if (renderW > 0 && renderH > 0) {
            Logger::info("Override DX10 RT " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " -> " +
                         std::to_string(renderW) + "x" + std::to_string(renderH));
            desc.Width = renderW;
            desc.Height = renderH;
        }
    }

    return g_OriginalCreateTexture2D(pDevice, &desc, pInitialData, ppTexture2D);
}

} // namespace GamePlug
