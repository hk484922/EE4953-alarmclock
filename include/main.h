#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>

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

// Audible pattern selected for an alarm.
enum class AlarmTone : uint8_t {
    TONE_1 = 0,
    TONE_2,
    TONE_3
};

// Maximum time an unattended alarm is allowed to sound.
enum class AlarmDuration : uint8_t {
    MINUTES_15 = 0,
    MINUTES_30,
    MINUTES_60,
    INDEFINITE
};

constexpr uint32_t BRIGHTNESS_SAMPLE_INTERVAL_MS = 100;

constexpr uint32_t alarmDurationMilliseconds(AlarmDuration duration) {
    return duration == AlarmDuration::MINUTES_15 ? 15UL * 60UL * 1000UL :
           duration == AlarmDuration::MINUTES_30 ? 30UL * 60UL * 1000UL :
           duration == AlarmDuration::MINUTES_60 ? 60UL * 60UL * 1000UL :
           duration == AlarmDuration::INDEFINITE ? 0UL :
           15UL * 60UL * 1000UL;
}

constexpr bool alarmDurationElapsed(
    AlarmDuration duration,
    uint32_t startedAtMs,
    uint32_t nowMs
) {
    return alarmDurationMilliseconds(duration) != 0UL &&
        static_cast<uint32_t>(nowMs - startedAtMs) >=
            alarmDurationMilliseconds(duration);
}

constexpr int timeFormatMenuIndex(TimeFormat format) {
    return format == TimeFormat::HOUR_24 ? 1 : 0;
}

constexpr int brightnessModeMenuIndex(bool manualBrightness) {
    return manualBrightness ? 1 : 0;
}

constexpr bool brightnessSampleDue(
    uint32_t nowMs,
    uint32_t lastSampleMs,
    bool hasSample
) {
    return !hasSample ||
        static_cast<uint32_t>(nowMs - lastSampleMs) >=
            BRIGHTNESS_SAMPLE_INTERVAL_MS;
}

constexpr uint8_t MIN_SNOOZE_MINUTES = 5;
constexpr uint8_t MAX_SNOOZE_MINUTES = 15;
constexpr uint8_t MIN_SNOOZE_LIMIT = 1;
constexpr uint8_t MAX_SNOOZE_LIMIT = 10;
constexpr uint8_t UNLIMITED_SNOOZE = 0;

// States within menu
enum class MenuState {
    MAIN_MENU, //  First menu screen, shows options for clocktime or alarm
    CLOCK_MENU, // Jumps to clocktime setting screen
    CLOCK_TIME, // Jumps to clocktime time setting screen
    CLOCK_DATE, // Jumps to clocktime date setting screen
    ALARM_SELECT, // Second menu screen, shows options for alarm 1, 2, or 3
    ALARM_MENU, // Jumps to alarm setting screen
    ALARM_ENABLE, // Jumps to alarm enabled setting screen
    ALARM_TYPE, // Jumps to alarm type setting screen
    ALARM_TIME, // Jumps to alarm time setting screen
    ALARM_DATE, // Jumps to alarm date setting screen
    ALARM_TONE, // Jumps to alarm tone selection screen
    ALARM_SNOOZE, // Jumps to alarm snooze-delay setting screen
    ALARM_SNOOZE_LIMIT, // Jumps to alarm snooze-limit setting screen
    ALARM_DURATION, // Jumps to alarm sound-duration setting screen
    SYSTEM_MENU, // Third menu screen, shows options for system settings
    TIME_FORMAT, // Jumps to time format setting screen
    MANUAL_BRIGHTNESS, // Jumps to brightness control method setting screen
    BRIGHTNESS_LEVEL // Jumps to manual brightness level setting screen

};

// Alarm menu selection
enum class AlarmSelection {
    ALARM_1,
    ALARM_2,
    ALARM_3
};


// Menu settings
struct SystemSettings {
    TimeFormat timeFormat;
    bool manualBrightness; // True if Manual brightness control is selected, false if Auto brightness control is selected
    uint8_t brightnessLevel; // 0-255 
};


// Alarm settings saved in ESP32 memory.
struct AlarmConfig {
    bool enabled = false;                              // Is the alarm on?
    bool daily = true;                                 // Is it a daily alarm?
    uint8_t hour = 0;                                  // Alarm hour (0-23)
    uint8_t minute = 0;                                // Alarm minute (0-59)
    uint8_t day = 1;                                   // Alarm day for a dated alarm
    uint8_t month = 1;                                 // Alarm month for a dated alarm
    uint16_t year = 2000;                              // Alarm year for a dated alarm
    uint8_t snoozeMinutes = MIN_SNOOZE_MINUTES;        // Snooze delay (5-15 minutes)
    uint8_t snoozeLimit = UNLIMITED_SNOOZE;            // Allowed snoozes (1-10, 0 = unlimited)
    AlarmDuration soundDuration = AlarmDuration::MINUTES_15;
    AlarmTone tone = AlarmTone::TONE_1;
};

// Volatile state used while an alarm is sounding or waiting to re-trigger.
struct AlarmRuntime {
    bool snoozed = false;
    uint32_t snoozeWakeEpoch = 0;
    uint8_t snoozeCount = 0;
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
void loadAlarmConfiguration(uint8_t alarmIndex);                    // Load one alarm's settings from memory
void saveAlarmConfiguration(uint8_t alarmIndex);                    // Save one alarm's settings to memory
void loadSystemSettings();                                         // Load system settings from memory
void saveSystemSettings();                                         // Save system settings to memory
bool isAlarmDue(const AlarmConfig& config, const ClockTime& time, int alarmIndex); // Check if the alarm should ring

// Change and run the current mode
void enterState(State newState);  // Change to a new mode
void updateCurrentState();        // Run the current mode

// Control the alarm and snooze
void startAlarm();     // Start the alarm
void stopAlarm();      // Stop the alarm
void startSnooze(uint8_t alarmIndex); // Schedule an alarm to re-trigger after its snooze delay
bool snoozeExpired(uint8_t alarmIndex, uint32_t nowEpoch); // Check a scheduled snooze

// Update the screen and brightness
void updateDisplay();     // Show the current screen
void updateBrightness();  // Set the display brightness

int daysInMonth(uint8_t month, uint16_t year); // Returns the number of days in a given month and year

//Check if the menu has been idle too long
bool isMenuTimedOut();

#endif // MAIN_H
