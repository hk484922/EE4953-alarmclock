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
//currently have a bunch of test code in here 
namespace {
    State currentState = State::STARTUP;  // The current clock mode

    State previousState = State::STARTUP; // The previous clock mode, used for testing


    //Alarm data for 3 alarms and a temporary alarm objects
    AlarmConfig alarms[3];
    AlarmRuntime alarmRuntime[3];
    AlarmConfig tempAlarm {}; // Used for alarm menu data

    bool hasAlarmTriggered[3] = {false, false, false}; // Track if daily alarm has already triggered for the day

    bool displayNeedsUpdating = true; // This will determine when clear screen function is needed

    uint8_t currentAlarmIndex = 0;  // Index of the currently active alarm (0, 1, or 2)

    uint8_t previousMenuIndex = 0; // Track the previous menu index for testing

    a21::EC11 encoder; // Instance of encoder class

    DS3231 rtc(Wire);

    // Time reported by the RTC during last synchronization
    uint32_t baseEpoch = 0;

    // ESP32 timer value during last synchronization
    int64_t baseTimerUs = 0;

    unsigned long lastMenuInteractionTime = 0; // Track the last time the user interacted with the menu

    // Time shared by the display, alarms, and menu
    ClockTime currentTime {};
    ClockTime tempTime {}; // Used for clocktime menu data

    SystemSettings currentSystemSettings {TimeFormat::HOUR_24, false, 128}; // Default system settings

    MenuState currentMenu = MenuState::MAIN_MENU;
    
    MenuState previousMenu = MenuState::MAIN_MENU; // Track the previous menu state for testing

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
    constexpr uint8_t SNOOZE_BUTTON_PIN = 38;
    constexpr uint8_t STOP_BUTTON_PIN = 39;
    constexpr uint8_t MENU_BUTTON_PIN = 40;
    constexpr uint8_t BACK_BUTTON_PIN = 41;

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

struct Note {
    uint16_t frequency;
    uint16_t durationMs;
};

Note alarmTone1[] = {
    { 440, 500 }, // A4
    { 494, 500 }, // B4
    { 523, 500 }, // C5
    { 587, 500 }, // D5
    { 659, 500 }, // E5
    { 698, 500 }, // F5
    { 784, 500 }, // G5
    { 880, 500 }, // A5
};

Note alarmTone2[] = {
    { 880, 500 }, // A5
    { 784, 500 }, // G5
    { 698, 500 }, // F5
    { 659, 500 }, // E5
    { 587, 500 }, // D5
    { 523, 500 }, // C5
    { 494, 500 }, // B4
    { 440, 500 }, // A4
};

Note alarmTone3[] = {
    { 659, 400 }, // E5
    { 659, 400 }, // E5
    { 698, 400 }, // F5
    { 784, 400 }, // G5
    { 784, 400 }, // G5
    { 698, 400 }, // F5
    { 659, 400 }, // E5
    { 587, 400 }, // D5
    { 523, 400 }, // C5
    { 523, 400 }, // C5
    { 587, 400 }, // D5
    { 659, 400 }, // E5
    { 659, 600 }, // E5 (held slightly longer)
    { 587, 200 }, // D5
    { 587, 800 }, // D5 (held)
};

Note* alarmTones[] = {alarmTone1, alarmTone2, alarmTone3};

const size_t alarmToneLengths[] = {
    sizeof(alarmTone1) / sizeof(Note),
    sizeof(alarmTone2) / sizeof(Note),
    sizeof(alarmTone3) / sizeof(Note)
};

uint8_t currentToneIndex = 0; // Index of the current tone in the alarm sequence
uint8_t currentNoteIndex = 0; // Index of the current note in the current tone
ulong toneStartTime = 0; // Time when the current tone started playing
bool alarmSoundActive = false; // Flag to indicate if the alarm sound is currently active

void playAlarmNote() {
    uint16_t freq = alarmTones[currentToneIndex][currentNoteIndex].frequency;
    if (freq > 0){
        tone(BUZZER_PIN, freq);
    } 
    else noTone(BUZZER_PIN);
    toneStartTime = millis();
}

void startAlarmSound(AlarmTone tone) {
    currentToneIndex = static_cast<uint8_t>(tone);
    currentNoteIndex = 0;
    alarmSoundActive = true;
    playAlarmNote();
}

void updateAlarmSound() {
    if (!alarmSoundActive) return;
    if (millis() - toneStartTime >= alarmTones[currentToneIndex][currentNoteIndex].durationMs) {
        currentNoteIndex++;
        if (currentNoteIndex >= alarmToneLengths[currentToneIndex]) {
            currentNoteIndex = 0; // loop
        }
        playAlarmNote();
    }
}

void stopAlarmSound() {
    alarmSoundActive = false;
    noTone(BUZZER_PIN);
}

void startSnooze(uint8_t alarmIndex) {
    if (alarmIndex >= 3) {
        return;
    }

    uint8_t snoozeMinutes = alarms[alarmIndex].snoozeMinutes;
    if (snoozeMinutes < MIN_SNOOZE_MINUTES) {
        snoozeMinutes = MIN_SNOOZE_MINUTES;
    } else if (snoozeMinutes > MAX_SNOOZE_MINUTES) {
        snoozeMinutes = MAX_SNOOZE_MINUTES;
    }

    AlarmRuntime& runtime = alarmRuntime[alarmIndex];
    runtime.snoozed = true;
    runtime.snoozeWakeEpoch = calculateCurrentEpoch() +
        static_cast<uint32_t>(snoozeMinutes) * 60U;
    if (runtime.snoozeCount < UINT8_MAX) {
        runtime.snoozeCount++;
    }
}

bool snoozeExpired(uint8_t alarmIndex, uint32_t nowEpoch) {
    if (alarmIndex >= 3 || !alarmRuntime[alarmIndex].snoozed) {
        return false;
    }

    // Signed subtraction keeps the comparison correct if uint32_t wraps.
    return static_cast<int32_t>(
        nowEpoch - alarmRuntime[alarmIndex].snoozeWakeEpoch
    ) >= 0;
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

    //commented out for now, as it was causing issues due to encoder not being attatched 
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

    Serial.begin(9600);
    //testing alarm melodies remove before final submission
    
    // startAlarmSound(AlarmTone::TONE_3);
    // currentState = State::ALARM_RINGING;
    // currentAlarmIndex = 0;
    
}

// Run repeatedly while the clock is powered on
void loop() {
    serviceRtcSynchronization();
    serviceClock();
    // Check scheduled alarms and snoozed alarms while no alarm is sounding.
    if (currentState != State::ALARM_RINGING) {
        const uint32_t currentEpoch = calculateCurrentEpoch();
        for (int i = 0; i < 3; i++) {
            if (!alarms[i].enabled && alarmRuntime[i].snoozed) {
                alarmRuntime[i] = AlarmRuntime {};
            }

            const bool scheduledAlarmDue = isAlarmDue(alarms[i], currentTime, i);
            const bool snoozedAlarmDue = alarms[i].enabled &&
                snoozeExpired(i, currentEpoch);

            if (scheduledAlarmDue || snoozedAlarmDue) {
                if (scheduledAlarmDue) {
                    hasAlarmTriggered[i] = true;
                    alarmRuntime[i].snoozeCount = 0;
                }
                if (snoozedAlarmDue) {
                    alarmRuntime[i].snoozed = false;
                    alarmRuntime[i].snoozeWakeEpoch = 0;
                }
                menuIndex = 0; // Reset menu index when alarm rings
                currentMenu = MenuState::MAIN_MENU; // Reset menu state when alarm rings
                currentState = State::ALARM_RINGING; // Change state to the alarm ringing state
                displayNeedsUpdating = true; // Call for screen to be updated
                currentAlarmIndex = i; // Set the current alarm index to the alarm that is ringing
                startAlarmSound(alarms[i].tone);
                break;
            }
        }
    }

    // Poll inputs
    EncoderEvent encoderEvent = readEncoderEvent();
    ButtonEvent buttonEvent = readButtonEvent();

    //testing
    if(currentState !=previousState) {
       switch(currentState) {
            case State::STARTUP:
                Serial.println("Entered STARTUP state");
                break;
            case State::RUNNING:
                Serial.println("Entered RUNNING state");
                break;
            case State::MENU:
                Serial.println("Entered MENU state");
                break;
            case State::ALARM_RINGING:
                Serial.println("Entered ALARM_RINGING state");
                break;
        }
        previousState = currentState; // Update previous state after handling the change
    }

    switch(encoderEvent) {
        case EncoderEvent::CLOCKWISE:
            Serial.println("Encoder turned clockwise");
            break;
        case EncoderEvent::COUNTER_CLOCKWISE:
            Serial.println("Encoder turned counter-clockwise");
            break;
        case EncoderEvent::PRESSED:
            Serial.println("Encoder button pressed");
            break;
        default:
            break;
    }

    switch(buttonEvent) {
        case ButtonEvent::SNOOZE_PRESSED:
            Serial.println("Snooze button pressed");
            break;
        case ButtonEvent::STOP_PRESSED:
            Serial.println("Stop button pressed");
            break;
        case ButtonEvent::MENU_PRESSED:
            Serial.println("Menu button pressed");
            break;
        case ButtonEvent::BACK_PRESSED:
            Serial.println("Back button pressed");
            break;
        default:
            break;
    }

    if(encoderEvent != EncoderEvent::NONE || buttonEvent != ButtonEvent::NONE) {
        lastMenuInteractionTime = millis(); // Reset the menu timeout timer on any interaction
    }
    if(isMenuTimedOut()) {
        currentMenu = MenuState::MAIN_MENU;
        menuIndex = 0;
        currentState = State::RUNNING;
        displayNeedsUpdating = true;
    }
    

    // Button handling will be added as the Menu and Alarm states are implemented.
    static_cast<void>(buttonEvent);

     switch (currentState) {
        case State::STARTUP:
            break;
        case State::RUNNING:

            if(buttonEvent == ButtonEvent::MENU_PRESSED) {
                menuIndex = 0; // Reset menu index when entering the menu
                currentMenu = MenuState::MAIN_MENU; // Reset menu state when entering the menu
                lastMenuInteractionTime = millis(); // Reset the menu timeout timer on entering the menu
                currentState = State::MENU;
                displayNeedsUpdating = true;
            }
            break;
        case State::MENU:

            switch (currentMenu) {

                case MenuState::MAIN_MENU:
                // Handle encoder events to navigate the main menu
                    if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex++;
                        displayNeedsUpdating = true;
                        if(menuIndex > 2) {
                            menuIndex = 0;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex--;
                        displayNeedsUpdating = true;
                        if(menuIndex < 0) {
                            menuIndex = 2;
                        }
                    }
                    if (encoderEvent == EncoderEvent::PRESSED) {
                        displayNeedsUpdating = true;
                        if (menuIndex == 0) {
                            tempTime = currentTime; // Load current time into tempTime for editing
                            currentMenu = MenuState::CLOCK_MENU;
                        } else if (menuIndex == 1) {
                            menuIndex = 0; // Reset menu index for alarm selection
                            currentMenu = MenuState::ALARM_SELECT;
                        } else if (menuIndex == 2) {
                            // Exit menu and return to running state
                            menuIndex = 0;
                            currentMenu = MenuState::SYSTEM_MENU;
                        }
                    }
                    // Handle button events to go back, typical for all instances
                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    }
                    // Handle button events to exit menu, typical for all instances
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    }
                    break;

                case MenuState::CLOCK_MENU:
                    if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex++;
                        displayNeedsUpdating = true;
                        if(menuIndex > 2) {
                            menuIndex = 0;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex--;
                        displayNeedsUpdating = true;
                        if(menuIndex < 0) {
                            menuIndex = 2;
                        }
                    }
                    if (encoderEvent == EncoderEvent::PRESSED) {
                        displayNeedsUpdating = true;
                        if (menuIndex == 0) {
                            //Time set configuration
                            menuIndex = 0;
                            currentMenu = MenuState::CLOCK_TIME;
                            currentState = State::MENU;  

                        } else if (menuIndex == 1) {
                            //Date set configuration
                            menuIndex = 0;
                            currentMenu = MenuState::CLOCK_DATE;
                            currentState = State::MENU;  

                        } else if (menuIndex == 2) {
                            // Save time and date Clock configuration
                            setCurrentTime(tempTime);
                            menuIndex = 0;
                            currentMenu = MenuState::MAIN_MENU;
                            currentState = State::MENU; 
                        }
                    }
                        if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                            menuIndex = 0;
                            currentMenu = MenuState::MAIN_MENU;
                            currentState = State::MENU;
                            displayNeedsUpdating = true;
                    }
                        if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                            menuIndex = 0;
                            currentMenu = MenuState::MAIN_MENU;
                            currentState = State::RUNNING;
                            displayNeedsUpdating = true;
                    }

                    break;
                case MenuState::CLOCK_TIME:
                    // When entering the menu, you will enter the hour setting first, then pressing the encoder button will move you to the minute setting.
                    if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        displayNeedsUpdating = true;
                        if(menuIndex == 0) {
                            if(tempTime.hour < 23) {
                                tempTime.hour++;
                            } else {
                                tempTime.hour = 0;
                            }
                        } else if(menuIndex == 1) {
                            if(tempTime.minute < 59) {
                                tempTime.minute++;
                            } else {
                                tempTime.minute = 0;
                              }
                            }
                    }    
                    else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        displayNeedsUpdating = true;
                        if(menuIndex == 0) {
                            if(tempTime.hour > 0) {
                                tempTime.hour--;
                            } else {
                                tempTime.hour = 23;
                            }
                        } else if(menuIndex == 1) {
                            if(tempTime.minute > 0) {
                                tempTime.minute--;
                            } else {
                                tempTime.minute = 59;
                            }
                        }
                    }
                    if (encoderEvent == EncoderEvent::PRESSED) {
                        displayNeedsUpdating = true;
                        if (menuIndex == 0) {
                            menuIndex = 1; // Move to minute setting
                        } else if (menuIndex == 1) {
                            menuIndex = 0; // Reset menu index
                            currentMenu = MenuState::CLOCK_MENU; // Return to clock menu after setting time
                    
                        }
                    }
                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::CLOCK_MENU;
                        currentState = State::MENU;
                        displayNeedsUpdating = true;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    }
                    break;
                case MenuState::CLOCK_DATE:
                    // Handle encoder events to adjust the date, only valid years are between 2000 and 2099, months between 1 and 12, and days between 1 and the maximum number of days in the selected month.
                    if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        displayNeedsUpdating = true;
                        if(menuIndex == 0) {
                            if(tempTime.year < 2099) {
                                tempTime.year++;
                            } else {
                                tempTime.year = 2000;
                            }
                            int maxDay = daysInMonth(tempTime.month, tempTime.year);
                              if(tempTime.day > maxDay) {
                                  tempTime.day = maxDay;
                              }
                        } else if(menuIndex == 1) {
                            if(tempTime.month < 12) {
                                tempTime.month++;
                            } else {
                                tempTime.month = 1;
                              }
                              // Using daysInMonth function to ensure the day is valid for the selected month and year
                              int maxDay = daysInMonth(tempTime.month, tempTime.year);
                              if(tempTime.day > maxDay) {
                                  tempTime.day = maxDay;
                              }
                        } else if(menuIndex == 2) {
                            int maxDay = daysInMonth(tempTime.month, tempTime.year);
                            if(tempTime.day < maxDay) {
                                tempTime.day++;
                            } else {
                                tempTime.day = 1;
                              }
                        }
                    }    
                    else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        displayNeedsUpdating = true;
                        if(menuIndex == 0) {
                            if(tempTime.year > 2000) {
                                tempTime.year--;
                            } else {
                                tempTime.year = 2099;
                            }
                            int maxDay = daysInMonth(tempTime.month, tempTime.year);
                              if(tempTime.day > maxDay) {
                                  tempTime.day = maxDay;
                              }
                        } else if(menuIndex == 1) {
                            if(tempTime.month > 1) {
                                tempTime.month--;
                            } else {
                                tempTime.month = 12;
                              }
                              int maxDay = daysInMonth(tempTime.month, tempTime.year);
                              if(tempTime.day > maxDay) {
                                  tempTime.day = maxDay;
                              }
                            } else if(menuIndex == 2) {
                                int maxDay = daysInMonth(tempTime.month, tempTime.year);
                                if(tempTime.day > 1) {
                                    tempTime.day--;
                                } else {
                                    tempTime.day = maxDay;
                              }
                            }
                    }

                    if (encoderEvent == EncoderEvent::PRESSED) {
                        displayNeedsUpdating = true;
                        if (menuIndex == 0) {
                            menuIndex = 1; // Move to month setting
                        } else if (menuIndex == 1) {
                            menuIndex = 2; // Move back to day setting
                        } else if (menuIndex == 2) {
                            menuIndex = 0; // Move back to year setting
                            currentMenu = MenuState::CLOCK_MENU; // Return to clock menu after setting date
                        }
                    }
                    
                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::CLOCK_MENU;
                        currentState = State::MENU;
                        displayNeedsUpdating = true;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    }
                    break;

                case MenuState::ALARM_SELECT:
                    if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex++;
                        displayNeedsUpdating = true;
                        if(menuIndex > 2) {
                            menuIndex = 0;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex--;
                        displayNeedsUpdating = true;
                        if(menuIndex < 0) {
                            menuIndex = 2;
                        }
                    }
                    if (encoderEvent == EncoderEvent::PRESSED) {
                        selectedAlarm = menuIndex;
                        tempAlarm = alarms[selectedAlarm];
                        menuIndex = 0;
                        currentMenu = MenuState::ALARM_MENU;
                        displayNeedsUpdating = true;
                       
                    }

                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::MENU;
                        displayNeedsUpdating = true;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    }

                    break;

                case MenuState::ALARM_MENU:
                    if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex++;
                        displayNeedsUpdating = true;
                        if(menuIndex > 8) {
                            menuIndex = 0;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex--;
                        displayNeedsUpdating = true;
                        if(menuIndex < 0) {
                            menuIndex = 8;
                        }
                    }
                    if (encoderEvent == EncoderEvent::PRESSED) {
                        displayNeedsUpdating = true;
                        if (menuIndex == 0) {
                            menuIndex = tempAlarm.enabled ? 1 : 0;
                            currentMenu = MenuState::ALARM_ENABLE;
                        } else if (menuIndex == 1) {
                            menuIndex = 0;
                            currentMenu = MenuState::ALARM_TIME;
                        } else if (menuIndex == 2) {
                            menuIndex = tempAlarm.daily ? 1 : 0;
                            currentMenu = MenuState::ALARM_TYPE;
                        } else if (menuIndex == 3) {
                            menuIndex = 0;
                            currentMenu = MenuState::ALARM_DATE;
                        } else if (menuIndex == 4) {
                            menuIndex = static_cast<int>(tempAlarm.tone);
                            currentMenu = MenuState::ALARM_TONE;
                        } else if (menuIndex == 5) {
                            menuIndex = tempAlarm.snoozeMinutes >= MIN_SNOOZE_MINUTES &&
                                                tempAlarm.snoozeMinutes <= MAX_SNOOZE_MINUTES
                                            ? tempAlarm.snoozeMinutes - MIN_SNOOZE_MINUTES
                                            : 0;
                            currentMenu = MenuState::ALARM_SNOOZE;
                        } else if (menuIndex == 6) {
                            menuIndex = tempAlarm.snoozeLimit <= MAX_SNOOZE_LIMIT
                                            ? tempAlarm.snoozeLimit
                                            : UNLIMITED_SNOOZE;
                            currentMenu = MenuState::ALARM_SNOOZE_LIMIT;
                        } else if (menuIndex == 7) {
                            menuIndex = static_cast<int>(tempAlarm.soundDuration);
                            currentMenu = MenuState::ALARM_DURATION;
                        } else if (menuIndex == 8) {
                            alarms[selectedAlarm] = tempAlarm;
                            menuIndex = 0;
                            currentMenu = MenuState::ALARM_SELECT;
                        }
                    }

                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::ALARM_SELECT;
                        currentState = State::MENU;
                        displayNeedsUpdating = true;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    }                
                    break;

                case MenuState::ALARM_ENABLE:
                    // Handle encoder events to toggle alarm enabled state
                    if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex++;
                        displayNeedsUpdating = true;
                        if(menuIndex > 1) {
                            menuIndex = 0;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex--;
                        displayNeedsUpdating = true;
                        if(menuIndex < 0) {
                            menuIndex = 1;
                        }
                    }

                    if (encoderEvent == EncoderEvent::PRESSED) {
                        displayNeedsUpdating = true;
                        if (menuIndex == 0) {
                            tempAlarm.enabled = false;
                        } else if (menuIndex == 1) {
                            tempAlarm.enabled = true;
                        }
                        menuIndex = 0;
                        currentMenu = MenuState::ALARM_MENU;
                    }

                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::ALARM_MENU;
                        currentState = State::MENU;
                        displayNeedsUpdating = true;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    }  


                    break;
                case MenuState::ALARM_TYPE:
                    // Handle encoder events to toggle alarm type (daily or one-time)
                    if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex++;
                        displayNeedsUpdating = true;
                        if(menuIndex > 1) {
                            menuIndex = 0;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex--;
                        displayNeedsUpdating = true;
                        if(menuIndex < 0) {
                            menuIndex = 1;
                        }
                    }

                    if (encoderEvent == EncoderEvent::PRESSED) {
                        displayNeedsUpdating = true;
                        if (menuIndex == 0) {
                            tempAlarm.daily = false;
                        } else if (menuIndex == 1) {
                            tempAlarm.daily = true;
                        }
                        menuIndex = 0;
                        currentMenu = MenuState::ALARM_MENU;
                    }

                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::ALARM_MENU;
                        currentState = State::MENU;
                        displayNeedsUpdating = true;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    }  
                    break;

                case MenuState::ALARM_TIME:
                    // Handle encoder events to adjust the alarm time
                    // When entering the menu, you will enter the hour setting first, then pressing the encoder button will move you to the minute setting.
                    if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        displayNeedsUpdating = true;
                        if(menuIndex == 0) {
                            if(tempAlarm.hour < 23) {
                                tempAlarm.hour++;
                            } else {
                                tempAlarm.hour = 0;
                            }
                        } else if(menuIndex == 1) {
                            if(tempAlarm.minute < 59) {
                                tempAlarm.minute++;
                            } else {
                                tempAlarm.minute = 0;
                              }
                            }
                    }    
                    else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        displayNeedsUpdating = true;
                        if(menuIndex == 0) {
                            if(tempAlarm.hour > 0) {
                                tempAlarm.hour--;
                            } else {
                                tempAlarm.hour = 23;
                            }
                        } else if(menuIndex == 1) {
                            if(tempAlarm.minute > 0) {
                                tempAlarm.minute--;
                            } else {
                                tempAlarm.minute = 59;
                              }
                            }
                    }

                    if (encoderEvent == EncoderEvent::PRESSED) {
                        displayNeedsUpdating = true;
                        if (menuIndex == 0) {
                            menuIndex = 1; // Move to minute setting
                        } else if (menuIndex == 1) {
                            menuIndex = 0; // Move back to hour setting
                            currentMenu = MenuState::ALARM_MENU; // Return to alarm menu after setting time
                    
                        }
                    }
                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::ALARM_MENU;
                        currentState = State::MENU;
                        displayNeedsUpdating = true;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    }

                    break;
                case MenuState::ALARM_DATE:
                    // Handle encoder events to adjust the alarm date
                    // When entering the menu, you will enter the year setting first, then pressing the encoder button will move you to the month setting, and then to the day setting.
                      if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        displayNeedsUpdating = true;
                        if(menuIndex == 0) {
                            if(tempAlarm.year < 2099) {
                                tempAlarm.year++;
                            } else {
                                tempAlarm.year = 2000;
                            }
                            int maxDay = daysInMonth(tempAlarm.month, tempAlarm.year);
                              if(tempAlarm.day > maxDay) {
                                  tempAlarm.day = maxDay;
                              }
                        } else if(menuIndex == 1) {
                            if(tempAlarm.month < 12) {
                                tempAlarm.month++;
                            } else {
                                tempAlarm.month = 1;
                              }
                              int maxDay = daysInMonth(tempAlarm.month, tempAlarm.year);
                              if(tempAlarm.day > maxDay) {
                                  tempAlarm.day = maxDay;
                              }
                        } else if(menuIndex == 2) {
                            
                            int maxDay = daysInMonth(tempAlarm.month, tempAlarm.year);
                            if(tempAlarm.day < maxDay) {
                                tempAlarm.day++;
                            } else {
                                tempAlarm.day = 1;
                              }     
                        }
                    }    
                    else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        displayNeedsUpdating = true;
                        if(menuIndex == 0) {
                            if(tempAlarm.year > 2000) {
                                tempAlarm.year--;
                            } else {
                                tempAlarm.year = 2099;
                            }
                            int maxDay = daysInMonth(tempAlarm.month, tempAlarm.year);
                              if(tempAlarm.day > maxDay) {
                                  tempAlarm.day = maxDay;
                              }
                        } else if(menuIndex == 1) {
                            if(tempAlarm.month > 1) {
                                tempAlarm.month--;
                            } else {
                                tempAlarm.month = 12;
                              }
                              int maxDay = daysInMonth(tempAlarm.month, tempAlarm.year);
                              if(tempAlarm.day > maxDay) {
                                  tempAlarm.day = maxDay;
                              }
                            } else if(menuIndex == 2) {
                                int maxDay = daysInMonth(tempAlarm.month, tempAlarm.year);
                                if(tempAlarm.day > 1) {
                                    tempAlarm.day--;
                                } else {
                                    tempAlarm.day = maxDay;
                              }
                            }
                    }

                    if (encoderEvent == EncoderEvent::PRESSED) {
                        displayNeedsUpdating = true;
                        if (menuIndex == 0) {
                            menuIndex = 1; // Move to month setting
                        } else if (menuIndex == 1) {
                            menuIndex = 2; // Move back to day setting
                        } else if (menuIndex == 2) {
                            menuIndex = 0; // Move back to year setting
                            currentMenu = MenuState::ALARM_MENU; // Return to alarm menu after setting date
                        }
                    }

                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::ALARM_MENU;
                        currentState = State::MENU;
                        displayNeedsUpdating = true;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    }

                    break;

                case MenuState::ALARM_TONE:
                    if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex++;
                        displayNeedsUpdating = true;
                        if(menuIndex > 2) {
                            menuIndex = 0;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex--;
                        displayNeedsUpdating = true;
                        if(menuIndex < 0) {
                            menuIndex = 2;
                        }
                    }

                    if (encoderEvent == EncoderEvent::PRESSED) {
                        tempAlarm.tone = static_cast<AlarmTone>(menuIndex);
                        menuIndex = 0;
                        currentMenu = MenuState::ALARM_MENU;
                        displayNeedsUpdating = true;
                    }

                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::ALARM_MENU;
                        displayNeedsUpdating = true;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    }
                    break;

                case MenuState::ALARM_SNOOZE:
                    if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex++;
                        displayNeedsUpdating = true;
                        if(menuIndex > MAX_SNOOZE_MINUTES - MIN_SNOOZE_MINUTES) {
                            menuIndex = 0;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex--;
                        displayNeedsUpdating = true;
                        if(menuIndex < 0) {
                            menuIndex = MAX_SNOOZE_MINUTES - MIN_SNOOZE_MINUTES;
                        }
                    }

                    if (encoderEvent == EncoderEvent::PRESSED) {
                        tempAlarm.snoozeMinutes = static_cast<uint8_t>(
                            MIN_SNOOZE_MINUTES + menuIndex
                        );
                        menuIndex = 0;
                        currentMenu = MenuState::ALARM_MENU;
                        displayNeedsUpdating = true;
                    }

                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::ALARM_MENU;
                        currentState = State::MENU;
                        displayNeedsUpdating = true;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    } 
                    break;

                case MenuState::ALARM_SNOOZE_LIMIT:
                    if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex++;
                        displayNeedsUpdating = true;
                        if(menuIndex > MAX_SNOOZE_LIMIT) {
                            menuIndex = UNLIMITED_SNOOZE;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex--;
                        displayNeedsUpdating = true;
                        if(menuIndex < UNLIMITED_SNOOZE) {
                            menuIndex = MAX_SNOOZE_LIMIT;
                        }
                    }

                    if (encoderEvent == EncoderEvent::PRESSED) {
                        tempAlarm.snoozeLimit = static_cast<uint8_t>(menuIndex);
                        menuIndex = 0;
                        currentMenu = MenuState::ALARM_MENU;
                        displayNeedsUpdating = true;
                    }

                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::ALARM_MENU;
                        displayNeedsUpdating = true;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    }
                    break;

                case MenuState::ALARM_DURATION:
                    if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex++;
                        displayNeedsUpdating = true;
                        if(menuIndex > static_cast<int>(AlarmDuration::INDEFINITE)) {
                            menuIndex = 0;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex--;
                        displayNeedsUpdating = true;
                        if(menuIndex < 0) {
                            menuIndex = static_cast<int>(AlarmDuration::INDEFINITE);
                        }
                    }

                    if (encoderEvent == EncoderEvent::PRESSED) {
                        tempAlarm.soundDuration = static_cast<AlarmDuration>(menuIndex);
                        menuIndex = 0;
                        currentMenu = MenuState::ALARM_MENU;
                        displayNeedsUpdating = true;
                    }

                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::ALARM_MENU;
                        displayNeedsUpdating = true;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    }
                    break;
                case MenuState::SYSTEM_MENU:
                    // Handle encoder events to navigate the system menu
                     if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex++;
                        displayNeedsUpdating = true;
                        if(menuIndex > 2) {
                            menuIndex = 0;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex--;
                        displayNeedsUpdating = true;
                        if(menuIndex < 0) {
                            menuIndex = 2;
                        }
                    }

                    if (encoderEvent == EncoderEvent::PRESSED) {
                        displayNeedsUpdating = true;
                        if (menuIndex == 0) {
                            menuIndex = 0; // Reset menu index for time format selection
                            currentMenu = MenuState::TIME_FORMAT;
                        } else if (menuIndex == 1) {
                            menuIndex = 0; // Reset menu index for brightness selection
                            currentMenu = MenuState::MANUAL_BRIGHTNESS;
                        } else if (menuIndex == 2) {
                            menuIndex = 0; // Reset menu index for brightness level selection
                            currentMenu = MenuState::BRIGHTNESS_LEVEL;
                        } 
                    }

                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::MENU;
                        displayNeedsUpdating = true;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    } 
                    break;
                case MenuState::TIME_FORMAT:
                    // Handle encoder events to toggle time format (12-hour or 24-hour)
                      if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex++;
                        displayNeedsUpdating = true;
                        if(menuIndex > 1) {
                            menuIndex = 0;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex--;
                        displayNeedsUpdating = true;
                        if(menuIndex < 0) {
                            menuIndex = 1;
                        }
                    }

                    if (encoderEvent == EncoderEvent::PRESSED) {
                        displayNeedsUpdating = true;
                        if (menuIndex == 0) {
                            currentSystemSettings.timeFormat = TimeFormat::HOUR_12;
                            menuIndex = 0; // Reset menu index for time format selection
                        } else if (menuIndex == 1) {
                            currentSystemSettings.timeFormat = TimeFormat::HOUR_24;
                            menuIndex = 0; // Reset menu index for time format selection
                            
                        }
                        currentMenu = MenuState::SYSTEM_MENU;
                    }

                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::SYSTEM_MENU;
                        currentState = State::MENU;
                        displayNeedsUpdating = true;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    } 
                    break;
                case MenuState::MANUAL_BRIGHTNESS:
                    // Handle encoder events to adjust manual brightness level
                     if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex++;
                        displayNeedsUpdating = true;
                        if(menuIndex > 1) {
                            menuIndex = 0;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex--;
                        displayNeedsUpdating = true;
                        if(menuIndex < 0) {
                            menuIndex = 1;
                        }
                    }

                    if (encoderEvent == EncoderEvent::PRESSED) {
                        displayNeedsUpdating = true;
                        if (menuIndex == 0) {
                            currentSystemSettings.manualBrightness = false; // Manual brightness disabled, automatic brightness enabled
                            menuIndex = 0; // Reset menu index for time format selection
                        } else if (menuIndex == 1) {
                            currentSystemSettings.manualBrightness = true; // Manual brightness enabled, automatic brightness disabled
                            menuIndex = 0; // Reset menu index for time format selection
                            
                        }
                        currentMenu = MenuState::SYSTEM_MENU;
                    }

                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::SYSTEM_MENU;
                        currentState = State::MENU;
                        displayNeedsUpdating = true;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    } 
                    break;
                case MenuState::BRIGHTNESS_LEVEL:
                    // Handle encoder events to adjust brightness level
                     if (encoderEvent == EncoderEvent::CLOCKWISE) {
                        menuIndex+=5;
                        displayNeedsUpdating = true;
                        if(menuIndex > 255) {
                            menuIndex = 0;
                        }
                    } else if (encoderEvent == EncoderEvent::COUNTER_CLOCKWISE) {
                        menuIndex-=5;
                        displayNeedsUpdating = true;
                        if(menuIndex < 0) {
                            menuIndex = 255;
                        }
                    }

                    if (encoderEvent == EncoderEvent::PRESSED) {
                        currentSystemSettings.brightnessLevel = menuIndex; // Set the brightness level based on the menu index
                        menuIndex = 0; // Reset menu index for time format selection
                        currentMenu = MenuState::SYSTEM_MENU;
                        displayNeedsUpdating = true;
                    }

                    if (buttonEvent == ButtonEvent::BACK_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::SYSTEM_MENU;
                        currentState = State::MENU;
                        displayNeedsUpdating = true;
                    }
                    if (buttonEvent == ButtonEvent::MENU_PRESSED) {
                        menuIndex = 0;
                        currentMenu = MenuState::MAIN_MENU;
                        currentState = State::RUNNING;
                        displayNeedsUpdating = true;
                    } 
                    break;
            }

            break;
        case State::ALARM_RINGING:
        //when alarm is acknowledged or snoozed, need to set the hasAlarmTriggered flag to true for that alarm index, so it doesn't ring again for the day
            updateAlarmSound();

            if (buttonEvent == ButtonEvent::STOP_PRESSED) {
                stopAlarmSound();
                hasAlarmTriggered[currentAlarmIndex] = true;
                alarmRuntime[currentAlarmIndex] = AlarmRuntime {};
                currentState = State::RUNNING;
                displayNeedsUpdating = true;
            } else if (buttonEvent == ButtonEvent::SNOOZE_PRESSED) {
                stopAlarmSound();
                hasAlarmTriggered[currentAlarmIndex] = true; // don't re-fire today's slot

                const uint8_t snoozeLimit = alarms[currentAlarmIndex].snoozeLimit;
                const bool snoozeAvailable = snoozeLimit == UNLIMITED_SNOOZE ||
                    alarmRuntime[currentAlarmIndex].snoozeCount < snoozeLimit;
                if (snoozeAvailable) {
                    startSnooze(currentAlarmIndex);
                } else {
                    alarmRuntime[currentAlarmIndex] = AlarmRuntime {};
                }
                currentState = State::RUNNING;
                displayNeedsUpdating = true;
            }
            break;
    }
    

    if(menuIndex != previousMenuIndex) {
        Serial.print("Menu Index: ");
        Serial.println(menuIndex);
    }
    if(currentMenu != previousMenu) {
        Serial.print("Current Menu: ");
        switch(currentMenu) {
            case MenuState::MAIN_MENU:
                Serial.println("Main Menu");
                break;
            case MenuState::CLOCK_MENU:
                Serial.println("Clock Menu");
                break;
            case MenuState::CLOCK_TIME:
                Serial.println("Clock Time");
                break;
            case MenuState::CLOCK_DATE:
                Serial.println("Clock Date");
                break;
            case MenuState::ALARM_SELECT:
                Serial.println("Alarm Select");
                break;
            case MenuState::ALARM_MENU:
                Serial.println("Alarm Menu");
                break;
            case MenuState::ALARM_ENABLE:
                Serial.println("Alarm Enable");
                break;
            case MenuState::ALARM_TIME:
                Serial.println("Alarm Time");
                break;
            case MenuState::ALARM_DATE:
                Serial.println("Alarm Date");
                break;
            case MenuState::ALARM_TYPE:
                Serial.println("Alarm Type");
                break;
            case MenuState::ALARM_TONE:
                Serial.println("Alarm Tone");
                break;
            case MenuState::ALARM_SNOOZE:
                Serial.println("Alarm Snooze Delay");
                break;
            case MenuState::ALARM_SNOOZE_LIMIT:
                Serial.println("Alarm Snooze Limit");
                break;
            case MenuState::ALARM_DURATION:
                Serial.println("Alarm Duration");
                break;
            case MenuState::SYSTEM_MENU:
                Serial.println("System Menu");
                break;
            case MenuState::TIME_FORMAT:
                Serial.println("Time Format");
                break;
            case MenuState::MANUAL_BRIGHTNESS:
                Serial.println("Manual Brightness");
                break;
            case MenuState::BRIGHTNESS_LEVEL:
                Serial.println("Brightness Level");
                break;
        }
    }
    previousMenu = currentMenu;
    previousMenuIndex = menuIndex;
    updateCurrentState();       //this currently does nothing
    updateDisplay();
    updateBrightness();
}

// Reads hardware RTC and saves both reference values
void synchronizeWithRtc() {
    DateTime rtcTime = RTClib::now();

    baseEpoch = rtcTime.unixtime();
    baseTimerUs = esp_timer_get_time();
}
// Checks if alarm is due based on current time and alarm configuration
bool isAlarmDue(const AlarmConfig& alarm, const ClockTime& currentTime, int alarmIndex) {
    if (!alarm.enabled) {
        return false; // Alarm is not enabled
    }
    if (hasAlarmTriggered[alarmIndex]){
        return false; // Alarm has already triggered for the day
    }

    if (alarm.daily) {
        // For daily alarms, only check the time
        return (alarm.hour == currentTime.hour && alarm.minute == currentTime.minute);
    } else {
        // For one-time alarms, check both date and time
        return (alarm.year == currentTime.year &&
                alarm.month == currentTime.month &&
                alarm.day == currentTime.day &&
                alarm.hour == currentTime.hour &&
                alarm.minute == currentTime.minute);
    }
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

//Dummy function to test booting

// EncoderEvent readEncoderEvent(){
//     return EncoderEvent::NONE; // Placeholder implementation
// }


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

    if (currentState == State::RUNNING ) {
        if (displayNeedsUpdating) {
            ssd1306_clearScreen();
            displayNeedsUpdating = false;
        }

        char timeText[9];
        snprintf(timeText, sizeof(timeText), "%02u:%02u:%02u",
            static_cast<unsigned>(currentTime.hour),
            static_cast<unsigned>(currentTime.minute),
            static_cast<unsigned>(currentTime.second));

        char dateText[11];
        snprintf(dateText, sizeof(dateText), "%02u/%02u/%04u",
            static_cast<unsigned>(currentTime.month),
            static_cast<unsigned>(currentTime.day),
            static_cast<unsigned>(currentTime.year));

        ssd1306_printFixed(0, 0, timeText, STYLE_NORMAL);
        ssd1306_printFixed(0, 16, dateText, STYLE_NORMAL);
        return; // done, skip the menu/alarm switch below
    }
    if(!displayNeedsUpdating){
        return;
    }

    displayNeedsUpdating = false;

    ssd1306_clearScreen();
    
    


    switch (currentState){

            case State::MENU: {
                switch(currentMenu){
                    case MenuState::MAIN_MENU: {
                        ssd1306_printFixed(0, 0, "Main Menu", STYLE_NORMAL);
                        if (menuIndex == 0) {
                            ssd1306_printFixed(0, 16, "> Clock", STYLE_NORMAL);
                            ssd1306_printFixed(0, 32, "  Alarms", STYLE_NORMAL);
                            ssd1306_printFixed(0, 48, "  System", STYLE_NORMAL);
                        }
                        else if (menuIndex == 1) {
                            ssd1306_printFixed(0, 16, "  Clock", STYLE_NORMAL);
                            ssd1306_printFixed(0, 32, "> Alarms", STYLE_NORMAL);
                            ssd1306_printFixed(0, 48, "  System", STYLE_NORMAL);
                        }
                        else {
                            ssd1306_printFixed(0, 16, "  Clock", STYLE_NORMAL);
                            ssd1306_printFixed(0, 32, "  Alarms", STYLE_NORMAL);
                            ssd1306_printFixed(0, 48, "> System", STYLE_NORMAL);
                        }
                        break;
            }
            case MenuState::CLOCK_MENU: {
                ssd1306_printFixed(0, 0, "Clock Menu", STYLE_NORMAL);
                if (menuIndex == 0) {
                    ssd1306_printFixed(0, 16, "> Clock Time", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "  Clock Date", STYLE_NORMAL);
                    ssd1306_printFixed(0, 48, "  Save", STYLE_NORMAL);
                }
                else if (menuIndex == 1) {
                    ssd1306_printFixed(0, 16, "  Clock Time", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "> Clock Date", STYLE_NORMAL);
                    ssd1306_printFixed(0, 48, "  Save", STYLE_NORMAL);
                }
                else {
                    ssd1306_printFixed(0, 16, "  Clock Time", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "  Clock Date", STYLE_NORMAL);
                    ssd1306_printFixed(0, 48, "> Save", STYLE_NORMAL);
                }
                break;
            }
            case MenuState::CLOCK_TIME: {
                ssd1306_printFixed(0, 0, "Set Time", STYLE_NORMAL);

                char timeText[6];
                snprintf(
                    timeText,
                    sizeof(timeText),
                    "%02u:%02u",
                    static_cast<unsigned>(tempTime.hour),
                    static_cast<unsigned>(tempTime.minute)
                );

                ssd1306_printFixed(0, 16, timeText, STYLE_NORMAL);
                if (menuIndex ==0) {
                    ssd1306_printFixed(0, 32, "^", STYLE_NORMAL);
                }
                else {
                    ssd1306_printFixed(18, 32, "^", STYLE_NORMAL);
                }
                break;
            }
            case MenuState::CLOCK_DATE: {
                ssd1306_printFixed(0, 0, "Set Date Y/M/D", STYLE_NORMAL);
                
                char dateText[11];
                snprintf(
                    dateText,
                    sizeof(dateText),
                    "%04u/%02u/%02u",
                    static_cast<unsigned>(tempTime.year),
                    static_cast<unsigned>(tempTime.month),
                    static_cast<unsigned>(tempTime.day)
                );

                ssd1306_printFixed(0, 16, dateText, STYLE_NORMAL);
                if (menuIndex == 0) {
                    ssd1306_printFixed(0, 32, "^", STYLE_NORMAL);
                }
                else if(menuIndex == 1) {
                    ssd1306_printFixed(30, 32, "^", STYLE_NORMAL);
                }
                else {
                    ssd1306_printFixed(48, 32, "^", STYLE_NORMAL);
            }
                break;
            }
             case MenuState::ALARM_SELECT: {
                ssd1306_printFixed(0, 0, "Select Alarm", STYLE_NORMAL); 
                if (menuIndex == 0) {
                    ssd1306_printFixed(0, 16, "> Alarm 1", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "  Alarm 2", STYLE_NORMAL);
                    ssd1306_printFixed(0, 48, "  Alarm 3", STYLE_NORMAL);
                }
                else if (menuIndex == 1) {
                    ssd1306_printFixed(0, 16, "  Alarm 1", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "> Alarm 2", STYLE_NORMAL);
                    ssd1306_printFixed(0, 48, "  Alarm 3", STYLE_NORMAL);
                }
                else {
                    ssd1306_printFixed(0, 16, "  Alarm 1", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "  Alarm 2", STYLE_NORMAL);
                    ssd1306_printFixed(0, 48, "> Alarm 3", STYLE_NORMAL);
                }
                break;
            }
            case MenuState::ALARM_MENU: {
                ssd1306_printFixed(0, 0, "Alarm Menu", STYLE_NORMAL);
                static const char* const alarmMenuItems[] = {
                    "Enabled",
                    "Time",
                    "Type",
                    "Date",
                    "Tone",
                    "Snooze Delay",
                    "Snooze Limit",
                    "Sound Duration",
                    "Save Alarm"
                };

                int firstItem = 0;
                if (menuIndex >= 2 && menuIndex <= 7) {
                    firstItem = menuIndex - 1;
                } else if (menuIndex == 8) {
                    firstItem = 6;
                }

                for (int row = 0; row < 3; row++) {
                    const int item = firstItem + row;
                    char line[22];
                    snprintf(
                        line,
                        sizeof(line),
                        "%c %s",
                        item == menuIndex ? '>' : ' ',
                        alarmMenuItems[item]
                    );
                    ssd1306_printFixed(0, 16 * (row + 1), line, STYLE_NORMAL);
                }
                break;
            }
            case MenuState::ALARM_ENABLE: {
                ssd1306_printFixed(0, 0, "Alarm Enable", STYLE_NORMAL);
                if (menuIndex == 0) {
                    ssd1306_printFixed(0, 16, "> Alarm Disabled", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "  Alarm Enabled", STYLE_NORMAL);
                }
                else if (menuIndex == 1) {
                    ssd1306_printFixed(0, 16, "  Alarm Disabled", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "> Alarm Enabled", STYLE_NORMAL);
                }
                break;
            }
            case MenuState::ALARM_TYPE: {
                ssd1306_printFixed(0, 0, "Alarm Type", STYLE_NORMAL);
                if (menuIndex == 0) {
                    ssd1306_printFixed(0, 16, "> One-Time-Alarm", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "  Daily Alarm", STYLE_NORMAL);
                }
                else if (menuIndex == 1) {
                    ssd1306_printFixed(0, 16, "  One-Time-Alarm", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "> Daily Alarm", STYLE_NORMAL);
                }
                break;
            }
            case MenuState::ALARM_TIME: {
                ssd1306_printFixed(0, 0, "Alarm Time", STYLE_NORMAL);

                char timeText[6];
                snprintf(
                    timeText,
                    sizeof(timeText),
                    "%02u:%02u",
                    static_cast<unsigned>(tempAlarm.hour),
                    static_cast<unsigned>(tempAlarm.minute)
                );

                ssd1306_printFixed(0, 16, timeText, STYLE_NORMAL);
                if (menuIndex ==0) {
                    ssd1306_printFixed(0, 32, "^", STYLE_NORMAL);
                }
                else {
                    ssd1306_printFixed(18, 32, "^", STYLE_NORMAL);
                }
                break;
            }
            case MenuState::ALARM_DATE: {
                ssd1306_printFixed(0, 0, "Alarm Date", STYLE_NORMAL);
                   
                char dateText[11];
                snprintf(
                    dateText,
                    sizeof(dateText),
                    "%04u/%02u/%02u",
                    static_cast<unsigned>(tempAlarm.year),
                    static_cast<unsigned>(tempAlarm.month),
                    static_cast<unsigned>(tempAlarm.day)
                );

                ssd1306_printFixed(0, 16, dateText, STYLE_NORMAL);
                if (menuIndex == 0) {
                    ssd1306_printFixed(0, 32, "^", STYLE_NORMAL);
                }
                else if(menuIndex == 1) {
                    ssd1306_printFixed(30, 32, "^", STYLE_NORMAL);
                }
                else {
                    ssd1306_printFixed(48, 32, "^", STYLE_NORMAL);
            }
                break;
            }
            case MenuState::ALARM_TONE: {
                ssd1306_printFixed(0, 0, "Alarm Tone", STYLE_NORMAL);
                ssd1306_printFixed(0, 16, menuIndex == 0 ? "> Tone 1" : "  Tone 1", STYLE_NORMAL);
                ssd1306_printFixed(0, 32, menuIndex == 1 ? "> Tone 2" : "  Tone 2", STYLE_NORMAL);
                ssd1306_printFixed(0, 48, menuIndex == 2 ? "> Tone 3" : "  Tone 3", STYLE_NORMAL);
                break;
            }
            case MenuState::ALARM_SNOOZE: {
                ssd1306_printFixed(0, 0, "Snooze Delay", STYLE_NORMAL);
                ssd1306_printFixed(0, 16, "Range: 5-15 min", STYLE_NORMAL);
                char snoozeText[14];
                snprintf(
                    snoozeText,
                    sizeof(snoozeText),
                    "> %u minutes",
                    static_cast<unsigned>(MIN_SNOOZE_MINUTES + menuIndex)
                );
                ssd1306_printFixed(0, 32, snoozeText, STYLE_NORMAL);
                break;
            }
            case MenuState::ALARM_SNOOZE_LIMIT: {
                ssd1306_printFixed(0, 0, "Snooze Limit", STYLE_NORMAL);
                ssd1306_printFixed(0, 16, "0 = unlimited", STYLE_NORMAL);
                char limitText[14];
                if (menuIndex == UNLIMITED_SNOOZE) {
                    snprintf(limitText, sizeof(limitText), "> Unlimited");
                } else {
                    snprintf(
                        limitText,
                        sizeof(limitText),
                        "> %u times",
                        static_cast<unsigned>(menuIndex)
                    );
                }
                ssd1306_printFixed(0, 32, limitText, STYLE_NORMAL);
                break;
            }
            case MenuState::ALARM_DURATION: {
                ssd1306_printFixed(0, 0, "Sound Duration", STYLE_NORMAL);
                static const char* const durationItems[] = {
                    "15 minutes",
                    "30 minutes",
                    "60 minutes",
                    "Indefinite"
                };
                const int firstItem = menuIndex == 3 ? 1 : 0;
                for (int row = 0; row < 3; row++) {
                    const int item = firstItem + row;
                    char line[18];
                    snprintf(
                        line,
                        sizeof(line),
                        "%c %s",
                        item == menuIndex ? '>' : ' ',
                        durationItems[item]
                    );
                    ssd1306_printFixed(0, 16 * (row + 1), line, STYLE_NORMAL);
                }
                break;
            }
            case MenuState::SYSTEM_MENU: {
                ssd1306_printFixed(0, 0, "System Settings", STYLE_NORMAL);
                 if (menuIndex == 0) {
                    ssd1306_printFixed(0, 16, "> Time Format", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "  Brightness Control", STYLE_NORMAL);
                    ssd1306_printFixed(0, 48, "  Brightness Level", STYLE_NORMAL);
                }
                else if (menuIndex == 1) {
                    ssd1306_printFixed(0, 16, "  Time Format", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "> Brightness Control", STYLE_NORMAL);
                    ssd1306_printFixed(0, 48, "  Brightness Level", STYLE_NORMAL);
                }
                else if (menuIndex == 2) {
                    ssd1306_printFixed(0, 16, "  Time Format", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "  Brightness Control", STYLE_NORMAL);
                    ssd1306_printFixed(0, 48, "> Brightness Level", STYLE_NORMAL);
                }
                break;
            }
            case MenuState::TIME_FORMAT: {
                ssd1306_printFixed(0, 0, "Time Format", STYLE_NORMAL);
                 if (menuIndex == 0) {
                    ssd1306_printFixed(0, 16, "> 12 Hour", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "  24 Hour", STYLE_NORMAL);
                }
                else if (menuIndex == 1) {
                    ssd1306_printFixed(0, 16, "  12 Hour", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "> 24 Hour", STYLE_NORMAL);
                }
                break;
            }
            case MenuState::MANUAL_BRIGHTNESS: {
                ssd1306_printFixed(0, 0, "Brightness Control", STYLE_NORMAL);
                    if (menuIndex == 0) {
                    ssd1306_printFixed(0, 16, "> Automatic", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "  Manual", STYLE_NORMAL);
                }
                else if (menuIndex == 1) {
                    ssd1306_printFixed(0, 16, "  Automatic", STYLE_NORMAL);
                    ssd1306_printFixed(0, 32, "> Manual", STYLE_NORMAL);
                }
                break;
            }
            case MenuState::BRIGHTNESS_LEVEL: {
                ssd1306_printFixed(0, 0, "Brightness Level", STYLE_NORMAL);
                ssd1306_printFixed(0, 16, "Set between 0-255", STYLE_NORMAL);
                   
                char brightnessText[4];
                snprintf(
                    brightnessText,
                    sizeof(brightnessText),
                    "%u",
                    static_cast<unsigned>(currentSystemSettings.brightnessLevel)
                );

                ssd1306_printFixed(0, 32, brightnessText, STYLE_NORMAL);
                break;
            }
        }
        break;
    }
    case State::ALARM_RINGING: {
        break;
    }       
    }


    /*char timeText[9];
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
*/
}

void updateBrightness() {
    // Brightness calibration and manual override behavior remain to be defined.
    if(currentSystemSettings.manualBrightness) {
        ssd1306_setContrast(currentSystemSettings.brightnessLevel);
    } else {
        // Automatic brightness control logic to be implemented.
        ssd1306_setContrast(255);
    }
}
//This function returns the number of days in a given month, accounting for leap years.
int daysInMonth(uint8_t month, uint16_t year) {
    switch (month) {
        case 1: case 3: case 5: case 7: case 8: case 10: case 12:
            return 31;
        case 4: case 6: case 9: case 11:
            return 30;
        case 2:
            // Check for leap year
            if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0)) {
                return 29; // Leap year
            } else {
                return 28; // Non-leap year
            }
        default:
            return 0; // Invalid month
    }
}

bool isMenuTimedOut() {
    return currentState == State::MENU && millis() - lastMenuInteractionTime >= 60000; // Reset timer on any menu interaction
    }
