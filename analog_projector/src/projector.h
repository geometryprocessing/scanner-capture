#ifndef PROJECTOR_H
#define PROJECTOR_H

#include "aos/device.h"

class GPIO;

class Projector : public Device
{
public:
    static constexpr const char * const ClassName = "projector";

    Projector(GPIO& front, GPIO& side);
    void soft_reset() override;

    /* Initialization */

    void init();
    void add_to(Kernel& k, const str& path) override;

    /* Operation */

    void front_on(bool on);
    void side_on(bool on);

    bool enabled;
    uint32_t pattern, white; // in us

private:
    GPIO &m_front, &m_side;
};

#endif /* PROJECTOR_H */
