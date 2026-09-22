#include <cassert>
#include <cstdint>
#include <iostream>

#include "main.h"

namespace {

void testAlarmDurations() {
    constexpr uint32_t FIFTEEN_MINUTES_MS = 15UL * 60UL * 1000UL;
    constexpr uint32_t THIRTY_MINUTES_MS = 30UL * 60UL * 1000UL;
    constexpr uint32_t SIXTY_MINUTES_MS = 60UL * 60UL * 1000UL;

    static_assert(
        alarmDurationMilliseconds(AlarmDuration::MINUTES_15) ==
            FIFTEEN_MINUTES_MS,
        "15-minute duration must be represented in milliseconds"
    );
    static_assert(
        alarmDurationMilliseconds(AlarmDuration::MINUTES_30) ==
            THIRTY_MINUTES_MS,
        "30-minute duration must be represented in milliseconds"
    );
    static_assert(
        alarmDurationMilliseconds(AlarmDuration::MINUTES_60) ==
            SIXTY_MINUTES_MS,
        "60-minute duration must be represented in milliseconds"
    );
    static_assert(
        alarmDurationMilliseconds(AlarmDuration::INDEFINITE) == 0UL,
        "indefinite alarms must not have an automatic timeout"
    );

    assert(!alarmDurationElapsed(AlarmDuration::MINUTES_15, 1000, 1000));
    assert(!alarmDurationElapsed(
        AlarmDuration::MINUTES_15,
        1000,
        1000 + FIFTEEN_MINUTES_MS - 1
    ));
    assert(alarmDurationElapsed(
        AlarmDuration::MINUTES_15,
        1000,
        1000 + FIFTEEN_MINUTES_MS
    ));
    assert(!alarmDurationElapsed(
        AlarmDuration::MINUTES_30,
        1000,
        1000 + THIRTY_MINUTES_MS - 1
    ));
    assert(alarmDurationElapsed(
        AlarmDuration::MINUTES_30,
        1000,
        1000 + THIRTY_MINUTES_MS
    ));
    assert(!alarmDurationElapsed(
        AlarmDuration::MINUTES_60,
        1000,
        1000 + SIXTY_MINUTES_MS - 1
    ));
    assert(alarmDurationElapsed(
        AlarmDuration::MINUTES_60,
        1000,
        1000 + SIXTY_MINUTES_MS
    ));
    assert(!alarmDurationElapsed(
        AlarmDuration::INDEFINITE,
        1000,
        UINT32_MAX
    ));

    const uint32_t rolloverStart = UINT32_MAX - 1000UL;
    assert(!alarmDurationElapsed(
        AlarmDuration::MINUTES_15,
        rolloverStart,
        static_cast<uint32_t>(
            rolloverStart + FIFTEEN_MINUTES_MS - 1
        )
    ));
    assert(alarmDurationElapsed(
        AlarmDuration::MINUTES_15,
        rolloverStart,
        static_cast<uint32_t>(rolloverStart + FIFTEEN_MINUTES_MS)
    ));

    assert(
        alarmDurationMilliseconds(static_cast<AlarmDuration>(255)) ==
        FIFTEEN_MINUTES_MS
    );
}

void testMenuSelections() {
    static_assert(
        timeFormatMenuIndex(TimeFormat::HOUR_12) == 0,
        "12-hour mode must select the first menu item"
    );
    static_assert(
        timeFormatMenuIndex(TimeFormat::HOUR_24) == 1,
        "24-hour mode must select the second menu item"
    );
    static_assert(
        brightnessModeMenuIndex(false) == 0,
        "automatic brightness must select the first menu item"
    );
    static_assert(
        brightnessModeMenuIndex(true) == 1,
        "manual brightness must select the second menu item"
    );
}

void testBrightnessSampling() {
    assert(brightnessSampleDue(0, 0, false));
    assert(!brightnessSampleDue(99, 0, true));
    assert(brightnessSampleDue(100, 0, true));

    const uint32_t rolloverStart = UINT32_MAX - 50UL;
    assert(!brightnessSampleDue(48, rolloverStart, true));
    assert(brightnessSampleDue(49, rolloverStart, true));
}

} // namespace

int main() {
    testAlarmDurations();
    testMenuSelections();
    testBrightnessSampling();
    std::cout << "clock logic tests passed\n";
    return 0;
}
