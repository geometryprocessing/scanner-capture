#ifndef SPI_H
#define SPI_H

#include "aos/device.h"

// Phase | Polarity
#define SPI_MODE_0 0x00
#define SPI_MODE_1 0x02
#define SPI_MODE_2 0x01
#define SPI_MODE_3 0x03

#define LSB_FIRST 0
#define MSB_FIRST 1

#define BOOST_PACK_SPI 2

/* TIVA C - TM4C123GH6PM, aka TARGET_IS_BLIZZARD_RB1

    Module   Base   Port
       0     SSI0    PA
       1     SSI1    PF
       2     SSI2    PB
       3     SSI3    PD

    Polarity  Phase     Mode
       0        0    SPI_MODE_0
       0        1    SPI_MODE_1
       1        0    SPI_MODE_2
       1        1    SPI_MODE_3

    Defaults to System Clock and 8 bit data
*/

class GPIO;

class SPI : public Device
{
public:
    static constexpr const char * const ClassName = "spi";

    SPI(uint8_t data_mode = SPI_MODE_0, uint8_t bit_order = MSB_FIRST);
    void soft_reset() override;

    /* Initialization */

    void init(uint8_t module = BOOST_PACK_SPI, uint32_t speed = 4000000);
    void add_to(Kernel& k, const str& path) override;

    GPIO* debug;

    void setBitOrder(uint8_t bit_order) { m_bit_order = bit_order & 0x1; }
    void setDataMode(uint8_t mode);
    void setClockDivider(uint8_t divider);  // Value must be even

    /* Operation */

    uint8_t transfer(uint8_t data); // DISABLED bit_order support for max performance
    void multiTransfer(uint32_t* data_out, uint32_t* data_in, uint8_t len);
    void multiTransfer(uint8_t* data_out, uint8_t* data_in, uint8_t len, uint8_t offset = 0);

    static uint8_t reverse(uint8_t n); // Reverse bit order

private:
    uint8_t m_module, m_data_mode, m_bit_order;
    uint32_t m_base, m_speed;
};

#endif /* SPI_H */
