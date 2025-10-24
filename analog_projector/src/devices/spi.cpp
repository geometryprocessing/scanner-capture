#include "spi.h"
#include "aos/kernel.h"
#include "gpio.h"

#include "inc/hw_memmap.h"
#include "inc/hw_ssi.h"
#include "driverlib/sysctl.h"
#include "driverlib/pin_map.h"
#include "driverlib/gpio.h"
#include "driverlib/ssi.h"

#define NUM_SSI  4

const uint32_t SSIPort[] = { GPIO_PORTA_BASE, GPIO_PORTF_BASE, GPIO_PORTB_BASE, GPIO_PORTD_BASE };

const uint32_t SSIBase[] = { SSI0_BASE, SSI1_BASE, SSI2_BASE, SSI3_BASE };

const uint32_t SSIPeriph[] = { SYSCTL_PERIPH_SSI0, SYSCTL_PERIPH_SSI1,
                               SYSCTL_PERIPH_SSI2, SYSCTL_PERIPH_SSI3 };

const uint32_t SSIConfig[][4] = { {GPIO_PA2_SSI0CLK, GPIO_PA3_SSI0FSS, GPIO_PA4_SSI0RX, GPIO_PA5_SSI0TX},
                                  {GPIO_PF2_SSI1CLK, GPIO_PF3_SSI1FSS, GPIO_PF0_SSI1RX, GPIO_PF1_SSI1TX},
                                  {GPIO_PB4_SSI2CLK, GPIO_PB5_SSI2FSS, GPIO_PB6_SSI2RX, GPIO_PB7_SSI2TX},
                                  {GPIO_PD0_SSI3CLK, GPIO_PD1_SSI3FSS, GPIO_PD2_SSI3RX, GPIO_PD3_SSI3TX}};

const uint32_t SSIPins[] = { GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5,
                             GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3,
                             GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6 | GPIO_PIN_7,
                             GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3};
//
// Initialization
//

SPI::SPI(uint8_t data_mode, uint8_t bit_order) : Device("spi"), m_module(0),
         m_data_mode(data_mode & 0x3), m_bit_order(bit_order & 0x1)
{
    soft_reset();
}

void SPI::soft_reset()
{
    Device::soft_reset();
    debug = nullptr;
}

void SPI::init(uint8_t module, uint32_t speed)
{
    soft_reset();

    if (module >= NUM_SSI) throw "SPI: not supported";

    m_module = module & 0x3;
    m_base = SSIBase[m_module];

    if(!SysCtlPeripheralPresent(SSIPeriph[m_module])) throw "SPI: Peripheral not present!";

	SysCtlPeripheralEnable(SSIPeriph[m_module]);
	while(!SysCtlPeripheralReady(SSIPeriph[m_module]));
	SSIDisable(m_base);

	GPIOPinConfigure(SSIConfig[m_module][0]);
	GPIOPinConfigure(SSIConfig[m_module][1]);
	GPIOPinConfigure(SSIConfig[m_module][2]);
	GPIOPinConfigure(SSIConfig[m_module][3]);
	GPIOPinTypeSSI(SSIPort[m_module], SSIPins[m_module]);

	SSIClockSourceSet(m_base, SSI_CLOCK_SYSTEM);
	SSIConfigSetExpClk(m_base, SysCtlClockGet(), m_data_mode, SSI_MODE_MASTER, m_speed = speed, 8);

	SSIEnable(m_base); // Enable by default

	// Clear out any initial data that might be present in the RX FIFO
    uint32_t initialData = 0;
	while(SSIDataGetNonBlocking(m_base, &initialData));

	m_initialized = true;
}

void SPI::add_to(Kernel& k, const str& path)
{
    k.add(&debug, path + s_"debug", rw_);
    k.add(&m_speed, path + s_"speed", r__);
    k.add(&m_module, path + s_"module", r__);
    k.add(&m_data_mode, path + s_"mode", r__);
    k.add(&m_bit_order, path + s_"order", r__);
}

void SPI::setDataMode(uint8_t data_mode)
{
    if (!m_initialized) throw "SPI: not initialized";

    m_data_mode = data_mode & 0x3;
	HWREG(m_base + SSI_O_CR0) &= ~(SSI_CR0_SPO | SSI_CR0_SPH);
	HWREG(m_base + SSI_O_CR0) |= m_data_mode << 6;
}

void SPI::setClockDivider(uint8_t divider)
{
    if (!m_initialized) throw "SPI: not initialized";

    HWREG(m_base + SSI_O_CPSR) = divider & 0xE;  // Value must be even
}

//
// Operation
//

static const uint8_t lookup[16] = {0x0, 0x8, 0x4, 0xc, 0x2, 0xa, 0x6, 0xe,
                                   0x1, 0x9, 0x5, 0xd, 0x3, 0xb, 0x7, 0xf};

uint8_t SPI::reverse(uint8_t n)
{
   // Reverse the top and bottom nibble and then swap them
   return (lookup[n & 0x0F] << 4) | lookup[n>>4];
}

uint8_t SPI::transfer(uint8_t data)
{
    if (!m_initialized) throw "SPI: not initialized";
    if (debug) debug->set();
//    uint32_t rxtx_data = (m_bit_order == LSB_FIRST ? reverse(data) : data);
    uint32_t rxtx_data = data;

	SSIDataPut(m_base, rxtx_data);

	while(SSIBusy(m_base));

	SSIDataGet(m_base, &rxtx_data);

//	uint8_t data_out = rxtx_data;
//    return m_bit_order == LSB_FIRST ? reverse(data_out) : data_out;
    if (debug) debug->clear();
    return rxtx_data;
}

void SPI::multiTransfer(uint32_t* data_out, uint32_t* data_in, uint8_t len)
{
    if (!m_initialized) throw "SPI: not initialized";
    if (debug) debug->set();

    for (uint8_t i = 0, j = 0; i < len || j < len; )
    {
        if (i < len) if ( SSIDataPutNonBlocking(m_base, data_out[i]) ) i++;
        if (j < len) if ( SSIDataGetNonBlocking(m_base, data_in + j) ) j++;
    }
    if (debug) debug->clear();
}

void SPI::multiTransfer(uint8_t* data_out, uint8_t* data_in, uint8_t len, uint8_t offset)
{
    if (!m_initialized) throw "SPI: not initialized";
    if (debug) debug->set();

    uint32_t rx_data;

    for (uint8_t i = 0, j = 0; i < len || j < len; )
    {
        if (i < len) if ( SSIDataPutNonBlocking(m_base, data_out[i]) ) i++;
        if (j < len) if ( SSIDataGetNonBlocking(m_base, &rx_data) )
        {
            if (data_in && j >= offset) data_in[j - offset] = rx_data;
            j++;
        }
    }
    if (debug) debug->clear();
}
