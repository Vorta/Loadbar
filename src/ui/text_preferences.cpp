#include "ui/text_preferences.hpp"

#include <roapi.h>
#include <windows.ui.viewmanagement.h>
#include <wrl/event.h>
#include <wrl/wrappers/corewrappers.h>

#include <algorithm>
#include <cmath>
#include <mutex>

namespace loadbar {
struct TextPreferences::Impl {
    struct Notification {
        std::mutex mutex;
        HWND target{};
        UINT message{};
    };
    // WinRT can retain a callback during revocation. Only this closed notification state,
    // never the Application or a raw renderer pointer, may outlive the UI owner.
    std::shared_ptr<Notification> notification = std::make_shared<Notification>();
    Microsoft::WRL::ComPtr<ABI::Windows::UI::ViewManagement::IUISettings2> settings;
    EventRegistrationToken cookie{};
    bool subscribed{};
    ~Impl() {
        {
            std::lock_guard lock(notification->mutex);
            notification->target = nullptr;
        }
        if (subscribed) {
            settings->remove_TextScaleFactorChanged(cookie);
        }
    }
};
TextPreferences::TextPreferences(HWND target, UINT message) : impl_(std::make_unique<Impl>()) {
    impl_->notification->target = target;
    impl_->notification->message = message;
    Microsoft::WRL::ComPtr<IInspectable> instance;
    const Microsoft::WRL::Wrappers::HStringReference name(L"Windows.UI.ViewManagement.UISettings");
    if (FAILED(RoActivateInstance(name.Get(), &instance)) ||
        FAILED(instance.As(&impl_->settings))) {
        return;
    }
    auto state = impl_->notification;
    const auto callback = Microsoft::WRL::Callback<ABI::Windows::Foundation::ITypedEventHandler<
        ABI::Windows::UI::ViewManagement::UISettings *, IInspectable *>>(
        [state](ABI::Windows::UI::ViewManagement::IUISettings *, IInspectable *) -> HRESULT {
            try {
                std::lock_guard lock(state->mutex);
                if (state->target && !PostMessageW(state->target, state->message, 0, 0)) {
                    return HRESULT_FROM_WIN32(GetLastError());
                }
                return S_OK;
            } catch (...) {
                return E_FAIL;
            }
        });
    impl_->subscribed =
        SUCCEEDED(impl_->settings->add_TextScaleFactorChanged(callback.Get(), &impl_->cookie));
}
TextPreferences::~TextPreferences() = default;
float TextPreferences::scale() const noexcept {
    double scale = 1;
    if (!impl_->settings || FAILED(impl_->settings->get_TextScaleFactor(&scale)) ||
        !std::isfinite(scale)) {
        return 1;
    }
    return static_cast<float>(std::clamp(scale, 1.0, 2.25));
}
} // namespace loadbar
