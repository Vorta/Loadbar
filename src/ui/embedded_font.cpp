#include "ui/embedded_font.hpp"
#include <string_view>

namespace loadbar {
namespace {
struct ResourceBytes {
    const void *data{};
    DWORD size{};
};
ResourceBytes resource(unsigned id) {
    const auto module = GetModuleHandleW(nullptr);
    const auto found = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
    const auto loaded = found ? LoadResource(module, found) : nullptr;
    return {loaded ? LockResource(loaded) : nullptr, found ? SizeofResource(module, found) : 0};
}
INT_PTR CALLBACK license_proc(HWND window, UINT message, WPARAM wparam, LPARAM) noexcept {
    try {
        if (message == WM_INITDIALOG) {
            std::string text;
            for (const auto id : {101U, 202U, 204U, 205U, 206U}) {
                const auto bytes = resource(id);
                if (!bytes.data || !bytes.size) {
                    EndDialog(window, IDCANCEL);
                    return TRUE;
                }
                text.append(static_cast<const char *>(bytes.data), bytes.size);
                text += "\r\n\r\n";
            }
            auto wide = utf16(text);
            // Native multiline EDIT expects CRLF even when repository notices use LF.
            std::wstring lines;
            lines.reserve(wide.size());
            wchar_t previous{};
            for (const auto c : wide) {
                if (c == L'\n' && previous != L'\r') {
                    lines += L'\r';
                }
                lines += c;
                previous = c;
            }
            SetDlgItemTextW(window, 3000, lines.c_str());
            return TRUE;
        }
        if (message == WM_CLOSE ||
            (message == WM_COMMAND && (LOWORD(wparam) == IDOK || LOWORD(wparam) == IDCANCEL))) {
            EndDialog(window, IDOK);
            return TRUE;
        }
    } catch (...) {
        MessageBoxW(window, L"The embedded notices could not be loaded.", L"Loadbar",
                    MB_OK | MB_ICONERROR);
        EndDialog(window, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}
} // namespace
void show_licenses(HWND owner) {
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(203), owner, license_proc, 0) ==
        -1) {
        MessageBoxW(owner, L"The license dialog could not be opened.", L"Loadbar",
                    MB_OK | MB_ICONERROR);
    }
}
EmbeddedFont::~EmbeddedFont() {
    reset();
}
void EmbeddedFont::reset() noexcept {
    collection_.Reset();
    if (registered_) {
        factory_->UnregisterFontFileLoader(loader_.Get());
    }
    registered_ = false;
    loader_.Reset();
    factory_.Reset();
}
HRESULT EmbeddedFont::initialize(IDWriteFactory *factory) {
    if (collection_) {
        return S_OK;
    }
    reset();
    const auto bytes = resource(201);
    if (!bytes.data || !bytes.size) {
        return HRESULT_FROM_WIN32(ERROR_RESOURCE_DATA_NOT_FOUND);
    }
    auto result = factory->QueryInterface(IID_PPV_ARGS(&factory_));
    if (SUCCEEDED(result)) {
        result = factory_->CreateInMemoryFontFileLoader(&loader_);
    }
    if (SUCCEEDED(result)) {
        result = factory_->RegisterFontFileLoader(loader_.Get());
        registered_ = SUCCEEDED(result);
    }
    Microsoft::WRL::ComPtr<IDWriteFontFile> file;
    if (SUCCEEDED(result)) {
        result = loader_->CreateInMemoryFontFileReference(factory_.Get(), bytes.data, bytes.size,
                                                          nullptr, &file);
    }
    Microsoft::WRL::ComPtr<IDWriteFontSetBuilder1> builder;
    if (SUCCEEDED(result)) {
        result = factory_->CreateFontSetBuilder(&builder);
    }
    if (SUCCEEDED(result)) {
        result = builder->AddFontFile(file.Get());
    }
    Microsoft::WRL::ComPtr<IDWriteFontSet> set;
    if (SUCCEEDED(result)) {
        result = builder->CreateFontSet(&set);
    }
    if (SUCCEEDED(result)) {
        result = factory_->CreateFontCollectionFromFontSet(set.Get(), &collection_);
    }
    if (SUCCEEDED(result)) {
        UINT32 index{};
        BOOL exists{};
        result = collection_->FindFamilyName(L"Geist Mono", &index, &exists);
        if (SUCCEEDED(result) && !exists) {
            result = DWRITE_E_NOFONT;
        }
    }
    if (FAILED(result)) {
        reset();
    }
    return result;
}
} // namespace loadbar
