#ifndef TIMER_H
#define TIMER_H

#include "aos/device.h"

class GPIO;

#define ONE_SHOT  0
#define PERIODIC  1

class Timer : public Device
{
public:
    static constexpr const char * const ClassName = "timer";
    static void Priority(uint8_t id, uint8_t p);

    Timer();
    void soft_reset() override;

    void init(uint8_t id, uint8_t mode, uint32_t period);
    void priority(uint8_t p);

    tAct IntAction;
    GPIO* int_debug;

    void add_to(Kernel& k, const str& path) override;

    void start(uint32_t period = 0);
    void start_over();
    void stop();

    inline uint8_t id() const { return m_id; }
    inline uint8_t mode() const { return m_mode; }
    inline uint32_t period() const { return m_period; }

    inline uint32_t read() { return HWREG(m_addr); }
    inline void write(uint32_t v) { HWREG(m_addr) = v; }

private:
    uint8_t m_id, m_mode;
    uint32_t m_base, m_addr, m_period;
};

#endif /* TIMER_H */
