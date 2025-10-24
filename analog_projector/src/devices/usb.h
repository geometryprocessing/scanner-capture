#ifndef USB_H_
#define USB_H_

#include "aos/connection.h"
#include "aos/ring_buffer.h"
#include "aos/packet.h"

class GPIO;

class USB : public Connection
{
public:
    static constexpr const char * const ClassName = "usb";

    USB(size_t rx_q_size = 4, size_t tx_q_size = 4, uint8_t packet_size = 64);
    void soft_reset() override;

    /* Initialization */

    void init();
    void priority(uint8_t p);

    GPIO *rx_debug, *tx_debug;

    void clear_stats() override;
    void add_to(Kernel& k, const str& path) override;
    void continuous(bool continuous) { m_continuous = continuous; }

    void setFlags(uint32_t flags) { m_flags = flags; }
    void setTimestamp(uint32_t timestamp) { m_timestamp = timestamp; }
    void setPosition(uint32_t position) { m_position = position; }
    uint32_t getFlags() const { return m_flags; }
    uint32_t getTimestamp() const { return m_timestamp; }
    uint32_t getPosition() const { return m_position; }

    /* Interrupt handling */

    uint32_t RxHandler(void *pvCBData, uint32_t ui32Event, uint32_t ui32MsgValue, void *pvMsgData);
    uint32_t TxHandler(void *pvCBData, uint32_t ui32Event, uint32_t ui32MsgValue, void *pvMsgData);

    /* Connection interface */

    bool rx_empty() override { return m_rx_q.empty(); }
    bool tx_full() override { return m_tx_q.full(); }
    bool tx_ready() override { return !m_tx_q.full(); }
    size_t rx_packets_available() override { return m_rx_q.size(); }
    size_t tx_bytes_available() override { return m_tx_q.head().available(); }

    void read(vec<char>& v, bool null_terminated) override;

private:
    uint32_t m_zlp_count;
    const uint8_t c_packet_size;
    bool m_continuous, m_tx_pending;
    vec<uint8_t> m_ft_buf;

    uint32_t m_flags, m_timestamp, m_position;

    /* Connection interface */

    RingBuffer<Packet<char> > m_rx_q, m_tx_q;

    char* get_printf_buf() override { return m_tx_q.head().data() + m_tx_q.head().size(); }

    // Just increase the size if buf is null
    size_t write(const char *buf, size_t len) override;
    bool submit() override { return m_tx_q.push_head(); }
    void transmit() override;
};

#endif /* USB_H_ */
