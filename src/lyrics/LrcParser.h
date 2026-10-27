#pragma once

#include <QStringView>
#include <variant>

#include "lyrics/LyricTypes.h"

struct ParseError {
    QString message;
};

// Parses line-timed LRC text. Malformed lines are skipped; input with no
// usable timed line is an error. Timestamps are integer milliseconds.
class LrcParser {
public:
    // End time given to the final line when nothing follows it.
    static constexpr qint64 kLastLineDurationMs = 5000;

    static std::variant<LyricDocument, ParseError> parse(QStringView text);
};
