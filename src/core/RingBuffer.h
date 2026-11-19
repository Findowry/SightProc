#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace sysmon {

/**
 * @brief Sabit kapasiteli, yığın tahsisi yapmayan dairesel veri tamponu.
 */
template <typename T, std::size_t Capacity>
class RingBuffer {
    static_assert(Capacity > 0, "RingBuffer kapasitesi sıfırdan büyük olmalıdır.");

public:
    constexpr RingBuffer() noexcept = default;

    void push(const T& value) noexcept {
        m_data[m_head] = value;
        m_head = (m_head + 1) % Capacity;
        if (m_count < Capacity) {
            ++m_count;
        }
    }

    void clear() noexcept {
        m_head = 0;
        m_count = 0;
    }

    [[nodiscard]] constexpr std::size_t capacity() const noexcept {
        return Capacity;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return m_count;
    }

    [[nodiscard]] bool empty() const noexcept {
        return m_count == 0;
    }

    /**
     * @brief Kronolojik sırayla öğe erişimi (0 = en eski, size()-1 = en yeni).
     */
    [[nodiscard]] const T& operator[](std::size_t index) const noexcept {
        const std::size_t start = (m_count == Capacity) ? m_head : 0;
        return m_data[(start + index) % Capacity];
    }

    [[nodiscard]] T latest(T fallback = T{}) const noexcept {
        if (m_count == 0) {
            return fallback;
        }
        const std::size_t lastIndex = (m_head + Capacity - 1) % Capacity;
        return m_data[lastIndex];
    }

    [[nodiscard]] T maxValue(T fallback = T{}) const noexcept {
        if (m_count == 0) {
            return fallback;
        }
        T best = (*this)[0];
        for (std::size_t i = 1; i < m_count; ++i) {
            best = std::max(best, (*this)[i]);
        }
        return best;
    }

private:
    std::array<T, Capacity> m_data{};
    std::size_t m_head{0};
    std::size_t m_count{0};
};

} // namespace sysmon
