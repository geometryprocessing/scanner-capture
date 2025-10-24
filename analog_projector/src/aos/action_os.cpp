#include <queue>

#include "action_os.h"
//#include "devices/gpio.h"

#include "inc/hw_nvic.h"
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_timer.h"
#include "driverlib/sysctl.h"
#include "driverlib/timer.h"
#include "driverlib/systick.h"
#include "driverlib/watchdog.h"
#include "driverlib/hibernate.h"
#include "driverlib/interrupt.h"

struct Task {
    uint64_t time;
    tAct job;

    Task(uint64_t time, tAct job) : time(time), job(job) {}
};

class Scheduler {
public:
    inline bool operator() (const Task& t1, const Task& t2) { return t1.time > t2.time; }
};

template <class Q>
void clear_queue(Q & q) { q = Q(); }

static std::priority_queue <Task, vec<Task>, Scheduler> g_tasks;

//GPIO* g_debug = nullptr;

#define MAX_EX  64
static char g_exception[MAX_EX] = {0};

static uint8_t g_boot_reason = 0;
static uint32_t g_clock_hz = 0;
static uint32_t g_us_ticks = 0;

static bool g_initialized = false;
static bool g_terminate = false;
static bool g_running = false;

//
// System Timers
//

extern "C"  // Interrupts must have C bindings
{
    void SysTickIntHandler(void) {} // dummy for sleep() function

    void WatchDogIntHandler(void)
    {
        WatchdogIntClear(WATCHDOG0_BASE); // clear to prevent reboot on second timeout
    }
}

//
// Action OS
//

namespace aos
{

void soft_reset()
{
    clear_queue(g_tasks);
    g_exception[0] = 0;
    g_terminate = false;
    g_running = false;
//    g_debug = nullptr;

    g_boot_reason = c_soft_reset;
}

void hard_reset()
{
    SysCtlReset();
}

void halt()
{
    stop();
    WatchdogResetDisable(WATCHDOG0_BASE);
    SysCtlDeepSleep();
    while(1);
}

void hibernate()
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_HIBERNATE);
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_HIBERNATE));
    HibernateEnableExpClk(SysCtlClockGet());
    HibernateClockConfig(HIBERNATE_OSC_LOWDRIVE);
    HibernateWakeSet(HIBERNATE_WAKE_PIN);
    HibernateGPIORetentionEnable();
    HibernateRequest();
    while(1);
}

void init(bool watchdog_on)
{
    if (g_initialized) return; // init only once

    soft_reset();

    /* Configure CPU speed (80 MHz for TM4C123GH6PM) */

    SysCtlClockSet(SYSCTL_SYSDIV_2_5 | SYSCTL_USE_PLL | SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);
    g_clock_hz = SysCtlClockGet();
    g_us_ticks = g_clock_hz / 1000000;

    /* Determine boot reason */

    uint32_t reason = SysCtlResetCauseGet();
    SysCtlResetCauseClear(reason);

    if (reason & SYSCTL_CAUSE_WDOG0) g_boot_reason = c_watchdog_reset;
    else if (reason & SYSCTL_CAUSE_POR) g_boot_reason = c_power_on_reset;
    else if (reason & SYSCTL_CAUSE_HIB) g_boot_reason = c_hibernate_awake;
    else if (reason & SYSCTL_CAUSE_HSRVREQ) g_boot_reason = c_hardware_request;
    else if (reason & SYSCTL_CAUSE_BOR) g_boot_reason = c_brown_out_reset;
    else if (reason & SYSCTL_CAUSE_EXT) g_boot_reason = c_external_reset;
    else if (reason & SYSCTL_CAUSE_SW) g_boot_reason = c_hard_reset;
    else if (reason) g_boot_reason = c_unknown_reason;
    else g_boot_reason = c_initial_boot;

    /* Use SysTick timer to implement sleep() function */

    SysTickIntRegister(SysTickIntHandler);
    IntPrioritySet(FAULT_SYSTICK, 7 << 5); // the lowest (let other interrupts wake first)
    SysCtlSleepPowerSet(SYSCTL_FLASH_NORMAL | SYSCTL_SRAM_NORMAL);
    SysTickPeriodSet(NVIC_ST_CURRENT_M);
    SysTickIntEnable();
    SysTickDisable();

    /* Use wide timer to implement time() function */

    SysCtlPeripheralEnable(SYSCTL_PERIPH_WTIMER0);
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_WTIMER0));
    TimerDisable(WTIMER0_BASE, TIMER_A);

    TimerConfigure(WTIMER0_BASE, TIMER_CFG_PERIODIC_UP);
    TimerControlStall(WTIMER0_BASE, TIMER_A, true); // stop counting during debugging
    TimerEnable(WTIMER0_BASE, TIMER_A);
    TimerLoadSet64(WTIMER0_BASE, 0xFFFFFFFFFFFFFFFFUL); // won't overflow again for a while :)

    /* Use WatchDog timer to recover from failed interrupts */

    if (watchdog_on)
    {
        SysCtlPeripheralEnable(SYSCTL_PERIPH_WDOG0);
        while(!SysCtlPeripheralReady(SYSCTL_PERIPH_WDOG0));
        if(WatchdogLockState(WATCHDOG0_BASE) == true) WatchdogUnlock(WATCHDOG0_BASE);

        WatchdogIntRegister(WATCHDOG0_BASE, WatchDogIntHandler);
        IntPrioritySet(INT_WATCHDOG, 7 << 5); // the lowest (to reboot on failed interrupts)

        WatchdogReloadSet(WATCHDOG0_BASE, 0x7FFFFFF); // ~1.5 sec @ 80 MHz (reboots on second timeout)
        WatchdogResetEnable(WATCHDOG0_BASE);
        WatchdogStallEnable(WATCHDOG0_BASE); // prevents timeout during debugging

        WatchdogIntEnable(WATCHDOG0_BASE);
        WatchdogEnable(WATCHDOG0_BASE);
    }

    g_initialized = true;
}

uint8_t boot_reason()
{
    return g_boot_reason;
}

const char * boot_reason_str()
{
    switch (g_boot_reason)
    {
        case c_unknown_reason: return "unknown reason";
        case c_watchdog_reset: return "watchdog reset";
        case c_hibernate_awake: return "hibernate awake";
        case c_soft_reset: return "soft reset";
        case c_hard_reset: return "hard reset";
        case c_power_on_reset: return "power on reset";
        case c_brown_out_reset: return "brown out reset";
        case c_external_reset: return "external reset";
        case c_hardware_request: return "hardware request";
        case c_initial_boot: return "initial boot";
        default: return "default";
    }
}

uint32_t clock()
{
    if (!g_initialized) throw "Clock: Uninitialized OS";

    return g_clock_hz;
}

uint32_t memory()
{
    uint32_t available = 0;

    for (uint32_t step = (1 << 14); step > 0; step >>= 1)
    try { volatile vec<uint8_t> buf(available += step); }
    catch (std::exception& ex) { available -= step; }

    return available;
}

uint64_t ticks()
{
    if (!g_initialized) throw "Ticks: Uninitialized OS";

    return TimerValueGet64(WTIMER0_BASE);
}

uint32_t us_ticks()
{
    if (!g_initialized) throw "us ticks: Uninitialized OS";

    return g_us_ticks;
}

uint32_t time(uint32_t units)
{
    if (!g_initialized) throw "Time: Uninitialized OS";
    if (!units) throw "Time: Invalid units";

    return TimerValueGet64(WTIMER0_BASE) / ((uint64_t)g_us_ticks * (uint64_t)units);
}

void delay(uint32_t duration, bool run)
{
    if (!g_initialized) throw "Delay: Uninitialized OS";
    if (!duration) throw "Delay: Invalid duration";

    // Adjust (approximately) for computation delay by subtracting 1 us from the duration
    uint64_t thr = TimerValueGet64(WTIMER0_BASE) + (uint64_t)g_us_ticks * (uint64_t)(duration - 1);
    while(TimerValueGet64(WTIMER0_BASE) < thr) if (run) serve();
}

void sleep(uint32_t duration)
{
    if (!g_initialized) throw "Delay: Uninitialized OS";
    if (!duration) throw "Delay: Invalid duration";

    // Adjust (approximately) for computation delay by subtracting 1 us from the duration
    SysTickPeriodSet(std::min(g_us_ticks * (duration - 1), (uint32_t)NVIC_ST_CURRENT_M));
    SysTickEnable();
    SysCtlSleep();
    SysTickDisable();
}

void schedule(uint32_t duration, tAct job)
{
    if (!g_initialized) throw "Schedule: Uninitialized OS";
    if (!job) throw "Schedule: Empty job";
    if (!duration) throw "Schedule: Invalid duration";

    // Adjust (approximately) for computation delay by subtracting 1 us from the duration
    g_tasks.emplace(TimerValueGet64(WTIMER0_BASE) + (uint64_t)g_us_ticks * (uint64_t)(duration - 1), job);
}

void priority(uint32_t int_id, uint8_t p)
{
    if (!g_initialized) throw "Priority: Uninitialized OS";
//    if (p == 0) throw "Priority: Reserved priority";
    if (p > 7) throw "Priority: Unsupported priority";

    IntPrioritySet(int_id, p << 5);
}

void start()
{
    IntMasterEnable();
    g_running = true;
}
void stop()
{
    IntMasterDisable();
    g_running = false;
}

bool running()
{
    return g_running && !g_terminate;
}

void terminate()
{
    g_terminate = true;
}

void serve()
{
    if (!g_tasks.empty())
    {
        const Task& t = g_tasks.top();

        if (t.time < TimerValueGet64(WTIMER0_BASE))
        {
            t.job();
            g_tasks.pop();
        }
    }
    if (g_exception[0]) throw g_exception;
}

void safe(tAct job)
{
    try { job(); }
    catch (const char* ex) { forward(ex); }
    catch (std::exception& ex) { forward(ex.what()); }
}

void forward(const char * ex)
{
    size_t n = strlen(ex);
    strncpy(g_exception, ex, MAX_EX - 1);
    g_exception[std::min(n, size_t(MAX_EX - 1))] = 0;
}

}
