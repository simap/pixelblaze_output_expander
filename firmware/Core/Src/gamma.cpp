#include <math.h>
#include <array>
#include <cstdint>

// Generate a gamma lookup table, converting from 8-bit to 16-bit using templates
// Gamma is in tenths, e.g., 10 = 1.0

template<uint8_t gammaTenths>
constexpr uint16_t g(uint8_t v) {
    double vNorm = v / 255.0;
    double gamma = gammaTenths / 10.0;
    double v16 = pow(vNorm, gamma) * 65535.0 + 0.5;
    if (v16 > 65535.0) {
        v16 = 65535.0;
    }
    if (v16 < 0.0) {
        v16 = 0.0;
    }
    return static_cast<uint16_t>(v16);
}

template<uint8_t gamma>
constexpr std::array<uint16_t, 256> generateGammaTable() {
    std::array<uint16_t, 256> table = {};
    for (uint16_t i = 0; i < 256; ++i) {
        table[i] = g<gamma>(i);
    }
    return table;
}

constexpr auto gammaTable10 = generateGammaTable<10>();
constexpr auto gammaTable11 = generateGammaTable<11>();
constexpr auto gammaTable12 = generateGammaTable<12>();
constexpr auto gammaTable13 = generateGammaTable<13>();
constexpr auto gammaTable14 = generateGammaTable<14>();
constexpr auto gammaTable15 = generateGammaTable<15>();
constexpr auto gammaTable16 = generateGammaTable<16>();
constexpr auto gammaTable17 = generateGammaTable<17>();
constexpr auto gammaTable18 = generateGammaTable<18>();
constexpr auto gammaTable19 = generateGammaTable<19>();
constexpr auto gammaTable20 = generateGammaTable<20>();
constexpr auto gammaTable21 = generateGammaTable<21>();
constexpr auto gammaTable22 = generateGammaTable<22>();
constexpr auto gammaTable23 = generateGammaTable<23>();
constexpr auto gammaTable24 = generateGammaTable<24>();
constexpr auto gammaTable25 = generateGammaTable<25>();


extern "C" {
    const uint16_t *gamma10 = gammaTable10.data();
    const uint16_t *gamma11 = gammaTable11.data();
    const uint16_t *gamma12 = gammaTable12.data();
    const uint16_t *gamma13 = gammaTable13.data();
    const uint16_t *gamma14 = gammaTable14.data();
    const uint16_t *gamma15 = gammaTable15.data();
    const uint16_t *gamma16 = gammaTable16.data();
    const uint16_t *gamma17 = gammaTable17.data();
    const uint16_t *gamma18 = gammaTable18.data();
    const uint16_t *gamma19 = gammaTable19.data();
    const uint16_t *gamma20 = gammaTable20.data();
    const uint16_t *gamma21 = gammaTable21.data();
    const uint16_t *gamma22 = gammaTable22.data();
    const uint16_t *gamma23 = gammaTable23.data();
    const uint16_t *gamma24 = gammaTable24.data();
    const uint16_t *gamma25 = gammaTable25.data();

    const uint16_t * gammaTables[16] = {
        gamma10, gamma11, gamma12, gamma13, gamma14,
        gamma15, gamma16, gamma17, gamma18, gamma19,
        gamma20, gamma21, gamma22, gamma23, gamma24,
        gamma25
    };
}
