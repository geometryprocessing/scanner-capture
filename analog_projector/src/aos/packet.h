#ifndef PACKET_H
#define PACKET_H

#include <vector>

template <typename T>
class Packet
{
public:
    // Preallocate the data
    Packet(size_t max_size, const T& init = T(0)) :
        m_max_size(max_size), m_size(0), m_data(max_size + 1, init) {}

    inline size_t size() const { return m_size; }
    inline size_t max_size() const { return m_max_size; }
    inline size_t set_size(size_t new_size) { return m_size = std::min(m_max_size, new_size); }
    inline size_t available() const { return m_max_size - m_size; }
    inline T* data() { return m_data.data(); }
    inline void purge() { m_size = 0; }

    inline size_t write(const T& v)
    {
        if (m_size == m_max_size) return 0;
        m_data[m_size++] = v;
        return 1;
    }

    inline size_t write(const T* buf)
    {
        size_t n = 0;
        while (buf[n])
        {
            if (write(buf[n])) n++;
            else break;
        }
        return n;
    }

    inline size_t write(const T* buf, size_t len)
    {
        size_t to_write = std::min(available(), len);
        memcpy(m_data.data() + m_size, buf, to_write);
        m_size += to_write;
        return to_write;
    }

private:
    size_t m_max_size, m_size;
    std::vector<T> m_data;
};

#endif /* PACKET_H */
