#include <Arduino.h>
#include <Wire.h>          // I2C communication
#include <Preferences.h>   // Save settings in ESP32 memory
#include <ssd1306.h>
#include <ssd1306_fonts.h>
#include <DS3231.h>
#include <a21/ec11.hpp>
#include <esp_timer.h>
#include <cstdio>
#include "main.h"

namespace {
    State currentState = State::STARTUP;  // The current clock mode

    //Alarm data for 3 alarms and a temporary alarm objects
    AlarmConfig alarms[3];
    AlarmConfig tempAlarm {}; // Used for alarm menu data

    uint8_t currentAlarmIndex = 0;  // Index of the currently selected alarm (0, 1, or 2)

    a21::EC11 encoder; // Instance of encoder class

    DS3231 rtc(Wire);

    // Time reported by the RTC during last synchronization
    uint32_t baseEpoch = 0;

    // ESP32 timer value during last synchronization
    int64_t baseTimerUs = 0;

    // Time shared by the display, alarms, and menu
    ClockTime currentTime {};
    ClockTime tempTime {}; // Used for clocktime menu data

    MenuState currentMenu = MenuState::MAIN_MENU;

    int menuIndex = 0;
    int selectedAlarm = 0;


    constexpr int64_t ONE_DAY_US =
        24LL * 60LL * 60LL * 1000000LL;

    constexpr uint8_t I2C_SCL_PIN = 0;
    constexpr uint8_t I2C_SDA_PIN = 1;
    constexpr uint8_t ENCODER_A_PIN = 2;
    constexpr uint8_t ENCODER_B_PIN = 4;
    constexpr uint8_t ENCODER_BUTTON_PIN = 5;
    constexpr uint8_t BUZZER_PIN = 6;
    constexpr uint8_t LIGHT_SENSOR_PIN = 7;
    constexpr uint8_t SNOOZE_BUTTON_PIN = 15;
    constexpr uint8_t STOP_BUTTON_PIN = 16;
    constexpr uint8_t MENU_BUTTON_PIN = 17;
    constexpr uint8_t BACK_BUTTON_PIN = 18;
    constexpr uint32_t DEBOUNCE_MS = 25;

    struct DebouncedButton {
        uint8_t pin;
        bool stableState;
        bool previousReading;
        uint32_t changedAtMs;
    };

    DebouncedButton encoderButton {ENCODER_BUTTON_PIN, HIGH, HIGH, 0};
    DebouncedButton snoozeButton {SNOOZE_BUTTON_PIN, HIGH, HIGH, 0};
    DebouncedButton stopButton {STOP_BUTTON_PIN, HIGH, HIGH, 0};
    DebouncedButton menuButton {MENU_BUTTON_PIN, HIGH, HIGH, 0};
    DebouncedButton backButton {BACK_BUTTON_PIN, HIGH, HIGH, 0};

    bool buttonWasPressed(DebouncedButton& button) {
        const uint32_t nowMs = millis();
        const bool reading = digitalRead(button.pin);

        if (reading != button.previousReading) {
            button.previousReading = reading;
            button.changedAtMs = nowMs;
        }

        if (nowMs - button.changedAtMs < DEBOUNCE_MS ||
            reading == button.stableState) {
            return false;
        }

        button.stableState = reading;
        return button.stableState == LOW;
    }
}

void pinDidChange() {
  encoder.checkPins(digitalRead(ENCODER_A_PIN), digitalRead(ENCODER_B_PIN));
}

// Start all currently defined hardware interfaces.
void initializeHardware() {
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Wire.setClock(400000);

    initializeRtc();
    initializeDisplay();
    initializeEncoder();
    initializeButtons();
    initializeBuzzer();
    initializeLightSensor();
}

void initializeRtc() {
    // The NorthernWidget DS3231 object uses the Wire bus initialized above.
}

void initializeDisplay() {
    // Wire is already initialized, so -1 keeps the existing bus pin selection.
    ssd1306_128x64_i2c_initEx(-1, -1, 0x3C);
    ssd1306_setFixedFont(ssd1306xled_font6x8);
    ssd1306_clearScreen();
}

void initializeEncoder() {
    pinMode(ENCODER_A_PIN, INPUT_PULLUP);
    pinMode(ENCODER_B_PIN, INPUT_PULLUP);
    pinMode(ENCODER_BUTTON_PIN, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(ENCODER_A_PIN), pinDidChange, CHANGE);
    attachInterrupt(digitalPinToInterrupt(ENCODER_B_PIN), pinDidChange, CHANGE);
    
}

void initializeButtons() {
    pinMode(SNOOZE_BUTTON_PIN, INPUT_PULLUP);
    pinMode(STOP_BUTTON_PIN, INPUT_PULLUP);
    pinMode(MENU_BUTTON_PIN, INPUT_PULLUP);
    pinMode(BACK_BUTTON_PIN, INPUT_PULLUP);
}

void initializeBuzzer() {
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
}

void initializeLightSensor() {
    pinMode(LIGHT_SENSOR_PIN, INPUT);
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
            break;
    }
}

// Run once when the ESP32 starts
void setup() {
    initializeHardware();

    synchronizeWithRtc();
    currentTime = readCurrentTime();

    enterState(State::RUNNING);
}

// Run repeatedly while the clock is powered on
void loop() {
    serviceRtcSynchronization();
    serviceClock();

    // Poll inputs
    EncoderEvent encoderEvent = readEncoderEvent();
    ButtonEvent buttonEvent = readButtonEvent();

   

    // Button handling will be added as the Menu and Alarm states are implemented.
    static_cast<void>(buttonEvent);

     switch (currentState) {
        case State::STARTUP:
            break;
        case State::RUNNING:
            break;
        case State::MENU:

            switch (currentMenu) {

                case MenuState::MAIN_MENU:
                // Handle encoder events to navigate the main menu
                    if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex++;
                        if(menuIndex > 1) {
                            menuIndex = 0;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex--;
                        if(menuIndex < 0) {
                            menuIndex = 1;
                        }
                    }
                    if (encoderEvent == EncoderEvent::PRESSED) {
                        if (menuIndex == 0) {
                            currentMenu = MenuState::CLOCK_MENU;
                        } else if (menuIndex == 1) {
                            currentMenu = MenuState::ALARM_SELECT;
                        }
                    }
                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                    }
                    break;

                case MenuState::CLOCK_MENU:
                   
                    break;

                case MenuState::ALARM_SELECT:
                    if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex++;
                        if(menuIndex > 2) {
                            menuIndex = 0;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex--;
                        if(menuIndex < 0) {
                            menuIndex = 2;
                        }
                    }
                    if (encoderEvent == EncoderEvent::PRESSED) {
                        selectedAlarm = menuIndex;
                        tempAlarm = alarms[selectedAlarm];
                        currentMenu = MenuState::ALARM_MENU;
                        menuIndex = 0;
                       
                    }

                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::MENU;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                    }
                    break;

                case MenuState::ALARM_MENU:
                    if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex++;
                        if(menuIndex > 4) {
                            menuIndex = 0;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex--;
                        if(menuIndex < 0) {
                            menuIndex = 4;
                        }
                    }
                    if (encoderEvent == EncoderEvent::PRESSED) {
                       if (menuIndex == 0) {
                        //Enabled state configuration
                        } else if (menuIndex == 1) {
                        //Time state configuration
                        } else if (menuIndex == 2) {
                        // Date state configuration
                        } else if (menuIndex == 3) {
                        // Type state configuration   
                        } else if (menuIndex == 4) {
                        //Snooze state configuration
                        }
                    }

                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        currentMenu = MenuState::ALARM_SELECT;
                        currentState = State::MENU;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                    }                
                    break;

            }

            break;
        case State::ALARM_RINGING:
            break;
    }

    updateCurrentState();
    updateDisplay();
    updateBrightness();
}

// Reads hardware RTC and saves both reference values
void synchronizeWithRtc() {
    DateTime rtcTime = RTClib::now();

    baseEpoch = rtcTime.unixtime();
    baseTimerUs = esp_timer_get_time();
}

// Calculate current time
uint32_t calculateCurrentEpoch() {
    int64_t elapsedUs = esp_timer_get_time() - baseTimerUs;
    uint32_t elapsedSeconds = elapsedUs / 1000000LL;

    return baseEpoch + elapsedSeconds;
}

// Returns calculated software time
ClockTime readCurrentTime() {
    DateTime now(calculateCurrentEpoch());

    return {
        now.year(),
        now.month(),
        now.day(),
        now.hour(),
        now.minute(),
        now.second(),
    };
}

// Write a user-confirmed time to the RTC and restart the software-time anchor.
void setCurrentTime(const ClockTime& time) {
    const DateTime updated(
        time.year,
        time.month,
        time.day,
        time.hour,
        time.minute,
        time.second
    );

    rtc.adjust(updated);
    baseEpoch = updated.unixtime();
    baseTimerUs = esp_timer_get_time();
    currentTime = time;
}

// Refresh the shared time without accessing the RTC hardware.
void serviceClock() {
    static uint32_t lastUpdateMs = 0;
    const uint32_t nowMs = millis();

    if (nowMs - lastUpdateMs < 100) {
        return;
    }

    lastUpdateMs = nowMs;
    currentTime = readCurrentTime();
}

// Correct the software clock against the RTC once per day.
void serviceRtcSynchronization() {
    int64_t timeSinceSync =
        esp_timer_get_time() - baseTimerUs;

    if (timeSinceSync >= ONE_DAY_US) {
        synchronizeWithRtc();
    }
}
/*
EncoderEvent readEncoderEvent() {
    if (buttonWasPressed(encoderButton)) {
        return EncoderEvent::PRESSED;
    }

    // Each valid quadrature transition contributes one quarter-step.
    static uint8_t previousState = 0b11;
    static int8_t movement = 0;
    static constexpr int8_t TRANSITION_TABLE[16] = {
         0, -1,  1,  0,
         1,  0,  0, -1,
        -1,  0,  0,  1,
         0,  1, -1,  0
    };

    const uint8_t currentEncoderState =
        (digitalRead(ENCODER_A_PIN) << 1) |
        digitalRead(ENCODER_B_PIN);
    const uint8_t transition =
        (previousState << 2) | currentEncoderState;

    previousState = currentEncoderState;
    movement += TRANSITION_TABLE[transition];

    if (movement >= 4) {
        movement = 0;
        return EncoderEvent::CLOCKWISE;
    }

    if (movement <= -4) {
        movement = 0;
        return EncoderEvent::COUNTER_CLOCKWISE;
    }

    return EncoderEvent::NONE;
}
*/

ButtonEvent readButtonEvent() {
    // Stop has highest priority when more than one button is pressed.
    if (buttonWasPressed(stopButton)) {
        return ButtonEvent::STOP_PRESSED;
    }
    if (buttonWasPressed(snoozeButton)) {
        return ButtonEvent::SNOOZE_PRESSED;
    }
    if (buttonWasPressed(menuButton)) {
        return ButtonEvent::MENU_PRESSED;
    }
    if (buttonWasPressed(backButton)) {
        return ButtonEvent::BACK_PRESSED;
    }

    return ButtonEvent::NONE;
}

// Display shared time
void updateDisplay() {
    static uint32_t lastDisplayMs = 0;
    const uint32_t nowMs = millis();

    if (nowMs - lastDisplayMs < 100) {
        return;
    }

    lastDisplayMs = nowMs;

    char timeText[9];
    snprintf(
        timeText,
        sizeof(timeText),
        "%02u:%02u:%02u",
        static_cast<unsigned>(currentTime.hour),
        static_cast<unsigned>(currentTime.minute),
        static_cast<unsigned>(currentTime.second)
    );

    char dateText[11];
    snprintf(
        dateText,
        sizeof(dateText),
        "%02u/%02u/%04u",
        static_cast<unsigned>(currentTime.month),
        static_cast<unsigned>(currentTime.day),
        static_cast<unsigned>(currentTime.year)
    );

    ssd1306_printFixed(0, 0, timeText, STYLE_NORMAL);
    ssd1306_printFixed(0, 16, dateText, STYLE_NORMAL);
}

void updateBrightness() {
    // Brightness calibration and manual override behavior remain to be defined.
}
