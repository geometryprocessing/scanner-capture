#include "aos/kernel.h"
#include "devices/usb.h"
#include "devices/uart.h"
#include "devices/gpio.h"
#include "devices/timer.h"
#include "motor.h"
#include "projector.h"
#include "scanner.h"

//uint8_t mode = WAIT;
uint32_t count = 1; // frame (pairs)
uint32_t left = 0; // frame (pairs)
uint8_t reps = 0;

// ultra-speed capture
uint32_t pattern_u = 1000; // us
uint32_t white_u = 500; // us
uint32_t fps_u = 470; // Hz (pairs)

// high-speed capture
uint32_t pattern_h = 1500; // us
uint32_t white_h = 1200; // us
uint32_t fps_h = 300; // Hz (pairs)

// optimal-speed capture
uint32_t pattern_o = 2400; // us
uint32_t white_o = 1100; // us
uint32_t fps_o = 270; // Hz (pairs)

// medium-speed capture
uint32_t pattern_m = 2500; // us
uint32_t white_m = 2000; // us
uint32_t fps_m = 200; // Hz (pairs)

// video-speed capture
uint32_t pattern_v = 3750; // us
uint32_t white_v = 3000; // us
uint32_t fps_v = 120; // Hz (pairs)

// capture parameters
uint32_t pattern = pattern_h; // us
uint32_t white = white_h; // us
uint32_t fps = fps_h; // Hz (pairs)

// calibration parameters
uint32_t delay_ref = 60; // us
uint32_t interval_ref = 12;  // 12 steps = 150 um
uint32_t interval = interval_ref;  // steps
uint32_t delay = delay_ref; // us
uint32_t amb_mul = 5;  // times
uint32_t settle = 1000;  // ms


vec<GPIO> sw = {PA|2, PA|3};
vec<GPIO> btn = {BTN_1, BTN_2};
vec<GPIO> led = {RED, GREEN, BLUE};
vec<GPIO> debug = {PD|0, PD|1, PD|2, PD|3};

GPIO &sw1 = sw[0], &sw2= sw[1];
GPIO &red = led[0], &green = led[1], &blue = led[2];

GPIO mode_gnd(PE|2), mode_sel(PA|5);

// Short rods-guide stage
//GPIO pul(PA|5), dir(PB|4), ena(PE|1), lim_h(PE|2), lim_p(PA|6), gnd_h(PE|3), gnd_p(PA|7);
//Motor motor(pul, dir, ena, lim_h, lim_p);

// Long dual-rail stage (with transistors)
GPIO pul(PB|3), dir(PC|5), ena(PC|7), lim_h(PD|7), lim_p(PE|3);
Motor motor(pul, dir, ena, lim_h, lim_p, true);  // inverted = true

vec<GPIO> LEDs = {PB|2, PE|0, PB|7, PA|4};
GPIO &front = LEDs[3], &side = LEDs[2], &front2 = LEDs[1], &side2 = LEDs[0];

//GPIO trigger(PC|6);
//Scanner scanner({Projector(LEDs[3], LEDs[2]), Projector(LEDs[1], LEDs[0])}, motor, trigger);
Scanner scanner({Projector(LEDs[3], LEDs[2]), Projector(LEDs[1], LEDs[0])}, motor, {GPIO(PC|6), GPIO(PD|6)});

//USB usb;
Kernel k;
UART term;

uint8_t button = 0;
bool debounce = false;
bool homed = false;
bool moving = false;
uint32_t offset = 0;

void job()
{
    if (mode_sel.read() == 0) return;

    if (offset == 0) offset = motor.speed() / 5;

    motor.move(button ? -offset : offset);
    motor.step();
}

void repeat()
{
    if (btn.at(button).read() == 0)
    {
        job();
        aos::schedule(199*ms, repeat);
    }
}

void on_debounce()
{
    debounce = false;
    aos::schedule(49*ms, repeat);
}

void push(uint8_t b)
{
    if (!debounce)
    {
        button = b;
        job();
        debounce = true;
        aos::schedule(150*ms, on_debounce);
    }
}

void mem()
{
    term.printf("Free memory: %d bytes\n", aos::memory());
}

int step(const vec<str>& args, Connection* cout, Connection* debug)
{
    if (args.size() < 1) return APP_TOO_FEW_ARGS;
    if (args.size() > 1) return APP_TOO_MANY_ARGS;

    if (args.front() == "f") motor.forward(true);
    else if (args.front() == "b") motor.forward(false);
    else return APP_INVALID_ARGS;

    motor.step();

    return APP_DONE;
}

int move(const vec<str>& args, Connection* cout, Connection* debug)
{
    if (args.size() < 1) return APP_TOO_FEW_ARGS;
    if (args.size() > 1) return APP_TOO_MANY_ARGS;

    try {
        int dist = stoi(args.front(), 0, 0);

        if (dist)
        {
            motor.move(dist);
            if (cout) cout->print("moving\n");
            if (debug) debug->printf("new target: %d\n", motor.target());
            moving = true;
        }
        else {
            if (cout) cout->print("ready\n");
            if (debug) debug->printf(YELLOW_"zero distance"_RESETn);
        }
    }
    catch (std::exception& ex) { return APP_INVALID_ARGS; }

    return APP_DONE;
}

int home(const vec<str>& args, Connection* cout, Connection* debug)
{
    if (args.size() > 0) return APP_TOO_MANY_ARGS;

    if (cout) cout->print("homing\n");
    else if (debug) debug->print("homing\n");

    motor.home();
    homed = false;

    return APP_DONE;
}

// Make similar config app as follows:
//config m 16 s 400 p1 +h p2 - p3 u
int config(const vec<str>& args, Connection* cout, Connection* debug)
{
    if (args.size() == 1) {
        try {
            scanner.fps(stoi(args.front()));
        }
        catch (std::exception& ex) { return APP_INVALID_ARGS; }
        scanner.print_status();
        return APP_DONE;
    }

    if (args.size() % 2 != 0) return APP_INVALID_ARGS;
    if (args.size() == 0) return APP_TOO_FEW_ARGS;

    vec<const str>::iterator it = args.begin();
    for (; it != args.end(); it+=2) {
        if (it[0][0] == 'm') {
            try {
                scanner.interval(stoi(it[1]));
            }
            catch (std::exception& ex) { return APP_INVALID_ARGS; }
        }
        else if (it[0][0] == 's') {
            try {
                scanner.fps(stoi(it[1]));
            }
            catch (std::exception& ex) { return APP_INVALID_ARGS; }
        }
        else if (it[0][0] == 'p') {
            str p{*it};
            int pos;
            try {
                pos = stoi(p.erase(0,1));
            }
            catch (std::exception& ex) { return APP_INVALID_ARGS; }

            if (pos > scanner.projs.size()) {
                if (cout) cout->printf("P%d not declared\n", pos);
                else if (debug) debug->printf("Only %d projectors declared\n", scanner.projs.size());
                continue;
            }
            Projector &proj = scanner.projs.at(pos-1);
            vec<const char>::iterator char_it = (it+1)->begin();
            for (; char_it != (it+1)->end(); char_it++) {
                if (*char_it == 'u') {
//                    proj.pattern = 1000; // us
                    proj.pattern = 500;
                    proj.white = 500; // us
                }
                else if (*char_it == 'h') {
                    proj.pattern = 1500; // us
                    proj.white = 1200; // us
                }
                else if (*char_it == 'o') {
                    proj.pattern = 2400; // us
                    proj.white = 1100; // us
                }
                else if (*char_it == 'm') {
                    proj.pattern = 2500; // us
                    proj.white = 2000; // us
                }
                else if (*char_it == 'v') {
                    proj.pattern = 3750; // us
                    proj.white = 3000; // us
                }
                else if (*char_it == '+') proj.enabled = true;
                else if (*char_it == '-') proj.enabled = false;
                else {
                    if (cout) cout->printf("Unrecognized argument for projector %s\n", char_it[0]);
                    else if (debug) debug->printf("Unrecognized argument for projector %s\n", char_it[0]);
                }
            }
        }
        else {
            if (cout) cout->printf("Unrecognized argument %s\n", it[0]);
            else if (debug) debug->printf("Unrecognized argument %s\n", it[0]);
        }

    }
    scanner.print_status();

    return APP_DONE;
}


int preset(const vec<str>& args, Connection* cout, Connection* debug)
{
    if (args.size() > 1) return APP_TOO_MANY_ARGS;
    if (args.size() == 0) return APP_TOO_FEW_ARGS;

    char p = args.front()[0];

    if (p == 'u') {
        pattern = pattern_u;
        white = white_u;
        fps = fps_u;
        delay = delay_ref / 2;  // 30 us
        interval = interval_ref;
    }
    else if (p == 'h') {
        pattern = pattern_h;
        white = white_h;
        fps = fps_h;
        delay = delay_ref;
        interval = interval_ref;  // 150 um
    }
    else if (p == 'o') {
        pattern = pattern_o;
        white = white_o;
        fps = fps_o;
        delay = delay_ref / 2;  // 30 us
        interval = interval_ref;  // 150 um
    }
    else if (p == 'm') {
        pattern = pattern_m;
        white = white_m;
        fps = fps_m;
        delay = delay_ref;
        interval = interval_ref * 4 / 3;  // 200 um
    }
    else if (p == 'v') {
        pattern = pattern_v;
        white = white_v;
        fps = fps_v;
        delay = delay_ref;
        interval = interval_ref * 5 / 3;  // 250 um
    }
    else return APP_INVALID_ARGS;

    cout->printf("preset - %s\n", args.front().c_str());

    return APP_DONE;
}

int status(const vec<str>& args, Connection* cout, Connection* debug)
{
    if (args.size() > 0) return APP_TOO_MANY_ARGS;

    scanner.print_status();

    return APP_DONE;
}

int scan(const vec<str>& args, Connection* cout, Connection* debug)
{
    if (args.size() > 1) return APP_TOO_MANY_ARGS;
    if (args.size() == 1) count = stoi(args.front());

    if (count < 1) return APP_INVALID_ARGS;

    left = count;
//    mode = SCANNING;
    scanner.schedule(left);
    scanner.start(SCANNER_RUN);

    cout->print("scanning...\n");

    return APP_DONE;
}

int calibrate(const vec<str>& args, Connection* cout, Connection* debug)
{
    if (args.size() > 3) return APP_TOO_MANY_ARGS;
    if (args.size() == 1) count = stoi(args.front());
    if (args.size() == 2) {
        count = stoi(args.front());
        reps = stoi(args.back());
    }

    if (count < 1 or reps < 1) return APP_INVALID_ARGS;

    left = count;
//    mode = CALIBRATING;
    scanner.schedule(left);
    scanner.start(SCANNER_CALIB, 0, reps);

    cout->print("calibrating...\n");

    return APP_DONE;
}

void stop()
{
//    front.set();
//    side.set();
//    trigger.set();
//    mode = WAIT;
    scanner.stop(true); // reset=true

}


void setup()
{
    term.init(0, 115200);
    term.emulate(true);

    if (term.initialized())
        term.printf("\r\nBoot reason: %s\n", aos::boot_reason_str())->flush();

//    usb.init();
//    usb.continuous(true);

    mode_gnd.init(OUTPUT, LOW);
    mode_sel.init(INPUT_PULLUP);

    for (auto& l : led) l.init(OUTPUT);
    for (auto& d : debug) d.init(OUTPUT);
    for (auto& s : sw) s.init(INPUT_PULLUP);
    for (auto& b : btn) b.init(INPUT_PULLUP, FALLING_EDGE);

    btn.at(0).IntAction = [&]() { push(0); };
    btn.at(1).IntAction = [&]() { push(1); };

    scanner.init(1, 2);
    motor.init(3, 4);
    // gnd_h.init(OUTPUT, LOW); // rods stage
    // gnd_p.init(OUTPUT, LOW);

    /* Build Kernel */

    k.init("lin_stage", rwx);

    k.add(tAct(mem), "mem", __x, "", "Check free memory");
    k.add(tAct(aos::terminate), "terminate", __x, "", "Terminate the program");

    k.add(&pattern, "pattern", rw_, "", "");
    k.add(&white, "white", rw_, "", "");
    k.add(&delay, "delay", rw_, "", "");
    k.add(&count, "count", rw_, "", "");
    k.add(&left, "left", rw_, "", "");
    k.add(&fps, "fps", rw_, "", "");
    k.add(&interval, "interval", rw_, "", "");
    k.add(&amb_mul, "amb_mul", rw_, "", "");
    k.add(&settle, "settle", rw_, "", "");
    k.add(tApp(scan), "scan", __x, "[frames]", "Start Scan");
    k.add(tApp(config), "config", __x, "[m] [interval] [s] [fps] [p] [preset]", "Set Configurations");
    k.add(tApp(status), "status", __x, "Return Scanner Status");
    k.add(tApp(preset), "preset", __x, "h|m|v", "Scanning Preset");
    k.add(tApp(calibrate), "calibrate", __x, "[steps]", "Start Calibration");
    k.add(tAct(stop), "stop", __x, "", "Cancel Scan/Calibration");

    k.add_device(&led.at(0), "led.r");
    k.add_device(&led.at(1), "led.g");
    k.add_device(&led.at(2), "led.b");
    k.add_device(&btn.at(0), "btn.0");
    k.add_device(&btn.at(1), "btn.1");
    k.add_device(&sw.at(0), "sw0");
    k.add_device(&sw.at(1), "sw1");

    k.add_device(&scanner, "s");
    k.add_device(&motor, "m");

    for(int i = 0; i < debug.size(); i++)
        k.add_device(&debug.at(i), "d." + std::to_string(i));

//    k.add_device(&usb, "u");
    k.add_device(&term, "t");
    k.add(tApp(step), "step", __x, "f|b", "Force motor step");
    k.add(tApp(move), "move", __x, "(-)n_steps", "Move by n_steps");
    k.add(tApp(home), "home", __x, "", "Re-home the motor");
    k.add(&offset, "offset", rw_, "", "Button press offset");

    /* Debugging */

    term.debug = &term;
    scanner.term = &term;
//    usb.debug = &term;

//    term.int_debug = &debug.at(2);
    scanner.debug = &debug.at(0);
    scanner.error = &debug.at(1);
    scanner.tmr = &debug.at(2);
    k.loop_debug = &debug.at(3);

    /* Interrupt Priorities */

    scanner.priority(2); // uses two slots
    motor.priority(1);
//    usb.priority(4);
    term.priority(5);
    GPIO::PortPriority(PF, 7); // buttons

    motor.start(24); // 24 / 24 = 1 kHz (0.5 ips)
    scanner.start(30); // 30 fps
}

void loop()
{
    if (!homed) if (homed = motor.homed())
    {
//        usb.print("homed\n");
        term.print("homed\n");
    }

    if (moving && (motor.pos() == motor.target()))
    {
        moving = false;
//        usb.print("ready\n");
        term.print("ready\n");
    }

    red.write(motor.park_stop());
    green.write(motor.home_stop());

    scanner.update();
}

int main(void)
{
    aos::init(true); // Configure system clocks and timers

    while(1) // MCU can never stop (reboot on exception)
    {
        k.run(setup, loop, &term); // Exits on exception
    }
}
