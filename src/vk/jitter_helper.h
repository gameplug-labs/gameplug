#pragma once
#include <cstdint>
#include <vector>

namespace GamePlug {

class JitterHelper {
public:
    static JitterHelper& Get() {
        static JitterHelper instance;
        return instance;
    }

    void Update(uint32_t width, uint32_t height);
    float GetJitterX() const { return m_jitterX * m_scale; }
    float GetJitterY() const { return m_jitterY * m_scale; }

    void SetScale(float scale) { m_scale = scale; }
    void SetBase(uint32_t baseX, uint32_t baseY) {
        m_baseX = baseX;
        m_baseY = baseY;
    }

private:
    JitterHelper()
        : m_jitterX(0.0f)
        , m_jitterY(0.0f)
        , m_index(0)
        , m_scale(1.0f)
        , m_baseX(2)
        , m_baseY(3)
        , m_width(0)
        , m_height(0) {}

    float Halton(uint32_t index, uint32_t base) {
        float f = 1.0f;
        float r = 0.0f;
        while (index > 0) {
            f /= (float)base;
            r += f * (float)(index % base);
            index /= base;
        }
        return r;
    }

    float m_jitterX;
    float m_jitterY;
    uint32_t m_index;
    float m_scale;
    uint32_t m_baseX;
    uint32_t m_baseY;
    uint32_t m_width;
    uint32_t m_height;
};

} // namespace GamePlug
