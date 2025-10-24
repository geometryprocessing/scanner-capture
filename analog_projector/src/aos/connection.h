#ifndef CONNECTION_H
#define CONNECTION_H

#include "device.h"
#include "escape_codes.h"

extern const char * esc_str(const tESC & esc);

class Connection : public Device
{
    friend Kernel;
public:
    Connection *cout, *debug, *pipe;

    static constexpr const char * const ClassName = "connection";
    Connection(size_t pf_buf_size, const char * type = "connection");

    void soft_reset() override;
    virtual void clear_stats();

    virtual bool rx_empty() = 0;
    virtual bool tx_full() = 0;
    virtual bool tx_ready() = 0;
    virtual size_t rx_packets_available() = 0;
    virtual size_t tx_bytes_available() = 0;

    virtual void read(vec<char>& v, bool null_terminated = false) = 0;

    Connection* print(const char* s, size_t n = 0);
    Connection* print(const str& s) { return print(s.c_str(), s.size()); }

    Connection* println(const char* s, size_t n = 0);
    Connection* println(const str& s) { return println(s.c_str(), s.size()); }

    template<typename... Args>
    Connection* printf(const char * fmt, Args... args)
    {
        char* buf = get_printf_buf(); // must provide available() + 1 byte for null string termination
        int limit = buf ? tx_bytes_available() : m_printf_buf.size() - 1;

        int to_print = snprintf(buf ? buf : m_printf_buf.data(), limit + 1, fmt, helper(args)...);

        if (to_print < 0) m_pf_packets_lost++;
        if (to_print > limit) m_pf_bytes_lost += to_print - limit;

        to_print = std::max(0, std::min(to_print, limit));
        if (to_print > 0) print(buf ? nullptr : m_printf_buf.data(), to_print);
        return this;
    }

    inline Connection* autoPost(bool auto_post = true) { m_auto_post = auto_post; return this; }
    inline Connection* retry(uint8_t retry) { m_retry = retry; return this; }

    void post();

    inline void flush()
    {
        post(); // Make sure the last writes were submitted
        transmit();
//        post(); // In case the first post failed because of the buffer being full before transmit():
    }

    void add_to(Kernel& k, const str& path) override;

    uint32_t lost_pf_bytes() const { return m_pf_bytes_lost; }
    uint32_t lost_rx_bytes() const { return m_rx_bytes_lost; }
    uint32_t lost_tx_bytes() const { return m_tx_bytes_lost; }
    uint32_t failed_retries() const { return m_retries_failed; }
    uint32_t lost_pf_packets() const { return m_pf_packets_lost; }
    uint32_t lost_rx_packets() const { return m_rx_packets_lost; }
    uint32_t lost_tx_packets() const { return m_tx_packets_lost; }

protected:
    uint32_t m_pf_bytes_lost, m_pf_packets_lost;
    uint32_t m_rx_bytes_lost, m_rx_packets_lost;
    uint32_t m_tx_bytes_lost, m_tx_packets_lost;
    uint32_t m_retries_failed;
    vec<char> m_printf_buf;

    std::function<str(const str&, vec<str>&, vec<int>&, bool)> m_autocomplete;

    // Provides a buffer with available() + 1 byte for null string termination
    virtual char* get_printf_buf() { return nullptr; }

    // Just increase the size if buf is null
    virtual size_t write(const char* buf, size_t len) = 0;

    virtual bool submit() = 0; // try to finish/push current packet into the tx queue
    virtual void transmit() = 0; // initiate transmission (if tx queue is not empty)

private:
    uint8_t m_retry;
    bool m_posted, m_attempted, m_auto_post;

    template<typename T>
    inline const T& helper(const T& x) { return x; }
    inline const char * helper(const str& s) { return s.c_str(); }
    inline int helper(const size_t& n) { return n; }
};

#endif /* CONNECTION_H */
