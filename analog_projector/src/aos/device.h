#ifndef DEVICE_H
#define DEVICE_H

#include "action_os.h"

class Kernel;

class Device
{
    friend Kernel;
public:
    static constexpr const char * const ClassName = "device";

    Device(const char * type) : m_type(type), m_path("???"), m_initialized(false) {}

    bool inline initialized() const { return m_initialized; }

    virtual void soft_reset() { m_initialized = false; }

protected:
    bool m_initialized;

    virtual void add_to(Kernel& k, const str& path) {}

private:
    str m_path;
    const char * m_type;
};

#endif /* DEVICE_H */
