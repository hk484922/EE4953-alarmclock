#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>

// The different modes of the clock
enum class State {
    STARTUP,        // Starting the clock
    RUNNING,        // Showing the time and checking the alarm
    MENU,           // Changing settings
    ALARM_RINGING,  // Alarm is sounding
    SNOOZED         // Waiting for the alarm to ring again
};

// Actions from the rotary encoder
enum class EncoderEvent {
    NONE,               // Nothing happened
    CLOCKWISE,          // Turned clockwise
    COUNTER_CLOCKWISE,  // Turned counter-clockwise
    PRESSED             // Encoder was pressed
};

// Actions from the separate buttons
enum class ButtonEvent {
    NONE,             // Nothing happened
    SNOOZE_PRESSED,   // Snooze was pressed
    STOP_PRESSED,     // Stop was pressed
    MENU_PRESSED,     // Menu was pressed
    BACK_PRESSED      // Back was pressed
};

// Alarm settings saved in ESP32 memory.
struct AlarmConfig {
    bool enabled;           // Is the alarm on?
    bool daily;             // Is it a daily alarm?
    uint8_t hour;           // Alarm hour
    uint8_t minute;         // Alarm minute
    uint8_t day;            // Alarm day for a date alarm
    uint8_t month;          // Alarm month for a date alarm
    uint16_t year;          // Alarm year for a date alarm
    uint16_t snoozeMinutes; // Snooze length
};

// The current time from the RTC
struct ClockTime {
    uint16_t year;   // Current year
    uint8_t month;   // Current month
    uint8_t day;     // Current day
    uint8_t hour;    // Current hour
    uint8_t minute;  // Current minute
    uint8_t second;  // Current second
};

// Start the clock hardware
void initializeHardware();      // Start all hardware
void initializeDisplay();       // Start the OLED display
void initializeRtc();           // Start the RTC
void initializeEncoder();       // Start the rotary encoder
void initializeButtons();       // Set up the buttons
void initializeBuzzer();        // Set up the buzzer
void initializeLightSensor();   // Set up the light sensor

// Read user input
EncoderEvent readEncoderEvent();  // Read the encoder
ButtonEvent readButtonEvent();    // Read the buttons

// Work with the time and alarm settings
ClockTime readCurrentTime();                                       // Get the time from the RTC
void setCurrentTime(const ClockTime& time);                        // Set the RTC time
AlarmConfig loadAlarmConfiguration();                              // Load alarm settings from memory
void saveAlarmConfiguration(const AlarmConfig& config);            // Save alarm settings to memory
bool isAlarmDue(const AlarmConfig& config, const ClockTime& time); // Check if the alarm should ring

// Change and run the current mode
void enterState(State newState);  // Change to a new mode
void updateCurrentState();        // Run the current mode

// Work with the settings menu
void openMenu();       // Open the settings menu
void closeMenu();      // Close the settings menu
void updateMenu();     // Handle menu input
bool menuTimedOut();   // Check if the menu has been idle too long

// Control the alarm and snooze
void startAlarm();     // Start the alarm
void stopAlarm();      // Stop the alarm
void startSnooze();    // Start snooze mode
bool snoozeExpired();  // Check if snooze time is over

// Update the screen and brightness
void updateDisplay();     // Show the current screen
void updateBrightness();  // Set the display brightness

#endif // MAIN_H