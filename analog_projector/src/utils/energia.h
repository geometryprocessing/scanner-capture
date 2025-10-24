#ifndef ENERGIA_H
#define ENERGIA_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"{
#endif

#define LOW  0
#define HIGH 1
#define RISING 2
#define FALLING 3
#define CHANGE 4

#define INPUT  0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3

#define PI 3.1415926535897932384626433832795
#define DEG_TO_RAD 0.017453292519943295769236907684886
#define RAD_TO_DEG 57.295779513082320876798154814105

#define round(x)     ((x)>=0?(long)((x)+0.5):(long)((x)-0.5))
#define radians(deg) ((deg)*DEG_TO_RAD)
#define degrees(rad) ((rad)*RAD_TO_DEG)

#define bitRead(value, bit) (((value) >> (bit)) & 0x01)
#define bitSet(value, bit) ((value) |= (1UL << (bit)))
#define bitClear(value, bit) ((value) &= ~(1UL << (bit)))
#define bitWrite(value, bit, bitvalue) (bitvalue ? bitSet(value, bit) : bitClear(value, bit))

#define TimerValueSet(ulBase, ulTimer, ulValue) \
        HWREG((ulBase) + ((ulTimer)==TIMER_A ? TIMER_O_TAV : TIMER_O_TBV)) = (ulValue)

void TimerInit(uint32_t id, uint32_t int_, uint32_t mode, uint32_t period);
void delay(uint32_t milliseconds);
void delayMicroseconds(unsigned int us);

void pinMode(uint8_t, uint8_t);
void digitalWrite(uint8_t, uint8_t);
int digitalRead(uint8_t);
void decodePin(uint8_t, uint32_t&, uint8_t&);

void analogReadResolution(int res);
uint16_t analogRead(uint8_t);
void analogWrite(uint8_t pin, int val);

extern const uint8_t digital_pin_to_port[];
extern const uint8_t digital_pin_to_bit_mask[];
extern const uint32_t port_to_base[];
extern const uint32_t digital_pin_to_analog_in[];
extern const uint8_t digital_pin_to_timer[];
extern const uint32_t timer_to_offset[];
extern const uint8_t timer_to_ab[];
extern const uint32_t timer_to_pin_config[];

#define digitalPinToPort(P)       ( digital_pin_to_port[P] )
#define digitalPinToBitMask(P)    ( digital_pin_to_bit_mask[P] )
#define digitalPinToADCIn(P)      ( digital_pin_to_analog_in[P] )
#define digitalPinToTimer(P)      ( digital_pin_to_timer[P] )
#define timerToAB(P)              ( timer_to_ab[P] )
#define timerToOffset(P)          ( timer_to_offset[P] )
#define timerToPinConfig(P)       ( timer_to_pin_config[P] )
#define portBASERegister(P)       ((volatile uint32_t *) port_to_base[P])
#define portDATARegister(P)       ((volatile uint32_t *)( port_to_base[P] + 0x3FC ))
#define portDIRRegister(P)        ((volatile uint32_t *)( port_to_base[P] + 0x400 ))
#define portLOCKRegister(P)       ((volatile uint32_t *)( port_to_base[P] + 0x520 ))
#define portCRRegister(P)         ((volatile uint32_t *)( port_to_base[P] + 0x524 ))

#ifdef __cplusplus
} // extern "C"
#endif

//
// Pin names based on the silk-screen
//
static const uint8_t PB_5 = 2;
static const uint8_t PB_0 = 3;
static const uint8_t PB_1 = 4;
static const uint8_t PE_4 = 5;
static const uint8_t PE_5 = 6;
static const uint8_t PB_4 = 7;
static const uint8_t PA_5 = 8;
static const uint8_t PA_6 = 9;
static const uint8_t PA_7 = 10;
static const uint8_t PA_2 = 11;
static const uint8_t PA_3 = 12;
static const uint8_t PA_4 = 13;
static const uint8_t PB_6 = 14;
static const uint8_t PB_7 = 15;
static const uint8_t PF_0 = 17;
static const uint8_t PE_0 = 18;
static const uint8_t PB_2 = 19;
static const uint8_t PD_0 = 23;
static const uint8_t PD_1 = 24;
static const uint8_t PD_2 = 25;
static const uint8_t PD_3 = 26;
static const uint8_t PE_1 = 27;
static const uint8_t PE_2 = 28;
static const uint8_t PE_3 = 29;
static const uint8_t PF_1 = 30;
static const uint8_t PF_4 = 31;
static const uint8_t PD_7 = 32;
static const uint8_t PD_6 = 33;
static const uint8_t PC_7 = 34;
static const uint8_t PC_6 = 35;
static const uint8_t PC_5 = 36;
static const uint8_t PC_4 = 37;
static const uint8_t PB_3 = 38;
static const uint8_t PF_3 = 39;
static const uint8_t PF_2 = 40;

static const uint8_t A0 =  29; //PE_3
static const uint8_t A1 =  28; //PE_2
static const uint8_t A2 =  27; //PE_1
static const uint8_t A3 =  18; //PE_0
static const uint8_t A4 =  26; //PD_3
static const uint8_t A5 =  25; //PD_2
static const uint8_t A6 =  24; //PD_1
static const uint8_t A7 =  23; //PD_0
static const uint8_t A8 =   6; //PE_5
static const uint8_t A9 =   5; //PE_4
static const uint8_t A10 =  7; //PB_4
static const uint8_t A11 =  2; //PB_5

static const uint8_t RED_LED = 30;
static const uint8_t GREEN_LED = 39;
static const uint8_t BLUE_LED = 40;

static const uint8_t PUSH1 = 31;
static const uint8_t PUSH2 = 17;
static const uint8_t TEMPSENSOR = 0;

#endif /* ENERGIA_H */
