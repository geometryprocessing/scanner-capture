#ifndef MOTOR_H
#define MOTOR_H

#include "aos/device.h"
#include "devices/timer.h"

class GPIO;

class Motor : public Device
{
public:
    static constexpr const char * const ClassName = "motor";

    Motor(GPIO& pul, GPIO& dir, GPIO& ena, GPIO& lim_h, GPIO& lim_p, bool inverted);
    void soft_reset() override;

    /* Initialization */

    void init(uint8_t speed_tmr_id, uint8_t reset_tmr_id);
    void add_to(Kernel& k, const str& path) override;
    void priority(uint8_t p); // timer priorities

    GPIO* debug;
    bool aiming;

    /* Operation */

    void start(uint8_t speed_divider);
    void home();
    void park();
    void step();

    bool forward() const { return m_forward; }
    void forward(bool new_forward);

    uint32_t pos() const { return m_pos; }
    uint32_t max_pos() const { return m_max_pos; }
    uint32_t target() const { return m_target; }
    void target(uint32_t new_target);
    void move(int32_t offset);

    bool homed() const { return m_homed; }
    bool homing() const { return m_homing; }
    bool parking() const { return m_parking; }
    bool home_stop() const { return m_home_stop == 0; }
    bool park_stop() const { return m_park_stop == 0; }
    uint32_t speed() const { return c_base_freq / m_speed_divider; }

private:
    const uint32_t c_base_freq = 24000;
//    const uint32_t c_steps_per_rev = 400;
    GPIO &m_pul, &m_dir, &m_ena, &m_lim_h, &m_lim_p;

    Timer m_speed_timer, m_reset_timer;

    void SpeedTimerHandler();
    void update();
    void do_step();
    void pause();

    bool m_homed, m_homing, m_parking, m_do_step, m_forward, m_step_once, m_inverted;
    uint8_t m_home_stop, m_park_stop, m_speed_divider, m_tick, m_bit, m_wait;
    uint32_t m_num_steps, m_num_skipped, m_pos, m_max_pos, m_target;
};

#endif /* MOTOR_H */
