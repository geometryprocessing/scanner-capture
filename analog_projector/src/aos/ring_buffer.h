#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <vector>

template <typename T>
class RingBuffer
{
public:
    // Can't fill in the last element because head would become equal to tail
    // and buffer would be considered empty.
    // That's why we allocate size + 1 bytes buffer to achieve the capacity requested by user.
    // Another benefit: always have space to write a new head value in advance

    RingBuffer(size_t size, const T& init = T(0)) : m_buf(size + 1, init), m_head(0), m_tail(0) {}

    inline bool full() const { return next(m_head) == m_tail; }
    inline bool empty() const { return m_head == m_tail; }
    inline size_t size() const { return m_buf.size() - 1; }
    inline size_t used() const { return m_head >= m_tail ? m_head - m_tail : m_buf.size() - (m_tail - m_head); }
    inline size_t available() const { return size() - used(); }
    inline void purge() { m_head = m_tail = 0; }

    inline T& head() { return m_buf[m_head]; }
    inline T& tail() { return m_buf[m_tail]; }

    inline bool push_head()
    {
        if (full()) return false;
        m_head = next(m_head);
        return true;
    }

    inline bool pop_tail()
    {
        if (empty()) return false;
        m_tail = next(m_tail);
        return true;
    }

    inline bool push(const T& v)
    {
        head() = v; // might be a waste of time but the buffer is unlikely to be full
        return push_head();
    }

    inline bool pop(T& v)
    {
        v = tail(); // might be a waste of time but only once (when buffer gets empty)
        return pop_tail();
    }

//    inline bool pop_head(T& v) // special for uart terminal
//    {
//        if (empty()) return false;
//        m_head = prev(m_head);
//        v = head();
//        return true;
//    }

private:
    std::vector<T> m_buf;
    size_t m_head, m_tail;

    inline size_t next(const size_t pos) const { return (pos + 1) % m_buf.size(); }
//    inline size_t prev(const size_t pos) const { return (pos + m_buf.size() - 1) % m_buf.size(); }
};

#endif /* RING_BUFFER_H */
