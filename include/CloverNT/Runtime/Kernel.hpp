#pragma once
#include <CloverNT/Runtime/Kernel.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <utility>

namespace CloverNT::Runtime::Kernel {

[[nodiscard]] inline std::uintptr_t moduleBase(const char* module) noexcept {
    return CloverModuleBase(module);
}

[[nodiscard]] inline std::size_t moduleSize(const char* module) noexcept {
    return CloverModuleSize(module);
}

template <typename Sig>
class InlineHook {
public:
    InlineHook() = default;

    [[nodiscard]] static InlineHook install(const std::uintptr_t target, Sig* detour) noexcept {
        void*            original = nullptr;
        const CloverHook handle   = CloverHookInline(target, reinterpret_cast<void*>(detour), &original);
        return adopt(handle, static_cast<Sig*>(original));
    }

    [[nodiscard]] static InlineHook installImport(const char* module, const char* symbol, Sig* detour) noexcept {
        void*            original = nullptr;
        const CloverHook handle   = CloverHookImport(module, symbol, reinterpret_cast<void*>(detour), &original);
        return adopt(handle, static_cast<Sig*>(original));
    }

    InlineHook(InlineHook&& other) noexcept
        : mHandle(std::exchange(other.mHandle, 0)), mOriginal(std::exchange(other.mOriginal, nullptr)) {}

    InlineHook& operator=(InlineHook&& other) noexcept {
        if (this != &other) {
            reset();
            mHandle   = std::exchange(other.mHandle, 0);
            mOriginal = std::exchange(other.mOriginal, nullptr);
        }
        return *this;
    }

    InlineHook(const InlineHook&)            = delete;
    InlineHook& operator=(const InlineHook&) = delete;

    ~InlineHook() {
        reset();
    }

    void reset() noexcept {
        if (mHandle != 0) {
            CloverHookRemove(mHandle);
            mHandle   = 0;
            mOriginal = nullptr;
        }
    }

    [[nodiscard]] bool valid() const noexcept {
        return mHandle != 0;
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return valid();
    }

    template <typename... Args>
    auto original(Args&&... args) const -> decltype(mOriginal(std::forward<Args>(args)...)) {
        return mOriginal(std::forward<Args>(args)...);
    }

    [[nodiscard]] Sig* originalFn() const noexcept {
        return mOriginal;
    }

private:
    [[nodiscard]] static InlineHook adopt(CloverHook handle, Sig* original) noexcept {
        InlineHook hook;
        hook.mHandle   = handle;
        hook.mOriginal = original;
        return hook;
    }

    CloverHook mHandle{0};
    Sig*       mOriginal{nullptr};
};

template <typename Sig>
[[nodiscard]] InlineHook<Sig> hookInline(std::uintptr_t target, Sig* detour) noexcept {
    return InlineHook<Sig>::install(target, detour);
}

template <typename Sig>
[[nodiscard]] InlineHook<Sig> hookImport(const char* module, const char* symbol, Sig* detour) noexcept {
    return InlineHook<Sig>::installImport(module, symbol, detour);
}

template <typename Sig>
class Original;

template <typename R, typename... Args>
class Original<R(Args...)> {
public:
    using Pointer = R (*)(Args...);

    explicit Original(void** slot) noexcept : mSlot(slot) {}

    auto operator()(Args... args) const -> R {
        auto* const fn = reinterpret_cast<Pointer>(*mSlot);
        if (fn == nullptr) {
            if constexpr (std::is_void_v<R>) {
                return;
            } else {
                return R{};
            }
        }
        if constexpr (std::is_void_v<R>) {
            fn(std::forward<Args>(args)...);
        } else {
            return fn(std::forward<Args>(args)...);
        }
    }

    [[nodiscard]] auto get() const noexcept -> Pointer {
        return reinterpret_cast<Pointer>(*mSlot);
    }

private:
    void** mSlot{};
};

namespace Detail {

    template <typename Sig>
    class DetourState;

    template <typename R, typename... Args>
    class DetourState<R(Args...)> {
    public:
        DetourState()          = default;
        virtual ~DetourState() = default;

        DetourState(const DetourState&)            = delete;
        DetourState& operator=(const DetourState&) = delete;

        virtual auto invoke(Original<R(Args...)>& original, Args... args) -> R = 0;

        [[nodiscard]] auto slot() noexcept -> void** {
            return &mOriginal;
        }

    private:
        void* mOriginal{};
    };

    template <typename Sig, typename D>
    class DetourStateImpl;

    template <typename R, typename... Args, typename D>
    class DetourStateImpl<R(Args...), D> final : public DetourState<R(Args...)> {
    public:
        explicit DetourStateImpl(D&& detour) : mDetour(std::forward<D>(detour)) {}

        auto invoke(Original<R(Args...)>& original, Args... args) -> R override {
            if constexpr (std::is_void_v<R>) {
                std::invoke(mDetour, original, std::forward<Args>(args)...);
            } else {
                return std::invoke(mDetour, original, std::forward<Args>(args)...);
            }
        }

    private:
        std::remove_cvref_t<D> mDetour;
    };

    template <typename Sig, int MaxSlots = 64>
    class DetourSlots;

    template <typename R, typename... Args, int MaxSlots>
    class DetourSlots<R(Args...), MaxSlots> {
    public:
        using State = DetourState<R(Args...)>;
        using FnPtr = R (*)(Args...);

        static auto instance() -> DetourSlots& {
            static DetourSlots pool;
            return pool;
        }

        [[nodiscard]] auto allocate(const std::shared_ptr<State>& state) -> std::optional<std::pair<int, FnPtr>> {
            std::lock_guard lock(mMutex);
            for (int i = 0; i < MaxSlots; ++i) {
                if (!mOccupied[i] && mInFlight[i].load(std::memory_order_acquire) == 0) {
                    mOccupied[i] = true;
                    mStates[i]   = state;
                    return std::pair{i, kTable[i]};
                }
            }
            return std::nullopt;
        }

        void release(const int index) {
            if (index < 0 || index >= MaxSlots) {
                return;
            }
            std::lock_guard lock(mMutex);
            mStates[index].reset();
            mOccupied[index] = false;
        }

        void drain(const int index) {
            if (index < 0 || index >= MaxSlots) {
                return;
            }
            while (mInFlight[index].load(std::memory_order_acquire) != 0) {
                std::this_thread::yield();
            }
        }

    private:
        DetourSlots() = default;

        [[nodiscard]] auto state(const int index) -> std::shared_ptr<State> {
            std::lock_guard lock(mMutex);
            return mStates[index].lock();
        }

        template <int I>
        static auto trampoline(Args... args) -> R {
            auto& pool = instance();
            pool.mInFlight[I].fetch_add(1, std::memory_order_acq_rel);
            struct InFlightGuard {
                std::atomic<int>* counter;
                ~InFlightGuard() {
                    counter->fetch_sub(1, std::memory_order_release);
                }
            } inFlightGuard{&pool.mInFlight[I]};

            auto held = pool.state(I);
            if (!held) {
                if constexpr (std::is_void_v<R>) {
                    return;
                } else {
                    return R{};
                }
            }
            Original<R(Args...)> original(held->slot());
            if constexpr (std::is_void_v<R>) {
                held->invoke(original, std::forward<Args>(args)...);
            } else {
                return held->invoke(original, std::forward<Args>(args)...);
            }
        }

        template <int... Is>
        static constexpr auto makeTable(std::integer_sequence<int, Is...>) -> std::array<FnPtr, MaxSlots> {
            return {&trampoline<Is>...};
        }

        static constexpr auto kTable = makeTable(std::make_integer_sequence<int, MaxSlots>{});

        std::mutex                                 mMutex;
        std::array<bool, MaxSlots>                 mOccupied{};
        std::array<std::atomic<int>, MaxSlots>     mInFlight{};
        std::array<std::weak_ptr<State>, MaxSlots> mStates{};
    };

} // namespace Detail

template <typename Sig>
class Detour {
public:
    Detour() = default;

    template <typename D>
    [[nodiscard]] static Detour install(const std::uintptr_t target, D&& detour) {
        Detour self;
        if (target == 0) {
            return self;
        }
        auto state      = std::make_shared<Detail::DetourStateImpl<Sig, D>>(std::forward<D>(detour));
        auto allocation = Detail::DetourSlots<Sig>::instance().allocate(state);
        if (!allocation) {
            return self;
        }
        const auto [index, raw] = *allocation;
        const CloverHook handle = CloverHookInline(target, reinterpret_cast<void*>(raw), state->slot());
        if (handle == 0) {
            Detail::DetourSlots<Sig>::instance().release(index);
            return self;
        }
        self.mHandle = handle;
        self.mState  = std::move(state);
        self.mSlot   = index;
        return self;
    }

    Detour(Detour&& other) noexcept
        : mHandle(std::exchange(other.mHandle, 0)),
          mState(std::move(other.mState)),
          mSlot(std::exchange(other.mSlot, -1)) {}

    Detour& operator=(Detour&& other) noexcept {
        if (this != &other) {
            reset();
            mHandle = std::exchange(other.mHandle, 0);
            mState  = std::move(other.mState);
            mSlot   = std::exchange(other.mSlot, -1);
        }
        return *this;
    }

    Detour(const Detour&)            = delete;
    Detour& operator=(const Detour&) = delete;

    ~Detour() {
        reset();
    }

    void reset() noexcept {
        if (mHandle != 0) {
            CloverHookRemove(mHandle);
            mHandle = 0;
        }
        if (mSlot >= 0) {
            Detail::DetourSlots<Sig>::instance().drain(mSlot);
            Detail::DetourSlots<Sig>::instance().release(mSlot);
            mSlot = -1;
        }
        mState.reset();
    }

    [[nodiscard]] bool valid() const noexcept {
        return mHandle != 0;
    }
    [[nodiscard]] explicit operator bool() const noexcept {
        return valid();
    }

private:
    CloverHook                                mHandle{0};
    std::shared_ptr<Detail::DetourState<Sig>> mState;
    int                                       mSlot{-1};
};

template <typename Sig, typename D>
[[nodiscard]] Detour<Sig> hookDetour(const std::uintptr_t target, D&& detour) {
    return Detour<Sig>::install(target, std::forward<D>(detour));
}


inline void log(const CloverLogLevel level, const std::string_view category, const std::string_view message) noexcept {
    const std::string cat(category);
    const std::string msg(message);
    CloverLogWrite(level, cat.c_str(), msg.c_str());
}

inline void logTrace(const std::string_view cat, const std::string_view msg) noexcept {
    log(CLOVER_LOG_TRACE, cat, msg);
}
inline void logDebug(const std::string_view cat, const std::string_view msg) noexcept {
    log(CLOVER_LOG_DEBUG, cat, msg);
}
inline void logInfo(const std::string_view cat, const std::string_view msg) noexcept {
    log(CLOVER_LOG_INFO, cat, msg);
}
inline void logWarn(const std::string_view cat, const std::string_view msg) noexcept {
    log(CLOVER_LOG_WARN, cat, msg);
}
inline void logError(const std::string_view cat, const std::string_view msg) noexcept {
    log(CLOVER_LOG_ERROR, cat, msg);
}
inline void logFatal(const std::string_view cat, const std::string_view msg) noexcept {
    log(CLOVER_LOG_FATAL, cat, msg);
}

class Subscription {
public:
    using Off = void(CloverNT_KERNEL_CALL*)(CloverSubscription);

    Subscription() = default;
    Subscription(const CloverSubscription sub, Off off) noexcept : mSub(sub), mOff(off) {}

    Subscription(Subscription&& other) noexcept
        : mSub(std::exchange(other.mSub, 0)), mOff(std::exchange(other.mOff, nullptr)) {}
    Subscription& operator=(Subscription&& other) noexcept {
        if (this != &other) {
            reset();
            mSub = std::exchange(other.mSub, 0);
            mOff = std::exchange(other.mOff, nullptr);
        }
        return *this;
    }
    Subscription(const Subscription&)            = delete;
    Subscription& operator=(const Subscription&) = delete;

    ~Subscription() {
        reset();
    }

    void reset() noexcept {
        if (mSub != 0 && mOff != nullptr) {
            mOff(mSub);
        }
        mSub = 0;
        mOff = nullptr;
    }

    [[nodiscard]] bool valid() const noexcept {
        return mSub != 0;
    }

private:
    CloverSubscription mSub{0};
    Off                mOff{nullptr};
};

[[nodiscard]] inline Subscription onDllLoad(CloverDllLoadCallback cb, void* user = nullptr) noexcept {
    return Subscription{CloverDllLoadSubscribe(cb, user), &CloverDllLoadUnsubscribe};
}

[[nodiscard]] inline Subscription onEvent(const char* name, CloverEventCallback cb, void* user = nullptr) noexcept {
    return Subscription{CloverEventSubscribe(name, cb, user), &CloverEventUnsubscribe};
}


[[nodiscard]] inline std::string_view pluginsDir() noexcept {
    const char* dir = CloverPluginsDirectory();
    return dir != nullptr ? std::string_view{dir} : std::string_view{};
}

[[nodiscard]] inline std::string_view qqVersion() noexcept {
    const char* ver = CloverQQVersion();
    return ver != nullptr ? std::string_view{ver} : std::string_view{};
}

[[nodiscard]] inline std::string_view selfVersion() noexcept {
    const char* ver = CloverSelfVersion();
    return ver != nullptr ? std::string_view{ver} : std::string_view{};
}

} // namespace CloverNT::Runtime::Kernel
