#include "app/application.hpp"

#include <exception>
#include <roapi.h>

namespace {
struct Apartment {
    HRESULT result{RoInitialize(RO_INIT_SINGLETHREADED)};
    ~Apartment() {
        if (SUCCEEDED(result)) {
            RoUninitialize();
        }
    }
};
} // namespace

int WINAPI wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE, _In_ PWSTR, _In_ int) {
    try {
        const Apartment apartment;
        if (FAILED(apartment.result)) {
            throw std::runtime_error("Initialize Windows Runtime failed");
        }
        loadbar::Application application(instance);
        return application.run();
    } catch (const std::exception &error) {
        MessageBoxW(nullptr, loadbar::utf16(error.what()).c_str(), L"Loadbar startup error",
                    MB_OK | MB_ICONERROR);
        return 1;
    } catch (...) {
        MessageBoxW(nullptr, L"Unexpected startup failure", L"Loadbar", MB_OK | MB_ICONERROR);
        return 1;
    }
}
