#include "usb.h"
#include "aos/kernel.h"
#include "gpio.h"

#include "inc/hw_memmap.h"
#include "inc/hw_ints.h"
#include "driverlib/sysctl.h"
#include "driverlib/gpio.h"
#include "driverlib/usb.h"
#include "usblib/usblib.h"
#include "usblib/usb-ids.h"
#include "usblib/device/usbdevice.h"
#include "usblib/device/usbdbulk.h"

const uint8_t lang_descriptor[] =
{
    4,
    USB_DTYPE_STRING,
    USBShort(USB_LANG_EN_US)
};

const uint8_t manufacturer_string[] =
{
    (12 + 1) * 2,
    USB_DTYPE_STRING,
    'Y', 0, 'u', 0, 'r', 0, 'i', 0, 'i', 0, ' ', 0, 'P', 0, 'i', 0, 'a', 0, 'd', 0, 'y', 0, 'k', 0
};

const uint8_t product_string[] =
{
    (29 + 1) * 2,
    USB_DTYPE_STRING,
    'L', 0, 'e', 0, 'g', 0, 'o', 0, 'T', 0, 'r', 0, 'a', 0, 'c', 0, 'k', 0, 'e', 0, 'r', 0, ' ', 0,
    'C', 0, 'a', 0, 'm', 0, 'e', 0, 'r', 0, 'a', 0, ' ', 0,
    'C', 0, 'o', 0, 'n', 0, 't', 0, 'r', 0, 'o', 0, 'l', 0, 'l', 0, 'e', 0, 'r', 0
};

const uint8_t serial_number_string[] =
{
    (9 + 1) * 2,
    USB_DTYPE_STRING,
    'V', 0, 'e', 0, 'r', 0, 's', 0, 'i', 0, 'o', 0, 'n', 0, ' ', 0, '0', 0
};

const uint8_t data_interface_string[] =
{
    (19 + 1) * 2,
    USB_DTYPE_STRING,
    'B', 0, 'u', 0, 'l', 0, 'k', 0, ' ', 0, 'D', 0, 'a', 0, 't', 0, 'a', 0, ' ', 0,
    'I', 0, 'n', 0, 't', 0, 'e', 0, 'r', 0, 'f', 0, 'a', 0, 'c', 0, 'e', 0
};

const uint8_t config_string[] =
{
    (23 + 1) * 2,
    USB_DTYPE_STRING,
    'B', 0, 'u', 0, 'l', 0, 'k', 0, ' ', 0, 'D', 0, 'a', 0, 't', 0,
    'a', 0, ' ', 0, 'C', 0, 'o', 0, 'n', 0, 'f', 0, 'i', 0, 'g', 0,
    'u', 0, 'r', 0, 'a', 0, 't', 0, 'i', 0, 'o', 0, 'n', 0
};

const uint8_t * const string_descriptors[] =
{
    lang_descriptor,
    manufacturer_string,
    product_string,
    serial_number_string,
    data_interface_string,
    config_string
};

#define NUM_STRING_DESCRIPTORS (sizeof(string_descriptors) / sizeof(uint8_t *))

//
// Interrupts
//

static USB* g_usb = nullptr;

uint32_t RxCallback(void *pvCBData, uint32_t ui32Event, uint32_t ui32MsgValue, void *pvMsgData)
{
    if (g_usb) return g_usb->RxHandler(pvCBData, ui32Event, ui32MsgValue, pvMsgData);
    return 0;
//    uint32_t ret = 0; // too much overhead and no user code involved anyway (that could throw an exception)
//    if (g_usb) aos::safe([&]() { ret = g_usb->RxHandler(pvCBData, ui32Event, ui32MsgValue, pvMsgData); });
//    return ret;
}
uint32_t TxCallback(void *pvCBData, uint32_t ui32Event, uint32_t ui32MsgValue, void *pvMsgData)
{
    if (g_usb) return g_usb->TxHandler(pvCBData, ui32Event, ui32MsgValue, pvMsgData);
    return 0;
//    uint32_t ret = 0; // too much overhead and no user code involved anyway (that could throw an exception)
//    if (g_usb) aos::safe([&]() { ret = g_usb->TxHandler(pvCBData, ui32Event, ui32MsgValue, pvMsgData); });
//    return ret;
}

tUSBDBulkDevice g_bulk_device =
{
    USB_VID_TI_1CBE,
    USB_PID_BULK,
    500,
    USB_CONF_ATTR_SELF_PWR,
    RxCallback,
    (void *)&g_bulk_device,
    TxCallback,
    (void *)&g_bulk_device,
    string_descriptors,
    NUM_STRING_DESCRIPTORS
};

//
// Initialization
//

USB::USB(size_t rx_q_size, size_t tx_q_size, uint8_t packet_size) :
         m_rx_q(rx_q_size, Packet<char>(packet_size)), Connection(0, "usb"),
         m_tx_q(tx_q_size, Packet<char>(packet_size)), c_packet_size(packet_size)
{
    soft_reset();
}

void USB::soft_reset()
{
    Connection::soft_reset();

    m_continuous = false;
    m_tx_pending = false;
    rx_debug = tx_debug = nullptr;
    m_flags = m_timestamp = m_position = 0;

    m_rx_q.purge();
    m_tx_q.purge();

    m_rx_q.head().purge();
    m_tx_q.head().purge();

    m_ft_buf = vec<uint8_t>(13, 0);

    clear_stats();
}

void USB::clear_stats()
{
    Connection::clear_stats();
    m_zlp_count = 0;
}

void USB::add_to(Kernel& k, const str& path)
{
    Connection::add_to(k, path);

    k.add(&m_continuous, path + s_"cont", r__);
    k.add(&rx_debug, path + s_"rx_debug", rw_);
    k.add(&tx_debug, path + s_"tx_debug", rw_);

    k.add(&(m_zlp_count), path + s_"stats" + s_"zlp", r__);
    k.add(tAct([&](){ clear_stats(); }), path + s_"clr_stats", __x);
}

void USB::init()
{
    soft_reset();

    if(!SysCtlPeripheralPresent(SYSCTL_PERIPH_USB0)) throw "USB: Peripheral not present!";

    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOD));

    GPIOPinTypeUSBAnalog(GPIO_PORTD_BASE, GPIO_PIN_4 | GPIO_PIN_5);
    USBIntRegister(INT_USB0, USB0DeviceIntHandler);

    USBStackModeSet(0, eUSBModeForceDevice, 0);
    USBDBulkInit(0, &g_bulk_device);

    g_usb = this;
    m_initialized = true;
}

void USB::priority(uint8_t p)
{
    if (!m_initialized) throw "USB: not initialized";

    aos::priority(INT_USB0, p);
}

//
// Interrupt handling
//

uint32_t USB::RxHandler(void *pvCBData, uint32_t ui32Event, uint32_t ui32MsgValue, void *pvMsgData)
{
    if (rx_debug) rx_debug->set();

    switch(ui32Event)
    {
        case USB_EVENT_CONNECTED:
        {
            if (debug) debug->print(GREEN_"Host connected"_RESETn);
            m_tx_pending = false;
            break;
        }

        case USB_EVENT_RX_AVAILABLE:
        {
            uint32_t available = USBDBulkRxPacketAvailable(pvCBData);

            if (available != ui32MsgValue && debug) debug->printf(YELLOW_"available != ui32MsgValue"_RESETn);

            if (!available)
            {
                m_zlp_count++;
                if (debug) debug->println("USB: Got ZLP");
            }

            uint32_t read = USBDBulkPacketRead(pvCBData, (uint8_t*)m_rx_q.head().data(),
                                               std::min(ui32MsgValue, (uint32_t)c_packet_size), true);

            m_rx_bytes_lost += available - read;

            if (!m_continuous || (m_continuous && m_rx_q.head().data()[0]))
            {
//                throw "Boom";  // For debugging

                m_rx_q.head().set_size(read);
                if (!m_rx_q.push_head()) m_rx_packets_lost++;
            }

            if (rx_debug) rx_debug->clear();
            return available;
        }
    }
    if (rx_debug) rx_debug->clear();
    return 0;
}

uint32_t USB::TxHandler(void *pvCBData, uint32_t ui32Event, uint32_t ui32MsgValue, void *pvMsgData)
{
    if (tx_debug) tx_debug->set();

    if(ui32Event == USB_EVENT_TX_COMPLETE)
    {
        m_tx_pending = false;
        transmit();
    }
    if (tx_debug) tx_debug->set();
    return 0;
}

//
// Connection interface implementation
//

void USB::transmit()
{
    if (!m_tx_pending)
    {
        if (!m_tx_q.empty())
        {
            USBDBulkPacketWrite((void*)&g_bulk_device, (uint8_t*)m_tx_q.tail().data(), m_tx_q.tail().size(), true);
            m_tx_pending = true;
            m_tx_q.tail().purge();
            m_tx_q.pop_tail();
        }
        else if (m_continuous)
        {
            m_ft_buf[0] = 0;
            *(uint32_t*)(m_ft_buf.data() + 1) = m_flags;
            *(uint32_t*)(m_ft_buf.data() + 5) = m_timestamp;
            *(uint32_t*)(m_ft_buf.data() + 9) = m_position;

            USBDBulkPacketWrite((void*)&g_bulk_device, m_ft_buf.data(), m_ft_buf.size(), true);
            m_tx_pending = true;
        }
    }
}

void USB::read(vec<char>& v, bool null_terminated)
{
    v.clear();
    if (!m_rx_q.empty())
    {
        v.insert(v.begin(), m_rx_q.tail().data(), m_rx_q.tail().data() + m_rx_q.tail().size());
        m_rx_q.tail().purge();
        m_rx_q.pop_tail();
    }
    if (null_terminated) v.push_back('\0');
}

size_t USB::write(const char *buf, size_t len)
{
    if (buf) return m_tx_q.head().write(buf, len);
    else {
        size_t to_write = std::min(len, tx_bytes_available());
        m_tx_q.head().set_size(m_tx_q.head().size() + to_write);
        return to_write;
    }
}
