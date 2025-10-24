#include "motor.h"
#include "aos/kernel.h"
#include "devices/gpio.h"

//
// Initialization
//

Motor::Motor(GPIO& pul, GPIO& dir, GPIO& ena, GPIO& lim_h, GPIO& lim_p, bool inverted) : Device("motor"),
    m_pul(pul), m_dir(dir), m_ena(ena), m_lim_h(lim_h), m_lim_p(lim_p), m_inverted(inverted)
{
    soft_reset();
}

void Motor::soft_reset()
{
    Device::soft_reset();

    debug = nullptr;
    m_speed_divider = 6; // max speed (4000 steps per second) by default
    m_home_stop = m_park_stop = m_tick = m_bit = m_wait = 0;
    m_num_steps = m_num_skipped = m_pos = m_max_pos = m_target = 0;
    m_homed = m_homing = m_parking = m_do_step = m_forward = m_step_once = aiming = false;
    aiming = m_homing = true; // auto-home on startup and start aiming
}

void Motor::add_to(Kernel& k, const str& path)
{
    k.add(&debug, path + s_"debug", rw_);
    k.add(&aiming, path + s_"aiming", rw_);
    k.add(&m_target, path + s_"target", rw_);
    k.add(&m_forward, path + s_"forward", rw_);
    k.add(&m_step_once, path + s_"step_once", rw_);
    k.add(&m_num_steps, path + s_"num_steps", rw_);
    k.add(&m_num_skipped, path + s_"skipped", rw_);
    k.add(&m_speed_divider, path + s_"divider", rw_);

    k.add(&m_home_stop, path + s_"h_stop", r__);
    k.add(&m_park_stop, path + s_"p_stop", r__);
    k.add(&m_inverted, path + s_"inverted", r__);
    k.add(&m_do_step, path + s_"do_step", r__);
    k.add(&m_max_pos, path + s_"max_pos", r__);
    k.add(&m_parking, path + s_"parking", r__);
    k.add(&m_homing, path + s_"homing", r__);
    k.add(&m_homed, path + s_"homed", r__);
    k.add(&m_wait, path + s_"wait", r__);
    k.add(&m_pos, path + s_"pos", r__);

    k.add_device(&m_pul, path + s_"pul");
    k.add_device(&m_dir, path + s_"dir");
    k.add_device(&m_ena, path + s_"ena");

    k.add_device(&m_lim_h, path + s_"lim_h");
    k.add_device(&m_lim_p, path + s_"lim_p");

    k.add_device(&m_speed_timer, path + s_"spd_tmr");
    k.add_device(&m_reset_timer, path + s_"rst_tmr");
    k.add(tAct([&](){ home(); }), path + s_"home", __x);
    k.add(tAct([&](){ park(); }), path + s_"park", __x);
    k.add(tAct([&](){ step(); }), path + s_"step", __x);
}

void Motor::init(uint8_t speed_tmr_id, uint8_t reset_tmr_id)
{
    soft_reset();

    if (m_inverted)
    {
        m_pul.init(OUTPUT, LOW); // active high
        m_dir.init(OUTPUT, HIGH); // backwards (to home) by default
        m_ena.init(OUTPUT, LOW); // enabled by default
    } else {
        m_pul.init(OUTPUT, HIGH); // active low
        m_dir.init(OUTPUT, HIGH); // backwards (to home) by default
        m_ena.init(OUTPUT, HIGH); // enabled by default
    }

    m_lim_h.init(INPUT_PULLUP);
    m_lim_p.init(INPUT_PULLUP);

    m_speed_timer.init(speed_tmr_id, PERIODIC, aos::clock() / c_base_freq);
    m_speed_timer.IntAction = [&]() { SpeedTimerHandler(); };

    m_reset_timer.init(reset_tmr_id, ONE_SHOT, 3 * aos::clock() / c_base_freq / 2);
    if (m_inverted) m_reset_timer.IntAction = [&](){ m_pul.clear(); if (debug) debug->clear(); };
    else m_reset_timer.IntAction = [&](){ m_pul.set(); if (debug) debug->clear(); };

    m_initialized = true;
}

void Motor::priority(uint8_t p)
{
    if (!m_initialized) throw "Motor: not initialized";

    m_speed_timer.priority(p);
    m_reset_timer.priority(p);
}

void Motor::forward(bool new_forward)
{
    if (new_forward != m_forward)
    {
//        if (m_inverted) m_dir.write(m_forward = new_forward);
//        else m_dir.write(!(m_forward = new_forward));
        m_dir.write(!(m_forward = new_forward));

        pause();
    }
}

void Motor::pause()
{
    // wait 6 us for dir pin change to take effect
    aos::delay(6 * us);
    // wait 10 ms for carriage to stop
    m_wait = c_base_freq / m_speed_divider / 100;
}

//
// Interrupt handling
//

void Motor::SpeedTimerHandler()
{
    update();
    m_tick++;

    if (m_tick % m_speed_divider == 0)
    {
        m_tick = 0;

        if ((m_do_step || m_step_once) && !m_wait) do_step();

        if (m_wait) m_wait--;
    }
}

void Motor::update()
{
    uint8_t mask = 1 << m_bit;
    m_bit = (m_bit + 1) % 3;

    if (m_lim_h.read()) m_home_stop |= mask;
    else m_home_stop &= ~mask;

    if (m_lim_p.read()) m_park_stop |= mask;
    else m_park_stop &= ~mask;

    m_do_step = false;

    if (m_homing)
    {
        forward(false);
        m_do_step = true;

        if (home_stop())
        {
            m_pos = 0;
            m_max_pos = 0;
            m_homed = true;
            m_homing = false;
            m_do_step = false;
            forward(true); // includes pause();
        }
    }
    else if (m_parking)
    {
        forward(true);
        m_do_step = true;

        if (park_stop())
        {
            m_parking = false;
            m_do_step = false;
            forward(false); // includes pause();
        }
    }
    else if (aiming)
    {
        if (park_stop() && m_pos < m_target) m_target = m_pos;
        if (home_stop() && m_pos > m_target) m_target = m_pos;

        m_do_step = (m_pos != m_target);

        if (m_do_step) forward(m_pos < m_target);
    }

    // make sure direction pin is set correctly
    // (e.g. in case of debugging interference bypassing pause)
//    if (m_inverted) m_dir.write(m_forward);
//    else m_dir.write(!m_forward);
    m_dir.write(!m_forward);
}

void Motor::do_step()
{
    m_step_once = false;

    if ((m_forward && park_stop()) || (!m_forward && home_stop()))
    {
        m_num_skipped++;
        return;
    }

    if (m_inverted) m_pul.set(); // active high
    else m_pul.clear(); // active low
    m_reset_timer.start(); // pull back high after the delay
    m_num_steps++;

    if (!m_homing)
    {
        if (m_forward) m_pos++;
        else m_pos = m_pos > 0 ? m_pos - 1 : 0;

        m_max_pos = std::max(m_pos, m_max_pos);
    }

    if (debug) debug->set();
}

//
// Operation
//

void Motor::start(uint8_t speed_divider)
{
    if (!m_initialized) throw "Motor: not initialized";
    if (speed_divider < 3) throw "Motor: speed divider must be greater than 2";

    m_tick = m_bit = 0;
    m_speed_divider = speed_divider;
    m_speed_timer.start();
}

void Motor::home()
{
    if (!m_initialized) throw "Motor: not initialized";

    if (m_parking) return;

    m_homing = true;
    m_homed = false;
}

void Motor::park()
{
    if (!m_initialized) throw "Motor: not initialized";

    if (m_homing) return;

    m_parking = true;
}

void Motor::step()
{
    if (!m_initialized) throw "Motor: not initialized";

    if (m_homing || m_parking) return;

    m_step_once = true;
}

void Motor::target(uint32_t new_target)
{
    if (!m_initialized) throw "Motor: not initialized";

    m_target = new_target;
}

void Motor::move(int32_t offset)
{
    if (!m_initialized) throw "Motor: not initialized";

    m_target = std::max(0, (int32_t)m_target + offset);
}
