#ifndef SCANNER_H
#define SCANNER_H

#include "aos/device.h"
#include "devices/timer.h"
#include "projector.h"

class GPIO;
class UART;
class Motor;

// Scanner operation
#define SCANNER_WAIT   0
#define SCANNER_RUN    1
#define SCANNER_CALIB  2

class Scanner : public Device
{
public:
    static constexpr const char * const ClassName = "scanner";

//    Scanner(vec<Projector> projectors, Motor& motor, GPIO& trigger);
    Scanner(vec<Projector> projectors, Motor& motor, vec<GPIO> triggers);
    // TODO: instead of GPIO& trigger, have vec<GPIO> triggers?
    void soft_reset() override;

    /* Initialization */

    void init(uint8_t fps_tmr_id, uint8_t trig_tmr_id);
    void add_to(Kernel& k, const str& path) override;
    void priority(uint8_t p); // timer priorities

    vec<Projector> projs;
    GPIO *debug, *tmr, *error;
    UART *term;

    /* Operation */

    void print_status();

    uint32_t left() const { return m_left; }
    uint32_t overruns() const { return m_overruns; }

    bool exposing() const { return m_exposing; }
    bool enabled() const { return m_running; }
    bool ready() const { return m_ready; }
    bool done() const { return m_done; }

    uint32_t fps() const { return m_fps; }
    void fps(uint32_t fps) { m_fps = fps; }
    uint32_t interval() const { return m_interval; }
    void interval(uint32_t interval) { m_interval = interval; }
    uint8_t reps() const { return m_reps; }
    void reps(uint8_t reps) { m_reps = reps; }


    bool valid(bool reset=true)
    {
        if (reset) m_overruns = 0;
        return true;
    }

    void schedule(uint32_t scans) { m_left += scans; }
    void start(uint8_t mode, uint32_t fps=0, uint8_t reps=0);
    void stop(bool reset=false);

    // blocking until the trigger is ready but serving during the optional wait of duration us
    void capture_frame(uint32_t duration, GPIO& t, bool wait=true);
    void capture_pair(Projector& p, GPIO& t);
    void capture_all();
    void update();

private:
    Motor& m_motor;
//    GPIO& m_trigger; //TODO: have this be called vec<GPIO> m_triggers;
    vec<GPIO> m_triggers;

    Timer m_fps_timer, m_trig_timer;

    void FPSTimerHandler();
    void TriggerTimerHandler();

    uint8_t m_mode; // FSM state
    uint8_t m_reps, m_rep_index; // repetitions
    bool m_running, m_ready, m_ack, m_done, m_exposing, m_ambient; // Operational flags
    uint32_t m_fps, m_readout, m_led_delay, m_trig_delay; // Hz (scans), us, us, and us
    uint32_t m_settle, m_interval; // ms, and steps (400 per revolution of 5 mm)
    uint32_t m_left, m_overruns;
    uint64_t m_last_exp, m_last_frame; // ticks
};


#endif /* SCANNER_H */
