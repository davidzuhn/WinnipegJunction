/* Winnipeg Junction
 *
 * This package implements the interlocking plant for 
 * Winnipeg Junction on Joe Binish's Central of Minnesota
 * layout.
 *
 * It's designed for a cpShield and an IOX32 expansion board.
 * 
 * No external communication is needed -- this is a standalone
 * program.
 *
 * Copyright 2015 by david d zuhn <zoo@whitepineroute.org>
 *
 * This work is licensed under the Creative Commons Attribution-ShareAlike
 * 4.0 International License. To view a copy of this license, visit
 * http://creativecommons.org/licenses/by-sa/4.0/deed.en_US.
 *
 * You may use this work for any purposes, provided that you make your
 * version available to anyone else.  
 */

#include <Metro.h>

#include <Wire.h>
#include <MCP23017.h>
#include <IOLine.h>
#include <SignalHead2.h>

/* number of seconds that all lights are on once power is applied, in seconds */
#define INITIAL_TIME 15

/* number of seconds that the plant will remain in STOP until the signals are displayed
   to match the position of the turnouts, in seconds */
#define TIMEWAIT_TIME 4


// state machine status values

enum PLANT_STATE { INITIALIZING,         // board powers on -- all lights lit, no controls active
		   ACTIVE,               // plant displays signals based on turnout position, checks for plant unlock
		   NEEDS_SETTING,        // 
		   CHANGING, TIMEWAIT 
                 };

PLANT_STATE currentState = INITIALIZING;

void changeState(PLANT_STATE newState);

Metro initializingTimer(15 * 1000);

Metro timewaitTimer(5 * 1000);


#define T1             0
#define T2             1
#define T3             2
#define T4             3

#define D1             0
#define D2             1
#define D3             2
#define D4             3
#define D5             4

#define LEVER1         0
#define LEVER2         1
#define LEVER3         2
#define LOCK1          3
#define LOCK2          4
#define LOCK3          5
#define LOCK_MASTER    6
#define MANUAL_SWITCH  7


IOLine *turnout_out[] = {
    new IOX(0x21, 1, 2, OUTPUT),        // T_OUT_1
    new IOX(0x21, 1, 3, OUTPUT),        // T_OUT_2
    new IOX(0x21, 1, 4, OUTPUT),        // T_OUT_3
    new IOX(0x21, 1, 5, OUTPUT) // T_OUT_4
};

IOLine *turnout_read[] = {
    new Pin(9, INPUT_PULLUP),   // T_IN_1
    new Pin(10, INPUT_PULLUP),  // T_IN_2
    new Pin(11, INPUT_PULLUP),  // T_IN_3
    new Pin(12, INPUT_PULLUP)   // T_IN_4
};



#define TURNOUT_COUNT NELEMENTS(turnout_out)

IOLine *lever[] = {
    new Pin(1, INPUT_PULLUP),   // LEVER1
    new Pin(3, INPUT_PULLUP),   // LEVER2
    new Pin(5, INPUT_PULLUP)    // LEVER3
};

#define LEVER_COUNT NELEMENTS(lever)

IOLine *lock[] = {
    new Pin(2, INPUT_PULLUP),   // LOCK1
    new Pin(4, INPUT_PULLUP),   // LOCK2
    new Pin(6, INPUT_PULLUP),   // LOCK3
    new Pin(7, INPUT_PULLUP)    // LOCK__MASTER
};

#define LOCK_COUNT NELEMENTS(lock)

IOBounce inputs[LEVER_COUNT + LOCK_COUNT + 1];  //  +1 to account for manual switch control 
IOBounce points[TURNOUT_COUNT];

#define INPUT_COUNT NELEMENTS(inputs)



SignalHead2 *dwarf[] = {
    new SignalHead2(new IOX(0x21, 0, 0, OUTPUT),
                    new IOX(0x21, 0, 1, OUTPUT)),
    new SignalHead2(new IOX(0x21, 0, 2, OUTPUT),
                    new IOX(0x21, 0, 3, OUTPUT)),
    new SignalHead2(new IOX(0x21, 0, 4, OUTPUT),
                    new IOX(0x21, 0, 5, OUTPUT)),
    new SignalHead2(new IOX(0x21, 0, 6, OUTPUT),
                    new IOX(0x21, 0, 7, OUTPUT)),
    new SignalHead2(new IOX(0x21, 1, 0, OUTPUT),
                    new IOX(0x21, 1, 1, OUTPUT)),
};

#define DWARF_COUNT NELEMENTS(dwarf)

#define S1_U 0
#define S1_L 1
#define S2_U 2
#define S2_L 3
#define S3_U 4
#define S3_L 5


SignalHead2 *mast[] = {
    new SignalHead2(new IOX(0x20, 0, 0, OUTPUT),
                    new IOX(0x20, 0, 1, OUTPUT)),       // S1_U
    new SignalHead2(new IOX(0x20, 0, 2, OUTPUT),
                    new IOX(0x20, 0, 3, OUTPUT)),       // S1_L
    new SignalHead2(new IOX(0x20, 0, 4, OUTPUT),
                    new IOX(0x20, 0, 5, OUTPUT)),       // S2_U
    new SignalHead2(new IOX(0x20, 0, 6, OUTPUT),
                    new IOX(0x20, 0, 7, OUTPUT)),       // S2_L
    new SignalHead2(new IOX(0x20, 1, 0, OUTPUT),
                    new IOX(0x20, 1, 1, OUTPUT)),       // S3_U
    new SignalHead2(new IOX(0x20, 1, 2, OUTPUT),
                    new IOX(0x20, 1, 3, OUTPUT)),       // S3_L
};

#define MAST_COUNT NELEMENTS(mast)


IOLine *manual_switch = new Pin(8, INPUT_PULLUP);


int pin[] = { 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, A0, A1, A2, A3, A4, A5 };
#define PIN_COUNT (sizeof(pin)/sizeof(pin[0]))



/*
 * configure all of the I/O lines used by this sketch
 *
 * Input lines are configured as such
 * Output lines are as well
 *
 * Lights are all LIT UP as a demonstration / test indication
 *
 */

void initialize_iolines()
{
    // shut off all of the arduino local pins 
    for (int i = 0; i < PIN_COUNT; i++) {
        pinMode(pin[i], OUTPUT);
        digitalWrite(pin[i], LOW);
    }

    // for each turnout, we have an output and an input line
    for (int i = 0; i < TURNOUT_COUNT; i++) {
        turnout_out[i]->init();
        turnout_read[i]->init();
    }

    // 4 levers -- one for each of three switches, plus the master plant lock lever
    for (int i = 0; i < LEVER_COUNT; i++) {
        lever[i]->init();
    }

    // 5 dwarf signals -- light 'em up
    for (int i = 0; i < DWARF_COUNT; i++) {
        dwarf[i]->init();
        dwarf[i]->setIndication(SignalHead2::LIT_UP_LIKE_THE_SUN);
    }

    // 3 masts (each with 2 heads -- light 'em all up
    for (int i = 0; i < MAST_COUNT; i++) {
        mast[i]->init();
        mast[i]->setIndication(SignalHead2::LIT_UP_LIKE_THE_SUN);
    }

    // one key switch for the industry turnout
    manual_switch->init();

    // An IOBounce is configured for each of the levers and 
    // the manual switch control
    for (int i = 0; i < INPUT_COUNT; i++) {
        inputs[i] = IOBounce();
    }
    inputs[LEVER1].attach(lever[0]);
    inputs[LEVER2].attach(lever[1]);
    inputs[LEVER3].attach(lever[2]);

    inputs[LOCK1].attach(lock[0]);
    inputs[LOCK2].attach(lock[1]);
    inputs[LOCK3].attach(lock[2]);

    inputs[LOCK_MASTER].attach(lock[3]);

    inputs[MANUAL_SWITCH].attach(manual_switch);


    // and 4 bounce detectors are set up for the
    // turnout position indicators
    for (int i = 0; i < TURNOUT_COUNT; i++) {
        points[i] = IOBounce();
        points[i].attach(turnout_read[i]);
    }
}


void read_initial_turnout_position()
{
    for (int i = 0; i < TURNOUT_COUNT; i++) {
        bool state = turnout_read[i]->digitalRead();
        turnout_out[i]->digitalWrite(state);
    }

}



void setup()
{
    currentState = INITIALIZING;

    initialize_iolines();

    read_initial_turnout_position();


}


void check_initial_timer()
{
    if (initializingTimer.check()) {
        changeState(NEEDS_SETTING);
    }
}

void check_timewait_timer()
{
    if (timewaitTimer.check()) {
        changeState(NEEDS_SETTING);
    }
}


void set_all_stop()
{
    for (int i = 0; i < DWARF_COUNT; i++) {
        dwarf[i]->setIndication(SignalHead2::STOP);
    }
    for (int i = 0; i < MAST_COUNT; i++) {
        mast[i]->setIndication(SignalHead2::STOP);
    }
}





void changeState(PLANT_STATE newState)
{
    switch (currentState) {
    case INITIALIZING:
        if (newState == NEEDS_SETTING) {
            currentState = newState;
            return;
        }
        break;
    case CHANGING:
        if (newState == TIMEWAIT) {
            timewaitTimer.reset();
            currentState = TIMEWAIT;
        }
        break;
    case TIMEWAIT:
        if (newState == NEEDS_SETTING) {
            currentState = NEEDS_SETTING;
        }
        break;
    case ACTIVE:
        if (newState == CHANGING) {
	    currentState = CHANGING;
            set_all_stop();
        }
        break;
    case NEEDS_SETTING:
	if (newState == ACTIVE) {
	    currentState = ACTIVE;
	}
	break;
    }
}


void check_master_lock() 
{
    bool changed = inputs[LOCK_MASTER].update();

    if (changed) {
        if (inputs[LOCK_MASTER].read() == 0) {
            changeState(TIMEWAIT);
        } else {
            changeState(CHANGING);
        }
    }
}

void check_inputs()
{
    bool changed[INPUT_COUNT];

    // first, update the bounce state on every input we're looking at
    for (int i = 0; i < INPUT_COUNT; i++) {
        changed[i] = inputs[i].update();
    }

    // then check the key lock for T2
    if (changed[MANUAL_SWITCH]) {
        turnout_out[T2]->digitalWrite(inputs[MANUAL_SWITCH].read());
    }
    // next, check the tower levers....

    if (changed[LEVER1] && currentState == CHANGING) {
        // off = normal, on=pulled
        if (inputs[LOCK1].read()) {
            turnout_out[T1]->digitalWrite(inputs[LEVER1].read());
        }
    }

    if (changed[LEVER2] && currentState == CHANGING) {
        // off = normal, on=pulled
        if (inputs[LOCK2].read()) {
            turnout_out[T3]->digitalWrite(inputs[LEVER2].read());
        }
    }


    if (changed[LEVER3] && currentState == CHANGING) {
        // off = normal, on=pulled
        if (inputs[LOCK3].read()) {
            turnout_out[T4]->digitalWrite(inputs[LEVER3].read());
        }
    }
}


void check_points(bool force)
{
    bool changed = false;

    for (int i = 0; i < TURNOUT_COUNT; i++) {
        if (points[i].update()) {
            changed = true;
        }
    }

    // configure all of the signals every time ANY turnout changes
    if (force || changed) {
        bool t1normal = points[T1].read();
        bool t2normal = points[T2].read();
        bool t3normal = points[T3].read();
        bool t4normal = points[T4].read();

        // D1 is tied to T1(N), D2 is tied to T1(R)
        dwarf[D1]->setIndication(t1normal ? SignalHead2::PROCEED : SignalHead2::STOP);
        dwarf[D2]->setIndication(!t1normal ? SignalHead2::PROCEED : SignalHead2::STOP);

        // D3 is tied to T2(R)
        dwarf[D3]->setIndication(!t2normal ? SignalHead2::PROCEED : SignalHead2::STOP);

        // D4 is tied to T3(R)
        dwarf[D4]->setIndication(!t3normal ? SignalHead2::PROCEED : SignalHead2::STOP);

        // D5 is tied to T4(N)
        dwarf[D5]->setIndication(t4normal ? SignalHead2::PROCEED : SignalHead2::STOP);

        // ****************************************************************

        // T1=N => Y over R, T1=R => R over Y
        mast[S1_U]->setIndication(t1normal ? SignalHead2::PROCEED : SignalHead2::STOP);
        mast[S1_L]->setIndication(SignalHead2::PROCEED);

        // T3=N && T4=R, Y over Y,   T3=R => R over Y   
        mast[S2_U]->setIndication((t3normal
                                   && !t4normal) ? SignalHead2::PROCEED : SignalHead2::STOP);
        mast[S2_L]->setIndication(SignalHead2::PROCEED);

        // T4=N => Y over Y, T4=R && T3=N && T2=N => R over Y, else R over R
        mast[S3_U]->setIndication(t4normal ? SignalHead2::PROCEED : SignalHead2::STOP);
        mast[S3_L]->setIndication(t4normal
                                  || (t3normal
                                      && t2normal) ? SignalHead2::PROCEED : SignalHead2::STOP);
    }

    if (force) {
	changeState (ACTIVE);
    }

}





/*
 * perform the long running work of the sketch 
 *
 * this is a non-blocking implememtation, so we have to check and see
 * what mode we're in now (currentState).
 *
 * based on the mode, we do something
 *
 */


void loop()
{
    // put your main code here, to run repeatedly:

    switch (currentState) {
    case INITIALIZING:
        check_initial_timer();
        break;
    case CHANGING:
	check_master_lock();
        check_inputs();
        break;
    case TIMEWAIT:
        // nothing can occur during timewait
        check_timewait_timer();
        break;
    case NEEDS_SETTING:
	check_points(true);
	break;
    case ACTIVE:
	check_master_lock();
        check_points(false);
        break;
    }
}
