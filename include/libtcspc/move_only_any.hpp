/*
 * This file is part of libtcspc
 * Copyright 2019-2024 Board of Regents of the University of Wisconsin System
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <algorithm>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace tcspc::internal {

struct bad_move_only_any_cast : std::bad_cast {};

// Like std::any, but move-only. Does not support types that throw during move
// (will call std::terminate).
class move_only_any {
    struct polymorphic {
        virtual ~polymorphic() = default;
        virtual void move_construct_at(std::byte *storage) noexcept = 0;
        [[nodiscard]] virtual auto const_ptr() const noexcept
            -> void const * = 0;
        [[nodiscard]] virtual auto ptr() noexcept -> void * = 0;
        [[nodiscard]] virtual auto has_value() const noexcept -> bool = 0;
        [[nodiscard]] virtual auto type() const noexcept
            -> std::type_info const & = 0;
    };

    struct polymorphic_no_value : public polymorphic {
        void move_construct_at(std::byte *storage) noexcept override {
            new (storage) polymorphic_no_value();
        }

        [[nodiscard]] auto const_ptr() const noexcept
            -> void const * override {
            return nullptr;
        }

        [[nodiscard]] auto ptr() noexcept -> void * override {
            return nullptr;
        }

        [[nodiscard]] auto has_value() const noexcept -> bool override {
            return false;
        }

        [[nodiscard]] auto type() const noexcept
            -> std::type_info const & override {
            return typeid(void);
        }
    };

    // Default: Store on heap
    template <typename V> struct polymorphic_on_heap : public polymorphic {
        std::unique_ptr<V> value;

        explicit polymorphic_on_heap(std::unique_ptr<V> v)
            : value(std::move(v)) {}

        void move_construct_at(std::byte *storage) noexcept override {
            new (storage) polymorphic_on_heap(std::move(value));
        }

        [[nodiscard]] auto const_ptr() const noexcept
            -> void const * override {
            return value.get();
        }

        [[nodiscard]] auto ptr() noexcept -> void * override {
            return value.get();
        }

        [[nodiscard]] auto has_value() const noexcept -> bool override {
            return true;
        }

        [[nodiscard]] auto type() const noexcept
            -> std::type_info const & override {
            return typeid(V);
        }
    };

    // Small-buffer optimization.
    template <typename V> struct polymorphic_sbo : public polymorphic {
        V value;

        template <typename... Args>
        explicit polymorphic_sbo(Args &&...args)
            : value(std::forward<Args>(args)...) {}

        void move_construct_at(std::byte *storage) noexcept override {
            new (storage) polymorphic_sbo(std::move(value));
        }

        [[nodiscard]] auto const_ptr() const noexcept
            -> void const * override {
            return &value;
        }

        [[nodiscard]] auto ptr() noexcept -> void * override { return &value; }

        [[nodiscard]] auto has_value() const noexcept -> bool override {
            return true;
        }

        [[nodiscard]] auto type() const noexcept
            -> std::type_info const & override {
            return typeid(V);
        }
    };

    static constexpr std::size_t storage_size = std::max(
        // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
        sizeof(polymorphic_on_heap<int>), sizeof(polymorphic_sbo<void *[3]>));

    static constexpr std::size_t storage_align =
        alignof(polymorphic_on_heap<int>);

    // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays)
    alignas(storage_align) std::byte storage[storage_size];

    // Explicit decay to stop clang-tidy from complaining.
    [[nodiscard]] auto p_storage() noexcept -> std::byte * {
        return static_cast<std::byte *>(storage);
    }

    [[nodiscard]] auto poly() noexcept -> polymorphic * {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<polymorphic *>(storage);
    }

    [[nodiscard]] auto poly() const noexcept -> polymorphic const * {
        // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
        return reinterpret_cast<polymorphic const *>(storage);
    }

    template <typename V> static constexpr auto uses_sbo() noexcept -> bool {
        return sizeof(polymorphic_sbo<V>) <= storage_size &&
               alignof(polymorphic_sbo<V>) <= storage_align;
    }

    template <typename U>
    friend auto move_only_any_cast(move_only_any const &operand) -> U;

    template <typename U>
    friend auto move_only_any_cast(move_only_any &&operand) -> U;

    template <typename V>
    friend auto move_only_any_cast(move_only_any const *operand) noexcept
        -> V const *;

    template <typename V>
    friend auto move_only_any_cast(move_only_any *operand) noexcept -> V *;

  public:
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    move_only_any() { new (p_storage()) polymorphic_no_value(); }

    ~move_only_any() { poly()->~polymorphic(); }

    move_only_any(move_only_any const &other) = delete;
    auto operator=(move_only_any const &rhs) = delete;

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    move_only_any(move_only_any &&other) noexcept {
        other.poly()->move_construct_at(p_storage());
        other.reset();
    }

    auto operator=(move_only_any &&rhs) noexcept -> move_only_any & {
        poly()->~polymorphic();
        rhs.poly()->move_construct_at(p_storage());
        rhs.reset();
        return *this;
    }

    template <typename V, typename = std::enable_if_t<not std::is_same_v<
                              std::remove_reference_t<V>, move_only_any>>>
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    move_only_any(V &&value) {
        using U = std::decay_t<V>;
        if constexpr (uses_sbo<U>()) {
            new (p_storage()) polymorphic_sbo<U>(std::forward<V>(value));
        } else {
            new (p_storage()) polymorphic_on_heap<U>(
                std::make_unique<U>(std::forward<V>(value)));
        }
    }

    template <typename V> auto operator=(V &&rhs) -> move_only_any & {
        using U = std::decay_t<V>;
        poly()->~polymorphic();
        if constexpr (uses_sbo<U>()) {
            new (p_storage()) polymorphic_sbo<U>(std::forward<V>(rhs));
        } else {
            new (p_storage()) polymorphic_on_heap<U>(
                std::make_unique<U>(std::forward<V>(rhs)));
        }
        return *this;
    }

    template <typename V, typename... Args>
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    explicit move_only_any([[maybe_unused]] std::in_place_type_t<V> tag,
                           Args &&...args) {
        if constexpr (uses_sbo<V>()) {
            new (p_storage()) polymorphic_sbo<V>(std::forward<Args>(args)...);
        } else {
            new (p_storage()) polymorphic_on_heap(
                std::make_unique<V>(std::forward<Args>(args)...));
        }
    }

    template <typename V, typename T, typename... Args>
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    explicit move_only_any([[maybe_unused]] std::in_place_type_t<V> tag,
                           std::initializer_list<T> il, Args &&...args) {
        if constexpr (uses_sbo<V>()) {
            new (p_storage())
                polymorphic_sbo<V>(il, std::forward<Args>(args)...);
        } else {
            new (p_storage()) polymorphic_on_heap(
                std::make_unique<V>(il, std::forward<Args>(args)...));
        }
    }

    template <typename V, typename... Args>
    auto emplace(Args &&...args) -> std::decay_t<V> & {
        using U = std::decay_t<V>;
        poly()->~polymorphic();
        if constexpr (uses_sbo<U>()) {
            new (p_storage()) polymorphic_sbo<U>(std::forward<Args>(args)...);
            return *static_cast<U *>(
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                reinterpret_cast<polymorphic_sbo<U> *>(storage)->ptr());
        } else {
            new (p_storage()) polymorphic_on_heap<U>(
                std::make_unique<U>(std::forward<Args>(args)...));
            return *static_cast<U *>(
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                reinterpret_cast<polymorphic_on_heap<U> *>(storage)->ptr());
        }
    }

    template <typename V, typename T, typename... Args>
    auto emplace(std::initializer_list<T> il, Args &&...args)
        -> std::decay_t<V> & {
        using U = std::decay_t<V>;
        poly()->~polymorphic();
        if constexpr (uses_sbo<U>()) {
            new (p_storage())
                polymorphic_sbo<U>(il, std::forward<Args>(args)...);
            return *static_cast<U *>(
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                reinterpret_cast<polymorphic_sbo<U> *>(storage)->ptr());
        } else {
            new (p_storage()) polymorphic_on_heap<U>(
                std::make_unique<U>(il, std::forward<Args>(args)...));
            return *static_cast<U *>(
                // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
                reinterpret_cast<polymorphic_on_heap<U> *>(storage)->ptr());
        }
    }

    void reset() noexcept {
        poly()->~polymorphic();
        new (p_storage()) polymorphic_no_value();
    }

    void swap(move_only_any &other) noexcept {
        using std::swap;
        swap(*this, other);
    }

    [[nodiscard]] auto has_value() const noexcept -> bool {
        // We could use type() == typeid(void), but this can avoid value
        // comparison of std::type_info (which, at least in theory, could
        // involve a strcmp).
        return poly()->has_value();
    }

    [[nodiscard]] auto type() const noexcept -> std::type_info const & {
        return poly()->type();
    }
};

// U must be V const &.
template <typename U>
auto move_only_any_cast(move_only_any const &operand) -> U {
    using V = std::remove_cv_t<std::remove_reference_t<U>>;
    auto const *ptr = move_only_any_cast<V>(&operand);
    if (ptr == nullptr)
        throw bad_move_only_any_cast();
    return *ptr;
}

// U can be V & or V const &, but not V.
template <typename U> auto move_only_any_cast(move_only_any &operand) -> U {
    using V = std::remove_cv_t<std::remove_reference_t<U>>;
    auto *ptr = move_only_any_cast<V>(&operand);
    if (ptr == nullptr)
        throw bad_move_only_any_cast();
    return *ptr;
}

// U can be V const & or V, but not V &.
// NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
template <typename U> auto move_only_any_cast(move_only_any &&operand) -> U {
    using V = std::remove_cv_t<std::remove_reference_t<U>>;
    auto *ptr = move_only_any_cast<V>(&operand);
    if (ptr == nullptr)
        throw bad_move_only_any_cast();
    return std::move(*ptr);
}

// V must match type of contained value.
template <typename V>
auto move_only_any_cast(move_only_any const *operand) noexcept -> V const * {
    static_assert(not std::is_void_v<V>);
    if (operand->type() != typeid(V))
        return nullptr;
    return static_cast<V const *>(operand->poly()->const_ptr());
}

// V must match type of contained value.
template <typename V>
auto move_only_any_cast(move_only_any *operand) noexcept -> V * {
    static_assert(not std::is_void_v<V>);
    if (operand->type() != typeid(V))
        return nullptr;
    return static_cast<V *>(operand->poly()->ptr());
}

template <typename V, typename... Args>
auto make_move_only_any(Args &&...args) -> move_only_any {
    static_assert(not std::is_void_v<V>);
    return move_only_any(std::in_place_type<V>, std::forward<Args>(args)...);
}

template <typename V, typename T, typename... Args>
auto make_move_only_any(std::initializer_list<T> il, Args &&...args)
    -> move_only_any {
    static_assert(not std::is_void_v<V>);
    return move_only_any(std::in_place_type<V>, il,
                         std::forward<Args>(args)...);
}

} // namespace tcspc::internal
