#include "scanner.h"
#include "aos/kernel.h"
#include "devices/gpio.h"
#include "devices/uart.h"
#include "motor.h"

//
// Initialization
//

//Scanner::Scanner(vec<Projector> projectors, Motor& motor, GPIO& trigger) : Device("scanner"),
//    projs(projectors), m_motor(motor), m_trigger(trigger)
//{
//    soft_reset();
//}
Scanner::Scanner(vec<Projector> projectors, Motor& motor, vec<GPIO> triggers) : Device("scanner"),
    projs(projectors), m_motor(motor), m_triggers(triggers)
{
    soft_reset();
}

void Scanner::soft_reset()
{
    Device::soft_reset();
    for (auto& p : projs) p.soft_reset();

    m_fps = 50; m_readout = 4950;  // 10 us Chronos camera trigger overhead (measured and overestimated)
    m_reps = 1, m_rep_index = 0;
    m_settle = 500; m_interval = 16;
    m_led_delay = 6;  // Camera contribute another 4 us due to delayed exposure start (calibrated)
    m_trig_delay = 6;  // Camera's exposure stop delay (also calibrated with the scope)

    m_mode = SCANNER_WAIT; m_done = true;
    m_running = m_ready = m_ack = m_exposing = m_ambient = false;
    m_left = m_overruns = 0;
    m_last_exp = 0;
    debug = nullptr;
    term = nullptr;
}

void Scanner::add_to(Kernel& k, const str& path)
{
    k.add(&m_trig_delay, path + s_"trig_delay", rw_);
    k.add(&m_led_delay, path + s_"led_delay", rw_);
    k.add(&m_interval, path + s_"interval", rw_);
    k.add(&m_readout, path + s_"readout", rw_);
    k.add(&m_settle, path + s_"settle", rw_);
    k.add(&debug, path + s_"debug", rw_);
    k.add(&error, path + s_"error", rw_);
    k.add(&m_fps, path + s_"fps", rw_);
    k.add(&m_reps, path + s_"reps", rw_);
    k.add(&m_ambient, path + s_"amb", rw_);

    k.add(&m_exposing, path + s_"exposing", r__);
    k.add(&m_running, path + s_"running", r__);
    k.add(&m_ready, path + s_"ready", r__);
    k.add(&m_done, path + s_"done", r__);
    k.add(&m_mode, path + s_"mode", r__);

    k.add(&m_overruns, path + s_"overrun", r__);
    k.add(&m_left, path + s_"left", r__);

    for (int i = 0; i < projs.size(); i++)
    {
        k.add_device(&projs[i], path + s_"p" + std::to_string(i+1));
    }


    for (int i = 0; i < m_triggers.size(); i++)
    {
        k.add_device(&m_triggers[i], path + s_"trig" + std::to_string(i+1));
    }

//    k.add_device(&m_trigger, path + s_"trig");
    k.add_device(&m_fps_timer, path + s_"fps_tmr");
    k.add_device(&m_trig_timer, path + s_"trig_tmr");
}

void Scanner::init(uint8_t fps_tmr_id, uint8_t trig_tmr_id)
{
    soft_reset();

    for (auto& p : projs) p.init();

//    m_trigger.init(OUTPUT, HIGH); // active low?
    for (auto& t : m_triggers) t.init(OUTPUT, HIGH);

    m_fps_timer.init(fps_tmr_id, PERIODIC, aos::clock() / m_fps); // original
    m_fps_timer.IntAction = [&]() { FPSTimerHandler(); };

    m_trig_timer.init(trig_tmr_id, ONE_SHOT, aos::clock() / 1000);
    m_trig_timer.IntAction = [&](){ TriggerTimerHandler(); };

    m_initialized = true;
}

void Scanner::priority(uint8_t p)
{
    if (!m_initialized) throw "Scanner: not initialized";

    m_trig_timer.priority(p);
    m_fps_timer.priority(p+1);
}

//
// Interrupt handling
//

void Scanner::FPSTimerHandler()
// TODO: remove FPS Timer Handler
// or, better, make it like the Trigger Timer Handler that
// it makes the system wait until enough time has passed to allow for the next capture
{
    if (m_running)
    {
        if (m_done) m_ready = true;
        else m_overruns += 1;
    }
    else m_ready = false;

    tmr->toggle();
    if (error) error->write(m_overruns);
}

void Scanner::TriggerTimerHandler()
{
//    m_trigger.set();
    for (auto& t : m_triggers) t.set();
    if (debug) debug->clear();

    m_exposing = false;
    m_last_exp = aos::ticks();
}

//
// Operation
//
void Scanner::print_status()
{
    if (term) {
        term->printf("fps - %d, readout - %d, interval - %d\n", m_fps, m_readout, m_interval);
        for (int i=0; i<projs.size(); i++) {
                term->printf("P%d - white: %d us, pattern: %d us, enabled: %d\n",
                             i+1, projs.at(i).white, projs.at(i).pattern, projs.at(i).enabled);
        }
    }

}

void Scanner::start(uint8_t mode, uint32_t fps, uint8_t reps)
{
    m_fps = fps ? fps : m_fps;
    m_reps = reps ? reps : m_reps;
    m_overruns = 0;
    m_mode = mode;
    m_running = true;
    if (m_mode == SCANNER_RUN) m_fps_timer.start(aos::clock() / m_fps);
    if (m_mode == SCANNER_CALIB) m_ready = true;
}

void Scanner::stop(bool reset)
{
    m_mode = SCANNER_WAIT;
    if (term) {
        term->print("stopped\n");
        term->printf("left %d, overrun %d\n", m_left, m_overruns);
    }

    if (reset) m_left = m_overruns = 0;
}

void Scanner::capture_frame(uint32_t duration, GPIO& t, bool wait)
{
    if (!m_initialized) throw "Scanner: not initialized";

    if (m_exposing) aos::delay(5 * us); // Wait for the trigger timer to be done with the previous exposure

    uint64_t elapsed = (aos::ticks() - m_last_exp) / uint64_t(aos::us_ticks()); // in us

    if (elapsed + (uint64_t)duration < (uint64_t)m_readout) // Ensure that the previous frame will be read out without tearing
        aos::delay((m_readout - duration - (uint32_t)elapsed) * us, true); // serve=true might introduce extra jitter

    m_trig_timer.start(duration * aos::us_ticks());
    // here, make it be the correct trigger that is getting cleared
    t.clear();

    if (debug) debug->set();

    aos::delay(m_trig_delay * us); // Camera's exposure is lagging behind the trigger

    if (wait) aos::delay((duration + 1) * us, true); // Optional wait so the next frame can be started immediately
}

void Scanner::capture_pair(Projector& p, GPIO& t)
{
    if (!m_initialized) throw "Scanner: not initialized";

    p.front_on(true); p.side_on(false);
    aos::delay(m_led_delay * us);

    capture_frame(p.pattern, t);

    p.front_on(false); p.side_on(true);
    aos::delay(m_led_delay * us);

    capture_frame(p.white, t);

    p.front_on(false); p.side_on(false);
    // only if you would like to capture ambient image

    if (m_ambient) {
        aos::delay(m_led_delay * us);
        capture_frame(p.white, t);
    }
}

void Scanner::capture_all()
{
    for (int i = 0; i < m_triggers.size(); i++)
    {
        if(projs[i].enabled) capture_pair(projs[i], m_triggers[i]);
    }
}

void Scanner::update()
{
    switch(m_mode)
    {
        case SCANNER_WAIT:
            m_running = false;
//            if (mode_sel.read() == 0)
//            {
//                front.write(btn[0].read());
//                side.write(btn[1].read());
//            }
            break;
        case SCANNER_RUN:
            if (m_left > 0 and m_ready)
            {
                m_ready = false;
                m_done = false;
                capture_all();

                m_left -= 1;
                m_done = true;
            }
            else {
                if (!m_left) {
                    m_mode = SCANNER_WAIT;
                    m_reps = 1;
                    if (term) {
                        term->print("done\n");
                        term->printf("left %d, overrun %d\n", m_left, m_overruns);
                    }
                }
            }
            break;
        case SCANNER_CALIB:
            if (m_left > 0 and m_ready)
            {
                m_ready = true; // original false
                m_done = true; //original false
                if (m_reps <= m_rep_index) {
                    m_motor.move(m_interval);
                    aos::delay(m_settle * ms, true);
                    // First let the sensor reset after long wait
                    // TODO: we needed this for Chronos, do we need this for any other camera?
//                    capture_frame(100 * us, m_triggers[0]); // projectors are expected to be all off

                    m_done = true;
                    m_left -= 1;
                    m_rep_index = 0;
                } else {
                    capture_all();
                    m_rep_index++;
                    m_done = true;
                }
            }
            else {
                if (!m_left) {
                    m_mode = SCANNER_WAIT;
                    m_reps = 1;
                    if (term) {
                        term->print("done\n");
                        term->printf("left %d, overrun %d\n", m_left, m_overruns);
                    }
                }
            }
    }
}
