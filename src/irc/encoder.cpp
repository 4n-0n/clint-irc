#include "encoder.hpp"

#include <algorithm>

namespace irc {

namespace {

// Reduces `maxLen` so the cut does not split a UTF-8 multibyte sequence:
// while the byte at the cut position is a continuation byte (10xxxxxx), step
// back to its lead byte.
int utf8SafeLength(const QByteArray& data, int maxLen) {
    int n = static_cast<int>(std::min<qsizetype>(data.size(), maxLen));
    while (n > 0 && n < data.size() &&
           (static_cast<unsigned char>(data.at(n)) & 0xC0) == 0x80) {
        --n;
    }
    return n;
}

} // namespace

QByteArray encodeCommand(const QByteArray& command, const QList<QByteArray>& params) {
    QByteArray out = command;

    for (int i = 0; i < params.size(); ++i) {
        out += ' ';
        const QByteArray& p = params.at(i);
        const bool isLast = (i == params.size() - 1);
        const bool needsTrailing =
            isLast && (p.isEmpty() || p.contains(' ') || p.startsWith(':'));
        if (needsTrailing) {
            out += ':';
        }
        out += p;
    }

    // Enforce the RFC line limit by clipping from the end. Clipping the tail
    // preserves the command, earlier params, and any trailing-':' separator;
    // only the final (free-text) parameter is shortened.
    if (out.size() > kMaxContentLength) {
        out.truncate(utf8SafeLength(out, kMaxContentLength));
    }

    return out;
}

int trailingParamBudget(const QByteArray& command,
                        const QList<QByteArray>& middleParams,
                        int maxContent) {
    int used = command.size();
    for (const QByteArray& p : middleParams) {
        used += 1 + p.size(); // separating space + param
    }
    used += 2; // " :" in front of the free-text parameter
    return maxContent - used;
}

QList<QByteArray> splitText(const QByteArray& text, int budget) {
    QList<QByteArray> chunks;
    if (text.isEmpty()) {
        chunks << QByteArray();
        return chunks;
    }
    if (budget <= 0) {
        chunks << text; // cannot split usefully; the encoder clips it
        return chunks;
    }

    int offset = 0;
    const int size = text.size();
    while (offset < size) {
        // Separator consumed by the previous cut.
        while (offset < size && text.at(offset) == ' ') {
            ++offset;
        }
        if (offset >= size) {
            break;
        }

        const int remaining = size - offset;
        int take = utf8SafeLength(text.mid(offset), std::min(remaining, budget));

        if (take < remaining) {
            // Prefer breaking at the last space inside the window, so words are
            // not cut in half; a single word longer than the budget is hard-cut.
            const int space = text.lastIndexOf(' ', offset + take - 1);
            if (space > offset && space - offset <= take) {
                take = space - offset;
            }
        }
        if (take <= 0) {
            take = 1; // cannot happen for valid UTF-8; never loop forever
        }

        chunks << text.mid(offset, take);
        offset += take;
    }

    if (chunks.isEmpty()) {
        chunks << QByteArray();
    }
    return chunks;
}

QByteArray withCrlf(const QByteArray& line) {
    QByteArray out = line;
    out += "\r\n";
    return out;
}

} // namespace irc
