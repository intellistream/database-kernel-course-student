#include "db/types/datetime.hpp"

#include <array>
#include <charconv>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

#include "db/base/checked.hpp"
#include "db/base/error.hpp"
#include "db/base/text.hpp"
#include "db/types/data_type.hpp"

namespace db::types {
namespace {

constexpr std::int64_t kMicrosecondsPerSecond = 1'000'000;
constexpr std::int64_t kSecondsPerDay = 86'400;
constexpr std::int64_t kPostgresEpochUnixDays = 10'957;

auto timestamp_error(std::string message) -> base::Error {
    return {base::ErrorCode::kInvalidArgument, std::move(message)};
}

auto parse_component(const std::string_view text, const std::string_view name)
    -> base::Result<std::int32_t> {
    if (text.empty()) {
        return base::fail(timestamp_error("timestamp is missing " + std::string{name}));
    }
    std::int32_t value = 0;
    const auto result = std::from_chars(text.begin(), text.end(), value);
    if (result.ec != std::errc{} || result.ptr != text.end()) {
        return base::fail(timestamp_error("invalid timestamp " + std::string{name}));
    }
    return value;
}

auto take_component(std::string_view& remaining, const char delimiter, const std::string_view name)
    -> base::Result<std::int32_t> {
    const auto delimiter_position = remaining.find(delimiter);
    if (delimiter_position == std::string_view::npos) {
        return base::fail(timestamp_error("timestamp is missing " + std::string{name}));
    }
    const auto component = remaining.substr(0, delimiter_position);
    remaining.remove_prefix(delimiter_position + 1U);
    return parse_component(component, name);
}

struct CivilDate final {
    std::int32_t year;
    std::int32_t month;
    std::int32_t day;
};

struct TimeOfDay final {
    std::int64_t seconds;
    std::int64_t fraction_microseconds;
};

auto validate_date(const CivilDate date) -> base::Status {
    const bool leap = date.year % 4 == 0 && (date.year % 100 != 0 || date.year % 400 == 0);
    std::int32_t days = 0;
    switch (date.month) {
        case 1:
        case 3:
        case 5:
        case 7:
        case 8:
        case 10:
        case 12:
            days = 31;
            break;
        case 4:
        case 6:
        case 9:
        case 11:
            days = 30;
            break;
        case 2:
            days = leap ? 29 : 28;
            break;
        default:
            return base::fail(timestamp_error("timestamp month is out of range"));
    }
    if (date.day < 1 || date.day > days) {
        return base::fail(timestamp_error("timestamp day is out of range"));
    }
    return {};
}

auto parse_date(std::string_view text) -> base::Result<CivilDate> {
    auto year = take_component(text, '-', "year");
    auto month = take_component(text, '-', "month");
    auto day = parse_component(text, "day");
    if (!year) {
        return base::fail(std::move(year).error());
    }
    if (!month) {
        return base::fail(std::move(month).error());
    }
    if (!day) {
        return base::fail(std::move(day).error());
    }
    if (year.value() < 1 || year.value() > 9'999) {
        return base::fail(timestamp_error("timestamp year must be between 0001 and 9999"));
    }
    const CivilDate date{year.value(), month.value(), day.value()};
    auto status = validate_date(date);
    if (!status) {
        return base::fail(std::move(status).error());
    }
    return date;
}

auto parse_fraction(const std::string_view fraction) -> base::Result<std::int64_t> {
    if (fraction.empty()) {
        return base::fail(timestamp_error("timestamp fractional seconds are empty"));
    }
    std::int64_t microseconds = 0;
    constexpr std::array powers{100'000, 10'000, 1'000, 100, 10, 1};
    for (std::size_t index = 0; index < fraction.size(); ++index) {
        const auto digit = static_cast<unsigned char>(fraction.at(index));
        if (digit < static_cast<unsigned char>('0') || digit > static_cast<unsigned char>('9')) {
            return base::fail(
                timestamp_error("timestamp fractional seconds must contain only digits")
            );
        }
        if (index < powers.size()) {
            microseconds += static_cast<std::int64_t>(digit - static_cast<unsigned char>('0'))
                            * powers.at(index);
        }
    }
    if (fraction.size() > powers.size() && fraction.at(powers.size()) >= '5') {
        ++microseconds;
    }
    return microseconds;
}

auto parse_time(std::string_view text) -> base::Result<TimeOfDay> {
    if (text.find_first_of("+Zz") != std::string_view::npos) {
        return base::fail(
            timestamp_error("timestamp without time zone does not accept a time zone")
        );
    }

    std::int64_t fraction_microseconds = 0;
    const auto dot = text.find('.');
    if (dot != std::string_view::npos) {
        auto fraction = parse_fraction(text.substr(dot + 1U));
        if (!fraction) {
            return base::fail(std::move(fraction).error());
        }
        fraction_microseconds = fraction.value();
        text = text.substr(0, dot);
    }

    auto hour = take_component(text, ':', "hour");
    auto minute = take_component(text, ':', "minute");
    auto second = parse_component(text, "second");
    if (!hour) {
        return base::fail(std::move(hour).error());
    }
    if (!minute) {
        return base::fail(std::move(minute).error());
    }
    if (!second) {
        return base::fail(std::move(second).error());
    }
    if (hour.value() < 0 || hour.value() > 23 || minute.value() < 0 || minute.value() > 59
        || second.value() < 0 || second.value() > 59) {
        return base::fail(timestamp_error("timestamp time is out of range"));
    }
    const auto seconds = (static_cast<std::int64_t>(hour.value()) * 3'600)
                         + (static_cast<std::int64_t>(minute.value()) * 60)
                         + static_cast<std::int64_t>(second.value());
    return TimeOfDay{seconds, fraction_microseconds};
}

// Howard Hinnant's proleptic-Gregorian civil date algorithms.
constexpr auto days_from_civil(const CivilDate date) noexcept -> std::int64_t {
    auto year = static_cast<std::int64_t>(date.year);
    const auto month = static_cast<std::int64_t>(date.month);
    const auto day = static_cast<std::int64_t>(date.day);
    year -= month <= 2 ? 1 : 0;
    const auto era = (year >= 0 ? year : year - 399) / 400;
    const auto year_of_era = year - (era * 400);
    const auto day_of_year = ((153 * (month + (month > 2 ? -3 : 9))) + 2) / 5 + day - 1;
    const auto day_of_era =
        (year_of_era * 365) + (year_of_era / 4) - (year_of_era / 100) + day_of_year;
    return (era * 146'097) + day_of_era - 719'468;
}

constexpr auto civil_from_days(std::int64_t days) noexcept -> CivilDate {
    days += 719'468;
    const auto era = (days >= 0 ? days : days - 146'096) / 146'097;
    const auto day_of_era = days - (era * 146'097);
    const auto year_of_era =
        (day_of_era - (day_of_era / 1'460) + (day_of_era / 36'524) - (day_of_era / 146'096)) / 365;
    auto year = year_of_era + (era * 400);
    const auto day_of_year =
        day_of_era - ((365 * year_of_era) + (year_of_era / 4) - (year_of_era / 100));
    const auto month_prime = ((5 * day_of_year) + 2) / 153;
    const auto day = day_of_year - (((153 * month_prime) + 2) / 5) + 1;
    const auto month = month_prime + (month_prime < 10 ? 3 : -9);
    year += month <= 2 ? 1 : 0;
    return {
        static_cast<std::int32_t>(year),
        static_cast<std::int32_t>(month),
        static_cast<std::int32_t>(day),
    };
}

void append_padded(std::string& output, const std::int64_t value, const std::size_t width) {
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.begin(), buffer.end(), value);
    if (result.ec != std::errc{}) {
        output.append("<range>");
        return;
    }
    const auto size = static_cast<std::size_t>(std::distance(buffer.begin(), result.ptr));
    if (size < width) {
        output.append(width - size, '0');
    }
    output.append(buffer.begin(), result.ptr);
}

constexpr auto floor_div(const std::int64_t numerator, const std::int64_t denominator) noexcept
    -> std::int64_t {
    const auto quotient = numerator / denominator;
    const auto remainder = numerator % denominator;
    return remainder < 0 ? quotient - 1 : quotient;
}

constexpr auto floor_mod(const std::int64_t numerator, const std::int64_t denominator) noexcept
    -> std::int64_t {
    const auto remainder = numerator % denominator;
    return remainder < 0 ? remainder + denominator : remainder;
}

auto compose_timestamp(const CivilDate date, const TimeOfDay time) -> base::Result<TimestampValue> {
    const auto days = days_from_civil(date) - kPostgresEpochUnixDays;
    const auto day_seconds = base::checked_multiply(days, kSecondsPerDay);
    if (!day_seconds) {
        return base::fail(timestamp_error("timestamp is out of range"));
    }
    const auto seconds = base::checked_add(day_seconds.value(), time.seconds);
    if (!seconds) {
        return base::fail(timestamp_error("timestamp is out of range"));
    }
    const auto whole_microseconds = base::checked_multiply(seconds.value(), kMicrosecondsPerSecond);
    if (!whole_microseconds) {
        return base::fail(timestamp_error("timestamp is out of range"));
    }
    const auto value = base::checked_add(whole_microseconds.value(), time.fraction_microseconds);
    if (!value) {
        return base::fail(timestamp_error("timestamp is out of range"));
    }
    return TimestampValue{value.value()};
}

}  // namespace

auto parse_timestamp(const std::string_view input) -> base::Result<TimestampValue> {
    const auto text = base::trim_ascii_space(input);
    const auto separator = text.find_first_of(" T");
    if (separator == std::string_view::npos) {
        return base::fail(timestamp_error("expected YYYY-MM-DD HH:MM:SS[.ffffff]"));
    }
    auto date = parse_date(text.substr(0, separator));
    auto time = parse_time(text.substr(separator + 1U));
    if (!date) {
        return base::fail(std::move(date).error());
    }
    if (!time) {
        return base::fail(std::move(time).error());
    }
    return compose_timestamp(date.value(), time.value());
}

auto format_timestamp(const TimestampValue value) -> std::string {
    const auto seconds = floor_div(value.microseconds(), kMicrosecondsPerSecond);
    const auto microseconds = floor_mod(value.microseconds(), kMicrosecondsPerSecond);
    const auto days = floor_div(seconds, kSecondsPerDay);
    const auto seconds_of_day = floor_mod(seconds, kSecondsPerDay);
    const auto date = civil_from_days(days + kPostgresEpochUnixDays);
    const auto hour = seconds_of_day / 3'600;
    const auto minute = (seconds_of_day % 3'600) / 60;
    const auto second = seconds_of_day % 60;

    std::string output;
    output.reserve(26);
    append_padded(output, date.year, 4);
    output.push_back('-');
    append_padded(output, date.month, 2);
    output.push_back('-');
    append_padded(output, date.day, 2);
    output.push_back(' ');
    append_padded(output, hour, 2);
    output.push_back(':');
    append_padded(output, minute, 2);
    output.push_back(':');
    append_padded(output, second, 2);
    if (microseconds != 0) {
        output.push_back('.');
        const auto fraction_start = output.size();
        append_padded(output, microseconds, 6);
        while (output.size() > fraction_start && output.back() == '0') {
            output.pop_back();
        }
    }
    return output;
}

auto adjust_timestamp_precision(const TimestampValue value, const std::uint32_t precision)
    -> base::Result<TimestampValue> {
    if (precision > kMaxTimestampPrecision) {
        return base::fail(
            base::Error{
                base::ErrorCode::kInvalidArgument,
                "timestamp precision must be between 0 and 6",
            }
        );
    }
    constexpr std::array<std::int64_t, 7> scales{
        1'000'000,
        100'000,
        10'000,
        1'000,
        100,
        10,
        1,
    };
    const auto scale = scales.at(precision);
    const auto offset = scale / 2;
    const auto microseconds = value.microseconds();

    std::int64_t rounded_units = 0;
    if (microseconds >= 0) {
        const auto adjusted = base::checked_add(microseconds, offset);
        if (!adjusted) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kOutOfRange,
                    "timestamp out of range after rounding",
                }
            );
        }
        rounded_units = adjusted.value() / scale;
    } else {
        if (microseconds == std::numeric_limits<std::int64_t>::min()) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kOutOfRange,
                    "timestamp out of range after rounding",
                }
            );
        }
        const auto adjusted = base::checked_add(-microseconds, offset);
        if (!adjusted) {
            return base::fail(
                base::Error{
                    base::ErrorCode::kOutOfRange,
                    "timestamp out of range after rounding",
                }
            );
        }
        rounded_units = -(adjusted.value() / scale);
    }
    const auto rounded = base::checked_multiply(rounded_units, scale);
    if (!rounded) {
        return base::fail(
            base::Error{
                base::ErrorCode::kOutOfRange,
                "timestamp out of range after rounding",
            }
        );
    }
    return TimestampValue{rounded.value()};
}

}  // namespace db::types
