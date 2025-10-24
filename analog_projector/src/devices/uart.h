#ifndef UART_H_
#define UART_H_

#include "aos/connection.h"
#include "aos/ring_buffer.h"

class GPIO;

#define UART1_PORTB 0
#define UART1_PORTC 1

class UART : public Connection
{
public:
    static constexpr const char * const ClassName = "uart";
    static void Priority(uint8_t port, uint8_t p);

    /* Initialization */

    UART(size_t rx_buf_size = 512, size_t tx_buf_size = 512, size_t pf_buf_size = 80);
    void soft_reset() override;

    void init(uint32_t port, uint32_t baudrate, uint32_t UART1_port = UART1_PORTC);
    void priority(uint8_t p);

    tInt IntHandler;
    GPIO* int_debug;
    Connection* bridge;

    void clear_stats() override;
    void add_to(Kernel& k, const str& path) override;

    void echo(bool echo) { m_echo = echo; }
    void decodeESC(bool decode_esc) { m_decode_esc = decode_esc; }
    void emulate(bool emulate) { m_emulate = emulate; }

    /* Connection interface */

    bool rx_empty() override { return m_num_delim == 0; }
    bool tx_full() override { return m_tx_buf.full(); }
    bool tx_ready() override { return m_tx_buf.available() > m_printf_buf.size(); }
    size_t rx_packets_available() override { return m_num_delim; }
    size_t tx_bytes_available() override { return m_tx_buf.available(); }

    void read(vec<char>& v, bool null_terminated) override;
    size_t write(const char *buf, size_t len = 0) override;

    bool submit() override { return m_tx_buf.push(m_delimiter); }
    void transmit() override;

private:
    uint8_t m_delimiter;
    bool m_echo, m_decode_esc, m_emulate;
    uint32_t m_port, m_base, m_num_delim, m_num_bad_tx;

    /* Connection interface */

    RingBuffer<uint8_t> m_rx_buf, m_tx_buf;


    /* Interrupt handling */

    void DefaultIntHandler(uint32_t status);
    void fill_tx_fifo();

    void push_rx(const uint8_t& c);
    inline void push_tx(const uint8_t& c) { if (!m_tx_buf.push(c)) m_num_bad_tx++; }

    /* Terminal emulation */

    typedef enum{
        Default,
        GotEsc,
        GotEscO,
        StartedCSI,
    } State;

    State m_state;
    uint32_t m_pos;
    vec<char> m_esc_buf, m_cmd_buf, m_last_cmd;

    uint8_t decode();
    uint8_t filter(uint8_t c);
    void feed_esc_fsm(uint8_t c);
    void feed_cmd_fsm(uint8_t c);
};

#endif /* UART_H_ */
