#include "serial_console.h"

#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"
#include "inc/hw_uart.h"
#include "driverlib/sysctl.h"
#include "driverlib/interrupt.h"
#include "driverlib/uart.h"

const uint32_t Console::UARTBase[NUM_PORTS] = { UART0_BASE, UART1_BASE, UART2_BASE };
const uint32_t Console::UARTInt[NUM_PORTS] = { INT_UART0, INT_UART1, INT_UART2 };
const uint32_t Console::UARTPeriph[NUM_PORTS] = { SYSCTL_PERIPH_UART0, SYSCTL_PERIPH_UART1, SYSCTL_PERIPH_UART2 };

Console* Console::s_console0 = nullptr;
Console* Console::s_console1 = nullptr;
Console* Console::s_console2 = nullptr;

extern "C"
{
    void Console0IntHandler(void)
    {
        if (Console::s_console0) Console::s_console0->IntHandler();
    }

    void Console1IntHandler(void)
    {
        if (Console::s_console1) Console::s_console1->IntHandler();
    }

    void Console2IntHandler(void)
    {
        if (Console::s_console2) Console::s_console2->IntHandler();
    }
}

//
// Initialization
//

void Console::bind()
{
    switch (m_port) {
        case 0: Console::s_console0 = this; break;
        case 1: Console::s_console1 = this; break;
        case 2: Console::s_console2 = this; break;
    }
}

void Console::init(uint32_t port, uint32_t baudrate, uint32_t frequency)
{
    assert(port < NUM_PORTS);

    if(!SysCtlPeripheralPresent(UARTPeriph[port]))
        return;

    m_port = port;
    m_base = UARTBase[port];
    m_prefix = "term." + std::to_string(port);

    bind();

    SysCtlPeripheralEnable(UARTPeriph[m_port]);
    UARTConfigSetExpClk(m_base, frequency, baudrate, UART_CONFIG_WLEN_8 | UART_CONFIG_PAR_NONE | UART_CONFIG_STOP_ONE);
    UARTFIFOLevelSet(m_base, UART_FIFO_TX1_8, UART_FIFO_RX1_8);

    UARTIntDisable(m_base, 0xFFFFFFFF);
    UARTIntEnable(m_base, UART_INT_RX | UART_INT_RT);
    IntEnable(UARTInt[m_port]);
    UARTEnable(m_base);

    set_mode(m_mode);
}

void Console::set_mode(eMode mode)
{
    m_mode = mode;
    m_state = eDefault;

    if (m_mode != eSilent)
        printf(CYAN_"Mode of %s: %s\r\n"_RESET, m_prefix.c_str(), mode_string(mode));
}

Console::Console(eMode mode, int rx_buf_size, int tx_buf_size, int max_cmd_size, int max_history_depth) :
    m_rx_buf(rx_buf_size), m_tx_buf(tx_buf_size), m_cmd_line(max_cmd_size+1), m_esc_buf(),
    m_max_cmd_size(max_cmd_size), m_max_history_depth(max_history_depth), m_cmd_count(0), m_prefix("term"), m_dir("~"),
    m_pos(0), m_length(0), m_mode(mode), m_state(eDefault), m_hist_pos(0),
    m_commands(), m_apps(),
    m_raw_cmd_callback(nullptr), m_raw_app_callback(nullptr), m_insert_mode(true), m_history_on(true)
{
    set_raw_cmd_callback(std::bind(&Console::default_command_callback, this, _1));
    set_raw_app_callback(std::bind(&Console::default_application_callback, this, _1));

    m_commands.emplace_back("help", std::bind(&Console::help, this, _1), false, "", "Print list of registered commands");
    m_commands.emplace_back("cd", std::bind(&Console::cd, this, _1), false, "", "Change directory");
    m_commands.emplace_back("history", std::bind(&Console::history, this, _1), false, "", "Print list of recently entered commands");
}

//
// Interrupts
//

void Console::IntHandler(void)
{
    uint32_t status;

    status = UARTIntStatus(m_base, true);
    UARTIntClear(m_base, status);

    // fires when TX FIFO is (almost) empty
    if(status & UART_INT_TX)
    {
        fill_tx_fifo();

        if(m_tx_buf.empty()) UARTIntDisable(m_base, UART_INT_TX);
    }

    // UART_INT_RT fires when RX FIFO is not empty and there was no new data for too long
    // so that no data get's stuck in RX FIFO unread
    if(status & (UART_INT_RX | UART_INT_RT))
    {
        // read (and analyze) raw stream
        while(UARTCharsAvail(m_base))
        {
            uint8_t c = (uint8_t)(UARTCharGetNonBlocking(m_base) & 0xFF);

            if (m_mode == eCommandLine || m_mode == eApplication)
            {
                feed_esc_fsm(c);
            }
            else // eSilent | eEcho
            {
                m_rx_buf.push(c);
                if (m_mode == eEcho) m_tx_buf.push(c);
            }
        }

        // process decoded stream
        if (m_mode == eCommandLine || m_mode == eApplication)
        {
            uint8_t c;
            while(m_rx_buf.pop(c))
            {
                feed_term_fsm(c);
            }
        }

        // make sure we transmit what was echoed (if anything)
        if(!m_tx_buf.empty())
        {
            fill_tx_fifo();
            UARTIntEnable(m_base, UART_INT_TX);
        }
    }
}

void Console::fill_tx_fifo()
{
    IntDisable(UARTInt[m_port]);

    uint8_t c;
    while(UARTSpaceAvail(m_base) && !m_tx_buf.empty())
    {
        m_tx_buf.pop(c);
        UARTCharPutNonBlocking(m_base, c);
    }

    IntEnable(UARTInt[m_port]);
}

int Console::write(const char* s, uint32_t n /*= 0*/)
{
    assert(s != 0);

    if (m_tx_buf.full()) return 0;

    int i;
    for(i = 0; (n > 0 ? i < n : true) && s[i] != '\0'; i++)
    {
        if(!m_tx_buf.push(s[i])) break;
    }

    if(!m_tx_buf.empty())
    {
        fill_tx_fifo();
        UARTIntEnable(m_base, UART_INT_TX);
    }

    return i;
}

//
// Escape codes processing
//

uint8_t Console::filter(uint8_t c)
{
    // 8-th bit (ALT_BIT) is used to denote presence of modifiers
    if (c > '\x7f') return eUnknown;

    switch (c)
    {
        case NULL: return ' '; // it's possible to input null character (e.g., Ctrl + @)
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

void Console::feed_esc_fsm(uint8_t c)
{
    // accept 7-bit input only
    c = filter(c);

    // escape codes detection FSM
    switch (m_state)
    {
        case eDefault:
            if (c == ESC) m_state = eGotEsc;
            else m_rx_buf.push(c);
            break;

        case eGotEsc:
            // F1-F4 start with ESC + O in gtkterm instead of ESC + [
            // As a result, you can't input Alt + O[PQRS] because of ambiguity
            if (c == '[' || c == 'O')
            {
                m_state = (c == '[') ? eStartedCSI : eGotEscO;
                m_esc_buf.clear();
            }
            else
            {
                m_state = eDefault;
                if (c == ESC) m_rx_buf.push(c); // 2 x ESC = ESC
                else m_rx_buf.push(ALT_BIT | c); // ESC + char corresponds to Alt + char
            }
            break;

        case eGotEscO:
            switch (c)
            {
                case 'P': m_rx_buf.push(eF1); break;
                case 'Q': m_rx_buf.push(eF2); break;
                case 'R': m_rx_buf.push(eF3); break;
                case 'S': m_rx_buf.push(eF4); break;
                default:
                    m_rx_buf.push(ALT_BIT | 'O');
                    m_rx_buf.push(c);
            }
            m_state = eDefault;
            break;

        case eStartedCSI:
            // the only valid parameter characters
            if ('\x20' <= c && c <= '\x3f') m_esc_buf.push_back(c);
            else {
                m_state = eDefault;

                // the only valid termination characters
                if ('\x40' <= c && c <= '\x7e') m_esc_buf.push_back(c);

                m_rx_buf.push(decode());
            }
    }
}

uint8_t Console::decode()
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
// Terminal processing
//

void Console::clear_cmd_line()
{
    m_cmd_line.clear();
    m_cmd_line.resize(m_max_cmd_size+1);
    m_pos = m_length = 0;
}

void Console::update_cmd_line(const std::string& str)
{
    strcpy(m_cmd_line.data(), str.c_str());
    m_pos = m_length = str.length();

    printf("\r" CLEAR_RIGHT "%s", str.c_str());
}

void Console::parse_cmd_ret(int ret)
{
    switch (ret)
    {
        case CMD_TOO_FEW_ARGS: printf(YELLOW_"Too few arguments\r\n"_RESET); break;
        case CMD_TOO_MANY_ARGS: printf(YELLOW_"Too many arguments\r\n"_RESET); break;
        case CMD_INVALID_ARGS: printf(YELLOW_"Invalid arguments\r\n"_RESET); break;
        case CMD_GOOD: break; // printf(GREEN_"Good\r\n"_RESET); break;
        default: printf(YELLOW_"Unknown return code %d\r\n"_RESET, ret);
    }
}

void Console::feed_term_fsm(uint8_t c)
{
    if (m_mode == eApplication)
    {
        if (m_raw_app_callback)
        {
            int ret = m_raw_app_callback(c);
            if (ret) printf("control_callback replied \"%d\"\r\n", ret);
        }
    }

    if (m_mode == eCommandLine)
    {
        bool has_alt = c & ALT_BIT;
        c &= 0x7F;

        if (c == eF1)
        {
            std::vector<std::string> args;
            help(args);
        }

        if (c == eUp && m_history_on && m_history.size() > 0)
        {
            clear_cmd_line();

            m_hist_pos = std::max(m_hist_pos - 1, 0);
            std::string h = m_history.at(m_hist_pos);

            update_cmd_line(h);
        }

        if (c == eDown && m_history_on && m_hist_pos < m_history.size())
        {
            clear_cmd_line();

            m_hist_pos++;
            std::string h = m_hist_pos != m_history.size() ? m_history.at(m_hist_pos) : "";

            update_cmd_line(h);
        }

        if (c == eEnter || c == eReturn)
        {
            m_cmd_line.at(m_length) = '\0';
            printf(GREEN_ "\r%s" _RESET ":" CYAN_ "%s" _RESET "$ %s\r\n" CLEAR_RIGHT,
                   m_prefix.c_str(), m_dir.c_str(), m_cmd_line.data());

            std::string raw_cmd(ps::strip(m_cmd_line.data()));
            auto args = ps::split(raw_cmd);

            clear_cmd_line();

            if (args.size() > 0)
            {
                if (m_history_on)
                {
                    m_history.push_back(raw_cmd);
                    m_cmd_count++;

                    if (m_history.size() > 1)
                    {
                        if (ps::lower(raw_cmd) == ps::lower(m_history.at(m_history.size() - 2)))
                        {
                            m_history.pop_back();
                            m_cmd_count--;
                        }
                    }

                    if (m_history.size() > m_max_history_depth) m_history.pop_front();
                    m_hist_pos = m_history.size();
                }

                auto name = args.at(0);
                args.erase(args.begin());
                bool matched = false;

                for (auto cmd : m_commands)
                {
                    if (ps::lower(cmd.name) == ps::lower(name) && cmd.callback)
                    {
                        if (cmd.case_sensitive && !(cmd.name == name)) continue;

                        matched = true;
                        int ret = cmd.callback(args);
                        parse_cmd_ret(ret);
                    }
                }

                if (!matched)
                {
                    if (m_raw_cmd_callback)
                    {
                        int ret = m_raw_cmd_callback(raw_cmd);
                        parse_cmd_ret(ret);
                    }
                    else printf(YELLOW_"No callback to process unregistered commands!\r\n"_RESET);
                }
            }
        }

        if (c == eLeft && m_pos > 0)
        {
            m_pos--;
            m_tx_buf.push('\b');

            while(has_alt && m_pos > 0 && m_cmd_line.at(m_pos) != ' ')
            {
                m_pos--;
                m_tx_buf.push('\b');
            }
        }

        if (c == eRight && m_pos < m_length)
        {
            m_pos++;
            write(CUR_RIGHT);

            while(has_alt && m_pos < m_length && m_cmd_line.at(m_pos) != ' ')
            {
                m_pos++;
                write(CUR_RIGHT);
            }
        }

        if (c == eHome)
        {
            m_pos = 0;
            m_tx_buf.push('\r');
        }

        if (c == eEnd)
        {
            m_pos = m_length;

            m_tx_buf.push('\r');
            if (m_length > 0) printf("\x1b[%dC", m_length);
        }

        if (c == eInsert)
        {
            m_insert_mode = !m_insert_mode;
            write(SAVE_CUR GO_HOME CLEAR_RIGHT "\r\n" CLEAR_RIGHT GO_HOME);
            printf(CYAN_"%s"_RESET, m_insert_mode ? "Insert Mode: On " : "Insert Mode: Off");
            write(LOAD_CUR);
        }

        if ('\x1f' < c && c < '\x7f' && m_length < m_max_cmd_size)
        {
            m_hist_pos = m_history.size();

            if (m_insert_mode)
            {
                for(int i = m_length; i > m_pos; i--)
                    m_cmd_line.at(i) = m_cmd_line.at(i-1);

                m_cmd_line.at(m_pos++) = c;
                m_length++;

                m_tx_buf.push(c);

                m_cmd_line.at(m_length) = '\0';
                printf(SAVE_CUR "%s" LOAD_CUR, m_cmd_line.data() + m_pos);
}
            else
            {
                m_cmd_line.at(m_pos++) = c;
                m_length = std::max(m_pos, m_length);

                m_tx_buf.push(c);
            }
        }

        if (c == eBackspace && m_pos > 0)
        {
            m_hist_pos = m_history.size();

            if (m_insert_mode)
            {
                for(int i = m_pos-1; i < m_length; i++)
                    m_cmd_line.at(i) = m_cmd_line.at(i+1);

                m_length--;
                m_pos--;

                m_cmd_line.at(m_length) = '\0';
                printf("\b" SAVE_CUR CLEAR_RIGHT "%s" LOAD_CUR, m_cmd_line.data() + m_pos);
            }
            else
            {
                m_cmd_line.at(--m_pos) = ' ';
                if (m_pos == m_length-1) m_length--;

                write("\b \b");
            }
        }

        if (c == eDelete && m_pos < m_length)
        {
            m_hist_pos = m_history.size();

            if (m_insert_mode)
            {
                for(int i = m_pos; i < m_length; i++)
                    m_cmd_line.at(i) = m_cmd_line.at(i+1);

                m_length--;

                m_cmd_line.at(m_length) = '\0';
                printf(SAVE_CUR CLEAR_RIGHT "%s" LOAD_CUR, m_cmd_line.data() + m_pos);
            }
            else
            {
                m_cmd_line.at(m_pos) = ' ';
                if (m_pos == m_length-1) m_length--;

                write(" \b");
            }
        }
    }
}

//
// Default Commands
//

int Console::default_command_callback(std::string& cmd)
{
    printf(YELLOW_"%s: command not found\r\n"_RESET, cmd.c_str());
    return CMD_GOOD;
}

int Console::default_application_callback(uint8_t c)
{
    printf(YELLOW_"Default application callback\r\n"_RESET);
    return APP_EXIT;
}

int Console::help(std::vector<std::string>& args)
{
    if (args.size() > 0) return CMD_TOO_MANY_ARGS;

    printf("List of %d registered command(s):\r\n", m_commands.size());
    for (auto cmd : m_commands)
        printf("    %s %s - %s\r\n", cmd.name.c_str(), cmd.usage.c_str(), cmd.description.c_str());

    return CMD_GOOD;
}

int Console::cd(std::vector<std::string>& args)
{
    if (args.size() > 1) return CMD_TOO_MANY_ARGS;

    if (args.size() == 0) m_dir = "~";
    else m_dir = args.at(0);

    return CMD_GOOD;
}

int Console::history(std::vector<std::string>& args)
{
    if (args.size() > 0) return CMD_TOO_MANY_ARGS;

    printf("Last %d command(s):\r\n", m_history.size());

    for (int i = 0; i < m_history.size(); i++)
    {
        printf("%3d %s\r\n", m_cmd_count - m_history.size() + i + 1, m_history.at(i).c_str());
    }

    return CMD_GOOD;
}

//
// Default Applications
//

