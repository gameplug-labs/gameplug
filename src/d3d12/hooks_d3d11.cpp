#include "hooks_common.h"

namespace GamePlug {

bool ShouldOverrideD3D11(const D3D11_TEXTURE2D_DESC& desc) {
    return false;
}

HRESULT STDMETHODCALLTYPE HookedCreateTexture2D(
    ID3D11Device* pDevice, const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData, ID3D11Texture2D** ppTexture2D) {
    if (g_InHook)
        return g_OriginalCreateTexture2D(pDevice, pDesc, pInitialData, ppTexture2D);
    ScopedRecursionGuard guard;

    D3D11_TEXTURE2D_DESC desc = *pDesc;
    if (ShouldOverrideD3D11(desc)) {
        uint32_t renderW = desc.Width;
        uint32_t renderH = desc.Height;

        if (renderW > 0 && renderH > 0) {
            Logger::info("FSR1: Override DX11 RT " + std::to_string(desc.Width) + "x" + std::to_string(desc.Height) + " -> " +
                         std::to_string(renderW) + "x" + std::to_string(renderH));
            desc.Width = renderW;
            desc.Height = renderH;
        }
    }

    return g_OriginalCreateTexture2D(pDevice, &desc, pInitialData, ppTexture2D);
}

} // namespace GamePlug
