#include "jitter_helper.h"
#include <cmath>
#include <string>
#include "logger.h"

namespace GamePlug {

void JitterHelper::Update(uint32_t width, uint32_t height) {
    if (width != m_width || height != m_height) {
        Logger::info("JitterHelper: Dimensions changed to " + std::to_string(width) + "x" + std::to_string(height));
        m_width = width;
        m_height = height;
        m_index = 0;
    }
    // Halton sequence for jitter
    // Increment the index each frame
    m_index++;
    
    // FSR2 recommends a phase count based on resolution
    // Standard phase count is 128 for most cases.
    if (m_index > 128) m_index = 1;

    // Use custom bases (default 2, 3)
    m_jitterX = Halton(m_index, m_baseX) - 0.5f;
    m_jitterY = Halton(m_index, m_baseY) - 0.5f;
}

} // namespace GamePlug
