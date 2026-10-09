#include "loadbar_version.h"
#include "platform/windows.hpp"

#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
struct ModuleDeleter {
    void operator()(HINSTANCE__ *module) const noexcept {
        FreeLibrary(module);
    }
};
} // namespace

void verify_resources(const wchar_t *path, const wchar_t *license) {
    // DATAFILE/IMAGE_RESOURCE mapping never executes the application's entry point.
    const std::unique_ptr<HINSTANCE__, ModuleDeleter> module(
        LoadLibraryExW(path, nullptr, LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE));
    if (!module) {
        throw std::runtime_error("Map artifact resources");
    }
    for (const auto type : {RT_GROUP_ICON, RT_VERSION, RT_MANIFEST}) {
        if (!FindResourceW(module.get(), MAKEINTRESOURCEW(1), type)) {
            throw std::runtime_error("Required embedded resource missing");
        }
    }
    const auto manifest = FindResourceW(module.get(), MAKEINTRESOURCEW(1), RT_MANIFEST);
    if (!FindResourceW(module.get(), MAKEINTRESOURCEW(203), RT_DIALOG)) {
        throw std::runtime_error("Embedded license dialog missing");
    }
    if (!manifest) {
        throw std::runtime_error("Embedded manifest missing");
    }
    const auto manifest_loaded = LoadResource(module.get(), manifest);
    const auto *manifest_data =
        manifest_loaded ? static_cast<const char *>(LockResource(manifest_loaded)) : nullptr;
    const auto manifest_size = SizeofResource(module.get(), manifest);
    if (!manifest_data || !manifest_size || manifest_size > 1024 * 1024) {
        throw std::runtime_error("Read embedded manifest");
    }
    const std::string_view manifest_text(manifest_data, manifest_size);
    for (const auto required : {"level=\"asInvoker\"", "uiAccess=\"false\"", ">PerMonitorV2<",
                                "Microsoft.Windows.Common-Controls"}) {
        if (manifest_text.find(required) == std::string_view::npos) {
            throw std::runtime_error("Required embedded manifest policy missing");
        }
    }
    const auto notice = FindResourceW(module.get(), MAKEINTRESOURCEW(101), RT_RCDATA);
    if (!notice) {
        throw std::runtime_error("Embedded notice missing");
    }
    const auto loaded = LoadResource(module.get(), notice);
    if (!loaded) {
        throw std::runtime_error("Load embedded notice");
    }
    const auto *data = static_cast<const char *>(LockResource(loaded));
    const auto size = SizeofResource(module.get(), notice);
    if (!data || size < 11 || std::memcmp(data, "MIT License", 11) != 0) {
        throw std::runtime_error("Embedded notice content invalid");
    }
    std::ifstream source(std::filesystem::path(license), std::ios::binary);
    if (!source) {
        throw std::runtime_error("Open source license");
    }
    const std::string expected_notice((std::istreambuf_iterator<char>(source)),
                                      std::istreambuf_iterator<char>());
    if (source.bad() || std::string_view(data, size) != expected_notice) {
        throw std::runtime_error("Embedded notice differs from source license");
    }
    for (const auto &[id, filename] : {std::pair{201, L"resources/fonts/GeistMono.ttf"},
                                       std::pair{202, L"resources/fonts/OFL.txt"},
                                       std::pair{204, L"third_party/nvapi/NOTICE.txt"},
                                       std::pair{205, L"third_party/adlx/LICENSE"},
                                       std::pair{206, L"third_party/igcl/LICENSE"}}) {
        const auto item = FindResourceW(module.get(), MAKEINTRESOURCEW(id), RT_RCDATA);
        const auto memory = item ? LoadResource(module.get(), item) : nullptr;
        const auto *contents = memory ? static_cast<const char *>(LockResource(memory)) : nullptr;
        const auto length = item ? SizeofResource(module.get(), item) : 0;
        std::ifstream file(std::filesystem::path(license).parent_path() / filename,
                           std::ios::binary);
        if (!file || !contents || !length) {
            throw std::runtime_error("Embedded font or third-party notice missing");
        }
        const std::string expected((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());
        if (file.bad() || std::string_view(contents, length) != expected) {
            throw std::runtime_error("Embedded font/notice differs from source");
        }
    }
    DWORD unused{};
    const auto bytes = GetFileVersionInfoSizeW(path, &unused);
    if (!bytes || bytes > 1024 * 1024) {
        throw std::runtime_error("Version resource size");
    }
    std::vector<std::byte> buffer(bytes);
    if (!GetFileVersionInfoW(path, 0, bytes, buffer.data())) {
        throw std::runtime_error("Read version resource");
    }
    void *fixed_data{};
    UINT fixed_size{};
    if (!VerQueryValueW(buffer.data(), L"\\", &fixed_data, &fixed_size) || !fixed_data ||
        fixed_size < sizeof(VS_FIXEDFILEINFO)) {
        throw std::runtime_error("Fixed version metadata missing");
    }
    const auto &fixed = *static_cast<const VS_FIXEDFILEINFO *>(fixed_data);
    const DWORD major_minor = MAKELONG(LOADBAR_VERSION_MINOR, LOADBAR_VERSION_MAJOR);
    const DWORD patch_build = MAKELONG(0, LOADBAR_VERSION_PATCH);
    if (fixed.dwSignature != VS_FFI_SIGNATURE || fixed.dwFileVersionMS != major_minor ||
        fixed.dwFileVersionLS != patch_build || fixed.dwProductVersionMS != major_minor ||
        fixed.dwProductVersionLS != patch_build) {
        throw std::runtime_error("Fixed version metadata mismatch");
    }
    const auto version = std::format(L"{}.{}.{}", LOADBAR_VERSION_MAJOR, LOADBAR_VERSION_MINOR,
                                     LOADBAR_VERSION_PATCH);
    for (const auto &[key, expected] :
         {std::pair{L"\\StringFileInfo\\040904b0\\ProductName", L"Loadbar"},
          std::pair{L"\\StringFileInfo\\040904b0\\OriginalFilename", L"Loadbar.exe"},
          std::pair{L"\\StringFileInfo\\040904b0\\CompanyName", L"Vorta"},
          std::pair{L"\\StringFileInfo\\040904b0\\FileVersion", version.c_str()},
          std::pair{L"\\StringFileInfo\\040904b0\\ProductVersion", version.c_str()}}) {
        void *value{};
        UINT characters{};
        if (!VerQueryValueW(buffer.data(), key, &value, &characters) || !value || !characters ||
            std::wstring_view(static_cast<const wchar_t *>(value), characters - 1) != expected) {
            throw std::runtime_error("Product metadata mismatch");
        }
    }
}
