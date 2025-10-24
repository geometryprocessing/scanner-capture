#include "connection.h"
#include "kernel.h"

Connection::Connection(size_t pf_buf_size, const char * type) :
    Device(type), m_printf_buf(pf_buf_size + 1)
{
    soft_reset();
}

void Connection::soft_reset()
{
    Device::soft_reset();

    cout = this;
    debug = pipe = nullptr;

    m_posted = m_auto_post = true;
    m_attempted = false;
    m_retry = 25; // 250 us max

    clear_stats();
}

void Connection::clear_stats()
{
    m_pf_bytes_lost = m_pf_packets_lost = 0;
    m_rx_bytes_lost = m_rx_packets_lost = 0;
    m_tx_bytes_lost = m_tx_packets_lost = 0;
    m_retries_failed = 0;
}

void Connection::add_to(Kernel& k, const str& path)
{
    k.add(&cout, path + s_"cout", rw_);
    k.add(&debug, path + s_"debug", rw_);
    k.add(&pipe, path + s_"pipe", rw_);

    k.add(&m_retry, path + s_"retry", rw_);
    k.add(&m_auto_post, path + s_"auto_post", rw_);
    k.add(&m_retries_failed, path + s_"stats" + s_"rtf", r__);
    k.add(&m_pf_bytes_lost, path + s_"stats" + s_"pfb", r__);
    k.add(&m_rx_bytes_lost, path + s_"stats" + s_"rxb", r__);
    k.add(&m_tx_bytes_lost, path + s_"stats" + s_"txb", r__);
    k.add(&m_pf_packets_lost, path + s_"stats" + s_"pfp", r__);
    k.add(&m_rx_packets_lost, path + s_"stats" + s_"rxp", r__);
    k.add(&m_tx_packets_lost, path + s_"stats" + s_"txp", r__);
}

Connection* Connection::print(const char* s, size_t n)
{
    if (s && !n) while(s[n]) n++;
    size_t wrote = write(s, n);
    if (wrote > 0)
    {
        m_tx_bytes_lost += n - wrote;
        if (m_attempted) m_tx_packets_lost++;
        m_posted = m_attempted = false;
        if (m_auto_post) post();
    }
    return this;
}

Connection* Connection::println(const char* s, size_t n)
{
    bool stash = m_auto_post;
    m_auto_post = false;
    print(s, n);
    m_auto_post = stash;
    print("\n", 1);
    return this;
}

void Connection::post()
{
    if (!m_posted)
    {
        m_posted = submit();

        uint8_t retries_left = m_retry;
        while(!m_posted && retries_left > 0)
        {
            aos::delay(10 * us);
            m_posted = submit();
            retries_left--;
        }
        if (!m_posted) m_retries_failed++;
        m_attempted = !m_posted;
    }
}

const char * esc_str(const tESC & esc)
{
    switch (esc)
    {
        case eNull: return "Null";
        case eHome: return "Home";
        case eEnd: return "End";
        case eCtrl_C: return "Ctrl_C";
        case eLeft: return "Left";
        case eRight: return "Right";
        case eUp: return "Up";
        case eDown: return "Down";
        case eBackspace: return "Backspace";
        case eTab: return "Tab";
        case eEnter: return "Enter";
        case eInsert: return "Insert";
        case eDelete: return "Delete";
        case eReturn: return "Return";
        case eF1: return "F1";
        case eF2: return "F2";
        case eF3: return "F3";
        case eF4: return "F4";
        case eF5: return "F5";
        case eF6: return "F6";
        case eF7: return "F7";
        case eF8: return "F8";
        case eF9: return "F9";
        case eF10: return "F10";
        case eF11: return "F11";
        case eF12: return "F12";
        case eNum_5: return "Num_5";
        case eEsc: return "Esc";
        case ePageUp: return "PageUp";
        case ePageDown: return "PageDown";
        case eReserved0: return "Reserved0";
        case eReserved1: return "Reserved1";
        case eUnknown: return "Unknown";
        default: return "Unknown";
    }
}
