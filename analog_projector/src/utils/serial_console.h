#ifndef SERIAL_CONSOLE_H_
#define SERIAL_CONSOLE_H_

#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <deque>
#include <functional>

#include "aos/ring_buffer.h"
#include "aos/escape_codes.h"
#include "utils/pystring.h"

namespace ps = pystring;
using namespace std::placeholders;

struct tCommand {
    std::string name;
    std::function<int(std::vector<std::string>&)> callback;
    bool case_sensitive;
    std::string usage;
    std::string description;

    tCommand(const char* name, std::function<int(std::vector<std::string>&)> callback, bool case_sensitive = false,
                       const char* usage = nullptr, const char* description = nullptr) :
        name(name), callback(callback), case_sensitive(case_sensitive), usage(usage), description(description) {}
};

#define CMD_GOOD 0
#define CMD_TOO_FEW_ARGS 1
#define CMD_TOO_MANY_ARGS 2
#define CMD_INVALID_ARGS 3


struct tApplication {
    std::string name;
    std::function<int(uint8_t)> callback;
    std::function<int(std::vector<std::string>&)> init;
    std::string usage;
    std::string description;

    tApplication(const char* name, std::function<int(uint8_t)> callback, std::function<int(std::vector<std::string>&)> init = nullptr,
                       const char* usage = nullptr, const char* description = nullptr) :
        name(name), callback(callback), init(init), usage(usage), description(description) {}
};

//#define APP_CONTINUE 0
#define APP_EXIT 1


typedef enum{
    eSilent,
    eEcho,
    eCommandLine,
    eApplication
} eMode;

static const char * mode_string(eMode mode)
{
    switch (mode)
    {
        case eSilent: return "Silent";
        case eEcho: return "Echo";
        case eCommandLine: return "Command Line";
        case eApplication: return "Application";
        default: return "Unknown";
    }
}


class Console
{
public:
    static const uint32_t NUM_PORTS = 3;
    static Console* s_console0;
    static Console* s_console1;
    static Console* s_console2;

    Console(eMode mode = eSilent, int rx_buf_size = 64, int tx_buf_size = 256, int max_cmd_size = 32, int max_history_depth = 5);
    virtual ~Console() {}

    void init(uint32_t port, uint32_t baudrate, uint32_t frequency);

    virtual void IntHandler(void);
    void set_raw_cmd_callback(std::function<int(std::string& cmd)> callback) { m_raw_cmd_callback = callback; }
    void set_raw_app_callback(std::function<int(uint8_t c)> callback) { m_raw_app_callback = callback; }
    void register_command(const tCommand& command) { m_commands.push_back(command); }

    std::string get_dir() { return m_dir; }
    void set_dir(const char * dir) { m_dir = dir; }
    void set_prefix(const char * prefix) { m_prefix = prefix; }
    void history_enabled(bool enabled) { m_history_on = enabled; }
    int write(const char* s, uint32_t n = 0);

    template<typename... Args>
    int printf(const char* fmt, Args... args)
    {
        uint32_t available = m_tx_buf.available();
        if (available == 0) return 0;

        std::vector<char> buffer(available + 1);
        snprintf(buffer.data(), available, fmt, args...);
        return write(buffer.data());
    }

protected:
    void bind();
    void set_mode(eMode mode);

    int default_command_callback(std::string& cmd);
    int default_application_callback(uint8_t c);

    int help(std::vector<std::string>& args);
    int cd(std::vector<std::string>& args);
    int history(std::vector<std::string>& args);

private:
    static const uint32_t UARTBase[NUM_PORTS];
    static const uint32_t UARTInt[NUM_PORTS];
    static const uint32_t UARTPeriph[NUM_PORTS];

    std::function<int(std::string& cmd)> m_raw_cmd_callback;
    std::function<int(uint8_t c)> m_raw_app_callback;
//    int (*m_raw_cmd_callback)(std::string& cmd);
//    int (*m_raw_app_callback)(uint8_t c);

    uint32_t m_port, m_base;
    int m_max_cmd_size, m_pos, m_length;
    bool m_insert_mode;

    RingBuffer<uint8_t> m_rx_buf;
    RingBuffer<uint8_t> m_tx_buf;
    std::vector<char> m_cmd_line;
    std::vector<char> m_esc_buf;

    typedef enum{
        eDefault,
        eGotEsc,
        eGotEscO,
        eStartedCSI,
    } eState;

    eState m_state;
    eMode m_mode;

    std::string m_prefix, m_dir;
    bool m_history_on;
    int m_max_history_depth, m_cmd_count, m_hist_pos;
    std::deque<std::string> m_history;

    std::vector<tCommand> m_commands;
    std::vector<tApplication> m_apps;

    void fill_tx_fifo();

    uint8_t filter(uint8_t c);
    void feed_esc_fsm(uint8_t c);
    uint8_t decode();

    void clear_cmd_line();
    void update_cmd_line(const std::string& str);
    void parse_cmd_ret(int ret);
    void feed_term_fsm(uint8_t c);
};

#endif /* SERIAL_CONSOLE_H_ */
