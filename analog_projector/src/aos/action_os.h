#ifndef ACTION_OS_H
#define ACTION_OS_H

#include <string>
#include <vector>
#include <functional>
#include <exception>

#define HWREG(x) (*((volatile uint32_t *)(x)))

const uint32_t us  = 1; // Default time units
const uint32_t ms  = 1000;
const uint32_t sec = 1000000;
const uint32_t min = 60000000;

template <typename T>
using vec = std::vector<T>;
using str = std::string;

using tAct = std::function<void()>;
using tInt = std::function<void(uint32_t)>;

//class GPIO;
//extern GPIO* g_debug;

namespace aos
{
    const uint8_t c_unknown_reason = 0;
    const uint8_t c_watchdog_reset = 1;
    const uint8_t c_hibernate_awake = 2;
    const uint8_t c_soft_reset = 3;
    const uint8_t c_hard_reset = 4;
    const uint8_t c_power_on_reset = 5;
    const uint8_t c_brown_out_reset = 6;
    const uint8_t c_external_reset = 7;
    const uint8_t c_hardware_request = 8;
    const uint8_t c_initial_boot = 9;

    extern void init(bool watchdog_on); // 80 MHz by default
    extern uint8_t boot_reason(); // Return boot reason
    extern const char * boot_reason_str(); // Formated boot reason
    extern void soft_reset(); // Resets the OS status and clears pending tasks
    extern void hard_reset(); // Resets the entire program
    extern uint32_t clock();  // CPU frequency in Hz
    extern uint32_t memory();  // Gauges the free SRAM memory available

    extern uint64_t ticks();
    extern uint32_t us_ticks();
    extern uint32_t time(uint32_t units);
    extern void delay(uint32_t duration, bool run = false); // 1 hour max (in us)

    extern void schedule(uint32_t duration, tAct job); // Execute job after duration
    extern void priority(uint32_t int_id, uint8_t p); // Set interrupt priority

    extern void start(); // Enable interrupts and set running flag
    extern void serve(); // Serve single task and forward exceptions (if any)
    extern void stop(); // Disable interrupts and clear running flag
    extern void sleep(uint32_t duration); // 200 ms max (in us)

    extern void safe(tAct job); // Catches and forwards exception (if any)
    extern void forward(const char * ex); // Forwards exception (if any)
    extern bool running(); // Running (interrupts enabled) flag
    extern void terminate(); // Forces running() to false without disabling interrupts
    extern void hibernate(); // Shut down with wake pin support
    extern void halt(); // Deep sleep with external reset support
}

#endif /* ACTION_OS_H */
