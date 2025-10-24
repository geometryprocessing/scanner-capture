#include "radio.h"
#include "aos/kernel.h"

#include "spi.h"
#include "gpio.h"
#include "timer.h"

#include "nRF24L01.h"

#define IRQ_RX        0x40
#define IRQ_TX        0x20
#define IRQ_TXFAILED  0x10
#define IRQ_MASK      0x70

#define MASK_CRC(X) (X & (RF24_EN_CRC | RF24_CRCO))

const uint8_t default_addr[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xF0 };

//
// Initialization
//

Radio::Radio(SPI& spi, Timer& timer, GPIO& ce, GPIO& csn, GPIO& irq,
             size_t rx_q_size, size_t tx_q_size, uint8_t packet_size) :
             m_spi(spi), m_timer(timer), m_ce(ce), m_csn(csn), m_irq(irq),
             m_rx_q(rx_q_size, Packet<char>(packet_size)), Connection(0, "radio"),
             m_tx_q(tx_q_size, Packet<char>(packet_size)), c_packet_size(packet_size)
{
    soft_reset();
}

void Radio::soft_reset()
{
    Connection::soft_reset();

    m_id = 0;
    m_master = false;
    m_base_char = '1';
    m_timestamp = m_flags = 0;

    rf_status = m_current_id = 0;
    m_addr_width = m_default_cfg = 0;

    m_tx_data_ready = m_tx_irq_pending = false;
    m_irq_lock = m_running = m_send_flags = m_interrupted = false;
    rx_debug = nullptr;

    m_rx_q.purge();
    m_tx_q.purge();

    m_rx_q.head().purge();
    m_tx_q.head().purge();

    m_cmd_out = vec<uint8_t>(c_packet_size + 2, RF24_NOP);
    m_data_in = vec<uint8_t>(c_packet_size + 2, 0);

    clear_stats();
}

void Radio::clear_stats()
{
    Connection::clear_stats();

    m_zlp_count = 0;
    m_tx_failed_count = 0;
    m_oversized_count = 0;
    m_packets_received = 0;

    for (size_t i = 0; i < m_stats.size(); i++)
    {
        m_stats.at(i).num_responses = 0;
        m_stats.at(i).packets_delivered = 0;
        m_stats.at(i).packets_received = 0;
    }
}

void Radio::add_to(Kernel& k, const str& path)
{
    Connection::add_to(k, path);

    k.add(&m_id, path + s_"id", r__);
    k.add(&m_master, path + s_"master", r__);
    k.add(&m_running, path + s_"running", r__);
    k.add(&rx_debug, path + s_"rx_debug", rw_);
    k.add(&m_timestamp, path + s_"timestamp", r__);
    k.add(&m_flags, path + s_"flags", r__);

    k.add_device(&m_spi, path + s_"spi");
    k.add_device(&m_timer, path + s_"tmr");
//    k.add_device(&m_ce, path + s_"ce");
//    k.add_device(&m_csn, path + s_"csn");
//    k.add_device(&m_irq, path + s_"irq");

    k.add(tAct([&](){ clear_stats(); }), path + s_"clr_stats", __x);

    k.add(&m_zlp_count, path + s_"stats" + s_"zlp", r__);
    k.add(&m_tx_failed_count, path + s_"stats" + s_"txf", r__);
    k.add(&m_oversized_count, path + s_"stats" + s_"osp", r__);
    k.add(&m_packets_received, path + s_"stats" + s_"prx", r__);

    for (size_t i = 0; i < m_stats.size(); i++)
    {
        k.add(&(m_stats.at(i).num_responses), path + s_"s" + to_char_id(i) + s_"resp", r__);
        k.add(&(m_stats.at(i).packets_delivered), path + s_"s" + to_char_id(i) + s_"deliv", r__);
        k.add(&(m_stats.at(i).packets_received), path + s_"s" + to_char_id(i) + s_"recv", r__);
    }
}

void Radio::init(uint32_t datarate, uint8_t channel)
{
    soft_reset();

    if (!m_spi.initialized()) throw "Radio: SPI not initialized";
    if (!m_timer.initialized()) throw "Radio: Timer not initialized";

    m_ce.init(OUTPUT, LOW);
    m_csn.init(OUTPUT, HIGH);
    m_irq.init(INPUT, FALLING_EDGE);

    m_irq.IntAction = [&]() { IrqHandler(); };
    m_timer.IntAction = [&]() { TimerHandler(); };

//    m_period = m_timer.period();
    m_spi.transfer(0);  // Strawman transfer

    if (!isAlive()) throw "Radio: Not Present!";

    // Wait for the module to initialize
    aos::delay(20 * ms);

    // Write to FEATURE register and see if changes stick
    writeReg(RF24_FEATURE, RF24_EN_DPL);

    // If changes do not stick, issue an activate command
    if (readReg(RF24_FEATURE) == 0)
    {
        uint8_t activate_data = 0x73;
        issueCmdPayload(RF24_ACTIVATE, &activate_data, 1);
        writeReg(RF24_FEATURE, RF24_EN_DPL);  // Enable dynamic payloads
    }

    // Init certain registers
    writeReg(RF24_CONFIG, 0x00);      // Deep power-down, everything disabled
    writeReg(RF24_STATUS, IRQ_MASK);  // Clear all IRQs
    writeReg(RF24_EN_RXADDR, 0x03);   // ???
    writeReg(RF24_RF_SETUP, 0x00);    // ???
    writeReg(RF24_DYNPD, 0x03);       // ???
    issueCmd(RF24_FLUSH_TX);
    issueCmd(RF24_FLUSH_RX);

    // Default configuration
    setTXpower(0);
    setChannel(channel);
    setSpeed(datarate);
    setAddressLength(5); // max
    setRXaddress(default_addr);
    setTXaddress(default_addr);
    setAutoAckParams();
    autoAck(false);
    setCRC(true, true);  // 16-bit

    m_initialized = true;
}

void Radio::priority(uint8_t p)
{
    if (!m_initialized) throw "Radio: not initialized";

    m_timer.priority(p);
    m_irq.port_priority(p + 1);
}

void Radio::check()
{
    if (m_running) throw "Radio: already running";
}

//
// Operation
//

void Radio::start(uint32_t frequency)
{
    if (!m_initialized) throw "Radio: not initialized";

    uint8_t reg = readReg(RF24_CONFIG);
    m_default_cfg = MASK_CRC(reg) | RF24_PWR_UP;
    writeReg(RF24_CONFIG, m_default_cfg);

    if (!(reg & RF24_PWR_UP))
    {
        if (debug) debug->printf(GREEN_"Radio: Powering up #%d (%s)"_RESETn, m_id, m_master ? "Master" : "Slave");
        aos::delay(3 * ms);
    }

    m_timer.start(frequency ? aos::clock() / frequency : m_timer.period());

    if (!m_master) enableRX();

    m_running = true;
}

void Radio::enableRX()
{
    if (!m_initialized) throw "Radio: not initialized";
    check();

    uint8_t reg = readReg(RF24_CONFIG);
    writeReg(RF24_CONFIG, MASK_CRC(reg) | RF24_PWR_UP | RF24_PRIM_RX);

    m_ce.set();
    if (rx_debug) rx_debug->set();

    // Powering up from deep-sleep requires 3 ms oscillator start delay
    if (!(reg & RF24_PWR_UP))
    {
        if (debug) debug->print(YELLOW_"Radio: was not powered up!"_RESETn);
        aos::delay(3 * ms);
    }
}

void Radio::disableRX()
{
    if (!m_initialized) throw "Radio: not initialized";
    check();

    m_ce.clear();
    if (rx_debug) rx_debug->clear();

    // Keep us in standby-I if we're coming from RX mode, otherwise stay
    // in deep-sleep if we call this while already in PWR_UP=0 mode
    uint8_t reg = readReg(RF24_CONFIG);
    writeReg(RF24_CONFIG, MASK_CRC(reg) | (reg & RF24_PWR_UP ? RF24_PWR_UP : 0));
}

void Radio::deepSleep()
{
    if (!m_initialized) throw "Radio: not initialized";
    check();

    uint8_t reg = readReg(RF24_CONFIG);
    if (reg & (RF24_PWR_UP | RF24_PRIM_RX)) writeReg(RF24_CONFIG, MASK_CRC(reg));

    m_ce.clear();
}

//
// Interrupt handling
//

void Radio::IrqHandler()
{
    m_interrupted = false;
    m_irq_lock = true;

    uint8_t n = readReg(RF24_R_RX_PL_WID);
    uint8_t irq = rf_status & IRQ_MASK;

    writeReg(RF24_STATUS, irq); // Clear all IRQs

    if (irq & IRQ_RX)
    {
        /* Check if RX payload is 0 bytes or >32 bytes (erroneous conditions)
         * Also check if data was received on pipe #0, which we are ignoring.
         * The reason for this is pipe #0 is needed for receiving AutoACK acknowledgements,
         * its address gets reset to the module's default and we do not care about data
         * coming in to that address...
         */
        if ((rf_status & 0x0E) != 0x0E)  // Only check if there is an actual packet
        {
            if (n == 0 || n > 32 || ((rf_status & 0x0E) >> 1) == 0)
            {
                // Zero-width RX payload is an error that happens a lot
                // with non-AutoAck, and must be cleared with FLUSH_RX.
                // Erroneous >32 bytes packets are a similar phenomenon
                if (n == 0)
                {
                    if (debug) debug->print(YELLOW_"Radio: zlp"_RESETn);
                    m_zlp_count++;
                }
                else if (n > 32)
                {
                    if (debug) debug->print(YELLOW_"Radio: oversized packet"_RESETn);
                    m_oversized_count++;
                }
                else if (debug) debug->print(YELLOW_"Radio: wrong pipe"_RESETn);

                issueCmd(RF24_FLUSH_RX);
            }
            else
            {
                uint8_t* buf = (uint8_t*)m_rx_q.head().data();
                readCmdPayload(RF24_R_RX_PAYLOAD, buf, n);

                bool cmd = buf[0] & RADIO_CMD;
                bool read = (buf[1] & RADIO_READ) && (n == 6);
                bool resp = (buf[1] & RADIO_RESP) && (n == 6);

                if (m_master)
                {
                    m_stats.at(m_current_id).num_responses++;

                    if (resp)
                    {
                        buf[1] &= ~RADIO_RESP; // clear slave resp flag

                        if (debug && buf[1] != m_current_id)
                            debug->print(YELLOW_"Radio: buf[1] != m_current_id"_RESETn);

                        if (buf[0] & RADIO_STATS)
                            m_stats.at(m_current_id).packets_delivered = *((uint32_t*)(buf+2));
                    }
                    else
                    {
                        m_packets_received++;
                        m_stats.at(m_current_id).packets_received++;

                        m_rx_q.head().data()[c_packet_size] = to_char_id(m_current_id);
                        m_rx_q.head().set_size(n);

                        if (!m_rx_q.push_head()) m_rx_packets_lost++;
                    }
                }
                else // slave
                if (cmd) // only process packets from master (not responses of other slaves)
                {
                    buf[0] &= ~RADIO_CMD; // clear master cmd flag

                    if (read)
                    {
                        buf[1] &= ~RADIO_READ; // clear master read flag

                        if ((m_flags & FLAG_SYNCH) && !m_interrupted)
                        {
                            m_timer.write(2 * m_timer.period() / 3);
                            if (buf[0] & RADIO_TIME) m_timestamp = *((uint32_t*)(buf+2));
                        }

                        if (buf[0] & RADIO_FLAGS) m_flags = *((uint32_t*)(buf+2));

                        if (buf[1] == m_id)
                        {
                            upload_tx();

                            if (!m_tx_data_ready)
                            {
                                uint8_t cmd[6];
                                cmd[0] = RADIO_STATS;
                                cmd[1] = m_id | RADIO_RESP;
                                *(uint32_t*)(cmd + 2) = m_packets_received;

                                issueCmdPayload(RF24_W_TX_PAYLOAD, cmd, 6);
                                m_tx_data_ready = true;
                            }
                            trigger_tx();
                        }
                    }
                    else
                    {
                        m_packets_received++;
                        m_rx_q.head().set_size(n);

                        if (!m_rx_q.push_head()) m_rx_packets_lost++;
                    }
                }
            }
        }
        else if (debug) debug->print(YELLOW_"Radio: not a packet"_RESETn);
    }

    if (irq & (IRQ_TX | IRQ_TXFAILED))
    {
        m_tx_data_ready = m_tx_irq_pending = false;

        if (irq & IRQ_TXFAILED)
        {
            m_tx_failed_count++;
            issueCmd(RF24_FLUSH_TX);

            if (debug) debug->print(YELLOW_"Radio: tx failed"_RESETn);
        }

        writeReg(RF24_CONFIG, m_default_cfg | RF24_PRIM_RX);
        m_ce.set(); // Enable RX
        if (rx_debug) rx_debug->set();
    }
    m_irq_lock = false;
}

void Radio::TimerHandler()
{
    m_interrupted = true;

    if (m_master)
    {
        if (!m_irq_lock && m_running)
        {
            upload_tx();

            if (!m_tx_data_ready)
            {
                uint8_t cmd[6];
                cmd[0] = RADIO_CMD;
                m_current_id = (m_current_id + 1) % m_id;
                cmd[1] = m_current_id | RADIO_READ;

                if (m_send_flags)
                {
                    cmd[0] |= RADIO_FLAGS;
                    *(uint32_t*)(cmd + 2) = m_flags;
                    m_send_flags = false;
                }
                else
                {
                    cmd[0] |= RADIO_TIME;
                    *(uint32_t*)(cmd + 2) = m_timestamp + 1; // only next timeout will be in synch
                    m_send_flags = true;
                }

                issueCmdPayload(RF24_W_TX_PAYLOAD, cmd, 6);
                m_tx_data_ready = true;
            }
            trigger_tx();
        }
        if (Callback) Callback(m_flags, m_timestamp);
    }
    else
    {
        if (Callback) Callback(m_flags, m_timestamp);
    }

    m_timestamp++;
}

void Radio::upload_tx()
{
    if (!m_tx_data_ready && !m_tx_q.empty())
    {
        if (m_tx_q.tail().size() == 0)
        {
//            m_tx_queue.tail().purge();
            if (debug) debug->print(YELLOW_"Radio doesn't support ZLP!"_RESETn);
            m_tx_q.pop_tail();
            return;
        }

        uint8_t* buf = (uint8_t*)m_tx_q.tail().data();
        if (m_master) buf[0] |= RADIO_CMD;
        else buf[0] &= ~RADIO_CMD;

        issueCmdPayload(RF24_W_TX_PAYLOAD, buf, m_tx_q.tail().size());
        m_tx_q.tail().purge();
        m_tx_q.pop_tail();
        m_tx_data_ready = true;
    }
}

void Radio::trigger_tx()
{
    if (m_tx_data_ready && !m_tx_irq_pending)
    {
        m_ce.clear(); // Disable RX
        if (rx_debug) rx_debug->clear();
        writeReg(RF24_CONFIG, m_default_cfg);

        m_ce.set();
        m_tx_irq_pending = true;
        aos::delay(11 * us);
        m_ce.clear();
    }
}

//
// SPI communication
//

uint8_t Radio::readReg(uint8_t addr)
{
    m_csn.clear();

    m_cmd_out[0] = RF24_R_REGISTER | addr;
    m_spi.multiTransfer(m_cmd_out.data(), m_data_in.data(), 2, 0);
    rf_status = m_data_in[0];

    m_csn.set();
    return m_data_in[1];
}

void Radio::writeReg(uint8_t addr, uint8_t val)
{
    m_csn.clear();

    m_cmd_out[0] = RF24_W_REGISTER | addr;
    m_cmd_out[1] = val;
    m_spi.multiTransfer(m_cmd_out.data(), nullptr, 2);
    m_cmd_out[1] = RF24_NOP;

    m_csn.set();
}

void Radio::readRegMultiLSB(uint8_t addr, uint8_t *buf, size_t len)
{
    m_csn.clear();
    m_spi.transfer(RF24_R_REGISTER | addr);
    for (uint8_t i = 0; i < len; i++) buf[len-i-1] = m_spi.transfer(RF24_NOP);
    m_csn.set();
}

void Radio::writeRegMultiLSB(uint8_t addr, const uint8_t* buf, size_t len)
{
    m_csn.clear();
    m_spi.transfer(RF24_W_REGISTER | addr);
    for (size_t i = 0; i < len; i++) m_spi.transfer(buf[len-i-1]);
    m_csn.set();
}

void Radio::issueCmd(uint8_t cmd)
{
    m_csn.clear();
    m_cmd_out[0] = cmd;
    m_spi.multiTransfer(m_cmd_out.data(), m_data_in.data(), 1);
    rf_status = m_data_in[0];
    m_csn.set();
}

void Radio::issueCmdPayload(uint8_t cmd, uint8_t* buf, size_t len)
{
    m_csn.clear();
    m_spi.transfer(cmd);
    m_spi.multiTransfer(buf, nullptr, len);
    m_csn.set();
}

void Radio::readCmdPayload(uint8_t cmd, uint8_t* buf, size_t len)
{
    m_csn.clear();
    m_cmd_out[0] = cmd;
    m_spi.multiTransfer(m_cmd_out.data(), buf, len + 1, 1);
    m_csn.set();
}

//
// Getters
//

bool Radio::isAlive()
{
    check();
    uint8_t aw = readReg(RF24_SETUP_AW);
    return ((aw & 0xFC) == 0x00 && (aw & 0x03) != 0x00);
}

bool Radio::signalDetected() { check(); return readReg(RF24_RPD); }

uint8_t Radio::radioState()
{
    check();
    if (!isAlive()) return RADIO_STATE_NOTPRESENT;

    uint8_t reg = readReg(RF24_CONFIG);
    if (!(reg & RF24_PWR_UP)) return RADIO_STATE_DEEPSLEEP;

    if (reg & RF24_PRIM_RX) return m_ce.read() ? RADIO_STATE_PRX : RADIO_STATE_IDLE;

    return readReg(RF24_FIFO_STATUS) & RF24_TX_EMPTY ? RADIO_STATE_IDLE : RADIO_STATE_PTX;
}

bool Radio::getAutoAck() { check(); return readReg(RF24_EN_AA); }
uint8_t Radio::getChannel() { check(); return readReg(RF24_RF_CH); }

uint32_t Radio::getSpeed()
{
    check();
    switch (readReg(RF24_RF_SETUP) & 0x28)
    {
        case 0x00: return 1000000;
        case 0x08: return 2000000;
        case 0x20: return 250000;
        default: return 0UL;
    }
}

int Radio::getTXpower()
{
    check();
    switch (readReg(RF24_RF_SETUP) & 0x07)
    {
        case 0x02: return -12;
        case 0x04: return -6;
        case 0x06: return 0;
        default: return -18;
    }
}

uint8_t Radio::getCRC()
{
    check();
    switch (readReg(RF24_CONFIG) & 0x0C)
    {
        case 0x08: return 8;
        case 0x0C: return 16;
        default: return 0;
    }
}

void Radio::getRXaddress(uint8_t* buf) { check(); readRegMultiLSB(RF24_RX_ADDR_P1, buf, m_addr_width); }
void Radio::getTXaddress(uint8_t* buf) { check(); readRegMultiLSB(RF24_TX_ADDR, buf, m_addr_width); }

//
// Setters
//

void Radio::setID(uint8_t id, bool master)
{
    check();
    m_id = id & 0x7F;
    m_master = master;

    if (master) m_stats.resize(m_id);
}

void Radio::autoAck(bool on) { check(); writeReg(RF24_EN_AA, on ? 0x3 : 0); }
void Radio::setChannel(uint8_t channel) { check(); writeReg(RF24_RF_CH, std::min(uint8_t(125), channel)); }

void Radio::setAutoAckParams(uint8_t retry_count, uint16_t timeout)
{
    check();
    writeReg(RF24_SETUP_RETR, (retry_count & 0x0F) | ((((timeout - 250) / 250) & 0x0F) << 4));
}

void Radio::setSpeed(uint32_t speed)
{
    check();
    uint8_t spd = 0x01, reg = readReg(RF24_RF_SETUP) & 0xD7;  // Preserve RF power settings

    if (speed < 2000000) spd = 0x00;
    if (speed < 1000000) spd = 0x04;

    writeReg(RF24_RF_SETUP, reg | (spd << 3));
}

void Radio::setTXpower(int dBm)
{
    check();
    uint8_t pwr = 0x06, reg = readReg(RF24_RF_SETUP) & 0xF8;  // Preserve RF speed settings

    if (dBm >= 7) pwr = 0x07;
    if (dBm < 0) pwr = 0x04;
    if (dBm < -6) pwr = 0x02;
    if (dBm < -12) pwr = 0x00;

    writeReg(RF24_RF_SETUP, reg | pwr);
}

void Radio::setCRC(bool on, bool crc16bit)
{
    check();
    uint8_t reg = readReg(RF24_CONFIG) & 0xF3;  // preserve IRQ mask, PWR_UP and PRIM_RX settings
    writeReg(RF24_CONFIG, reg | (on ? RF24_EN_CRC : 0) | (crc16bit ? RF24_CRCO : 0));
}

void Radio::setAddressLength(uint8_t len)
{
    check();
    m_addr_width = std::max(uint8_t(3), std::min(len, uint8_t(5)));
    writeReg(RF24_SETUP_AW, m_addr_width - 2);
}

void Radio::setRXaddress(const uint8_t* buf) { check(); writeRegMultiLSB(RF24_RX_ADDR_P1, buf, m_addr_width); }
void Radio::setTXaddress(const uint8_t* buf) { check(); writeRegMultiLSB(RF24_TX_ADDR, buf, m_addr_width); }

//
// Connection interface implementation
//

void Radio::read(vec<char>& v, bool null_terminated)
{
    v.clear();
    if (!m_rx_q.empty())
    {
        char* data = m_rx_q.tail().data();
        bool add_prefix = m_master && data[0] != '>';

        if (add_prefix)
        {
            v.push_back(data[c_packet_size]);
            v.push_back(':');
            v.push_back(' ');
        }
        v.insert(v.begin() + (add_prefix ? 3 : 0), data, data + m_rx_q.tail().size());
        m_rx_q.tail().purge();
        m_rx_q.pop_tail();
    }
    if (null_terminated) v.push_back('\0');
}

size_t Radio::write(const char *buf, size_t len)
{
    if (buf) return m_tx_q.head().write(buf, len);
    else {
        size_t to_write = std::min(len, tx_bytes_available());
        m_tx_q.head().set_size(m_tx_q.head().size() + to_write);
        return to_write;
    }
}
