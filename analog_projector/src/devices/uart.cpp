#include "uart.h"
#include "aos/kernel.h"
#include "gpio.h"

#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"
#include "inc/hw_uart.h"
#include "driverlib/sysctl.h"
#include "driverlib/uart.h"
#include "driverlib/gpio.h"
#include "driverlib/pin_map.h"
#include "driverlib/interrupt.h"

#ifdef PART_TM4C123GH6PM
    #define GPIO_LOCK_KEY 0x4C4F434B
#endif

#define NUM_UARTS  4

const uint32_t UARTBase[] = { UART0_BASE, UART1_BASE, UART2_BASE, UART3_BASE };

const uint32_t UARTInt[] = { INT_UART0, INT_UART1, INT_UART2, INT_UART3 };

const uint32_t UARTPeriph[] = { SYSCTL_PERIPH_UART0, SYSCTL_PERIPH_UART1,
                                SYSCTL_PERIPH_UART2, SYSCTL_PERIPH_UART3 };

static vec<UART*> g_uarts(4, nullptr);

//
// Interrupts
//

static void UARTIntHandler(uint8_t port);

extern "C"  // Interrupts must have C bindings
{
    void UART0IntHandler(void) { UARTIntHandler(0); }
    void UART1IntHandler(void) { UARTIntHandler(1); }
    void UART2IntHandler(void) { UARTIntHandler(2); }
    void UART3IntHandler(void) { UARTIntHandler(3); }
}

static void (*pUARTIntHandlers[])(void)
{
    UART0IntHandler, UART1IntHandler, UART2IntHandler, UART3IntHandler
};

void UARTIntHandler(uint8_t port)
{
    // Make sure to clear interrupt before running any user code
    uint32_t base = UARTBase[port & 0x3];
    uint32_t status = UARTIntStatus(base, true);
    UARTIntClear(base, status);

    aos::safe([&]()
    {
        UART* uart = g_uarts.at(port); // Extra check for valid port
        if (uart)
        {
            if (uart->int_debug) uart->int_debug->set();
            if (uart->IntHandler) uart->IntHandler(status);
            if (uart->int_debug) uart->int_debug->clear();
        }
    });
}

void UART::Priority(uint8_t port, uint8_t p)
{
    if (port >= NUM_UARTS) throw "UART: not supported";

    aos::priority(UARTInt[port & 0x3], p);
}

//
// Initialization
//

UART::UART(size_t rx_buf_size, size_t tx_buf_size, size_t pf_buf_size) : Connection(pf_buf_size, "uart"),
           m_rx_buf(rx_buf_size), m_tx_buf(tx_buf_size), m_port(0), m_base(0), m_delimiter('\r')
{
    soft_reset();
}

void UART::soft_reset()
{
    Connection::soft_reset();

    m_echo = m_decode_esc = m_emulate = false;
    int_debug = nullptr;
    bridge = nullptr;

    m_num_delim = 0;
    m_rx_buf.purge();
    m_tx_buf.purge();

    m_pos = 0;
    m_state = Default;
    m_esc_buf.clear();
    m_cmd_buf.clear();
    m_last_cmd.clear();

    m_num_bad_tx = 0;
    clear_stats();
}

void UART::clear_stats()
{
    Connection::clear_stats();
    m_num_bad_tx = 0;
}

void UART::add_to(Kernel& k, const str& path)
{
    Connection::add_to(k, path);

    k.add(&m_port, path + s_"port", r__);
    k.add(&int_debug, path + s_"int_debug", rw_);
    k.add(&bridge, path + s_"bridge", rw_);

    k.add(&m_echo, path + s_"echo", rw_);
    k.add(&m_decode_esc, path + s_"decode_esc", rw_);
    k.add(&m_emulate, path + s_"emulate", rw_);

    k.add(&m_num_bad_tx, path + s_"stats" + s_"bptx", r__);
    k.add(tAct([&](){ clear_stats(); }), path + s_"clr_stats", __x);
}

void UART::init(uint32_t port, uint32_t baudrate, uint32_t UART1_port)
{
    soft_reset();

    if (port >= NUM_UARTS) throw "UART: not supported";

    m_port = port & 0x3;
    m_base = UARTBase[m_port];

    if(!SysCtlPeripheralPresent(UARTPeriph[m_port])) throw "UART: Peripheral not present!";

    SysCtlPeripheralEnable(UARTPeriph[m_port]);
    while(!SysCtlPeripheralReady(UARTPeriph[m_port]));
    IntDisable(UARTInt[m_port]);
    UARTDisable(m_base);

    if (m_port == 0) // RX : Green; TX : White
    {
        SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
        while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOA));

        GPIOPinConfigure(GPIO_PA0_U0RX);
        GPIOPinConfigure(GPIO_PA1_U0TX);
        GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    }
    else if (m_port == 1)
    {
        if (UART1_port == UART1_PORTB)
        {
            SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
            while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB));

            GPIOPinConfigure(GPIO_PB0_U1RX);
            GPIOPinConfigure(GPIO_PB1_U1TX);
            GPIOPinTypeUART(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1);
        }
        else {
            SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
            while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOC));

            GPIOPinConfigure(GPIO_PC4_U1RX);
            GPIOPinConfigure(GPIO_PC5_U1TX);
            GPIOPinTypeUART(GPIO_PORTC_BASE, GPIO_PIN_4 | GPIO_PIN_5);
        }
    }
    else if (m_port == 2)
    {
        SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
        while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOD));
        // Unlock pin PD_7
        volatile uint32_t *lock = (volatile uint32_t *)(GPIO_PORTD_BASE + 0x520);
        volatile uint32_t *cr = (volatile uint32_t *)(GPIO_PORTD_BASE + 0x524);
        *lock = GPIO_LOCK_KEY;
        *cr |= GPIO_PIN_7;
        GPIOPinConfigure(GPIO_PD6_U2RX);
        GPIOPinConfigure(GPIO_PD7_U2TX);
        GPIOPinTypeUART(GPIO_PORTD_BASE, GPIO_PIN_6 | GPIO_PIN_7);
        *cr &= ~GPIO_PIN_7;
        *lock = 0;
    }
    else if (m_port == 3)
    {
        SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
        while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOC));

        GPIOPinConfigure(GPIO_PC6_U3RX);
        GPIOPinConfigure(GPIO_PC7_U3TX);
        GPIOPinTypeUART(GPIO_PORTC_BASE, GPIO_PIN_6 | GPIO_PIN_7);
    }

    UARTConfigSetExpClk(m_base, SysCtlClockGet(), baudrate, UART_CONFIG_WLEN_8 | UART_CONFIG_PAR_NONE | UART_CONFIG_STOP_ONE);
    UARTFIFOLevelSet(m_base, UART_FIFO_TX1_8, UART_FIFO_RX1_8);

    UARTIntDisable(m_base, 0xFFFFFFFF);
    UARTIntEnable(m_base, UART_INT_RX | UART_INT_RT);
    UARTIntRegister(m_base, pUARTIntHandlers[m_port]);
    IntHandler = std::bind(&UART::DefaultIntHandler, this, _1);

    g_uarts.at(m_port) = this;

    IntEnable(UARTInt[m_port]);
    UARTEnable(m_base); // Enable by default

    m_initialized = true;
}

void UART::priority(uint8_t p)
{
    if (!m_initialized) throw "UART: not initialized";

    Priority(m_port, p);
}

//
// Interrupt handling
//

void UART::DefaultIntHandler(uint32_t status)
{
    // Fires when TX FIFO is (almost) empty
    if(status & UART_INT_TX)
    {
        fill_tx_fifo();

        if(m_tx_buf.empty()) UARTIntDisable(m_base, UART_INT_TX);
    }

    // UART_INT_RX fires when RX FIFO is not empty and there was no new data for too long (UART_INT_RT)
    // This way no data get's stuck in RX FIFO unread
    if(status & (UART_INT_RX | UART_INT_RT))
    {
        UART* br = dynamic_cast<UART*>(bridge);

        // Read (and analyze) raw stream
        while(UARTCharsAvail(m_base))
        {
            uint8_t c = (uint8_t)(UARTCharGetNonBlocking(m_base) & 0xFF);

            if (br)
            {
                br->bridge = this;

                if (c == '\0')
                {
                    br->bridge = nullptr;
                    bridge = nullptr;
                    push_tx('\r');
                    push_tx('\n');
                }
                else br->push_tx(c);
            }
            else
            {
                if (m_decode_esc || m_emulate) feed_esc_fsm(c);
                else push_rx(c);
            }
        }

        // Make sure we transmit what was echoed in push_rx (if anything)
        if (m_echo || m_emulate) transmit();
        if (br) br->transmit();
    }
}

void UART::fill_tx_fifo()
{
    IntDisable(UARTInt[m_port]);

    while(UARTSpaceAvail(m_base) && !m_tx_buf.empty())
    {
        UARTCharPutNonBlocking(m_base, m_tx_buf.tail());
        m_tx_buf.pop_tail();
    }

    IntEnable(UARTInt[m_port]);
}

void UART::transmit()
{
    if(!m_tx_buf.empty())
    {
        fill_tx_fifo();
        UARTIntEnable(m_base, UART_INT_TX);
    }
}

void UART::push_rx(const uint8_t& c)
{
    if (m_emulate)
    {
        feed_cmd_fsm(c);
        return;
    }

    if (!m_rx_buf.push(c))
    {
        if (c == m_delimiter) m_rx_packets_lost++;
        else m_rx_bytes_lost++;
    }
    else {
        if (c == m_delimiter) m_num_delim++;
        if (m_echo) push_tx(c);
    }
}

//
// Escape codes processing
//

uint8_t UART::filter(uint8_t c)
{
    // 8-th bit (ALT_BIT) is used to denote presence of modifiers
    if (c > '\x7f') return eUnknown;

    switch (c)
    {
        case 0: return uint8_t(' '); // It is possible to input null character (e.g., Ctrl + @)
        case CTRL_C:
        case BACKSPACE:
        case TAB:
        case LF:
        case CR:
        case ESC:
            return c;
        case DEL:
            return eDelete;
        default:
            return c > '\x1f' ? c : eUnknown;
    }
}

void UART::feed_esc_fsm(uint8_t c)
{
    // accept 7-bit input only
    c = filter(c);

    // escape codes detection FSM
    switch (m_state)
    {
        case Default:
            if (c == ESC) m_state = GotEsc;
            else push_rx(c);
            break;

        case GotEsc:
            // F1-F4 start with ESC + O in gtkterm instead of ESC + [
            // As a result, you can't input Alt + O[PQRS] because of ambiguity
            if (c == '[' || c == 'O')
            {
                m_state = (c == '[') ? StartedCSI : GotEscO;
                m_esc_buf.clear();
            }
            else
            {
                m_state = Default;
                if (c == ESC) push_rx(c); // 2 x ESC = ESC
                else push_rx(ALT_BIT | c); // ESC + char corresponds to Alt + char
            }
            break;

        case GotEscO:
            switch (c)
            {
                case 'P': push_rx(eF1); break;
                case 'Q': push_rx(eF2); break;
                case 'R': push_rx(eF3); break;
                case 'S': push_rx(eF4); break;
                default:
                    push_rx(ALT_BIT | 'O');
                    push_rx(c);
            }
            m_state = Default;
            break;

        case StartedCSI:
            // the only valid parameter characters
            if ('\x20' <= c && c <= '\x3f') m_esc_buf.push_back(c);
            else {
                m_state = Default;

                // the only valid termination characters
                if ('\x40' <= c && c <= '\x7e') m_esc_buf.push_back(c);

                push_rx(decode());
            }
    }
}

uint8_t UART::decode()
{
    if (m_esc_buf.size() == 0) return eUnknown;

    char last = m_esc_buf.back();
    m_esc_buf.push_back('\0');

    char * pos = strchr(m_esc_buf.data(), ';');
    uint8_t modifiers = pos ? ALT_BIT : 0;

    if (last == '~')
    {
        if (pos) *pos = '\0';
        m_esc_buf.at(m_esc_buf.size() - 2) = '\0';

        int code = atoi(m_esc_buf.data());

        switch (code)
        {
            case 1: return eHome | modifiers;
            case 2: return eInsert | modifiers;
            case 3: return eDelete | modifiers;
            case 4: return eEnd | modifiers;
            case 5: return ePageUp | modifiers;
            case 6: return ePageDown | modifiers;
            case 7: return eHome | modifiers;
            case 8: return eEnd | modifiers;

            case 11: return eF1 | modifiers;
            case 12: return eF2 | modifiers;
            case 13: return eF3 | modifiers;
            case 14: return eF4 | modifiers;
            case 15: return eF5 | modifiers;
            case 17: return eF6 | modifiers;
            case 18: return eF7 | modifiers;
            case 19: return eF8 | modifiers;
            case 20: return eF9 | modifiers;
            case 21: return eF10 | modifiers;
            case 23: return eF11 | modifiers;
            case 24: return eF12 | modifiers;

            default: return eUnknown | modifiers;
        }
    }
    else
    {
        switch (last)
        {
            case 'A': return eUp | modifiers;
            case 'B': return eDown | modifiers;
            case 'C': return eRight | modifiers;
            case 'D': return eLeft | modifiers;

            case 'F': return eEnd | modifiers;
            case 'E': return eNum_5 | modifiers;
            case 'G': return eNum_5 | modifiers;
            case 'H': return eHome | modifiers;

            case 'P': return eF1 | modifiers;
            case 'Q': return eF2 | modifiers;
            case 'R': return eF3 | modifiers;
            case 'S': return eF4 | modifiers;

            default: return eUnknown | modifiers;
        }
    }
}

//
// Command line processing
//

void UART::feed_cmd_fsm(uint8_t c)
{
    bool has_alt = c & ALT_BIT;
    c &= 0x7F;

    if ((c == eUp || (c == eReturn && has_alt)) && m_cmd_buf.size() == 0)
    {
        m_cmd_buf = m_last_cmd;
        m_pos = m_cmd_buf.size();
        for (auto c : m_cmd_buf) push_tx(c);
    }

    if (c == eDown)
    {
        m_cmd_buf.clear();
        m_pos = 0;
        push_tx('\r');
        write(CLEAR_RIGHT);
    }

    if (c == eLeft && m_pos > 0)
    {
        m_pos--;
        push_tx('\b');

        while(has_alt && m_pos > 0 && m_cmd_buf.at(m_pos) != ' ')
        {
            m_pos--;
            push_tx('\b');
        }
    }

    if (c == eRight && m_pos < m_cmd_buf.size())
    {
        m_pos++;
        write(CUR_RIGHT);

        while(has_alt && m_pos < m_cmd_buf.size() && m_cmd_buf.at(m_pos) != ' ')
        {
            m_pos++;
            write(CUR_RIGHT);
        }
    }

    if (c == eHome)
    {
        m_pos = 0;
        push_tx('\r');
    }

    if (c == eEnd)
    {
        m_pos = m_cmd_buf.size();
        push_tx('\r');

        if (m_pos > 0)
        {
            write("\x1b[");
            write(std::to_string(m_pos).c_str());
            push_tx('C');
        }
    }

    if (c == eBackspace && m_pos > 0)
    {
        m_cmd_buf.erase(m_cmd_buf.begin() + --m_pos);

        if (m_pos != m_cmd_buf.size())
        {
            write("\b" SAVE_CUR CLEAR_RIGHT);
            write(m_cmd_buf.data() + m_pos, m_cmd_buf.size() - m_pos);
            write(LOAD_CUR);
        }
        else write("\b \b");
    }

    if (c == eDelete && m_pos < m_cmd_buf.size())
    {
        m_cmd_buf.erase(m_cmd_buf.begin() + m_pos);

        if (m_pos != m_cmd_buf.size())
        {
            write(SAVE_CUR CLEAR_RIGHT);
            write(m_cmd_buf.data() + m_pos, m_cmd_buf.size() - m_pos);
            write(LOAD_CUR);
        }
        else write(" \b");
    }

    if ('\x1f' < c && c < '\x7f')
    {
        m_cmd_buf.insert(m_cmd_buf.begin() + m_pos++, c);
        push_tx(c);

        if (m_pos != m_cmd_buf.size())
        {
            write(SAVE_CUR CLEAR_RIGHT);
            write(m_cmd_buf.data() + m_pos, m_cmd_buf.size() - m_pos);
            write(LOAD_CUR);
        }
    }

    if (c == eTab && m_autocomplete && m_pos == m_cmd_buf.size())
    {
        int p = m_cmd_buf.size() - 1;

        if (p == -1) p = 0;
        else {
            while (p > 0 && m_cmd_buf.at(p) != ' ') p--;
            if (m_cmd_buf.at(p) == ' ') p++;
        }

        str s(m_cmd_buf.begin() + p, m_cmd_buf.end());

        vec<str> options;
        vec<int> counts;
        str completed = m_autocomplete(s, options, counts, true);

        if (completed == "" && options.size() > 1)
        {
            write("\r\n");

            for(str opt : options)
            {
                for(size_t i = 0; i < m_pos; i++) push_tx(' ');
                for(size_t i = s.size(); i < opt.size(); i++) push_tx(opt[i]);
                write("\r\n");
            }
            for (auto c : m_cmd_buf) push_tx(c);
        }

        for (auto c : completed)
        {
            m_cmd_buf.push_back(c);
            push_tx(c);
        }
        m_pos = m_cmd_buf.size();
    }

    if (c == eReturn)
    {
        m_emulate = false;
        for (auto c : m_cmd_buf) push_rx(c);
        push_rx(m_delimiter);
        m_emulate = true;

        write("\r" CLEAR_RIGHT CYAN_);
        for (auto c : m_cmd_buf) push_tx(c);
        write("\r"_RESETn);

        if (m_cmd_buf.size()) m_last_cmd = m_cmd_buf;
        m_cmd_buf.clear();
        m_pos = 0;
    }
}

//
// Connection interface implementation
//

void UART::read(vec<char>& v, bool null_terminated)
{
    v.clear();
    if (m_num_delim > 0)
    {
        uint8_t c;

        bool new_line = (m_rx_buf.tail() == uint8_t('\n')) && !m_rx_buf.empty();
        if (new_line) m_rx_buf.pop(c);

        while(m_rx_buf.pop(c) && c != m_delimiter) v.push_back(reinterpret_cast<const char&>(c));

        if (new_line) v.push_back('\n'); // forward \n in \n...\r sequence
        m_num_delim--;
    }
    if (null_terminated) v.push_back('\0');
}

size_t UART::write(const char *buf, size_t len)
{
    if (!buf) return 0;

    if (len == 0) while(buf[len]) len++;

    size_t n = 0;
    while (n < len)
    {
        if (m_tx_buf.push(reinterpret_cast<const uint8_t&>(buf[n]))) n++;
        else break;
    }
    return n;
}
