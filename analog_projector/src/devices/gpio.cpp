#include "gpio.h"
#include "aos/kernel.h"

#include "inc/hw_memmap.h"
#include "inc/hw_gpio.h"
#include "inc/hw_ints.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"

//#ifdef PART_TM4C123GH6PM
//    #define GPIO_LOCK_KEY        0x4C4F434B
//#endif

#define NUM_PORTS  8

const uint32_t PortBase[] = { GPIO_PORTA_BASE, GPIO_PORTB_BASE, GPIO_PORTC_BASE, GPIO_PORTD_BASE,
                              GPIO_PORTE_BASE, GPIO_PORTF_BASE, GPIO_PORTG_BASE, GPIO_PORTH_BASE};

const uint32_t PortInt[] = { INT_GPIOA, INT_GPIOB, INT_GPIOC, INT_GPIOD,
                             INT_GPIOE, INT_GPIOF, INT_GPIOG, INT_GPIOH};

const uint32_t PortPeriph[] = { SYSCTL_PERIPH_GPIOA, SYSCTL_PERIPH_GPIOB,
                                SYSCTL_PERIPH_GPIOC, SYSCTL_PERIPH_GPIOD,
                                SYSCTL_PERIPH_GPIOE, SYSCTL_PERIPH_GPIOF,
                                SYSCTL_PERIPH_GPIOG, SYSCTL_PERIPH_GPIOH};

static vec<GPIO*> g_port_debug(NUM_PORTS, nullptr);  // PA, PB ...
static vec<GPIO*> g_port_pins(NUM_PORTS * 8, nullptr);  // PA[0] .. PA[7], PB[0] ...

//
// Interrupts
//

static void PortIntHandler(uint8_t port);

extern "C"  // Interrupts must have C bindings
{
    void PortAIntHandler(void) { PortIntHandler(PA >> 4); }
    void PortBIntHandler(void) { PortIntHandler(PB >> 4); }
    void PortCIntHandler(void) { PortIntHandler(PC >> 4); }
    void PortDIntHandler(void) { PortIntHandler(PD >> 4); }
    void PortEIntHandler(void) { PortIntHandler(PE >> 4); }
    void PortFIntHandler(void) { PortIntHandler(PF >> 4); }
    void PortGIntHandler(void) { PortIntHandler(PG >> 4); }
    void PortHIntHandler(void) { PortIntHandler(PH >> 4); }
}

static void (*pPortIntHandlers[])(void)
{
    PortAIntHandler, PortBIntHandler, PortCIntHandler, PortDIntHandler,
    PortEIntHandler, PortFIntHandler, PortGIntHandler, PortHIntHandler
};

void PortIntHandler(uint8_t port)
{
    // Make sure to clear interrupt before running any user code
    uint32_t base = PortBase[port & 0x7];
    uint32_t status = GPIOIntStatus(base, true);
    GPIOIntClear(base, status);

    aos::safe([&]()
    {
        GPIO* port_debug = g_port_debug.at(port); // Extra check for valid port
        if (port_debug) port_debug->set();

        GPIO** pin = g_port_pins.data() + port * 8;

        while (status)
        {
            if ((status & 0x1) && *pin)
            {
                if ((*pin)->pin_debug) (*pin)->pin_debug->set();
                if ((*pin)->IntAction) (*pin)->IntAction();
                if ((*pin)->pin_debug) (*pin)->pin_debug->clear();
            }
            status = status >> 1;
            pin++;
        }

        if (port_debug) port_debug->clear();
    });
}

void GPIO::PortPriority(uint8_t port, uint8_t p)
{
    if (port & 0xF0) port = port >> 4;

    if (port >= NUM_PORTS) throw "Port: not supported";

    aos::priority(PortInt[port & 0x7], p);
}

void GPIO::PortDebug(uint8_t port, GPIO* pin)
{
    if (port & 0xF0) port = port >> 4;

    if (port >= NUM_PORTS) throw "Port: not supported";

    g_port_debug.at(port) = pin;
}

//
// Initialization
//

GPIO::GPIO(uint8_t port_pin) : Device("gpio"), m_mode(0), m_config(0)
{
    m_pin = port_pin & 0x7;
    m_port = (port_pin >> 4) & 0x7;
    m_mask = 1 << (m_pin);

    m_addr = PortBase[m_port] + GPIO_O_DATA + (m_mask << 2);

    soft_reset();
}

void GPIO::soft_reset()
{
    Device::soft_reset();

    IntAction = nullptr;
    pin_debug = nullptr;
}

void GPIO::add_to(Kernel& k, const str& path)
{
//    k.add(&m_pin, path + s_"pin", r__);
//    k.add(&m_port, path + s_"port", r__);
//    k.add(&m_mode, path + s_"mode", r__);
//    k.add(&m_config, path + s_"config", r__);
    k.add(&IntAction, path + s_"int", rw_);
    k.add(&pin_debug, path + s_"debug", rw_);
//    k.add(g_port_debug.data() + m_port, path + s_"debug", rw_);

    k.add(tAct([&](){ set(); }), path + s_"set", __x);
    k.add(tAct([&](){ clear(); }), path + s_"clear", __x);
    k.add(tAct([&](){ toggle(); }), path + s_"toggle", __x);
//    k.add(tGet<uint8_t>([&]() { return read(); }), path + s_"read", __x);
//    k.add(tSet<uint8_t>([&](uint8_t v) { write(v); }), path + s_"write", __x);
}

void GPIO::init(uint8_t mode, uint8_t config)
{
    soft_reset();

    if (mode > 3) throw "GPIO: invalid mode";
    if (config > 3) throw "GPIO: invalid config";

    m_mode = mode & 0x3;
    m_config = config & 0x3;

    uint32_t base = PortBase[m_port];
    volatile uint32_t* lock = (volatile uint32_t *)(base + 0x520);
    volatile uint32_t* cr = (volatile uint32_t *)(base + 0x524);

    if(!SysCtlPeripheralPresent(PortPeriph[m_port])) throw "GPIO: Port not present!";

    SysCtlPeripheralEnable(PortPeriph[m_port]);
    while(!SysCtlPeripheralReady(PortPeriph[m_port]));

    *lock = GPIO_LOCK_KEY;
    *cr |= m_mask;

    if (m_mode == INPUT)
    {
        GPIOPinTypeGPIOInput(base, m_mask);
    }
    else if (m_mode == INPUT_PULLUP)
    {
//        *lock = GPIO_LOCK_KEY;
//        *cr |= m_mask;
        GPIODirModeSet(base, m_mask, GPIO_DIR_MODE_IN);
        GPIOPadConfigSet(base, m_mask, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
//        *cr &= ~m_mask;
//        *lock = 0;
    }
    else if (m_mode == INPUT_PULLDOWN)
    {
//        *lock = GPIO_LOCK_KEY;
//        *cr |= m_mask;
        GPIODirModeSet(base, m_mask, GPIO_DIR_MODE_IN);
        GPIOPadConfigSet(base, m_mask, GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPD);
//        *cr &= ~m_mask;
//        *lock = 0;
    }
    else // m_mode == OUTPUT
    {
        GPIOPinTypeGPIOOutput(base, m_mask);
        GPIOIntDisable(base, m_mask);
        write(config);
    }

    *cr &= ~m_mask;
    *lock = 0;

    if (m_config && m_mode != OUTPUT)
    {
        if (m_config == RISING_EDGE) GPIOIntTypeSet(base, m_mask, GPIO_RISING_EDGE);
        else if (m_config == FALLING_EDGE) GPIOIntTypeSet(base, m_mask, GPIO_FALLING_EDGE);
        else if (m_config == BOTH_EDGES) GPIOIntTypeSet(base, m_mask, GPIO_BOTH_EDGES);

        GPIOIntRegister(base, pPortIntHandlers[m_port]);
        GPIOIntEnable(base, m_mask);
    }

    g_port_pins.at(m_port * 8 + m_pin) = this;

    m_initialized = true;
}

void GPIO::port_priority(uint8_t p)
{
    if (!m_initialized) throw "GPIO: not initialized";

    PortPriority(m_port, p);
}

void GPIO::port_debug(GPIO* pin)
{
    if (!m_initialized) throw "GPIO: not initialized";

    PortDebug(m_port, pin);
}

