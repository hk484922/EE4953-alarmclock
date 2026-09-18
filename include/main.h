#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>

// The different modes of the clock
enum class State {
    STARTUP,        // Starting the clock
    RUNNING,        // Showing the time and checking the alarm
    MENU,           // Changing settings
    ALARM_RINGING,  // Alarm is sounding
    //Removed Snooze state, as it is now handled in the RUNNING state
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

// 12 or 24 hour time format
enum class TimeFormat {
    HOUR_12,
    HOUR_24
};

// States within menu
enum class MenuState {
    MAIN_MENU, //  First menu screen, shows options for clocktime or alarm
    CLOCK_MENU, // Jumps to clocktime setting screen
    ALARM_SELECT, // Second menu screen, shows options for alarm 1, 2, or 3
    ALARM_MENU // Jumps to alarm setting screen
};

// Main menu shows options for clocktime or alarm
enum class MainMenu {
    CLOCK_MENU,
    ALARM_SELECT 
};

// Alarm menu selection
enum class AlarmSelection {
    ALARM_1,
    ALARM_2,
    ALARM_3
};
// Alarm items show the configurable alarm settings
enum class AlarmMenu {
    ENABLED, // Is the alarm enabled
    ALARM_TYPE, // Is it a daily-repeated alarm or one-time alarm?
    TIME, // Time for alarm to sound
    DATE, // Date for one-time alarms, will not be relevent for repeated alarms
    SNOOZE_TIME // How long will alarm snooze for?
};


// Menu settings
struct SystemSettings {
    TimeFormat timeFormat;
    bool ManualBrightness;  // 0 for auto brightness, 1 for manual
    // Add brightness settings?
};

// Three different alarm tones
enum class AlarmTone {
    TONE_1,
    TONE_2,
    TONE_3
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
    AlarmTone tone;         // Alarm sound
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

// Temporary Menu Settings
struct TempMenu {
    SystemSettings settings;
    AlarmConfig alarms[3];
    ClockTime time;

    bool settingsChanged;
    bool alarmsChanged;
    bool timeChanged;
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
ClockTime readCurrentTime();                                       // Get the calculated software time
void setCurrentTime(const ClockTime& time);                        // Set the RTC time
void synchronizeWithRtc();                                        // Refresh software time from the RTC
uint32_t calculateCurrentEpoch();                                 // Add elapsed ESP32 time to the RTC anchor
void serviceClock();                                               // Refresh the shared current-time snapshot
void serviceRtcSynchronization();                                  // Resynchronize the RTC anchor once per day
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
