#ifndef GPIO_H
#define GPIO_H

#include "aos/device.h"

#define PA  0x00
#define PB  0x10
#define PC  0x20
#define PD  0x30
#define PE  0x40
#define PF  0x50
#define PG  0x60
#define PH  0x70

#ifdef PART_TM4C123GH6PM
    #define  RED    PF|1
    #define  BLUE   PF|2
    #define  GREEN  PF|3

    #define  BTN_1  PF|4
    #define  BTN_2  PF|0
#endif

#define LOW     0
#define HIGH    1

#define INPUT   0
#define OUTPUT  1
#define INPUT_PULLUP    2
#define INPUT_PULLDOWN  3

#define NO_INTERRUPT  0
#define RISING_EDGE   1
#define FALLING_EDGE  2
#define BOTH_EDGES    3

class GPIO : public Device
{
public:
    static constexpr const char * const ClassName = "gpio";
    static void PortPriority(uint8_t port, uint8_t p);
    static void PortDebug(uint8_t port, GPIO* pin);

    /* Initialization */

    // Use first 4 bits to denote the port and "or" it with pin number
    GPIO(uint8_t port_pin);  // e.g. PB | 3 - pin #3 of port B
    void soft_reset() override;

    // Use config for interrupt (INPUT) or default value (OUTPUT mode)
    void init(uint8_t mode, uint8_t config = 0);
    void port_priority(uint8_t p);
    void port_debug(GPIO* pin);

    tAct IntAction;
    GPIO* pin_debug;

    void add_to(Kernel& k, const str& path) override;

    /* Operation */

    // Don't check initialization flag for max performance
    // (can be checked with Kernel::validate() though)
    inline void set() { HWREG(m_addr) = m_mask; }
    inline void clear() { HWREG(m_addr) = 0; }
    inline void toggle() { HWREG(m_addr) = HWREG(m_addr) ? 0 : m_mask; }

    inline uint8_t pin() const { return m_pin; }
    inline uint8_t port() const { return m_port; }
    inline uint8_t mask() const { return m_mask; }
    inline uint8_t read() { return HWREG(m_addr); }
    inline void write(uint8_t v) { HWREG(m_addr) = v ? m_mask : 0; }

private:
    uint8_t m_port, m_mask, m_pin, m_mode, m_config;
    uint32_t m_addr;
};

#endif /* GPIO_H */
