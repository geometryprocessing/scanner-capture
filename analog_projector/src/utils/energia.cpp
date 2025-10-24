#include "energia.h"
#include "inc/hw_types.h"
#include "inc/hw_memmap.h"
#include "inc/hw_gpio.h"
#include "inc/hw_timer.h"
#include "driverlib/sysctl.h"
#include "driverlib/pin_map.h"
#include "driverlib/gpio.h"
#include "driverlib/adc.h"
#include "driverlib/timer.h"
#include "driverlib/interrupt.h"

#define NOT_A_PORT 0
#define NOT_A_PIN 0
#define NOT_ON_ADC 0x10
#define NOT_ON_TIMER 0

#define GPIO_LOCK_KEY_DD        0x4C4F434B

#define PA 1
#define PB 2
#define PC 3
#define PD 4
#define PE 5
#define PF 6

#define T0A0 0
#define T0A1 1
#define T0B0 2
#define T0B1 3
#define T1A0 4
#define T1A1 5
#define T1B0 6
#define T1B1 7
#define T2A0 8
#define T2A1 9
#define T2B 10
#define T3A 11
#define T3B 12
#define WT0A 13
#define WT0B 14
#define WT1A 15
#define WT1B 16
#define WT2A 17
#define WT2B 18
#define WT3A 19
#define WT3B 20
#define WT5A 21
#define WT5B 22

#define TIMER0 0
#define TIMER1 1
#define TIMER2 2
#define TIMER3 3
#define WTIMER0 4
#define WTIMER1 5
#define WTIMER2 6
#define WTIMER3 7
#define WTIMER5 9

#define TIMA 0
#define TIMB 8

void TimerInit(uint32_t id, uint32_t int_, uint32_t mode, uint32_t period)
{
    uint32_t base = (id > 5 ? TIMER6_BASE : TIMER0_BASE) + 0x1000 * (id > 5 ? id - 6 : id);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0 + id);
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_TIMER0 + id));
    TimerConfigure(base, mode);
    TimerLoadSet(base, TIMER_A, period);
    TimerIntEnable(base, TIMER_TIMA_TIMEOUT);
    IntEnable(int_);
};

void delay(uint32_t milliseconds)
{
    // 1 clock cycle = 1 / SysCtlClockGet() second
    // 1 SysCtlDelay = 3 clock cycle = 3 / SysCtlClockGet() second
    // 1 second = SysCtlClockGet() / 3
    // 0.001 second = 1 ms = SysCtlClockGet() / 3 / 1000
    SysCtlDelay(milliseconds * (SysCtlClockGet() / 3 / 1000));
}

void delayMicroseconds(unsigned int us)
{
    SysCtlDelay(us * (SysCtlClockGet() / 1000000 / 3));
}

void pinMode(uint8_t pin, uint8_t mode)
{
    uint8_t bit = digitalPinToBitMask(pin);
    uint8_t port = digitalPinToPort(pin);
    uint32_t portBase = (uint32_t) portBASERegister(port);
    volatile uint32_t *lock = portLOCKRegister(port);
    volatile uint32_t *cr = portCRRegister(port);

    if (port == NOT_A_PORT) return;

    if (mode == INPUT) {
        GPIOPinTypeGPIOInput(portBase, bit);
    } else if (mode == INPUT_PULLUP) {
        *lock = GPIO_LOCK_KEY_DD;
        *cr |= bit;
        *lock = 0;
        GPIODirModeSet(portBase, bit, GPIO_DIR_MODE_IN);
        GPIOPadConfigSet(portBase, bit,
                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPU);
        *cr &= ~bit;
    } else if (mode == INPUT_PULLDOWN) {
        *lock = GPIO_LOCK_KEY_DD;
        *cr |= bit;
        *lock = 0;
        GPIODirModeSet(portBase, bit, GPIO_DIR_MODE_IN);
        GPIOPadConfigSet(portBase, bit,
                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD_WPD);
        *cr &= ~bit;
    } else { // mode == OUTPUT
        GPIOPinTypeGPIOOutput(portBase, bit);
    }
}

int digitalRead(uint8_t pin)
{
    uint8_t bit = digitalPinToBitMask(pin);
    uint8_t port = digitalPinToPort(pin);
    uint32_t portBase = (uint32_t) portBASERegister(port);

    if (port == NOT_A_PORT) return LOW;

    if(GPIOPinRead(portBase, bit)) return HIGH;
    return LOW;
}

void digitalWrite(uint8_t pin, uint8_t val)
{
    uint8_t bit = digitalPinToBitMask(pin);
    uint8_t mask = val ? bit : 0;
    uint8_t port = digitalPinToPort(pin);
    uint32_t portBase = (uint32_t) portBASERegister(port);

    if (port == NOT_A_PORT) return;

    GPIOPinTypeGPIOOutput(portBase, bit); // Override analogWrite()
    GPIOPinWrite(portBase, bit, mask);
}

void decodePin(uint8_t pin, uint32_t& addr, uint8_t& bit)
{
    bit = digitalPinToBitMask(pin);
    uint8_t port = digitalPinToPort(pin);
    uint32_t portBase = (uint32_t) portBASERegister(port);

    addr = (port == NOT_A_PORT) ? 0 : portBase + GPIO_O_DATA + (bit << 2);
}


static int _readResolution = 12;
#define PWM_MODE 0x20A

void analogReadResolution(int res) {
    _readResolution = res;
}

static inline uint32_t mapResolution(uint32_t value, uint32_t from, uint32_t to) {
    if (from == to)
        return value;
    if (from > to)
        return value >> (from-to);
    else
        return value << (to-from);
}

uint16_t analogRead(uint8_t pin) {
    uint8_t port = digitalPinToPort(pin);
    uint16_t value[1];
    uint32_t channel = digitalPinToADCIn(pin);
    if (channel == NOT_ON_ADC) { //invalid ADC pin
        return 0;
    }
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
    if(channel != ADC_CTL_TS)
        GPIOPinTypeADC((uint32_t) portBASERegister(port), digitalPinToBitMask(pin));
    ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);
    ADCSequenceStepConfigure(ADC0_BASE, 3, 0, channel | ADC_CTL_IE | ADC_CTL_END);
    ADCSequenceEnable(ADC0_BASE, 3);

    ADCIntClear(ADC0_BASE, 3);
    ADCProcessorTrigger(ADC0_BASE, 3);
    while(!ADCIntStatus(ADC0_BASE, 3, false));
    ADCIntClear(ADC0_BASE, 3);
    ADCSequenceDataGet(ADC0_BASE, 3, (uint32_t*) value);

    return mapResolution(value[0], 12, _readResolution);
}

uint32_t getTimerBase(uint32_t offset) {
    if(offset > WTIMER1) {
        return (WTIMER2_BASE + ((offset - 6) << 12));
    }
    else if(offset < WTIMER0){
        return (TIMER0_BASE + (offset << 12));
    }
    else {
        return WTIMER0_BASE + ((offset-4) << 12);
    }
}

void enableTimerPeriph(uint32_t offset) {
    if(offset > TIMER3) {
        SysCtlPeripheralEnable(SYSCTL_PERIPH_WTIMER0 + offset - 4);
    }
    else {
        SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0 + offset);
    }
}

void PWMWrite(uint8_t pin, uint32_t analog_res, uint32_t duty, unsigned int freq) {
    if (duty == 0) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    else if (duty >= analog_res) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);
    }
    else {
        uint8_t bit = digitalPinToBitMask(pin); // get pin bit
        uint8_t port = digitalPinToPort(pin);   // get pin port
        uint8_t timer = digitalPinToTimer(pin);
        uint32_t portBase = (uint32_t) portBASERegister(port);
        uint32_t offset = timerToOffset(timer);
        uint32_t timerBase = getTimerBase(offset);
        uint32_t timerAB = TIMER_A << timerToAB(timer);

        if (port == NOT_A_PORT) return;     // pin on timer?

        uint32_t periodPWM = SysCtlClockGet()/freq;

        enableTimerPeriph(offset);
        GPIOPinConfigure(timerToPinConfig(timer));
        GPIOPinTypeTimer((long unsigned int) portBase, bit);

        //
        // Configure for half-width mode, allowing timers A and B to
        // operate independently
        //
        HWREG(timerBase + TIMER_O_CFG) = 0x04;

        if(timerAB == TIMER_A) {
            HWREG(timerBase + TIMER_O_CTL) &= ~TIMER_CTL_TAEN;
            HWREG(timerBase + TIMER_O_TAMR) = PWM_MODE;
        }
        else {
            HWREG(timerBase + TIMER_O_CTL) &= ~TIMER_CTL_TBEN;
            HWREG(timerBase + TIMER_O_TBMR) = PWM_MODE;
        }
        TimerLoadSet(timerBase, timerAB, periodPWM);
        TimerMatchSet(timerBase, timerAB,
                (analog_res-duty)*periodPWM/analog_res);

        //
        // If using a 16-bit timer, with a periodPWM > 0xFFFF,
        // need to use a prescaler
        //
        if((offset < WTIMER0) && (periodPWM > 0xFFFF)) {
            TimerPrescaleSet(timerBase, timerAB,
                (periodPWM & 0xFFFF0000) >> 16);
            TimerPrescaleMatchSet(timerBase, timerAB,
                (((analog_res-duty)*periodPWM/analog_res) & 0xFFFF0000) >> 16);
        }
        TimerEnable(timerBase, timerAB);
    }
}

void analogWrite(uint8_t pin, int val) {
    //  duty cycle(%) = val / 255;
    //  Frequency of 490Hz specified by Arduino API
    PWMWrite(pin, 255, val, 490);
}

const uint8_t digital_pin_to_port[] = {
        NOT_A_PIN,      /*  dummy */
        NOT_A_PIN,      /*  1 - 3.3V */
        PB,             /*  2 - PB5 */
        PB,             /*  3 - PB0 */
        PB,             /*  4 - PB1 */
        PE,             /*  5 - PE4 */
        PE,             /*  6 - PE5 */
        PB,             /*  7 - PB4 */
        PA,             /*  8 - PA5 */
        PA,             /*  9 - PA6 */
        PA,             /*  10 - PA7 */
        PA,             /*  11 - PA2 */
        PA,             /*  12 - PA3 */
        PA,             /*  13 - PA4 */
        PB,             /*  14 - PB6 */
        PB,             /*  15 - PB7 */
        NOT_A_PIN,      /*  16 - RST */
        PF,             /*  17 - PF0 */
        PE,             /*  18 - PE0 */
        PB,             /*  19 - PB2 */
        NOT_A_PIN,      /*  20 - GND */
        NOT_A_PIN,      /*  21 - VBUS */
        NOT_A_PIN,      /*  22 - GND */
        PD,             /*  23 - PD0 */
        PD,             /*  24 - PD1 */
        PD,             /*  25 - PD2 */
        PD,             /*  26 - PD3 */
        PE,             /*  27 - PE1 */
        PE,             /*  28 - PE2 */
        PE,             /*  29 - PE3 */
        PF,             /*  30 - PF1 */
        PF,             /*  31 - PF4 */
        PD,             /*  32 - PD7 */
        PD,             /*  33 - PD6 */
        PC,             /*  34 - PC7 */
        PC,             /*  35 - PC6 */
        PC,             /*  36 - PC5 */
        PC,             /*  37 - PC4 */
        PB,             /*  38 - PB3 */
        PF,             /*  39 - PF3 */
        PF,             /*  40 - PF2 */
};

#ifndef BV
    #define BV(x) (1 << (x))
#endif

const uint8_t digital_pin_to_bit_mask[] = {
        NOT_A_PIN,      /*  dummy */
        NOT_A_PIN,      /*  1 - 3.3V */
        BV(5),          /*  2 - PB5 */
        BV(0),          /*  3 - PB0 */
        BV(1),          /*  4 - PB1 */
        BV(4),          /*  5 - PE4 */
        BV(5),          /*  6 - PE5 */
        BV(4),          /*  7 - PB4 */
        BV(5),          /*  8 - PA5 */
        BV(6),          /*  9 - PA6 */
        BV(7),          /*  10 - PA7 */
        BV(2),          /*  11 - PA2 */
        BV(3),          /*  12 - PA3 */
        BV(4),          /*  13 - PA4 */
        BV(6),          /*  14 - PB6 */
        BV(7),          /*  15 - PB7 */
        NOT_A_PIN,      /*  16 - RST */
        BV(0),          /*  17 - PF0 */
        BV(0),          /*  18 - PE0 */
        BV(2),          /*  19 - PB2 */
        NOT_A_PIN,      /*  20 - GND */
        NOT_A_PIN,      /*  21 - VBUS */
        NOT_A_PIN,      /*  22 - GND */
        BV(0),          /*  23 - PD0 */
        BV(1),          /*  24 - PD1 */
        BV(2),          /*  25 - PD2 */
        BV(3),          /*  26 - PD3 */
        BV(1),          /*  27 - PE1 */
        BV(2),          /*  28 - PE2 */
        BV(3),          /*  29 - PE3 */
        BV(1),          /*  30 - PF1 */
        BV(4),          /*  31 - PF4 */
        BV(7),          /*  32 - PD7 */
        BV(6),          /*  33 - PD6 */
        BV(7),          /*  34 - PC7 */
        BV(6),          /*  35 - PC6 */
        BV(5),          /*  36 - PC5 */
        BV(4),          /*  37 - PC4 */
        BV(3),          /*  38 - PB3 */
        BV(3),          /*  39 - PF3 */
        BV(2),          /*  40 - PF2 */
};

const uint32_t port_to_base[] = {
        NOT_A_PORT,
        (uint32_t) GPIO_PORTA_BASE,
        (uint32_t) GPIO_PORTB_BASE,
        (uint32_t) GPIO_PORTC_BASE,
        (uint32_t) GPIO_PORTD_BASE,
        (uint32_t) GPIO_PORTE_BASE,
        (uint32_t) GPIO_PORTF_BASE,
};

const uint32_t digital_pin_to_analog_in[] = {
        ADC_CTL_TS,     /*  0 - TempSensor   */
        NOT_ON_ADC,     /*  1 - 3.3V*/
        ADC_CTL_CH11,   /*  2 - PB5 */
        NOT_ON_ADC,     /*  3 - PB0 */
        NOT_ON_ADC,     /*  4 - PB1 */
        ADC_CTL_CH9,    /*  5 - PE4 */
        ADC_CTL_CH8,    /*  6 - PE5 */
        ADC_CTL_CH10,   /*  7 - PB4 */
        NOT_ON_ADC,     /*  8 - PA5 */
        NOT_ON_ADC,     /*  9 - PA6 */
        NOT_ON_ADC,     /*  10 - PA7 */
        NOT_ON_ADC,     /*  11 - PA2 */
        NOT_ON_ADC,     /*  12 - PA3 */
        NOT_ON_ADC,     /*  13 - PA4 */
        NOT_ON_ADC,     /*  14 - PB6 */
        NOT_ON_ADC,     /*  15 - PB7 */
        NOT_ON_ADC,     /*  16 - RST */
        NOT_ON_ADC,     /*  17 - PF0 */
        ADC_CTL_CH3,    /*  18 - PE0 */
        NOT_ON_ADC,     /*  19 - PB2 */
        NOT_ON_ADC,     /*  20 - GND */
        NOT_ON_ADC,     /*  21 - VBUS */
        NOT_ON_ADC,     /*  22 - GND */
        ADC_CTL_CH7,    /*  23 - PD0 */
        ADC_CTL_CH6,    /*  24 - PD1 */
        ADC_CTL_CH5,    /*  25 - PD2 */
        ADC_CTL_CH4,    /*  26 - PD3 */
        ADC_CTL_CH2,    /*  27 - PE1 */
        ADC_CTL_CH1,    /*  28 - PE2 */
        ADC_CTL_CH0,    /*  29 - PE3 */
        NOT_ON_ADC,     /*  30 - PF1 */
        NOT_ON_ADC,     /*  31 - PF4 */
        NOT_ON_ADC,     /*  32 - PD7 */
        NOT_ON_ADC,     /*  33 - PD6 */
        NOT_ON_ADC,     /*  34 - PC7 */
        NOT_ON_ADC,     /*  35 - PC6 */
        NOT_ON_ADC,     /*  36 - PC5 */
        NOT_ON_ADC,     /*  37 - PC4 */
        NOT_ON_ADC,     /*  38 - PB3 */
        NOT_ON_ADC,     /*  39 - PF3 */
        NOT_ON_ADC,     /*  40 - PF2 */
};

const uint32_t timer_to_offset[] = {
        TIMER0,
        TIMER0,
        TIMER0,
        TIMER0,
        TIMER1,
        TIMER1,
        TIMER1,
        TIMER1,
        TIMER2,
        TIMER2,
        TIMER2,
        TIMER3,
        TIMER3,
        WTIMER0,
        WTIMER0,
        WTIMER1,
        WTIMER1,
        WTIMER2,
        WTIMER2,
        WTIMER3,
        WTIMER3,
        WTIMER5,
        WTIMER5,
};

const uint8_t timer_to_ab[] = {
        TIMA,
        TIMA,
        TIMB,
        TIMB,
        TIMA,
        TIMA,
        TIMB,
        TIMB,
        TIMA,
        TIMA,
        TIMB,
        TIMA,
        TIMB,
        TIMA,
        TIMB,
        TIMA,
        TIMB,
        TIMA,
        TIMB,
        TIMA,
        TIMB,
        TIMA,
        TIMB,
};

const uint32_t timer_to_pin_config[] = {
        GPIO_PB6_T0CCP0,
        GPIO_PF0_T0CCP0,
        GPIO_PB7_T0CCP1,
        GPIO_PF1_T0CCP1,
        GPIO_PB4_T1CCP0,
        GPIO_PF2_T1CCP0,
        GPIO_PB5_T1CCP1,
        GPIO_PF3_T1CCP1,
        GPIO_PB0_T2CCP0,
        GPIO_PF4_T2CCP0,
        GPIO_PB1_T2CCP1,
        GPIO_PB2_T3CCP0,
        GPIO_PB3_T3CCP1,
        GPIO_PC4_WT0CCP0,
        GPIO_PC5_WT0CCP1,
        GPIO_PC6_WT1CCP0,
        GPIO_PC7_WT1CCP1,
        GPIO_PD0_WT2CCP0,
        GPIO_PD1_WT2CCP1,
        GPIO_PD2_WT3CCP0,
        GPIO_PD3_WT3CCP1,
        GPIO_PD6_WT5CCP0,
        GPIO_PD7_WT5CCP1,
};

const uint8_t digital_pin_to_timer[] = {
        NOT_ON_TIMER,   /*  dummy */
        NOT_ON_TIMER,   /*  1 - 3.3V */
        T1B0,           /*  2 - PB5 */
        T2A0,           /*  3 - PB0 */
        T2B,            /*  4 - PB1 */
        NOT_ON_TIMER,   /*  5 - PE4 */
        NOT_ON_TIMER,   /*  6 - PE5 */
        T1A0,           /*  7 - PB4 */
        NOT_ON_TIMER,   /*  8 - PA5 */
        NOT_ON_TIMER,   /*  9 - PA6 */
        NOT_ON_TIMER,   /*  10 - PA7 */
        NOT_ON_TIMER,   /*  11 - PA2 */
        NOT_ON_TIMER,   /*  12 - PA3 */
        NOT_ON_TIMER,   /*  13 - PA4 */
        T0A0,           /*  14 - PB6 */
        T0B0,           /*  15 - PB7 */
        NOT_ON_TIMER,   /*  16 - RST */
        T0A1,           /*  17 - PF0 */
        NOT_ON_TIMER,   /*  18 - PE0 */
        T3A,            /*  19 - PB2 */
        NOT_ON_TIMER,   /*  20 - GND */
        NOT_ON_TIMER,   /*  21 - VBUS */
        NOT_ON_TIMER,   /*  22 - GND */
        WT2A,           /*  23 - PD0 */
        WT2B,           /*  24 - PD1 */
        WT3A,           /*  25 - PD2 */
        WT3B,           /*  26 - PD3 */
        NOT_ON_TIMER,   /*  27 - PE1 */
        NOT_ON_TIMER,   /*  28 - PE2 */
        NOT_ON_TIMER,   /*  29 - PE3 */
        T0B1,           /*  30 - PF1 */
        T2A1,           /*  31 - PF4 */
        WT5B,           /*  32 - PD7 */
        WT5A,           /*  33 - PD6 */
        WT1B,           /*  34 - PC7 */
        WT1A,           /*  35 - PC6 */
        WT0B,           /*  36 - PC5 */
        WT0A,           /*  37 - PC4 */
        T3B,            /*  38 - PB3 */
        T1B1,           /*  39 - PF3 */
        T1A1,           /*  40 - PF2 */
};

