#pragma once

#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace slc::compat {

template <typename E>
class unexpected {
public:
    explicit unexpected(E error) : m_error(std::move(error)) {}
    const E& error() const& { return m_error; }
    E&& error() && { return std::move(m_error); }
private:
    E m_error;
};

template <typename T, typename E>
class expected {
public:
    expected(const T& value) : m_value(value) {}
    expected(T&& value) : m_value(std::move(value)) {}
    template <typename U>
    expected(unexpected<U> error)
        : m_value(E(std::move(error).error())) {}

    template <typename U>
    expected(const expected<U, E>& other) {
        if (other) m_value = T(other.value());
        else m_value = other.error();
    }

    template <typename U>
    expected(expected<U, E>&& other) {
        if (other) m_value = T(std::move(other).value());
        else m_value = std::move(other).error();
    }

    explicit operator bool() const { return std::holds_alternative<T>(m_value); }
    bool has_value() const { return static_cast<bool>(*this); }
    T& operator*() { return std::get<T>(m_value); }
    const T& operator*() const { return std::get<T>(m_value); }
    T& value() { return std::get<T>(m_value); }
    const T& value() const { return std::get<T>(m_value); }
    T* operator->() { return &std::get<T>(m_value); }
    const T* operator->() const { return &std::get<T>(m_value); }
    E& error() { return std::get<E>(m_value); }
    const E& error() const { return std::get<E>(m_value); }

private:
    std::variant<T, E> m_value;
};

template <typename E>
class expected<void, E> {
public:
    expected() = default;
    template <typename U>
    expected(unexpected<U> error) : m_error(E(std::move(error).error())) {}

    explicit operator bool() const { return !m_error.has_value(); }
    bool has_value() const { return static_cast<bool>(*this); }
    void value() const {}
    void operator*() const {}
    expected* operator->() { return this; }
    const expected* operator->() const { return this; }
    E& error() { return *m_error; }
    const E& error() const { return *m_error; }

private:
    std::optional<E> m_error;
};

} // namespace slc::compat
