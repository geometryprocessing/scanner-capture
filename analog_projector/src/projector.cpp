#include "projector.h"
#include "aos/kernel.h"
#include "devices/gpio.h"

//
// Initialization
//

Projector::Projector(GPIO& front, GPIO& side) : Device("projector"),
    m_front(front), m_side(side)
{
    soft_reset();
}

void Projector::soft_reset()
{
    Device::soft_reset();

    pattern = white = 4900;
    enabled = true;
}

void Projector::add_to(Kernel& k, const str& path)
{
    k.add(&enabled, path + s_"ena", rw_);
    k.add(&pattern, path + s_"pat", rw_);
    k.add(&white, path + s_"wht", rw_);

    k.add_device(&m_front, path + s_"f");
    k.add_device(&m_side, path + s_"s");
}

void Projector::init()
{
    soft_reset();

    m_front.init(OUTPUT, HIGH); // active low
    m_side.init(OUTPUT, HIGH); // active low

    m_initialized = true;
}

//
// Operation
//

void Projector::front_on(bool on)
{
    if (on) m_front.clear();
    else m_front.set();
}

void Projector::side_on(bool on)
{
    if (on) m_side.clear();
    else m_side.set();
}
