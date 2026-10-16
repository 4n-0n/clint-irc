#pragma once

#include <optional>
#include <utility>

namespace irc {

// Result<T,E>: value-returning error handling for expected failures.
// Functions return either a value or an error; they do NOT throw for normal
// flow. Exceptions remain only for truly exceptional cases (e.g. OOM).
//
// Contract: call isOk()/isErr() before value()/error().
template <typename T, typename E>
class Result {
public:
    static Result ok(T value) {
        Result r;
        r.m_value = std::move(value);
        return r;
    }

    static Result err(E error) {
        Result r;
        r.m_error = std::move(error);
        return r;
    }

    [[nodiscard]] bool isOk() const { return m_value.has_value(); }
    [[nodiscard]] bool isErr() const { return m_error.has_value(); }

    [[nodiscard]] const T& value() const { return *m_value; }
    [[nodiscard]] T& value() { return *m_value; }

    [[nodiscard]] const E& error() const { return *m_error; }

private:
    Result() = default;
    std::optional<T> m_value;
    std::optional<E> m_error;
};

} // namespace irc
