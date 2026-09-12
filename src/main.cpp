#include <Arduino.h>
#include <Wire.h>          // I2C communication
#include <Preferences.h>   // Save settings in ESP32 memory
#include <ssd1306.h>
#include <ssd1306_fonts.h>
#include <DS3231.h>
#include <a21/ec11.hpp>
#include "main.h"

namespace {
    State currentState = State::STARTUP;  // The current clock mode
}

// Change the current clock mode
void enterState(State newState) {
    currentState = newState;
}

// Run the behavior for the current clock mode
void updateCurrentState() {
    switch (currentState) {
        case State::STARTUP:        // Start hardware and load settings
        case State::RUNNING:        // Show time and check the alarm
        case State::MENU:           // Handle menu input
        case State::ALARM_RINGING:  // Handle the active alarm
        case State::SNOOZED:        // Wait for snooze to finish
            break;
    }
}

// Run once when the ESP32 starts
void setup() {
    enterState(State::RUNNING);
}

// Run repeatedly while the clock is powered on
void loop() {
    updateCurrentState();
}