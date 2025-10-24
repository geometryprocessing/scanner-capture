#include "timer.h"
#include "aos/kernel.h"
#include "gpio.h"

#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"
#include "inc/hw_timer.h"
#include "driverlib/sysctl.h"
#include "driverlib/timer.h"
#include "driverlib/interrupt.h"

#ifdef PART_TM4C123GH6PM
    #define INT_TIMER6A_TM4C123 0
    #define INT_TIMER7A_TM4C123 0
#endif

#define NUM_TIMERS  8

const uint32_t TimerBase[] = { TIMER0_BASE, TIMER1_BASE, TIMER2_BASE, TIMER3_BASE,
                               TIMER4_BASE, TIMER5_BASE, TIMER6_BASE, TIMER7_BASE};

const uint32_t TimerInt[] = { INT_TIMER0A, INT_TIMER1A, INT_TIMER2A, INT_TIMER3A,
                              INT_TIMER4A, INT_TIMER5A, INT_TIMER6A, INT_TIMER7A};

const uint32_t TimerPeriph[] = { SYSCTL_PERIPH_TIMER0, SYSCTL_PERIPH_TIMER1,
                                 SYSCTL_PERIPH_TIMER2, SYSCTL_PERIPH_TIMER3,
                                 SYSCTL_PERIPH_TIMER4, SYSCTL_PERIPH_TIMER5,
                                 SYSCTL_PERIPH_TIMER6, SYSCTL_PERIPH_TIMER7};

static vec<Timer*> g_timers(NUM_TIMERS, nullptr);

//
// Interrupts
//

static void TimerIntHandler(uint8_t id);

extern "C"  // Interrupts must have C bindings
{
    void Timer0IntHandler(void) { TimerIntHandler(0); }
    void Timer1IntHandler(void) { TimerIntHandler(1); }
    void Timer2IntHandler(void) { TimerIntHandler(2); }
    void Timer3IntHandler(void) { TimerIntHandler(3); }
    void Timer4IntHandler(void) { TimerIntHandler(4); }
    void Timer5IntHandler(void) { TimerIntHandler(5); }
    void Timer6IntHandler(void) { TimerIntHandler(6); }
    void Timer7IntHandler(void) { TimerIntHandler(7); }
}

static void (*pTimerIntHandlers[])(void)
{
    Timer0IntHandler, Timer1IntHandler, Timer2IntHandler, Timer3IntHandler,
    Timer4IntHandler, Timer5IntHandler, Timer6IntHandler, Timer7IntHandler
};

void TimerIntHandler(uint8_t id)
{
    // Make sure to clear interrupt before running any user code
    uint32_t base = TimerBase[id & 0x7];
    uint32_t status = TimerIntStatus(base, true);
    TimerIntClear(base, status);

    aos::safe([&]()
    {
        Timer* timer = g_timers.at(id); // Extra check for valid id
        if (timer) {
            if (timer->int_debug) timer->int_debug->set();
            if (timer->IntAction) timer->IntAction();
            if (timer->int_debug) timer->int_debug->clear();
        }
    });
}

void Timer::Priority(uint8_t id, uint8_t p)
{
    if (id >= NUM_TIMERS) throw "Timer: not supported";

    aos::priority(TimerInt[id & 0x7], p);
}

//
// Initialization
//

Timer::Timer() : Device("timer"), m_id(0), m_mode(0), m_base(0), m_addr(0) { soft_reset(); }

void Timer::soft_reset()
{
    Device::soft_reset();

    m_period = 0;
    IntAction = nullptr;
    int_debug = nullptr;
}

void Timer::add_to(Kernel& k, const str& path)
{
    k.add(&m_id, path + s_"id", r__);
    k.add(&m_mode, path + s_"mode", r__);
    k.add(&m_period, path + s_"period", r__);
    k.add(&IntAction, path + s_"int", rw_);
    k.add(&int_debug, path + s_"debug", rw_);

    k.add(tAct([&](){ start(); }), path + s_"start", __x);
    k.add(tAct([&](){ stop(); }), path + s_"stop", __x);
    k.add(tAct([&](){ soft_reset(); }), path + s_"reset", __x);
//    k.add(tGet<uint32_t>([&]() { return read(); }), path + s_"read", __x);
//    k.add(tSet<uint32_t>([&](uint32_t v) { write(v); }), path + s_"write", __x);
}

void Timer::init(uint8_t id, uint8_t mode, uint32_t period)
{
    soft_reset();

    if (id >= NUM_TIMERS) throw "Timer: not supported";
    if (mode > 1) throw "Timer: invalid mode";

    m_id = id & 0x7;
    m_mode = mode & 0x1;
    m_base = TimerBase[m_id];
    m_addr = m_base + TIMER_O_TAV;

    if(!SysCtlPeripheralPresent(TimerPeriph[m_id])) throw "Timer: Peripheral not present!";

    SysCtlPeripheralEnable(TimerPeriph[m_id]);
    while(!SysCtlPeripheralReady(TimerPeriph[m_id]));
    TimerDisable(m_base, TIMER_A);

    TimerConfigure(m_base, m_mode == PERIODIC ? TIMER_CFG_PERIODIC : TIMER_CFG_ONE_SHOT);
    if (period) TimerLoadSet(m_base, TIMER_A, (m_period = period) - 1);

    TimerIntRegister(m_base, TIMER_A, pTimerIntHandlers[m_id]);
    TimerIntEnable(m_base, TIMER_TIMA_TIMEOUT);

    g_timers.at(id) = this;

    IntEnable(TimerInt[m_id]);

    m_initialized = true;
};

void Timer::priority(uint8_t p)
{
    if (!m_initialized) throw "Timer: not initialized";

    Priority(m_id, p);
}

//
// Operation
//

void Timer::start(uint32_t period)
{
    if (!m_initialized) throw "Timer: not initialized";

    if (period) TimerLoadSet(m_base, TIMER_A, (m_period = period) - 1);

    if (!m_period) throw "Timer: invalid period";

    TimerEnable(m_base, TIMER_A);
}

void Timer::start_over()
{
    if (!m_initialized) throw "Timer: not initialized";
    if (!m_period) throw "Timer: invalid period";

    HWREG(m_addr) = m_period - 1;
}

void Timer::stop()
{
    if (!m_initialized) throw "Timer: not initialized";

    TimerDisable(m_base, TIMER_A);
}
