#ifndef RADIO_H
#define RADIO_H

#include "aos/connection.h"
#include "aos/ring_buffer.h"
#include "aos/packet.h"

#define RADIO_STATE_NOTPRESENT 0
#define RADIO_STATE_DEEPSLEEP 1
#define RADIO_STATE_IDLE 2
#define RADIO_STATE_PTX 3
#define RADIO_STATE_PRX 4
#define RADIO_STATE_BUSY 5

#define RADIO_CMD       0x80  // first byte of each master -> slave packet
#define RADIO_READ      0x80  // second byte of each master -> slave special packet
#define RADIO_RESP      0x80  // second byte of each slave -> master special packet

#define RADIO_TIME      0x10
#define RADIO_FLAGS     0x20
#define RADIO_STATS     0x40

#define FLAG_SYNCH      0x80
#define FLAG_RECORD     0x01
#define FLAG_FOLLOW     0x02

class SPI;
class GPIO;
class Timer;

class Radio : public Connection
{
public:
    static constexpr const char * const ClassName = "radio";
    using tCallback = std::function<void(uint32_t, uint32_t)>;

    /* Initialization */

    Radio(SPI& spi, Timer& timer, GPIO& ce, GPIO& csn, GPIO& irq,
          size_t rx_q_size = 8, size_t tx_q_size = 8, uint8_t packet_size = 32);
    void soft_reset();

    void init(uint32_t datarate = 1000000, uint8_t channel = 0);
    void priority(uint8_t p);
    void check(); // Prevent configuration while running

    tCallback Callback;
    GPIO* rx_debug;

    void clear_stats() override;
    void add_to(Kernel& k, const str& path) override;

    /* Operation */

    void start(uint32_t frequency = 0);
    void deepSleep();  // ~0.9 uA power consumption
    void enableRX();   // ~12 mA power consumption
    void disableRX();  // ~22 uA power consumption
    void interrupted(bool yesno) { m_interrupted = yesno; } // by higher priority interrupt
                                                            // (don't sync then)

    /* Getters */

    uint8_t getID() const { return m_id; }
    uint32_t getFlags() const { return m_flags; }
    uint32_t getTimestamp() const { return m_timestamp; }
    bool isMaster() const { return m_master; }
    str str_id() { return str(1, to_char_id(m_id)); }

    bool isAlive();
    uint8_t radioState();  // see RADIO_STATE_* defines
    bool signalDetected(); // Check if RF signal of -64dBm or greater was detected

    int getTXpower();
    uint32_t getSpeed();
    uint8_t getChannel();
    size_t getAddressLength() { return m_addr_width; };
    void getRXaddress(uint8_t* buf);
    void getTXaddress(uint8_t* buf);
    bool getAutoAck();
    uint8_t getCRC();

    /* Setters */

    void setID(uint8_t id, bool master);
    void setFlags(uint32_t flags) { m_flags = flags; }
    void setTimestamp(uint32_t timestamp) { m_timestamp = timestamp; }
    void setBaseChar(char base_char) { m_base_char = base_char & 0x7F; }

    void setChannel(uint8_t channel); // Defaults to 0
    void setTXpower(int dBm = 0);  // Supports 0, -6, -12 and -18 dBm
    void setSpeed(uint32_t speed); // 250000, 1000000 or 2000000 bits/second

    // Enable/disable 8 or 16-bit CRC usage inside nRF24's hardware packet engine
    void setCRC(bool on, bool crc16bit = false); // 8 bit on by default

    // Set AutoACK retry count and timeout params (0-15, 250-4000 respectively)
    void setAutoAckParams(uint8_t retry_count = 15, uint16_t timeout = 2000);
    void autoAck(bool on = true);  // Enable/disable AutoACK (off by default)

    void setAddressLength(uint8_t len);  // Valid parameters = 3, 4 or 5 (default = 4)
    void setRXaddress(const uint8_t* rxaddr);  // Loaded into pipe #1
    void setTXaddress(const uint8_t* txaddr);  // Loaded into TXaddr register

    /* Connection interface */

    bool rx_empty() override { return m_rx_q.empty(); }
    bool tx_full() override { return m_tx_q.full(); }
    bool tx_ready() override { return !m_tx_q.full(); }
    size_t rx_packets_available() override { return m_rx_q.size(); }
    size_t tx_bytes_available() override { return m_tx_q.head().available(); }

    void read(vec<char>& v, bool null_terminated) override;
    
private:
    SPI &m_spi;  // Hardware dependencies
    Timer &m_timer;
    GPIO &m_ce, &m_csn, &m_irq;

    uint8_t m_id;
    bool m_master;
    char m_base_char;

    uint8_t m_addr_width, m_default_cfg;
    uint32_t m_timestamp, m_flags;

    /* Interrupt handling */

    uint8_t m_current_id;
    bool m_tx_data_ready, m_tx_irq_pending;
    bool m_irq_lock, m_running, m_send_flags, m_interrupted;

    void IrqHandler();
    void TimerHandler();

    void upload_tx();
    void trigger_tx();

    char to_char_id(uint8_t id) { return m_base_char + id; }

    /* SPI communication */

    uint8_t rf_status;
    vec<uint8_t> m_cmd_out, m_data_in;

    uint8_t readReg(uint8_t addr);
    void writeReg(uint8_t addr, uint8_t val);

    void readRegMultiLSB(uint8_t addr, uint8_t* buf, size_t len);
    void writeRegMultiLSB(uint8_t addr, const uint8_t* buf, size_t len);

    void issueCmd(uint8_t cmd);
    void issueCmdPayload(uint8_t cmd, uint8_t *buf, size_t len);
    void readCmdPayload(uint8_t addr, uint8_t *buf, size_t len);

    /* Statistics */

    struct SlaveStats {
        uint32_t num_responses, packets_delivered, packets_received;
        SlaveStats() : num_responses(0), packets_delivered(0), packets_received(0) {}
    };

    vec<SlaveStats> m_stats;
    uint32_t m_tx_failed_count, m_zlp_count, m_oversized_count, m_packets_received;

    /* Connection interface */

    const uint8_t c_packet_size;
    RingBuffer<Packet<char> > m_rx_q, m_tx_q;

    char* get_printf_buf() override { return m_tx_q.head().data() + m_tx_q.head().size(); }

    // Just increase the size if buf is null
    size_t write(const char *buf, size_t len) override;
    bool submit() override { return m_tx_q.push_head(); }
    void transmit() override {} // Managed by timer
};

#endif /* RADIO_H */
