#include <windows.h>
#include <initguid.h>
#include <newdev.h>
#include <setupapi.h>
#include <softpub.h>
#include <wintrust.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <devguid.h>
#include <devpkey.h>
#include <propvarutil.h>
#include <VersionHelpers.h>
#include <winternl.h>

#include "VirtualAudioIdentifiers.h"
#include "VirtualAudioPackagePolicy.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using cuelet::virtual_audio::driverInfName;
using cuelet::virtual_audio::driverBinaryName;
using cuelet::virtual_audio::driverServiceName;
using cuelet::virtual_audio::hardwareId;
using cuelet::virtual_audio::providerName;

inline constexpr GUID audioEndpointClass{
    0xc166523c, 0xfe0c, 0x4a94,
    {0xa5, 0x86, 0xf1, 0xa8, 0x0c, 0xfb, 0xbf, 0x3e}};

enum class ExitCode : int {
    Success = 0,
    InvalidArguments = 2,
    NotElevated = 10,
    PackageMissing = 11,
    PackageRejected = 12,
    SignatureRejected = 13,
    InstallationFailed = 14,
    VerificationFailed = 15,
    UninstallFailed = 16,
    ResultWriteFailed = 17,
    UnsupportedPlatform = 18,
};

struct DriverStatus {
    bool packageInstalled = false;
    bool signatureTrusted = false;
    bool renderEndpointPresent = false;
    bool captureEndpointPresent = false;
    bool endpointPairValid = false;
    bool restartRequired = false;
    bool updateAvailable = false;
    bool newerDriverInstalled = false;
    std::wstring publishedInf;
    std::wstring installedVersion;
    std::wstring bundledVersion;
    std::wstring renderEndpointId;
    std::wstring captureEndpointId;
};

struct Result {
    ExitCode code = ExitCode::Success;
    std::wstring operation;
    std::wstring message;
    std::wstring diagnostic;
    DriverStatus status;
};

struct DeviceRecord {
    HDEVINFO set = INVALID_HANDLE_VALUE;
    SP_DEVINFO_DATA data{sizeof(SP_DEVINFO_DATA)};
    std::wstring instanceId;
    std::wstring publishedInf;
    std::wstring driverVersion;
};

std::wstring lower(std::wstring_view value)
{
    std::wstring result(value);
    std::transform(result.begin(), result.end(), result.begin(), [](wchar_t character) {
        return static_cast<wchar_t>(std::towlower(character));
    });
    return result;
}

bool startsWithInsensitive(std::wstring_view value, std::wstring_view prefix)
{
    return lower(value).rfind(lower(prefix), 0) == 0;
}

std::wstring win32Message(DWORD code)
{
    wchar_t* buffer = nullptr;
    const auto length = ::FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, code, 0, reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);
    std::wstring result = length && buffer ? std::wstring(buffer, length) : L"Unknown error";
    if (buffer) ::LocalFree(buffer);
    while (!result.empty() && (result.back() == L'\r' || result.back() == L'\n')) result.pop_back();
    return result + L" (" + std::to_wstring(code) + L")";
}

bool isElevated()
{
    HANDLE token = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD bytes = 0;
    const bool elevated =
        ::GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes) &&
        elevation.TokenIsElevated != 0;
    ::CloseHandle(token);
    return elevated;
}

std::filesystem::path executableDirectory()
{
    std::wstring buffer(32768, L'\0');
    const auto length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();
}

std::filesystem::path bundledInf()
{
#if defined(CUELET_TEST_BUNDLE_LAYOUT)
    // The isolated-test helper lives in <bundle>\tools and may resolve only
    // the hash-locked package at <bundle>\DriverPackage.
    return executableDirectory().parent_path() /
           L"DriverPackage" / driverInfName;
#else
    return executableDirectory() / L"DriverPackage" / driverInfName;
#endif
}

bool supportedWindowsBuild(std::wstring& diagnostic)
{
    using RtlGetVersionFunction = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const auto module = ::GetModuleHandleW(L"ntdll.dll");
    const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFunction>(
        module ? ::GetProcAddress(module, "RtlGetVersion") : nullptr);
    if (!rtlGetVersion) {
        diagnostic = L"Windows version information is unavailable.";
        return false;
    }

    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (rtlGetVersion(&version) != 0 ||
        version.dwMajorVersion < 10 ||
        version.dwBuildNumber < 22621) {
        diagnostic =
            L"Cuelet Virtual Audio currently requires Windows 11 build 22621 or later.";
        return false;
    }
    return true;
}

bool packageArchitectureMatchesNative(
    const std::filesystem::path& inf,
    std::wstring& diagnostic)
{
    const auto driver = inf.parent_path() / L"CueletVirtualAudio.sys";
    std::ifstream stream(driver, std::ios::binary);
    if (!stream) {
        diagnostic = L"The Cuelet driver binary is missing.";
        return false;
    }

    IMAGE_DOS_HEADER dos{};
    stream.read(reinterpret_cast<char*>(&dos), sizeof(dos));
    if (!stream || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0) {
        diagnostic = L"The Cuelet driver binary is not a valid PE image.";
        return false;
    }
    stream.seekg(dos.e_lfanew, std::ios::beg);
    DWORD signature = 0;
    IMAGE_FILE_HEADER fileHeader{};
    stream.read(reinterpret_cast<char*>(&signature), sizeof(signature));
    stream.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
    if (!stream || signature != IMAGE_NT_SIGNATURE) {
        diagnostic = L"The Cuelet driver binary has an invalid PE header.";
        return false;
    }

    USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
    if (!::IsWow64Process2(
            ::GetCurrentProcess(), &processMachine, &nativeMachine)) {
        diagnostic = L"Windows could not determine the native processor architecture.";
        return false;
    }
    if (fileHeader.Machine != nativeMachine) {
        std::wostringstream detail;
        detail << L"Driver architecture 0x" << std::hex << fileHeader.Machine
               << L" does not match native Windows architecture 0x"
               << nativeMachine << L".";
        diagnostic = detail.str();
        return false;
    }
    return true;
}

std::optional<std::filesystem::path> allowedResultRoot()
{
    wchar_t buffer[32768]{};
    const auto length = ::GetEnvironmentVariableW(
        L"LOCALAPPDATA", buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) return std::nullopt;
    return std::filesystem::path(buffer) / L"Cuelet" / L"InstallerResults";
}

bool isAllowedResultPath(const std::filesystem::path& candidate)
{
    const auto root = allowedResultRoot();
    if (!root || candidate.empty() || candidate.extension() != L".json") return false;
    std::error_code error;
    const auto normalizedRoot = std::filesystem::weakly_canonical(*root, error);
    if (error) return false;
    const auto parent = std::filesystem::weakly_canonical(candidate.parent_path(), error);
    if (error || lower(parent.wstring()) != lower(normalizedRoot.wstring())) return false;
    const auto filename = lower(candidate.filename().wstring());
    return filename.rfind(L"cuelet-", 0) == 0 &&
           filename.find_first_of(L"/\\:") == std::wstring::npos;
}

std::wstring escapeJson(std::wstring_view value)
{
    std::wstring result;
    for (const auto character : value) {
        switch (character) {
        case L'\\': result += L"\\\\"; break;
        case L'"': result += L"\\\""; break;
        case L'\r': result += L"\\r"; break;
        case L'\n': result += L"\\n"; break;
        case L'\t': result += L"\\t"; break;
        default:
            if (character >= 0 && character < 0x20) {
                wchar_t encoded[7]{};
                swprintf_s(encoded, L"\\u%04x", static_cast<unsigned>(character));
                result += encoded;
            } else {
                result += character;
            }
        }
    }
    return result;
}

std::wstring toJson(const Result& result)
{
    const auto boolean = [](bool value) { return value ? L"true" : L"false"; };
    std::wostringstream json;
    json << L"{"
         << L"\"schema\":1,"
         << L"\"exitCode\":" << static_cast<int>(result.code) << L","
         << L"\"operation\":\"" << escapeJson(result.operation) << L"\","
         << L"\"message\":\"" << escapeJson(result.message) << L"\","
         << L"\"diagnostic\":\"" << escapeJson(result.diagnostic) << L"\","
         << L"\"packageInstalled\":" << boolean(result.status.packageInstalled) << L","
         << L"\"signatureTrusted\":" << boolean(result.status.signatureTrusted) << L","
         << L"\"renderEndpointPresent\":" << boolean(result.status.renderEndpointPresent) << L","
         << L"\"captureEndpointPresent\":" << boolean(result.status.captureEndpointPresent) << L","
         << L"\"endpointPairValid\":" << boolean(result.status.endpointPairValid) << L","
         << L"\"restartRequired\":" << boolean(result.status.restartRequired) << L","
         << L"\"updateAvailable\":" << boolean(result.status.updateAvailable) << L","
         << L"\"newerDriverInstalled\":" << boolean(result.status.newerDriverInstalled) << L","
         << L"\"publishedInf\":\"" << escapeJson(result.status.publishedInf) << L"\","
         << L"\"installedVersion\":\"" << escapeJson(result.status.installedVersion) << L"\","
         << L"\"bundledVersion\":\"" << escapeJson(result.status.bundledVersion) << L"\","
         << L"\"renderEndpointId\":\"" << escapeJson(result.status.renderEndpointId) << L"\","
         << L"\"captureEndpointId\":\"" << escapeJson(result.status.captureEndpointId) << L"\""
         << L"}";
    return json.str();
}

bool writeResult(const std::filesystem::path& path, const Result& result)
{
    if (!isAllowedResultPath(path)) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    const auto temporary = path.wstring() + L".tmp";
    const auto json = toJson(result);
    const auto handle = ::CreateFileW(
        temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
        FILE_ATTRIBUTE_TEMPORARY | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    DWORD written = 0;
    bool ok = ::WriteFile(handle, bom, sizeof(bom), &written, nullptr) != FALSE;
    if (ok) {
        const auto required = ::WideCharToMultiByte(
            CP_UTF8, 0, json.data(), static_cast<int>(json.size()), nullptr, 0, nullptr, nullptr);
        std::string utf8(static_cast<std::size_t>(required), '\0');
        ::WideCharToMultiByte(
            CP_UTF8, 0, json.data(), static_cast<int>(json.size()),
            utf8.data(), required, nullptr, nullptr);
        ok = ::WriteFile(handle, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr) != FALSE;
    }
    ::FlushFileBuffers(handle);
    ::CloseHandle(handle);
    if (!ok) {
        ::DeleteFileW(temporary.c_str());
        return false;
    }
    if (!::MoveFileExW(
            temporary.c_str(), path.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        ::DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

std::optional<std::wstring> readDeviceProperty(
    HDEVINFO set, SP_DEVINFO_DATA& data, DWORD property)
{
    DWORD type = 0;
    DWORD bytes = 0;
    ::SetupDiGetDeviceRegistryPropertyW(set, &data, property, &type, nullptr, 0, &bytes);
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) return std::nullopt;
    std::vector<BYTE> buffer(bytes);
    if (!::SetupDiGetDeviceRegistryPropertyW(
            set, &data, property, &type, buffer.data(), bytes, nullptr)) {
        return std::nullopt;
    }
    if ((type != REG_SZ && type != REG_EXPAND_SZ) ||
        bytes < sizeof(wchar_t) ||
        bytes % sizeof(wchar_t) != 0) {
        return std::nullopt;
    }
    const auto* text = reinterpret_cast<wchar_t const*>(buffer.data());
    const auto characters = bytes / sizeof(wchar_t);
    if (text[characters - 1] != L'\0') return std::nullopt;
    return std::wstring(text);
}

std::optional<std::wstring> readDevProperty(
    HDEVINFO set, SP_DEVINFO_DATA& data, const DEVPROPKEY& key)
{
    DEVPROPTYPE type = 0;
    DWORD bytes = 0;
    ::SetupDiGetDevicePropertyW(set, &data, &key, &type, nullptr, 0, &bytes, 0);
    if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes == 0) return std::nullopt;
    std::vector<BYTE> buffer(bytes);
    if (!::SetupDiGetDevicePropertyW(
            set, &data, &key, &type, buffer.data(), bytes, nullptr, 0)) {
        return std::nullopt;
    }
    if (type != DEVPROP_TYPE_STRING ||
        bytes < sizeof(wchar_t) ||
        bytes % sizeof(wchar_t) != 0) {
        return std::nullopt;
    }
    const auto* text = reinterpret_cast<wchar_t const*>(buffer.data());
    const auto characters = bytes / sizeof(wchar_t);
    if (text[characters - 1] != L'\0') return std::nullopt;
    return std::wstring(text);
}

bool multiStringContains(std::wstring_view values, std::wstring_view expected)
{
    const auto* current = values.data();
    const auto* const end = values.data() + values.size();
    while (current < end && *current) {
        const auto terminator = std::find(current, end, L'\0');
        if (terminator == end) return false;
        const std::wstring_view item(
            current, static_cast<std::size_t>(terminator - current));
        if (lower(item) == lower(expected)) return true;
        current += item.size() + 1;
    }
    return false;
}

std::vector<DeviceRecord> cueletDevices(bool presentOnly)
{
    std::vector<DeviceRecord> records;
    const auto flags = DIGCF_ALLCLASSES | (presentOnly ? DIGCF_PRESENT : 0);
    const auto set = ::SetupDiGetClassDevsW(nullptr, nullptr, nullptr, flags);
    if (set == INVALID_HANDLE_VALUE) return records;
    for (DWORD index = 0;; ++index) {
        SP_DEVINFO_DATA data{sizeof(SP_DEVINFO_DATA)};
        if (!::SetupDiEnumDeviceInfo(set, index, &data)) {
            if (::GetLastError() == ERROR_NO_MORE_ITEMS) break;
            continue;
        }
        DWORD type = 0;
        DWORD bytes = 0;
        ::SetupDiGetDeviceRegistryPropertyW(
            set, &data, SPDRP_HARDWAREID, &type, nullptr, 0, &bytes);
        if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER || bytes < sizeof(wchar_t) * 2) continue;
        std::vector<BYTE> buffer(bytes);
        if (!::SetupDiGetDeviceRegistryPropertyW(
                set, &data, SPDRP_HARDWAREID, &type, buffer.data(), bytes, nullptr)) continue;
        const std::wstring_view ids(
            reinterpret_cast<wchar_t const*>(buffer.data()), bytes / sizeof(wchar_t));
        if (!multiStringContains(ids, hardwareId)) continue;
        const auto manufacturer = readDeviceProperty(set, data, SPDRP_MFG).value_or(L"");
        if (lower(manufacturer) != lower(providerName)) continue;
        DeviceRecord record;
        record.set = set;
        record.data = data;
        record.instanceId =
            readDevProperty(set, data, DEVPKEY_Device_InstanceId).value_or(L"");
        record.publishedInf = readDevProperty(set, data, DEVPKEY_Device_DriverInfPath).value_or(L"");
        record.driverVersion = readDevProperty(
            set, data, DEVPKEY_Device_DriverVersion).value_or(L"");
        records.push_back(record);
    }
    if (records.empty()) ::SetupDiDestroyDeviceInfoList(set);
    return records;
}

void closeDeviceRecords(std::vector<DeviceRecord>& records)
{
    std::set<HDEVINFO> handles;
    for (auto& record : records) {
        if (record.set != INVALID_HANDLE_VALUE) handles.insert(record.set);
        record.set = INVALID_HANDLE_VALUE;
    }
    for (const auto handle : handles) ::SetupDiDestroyDeviceInfoList(handle);
}

bool endpointBelongsToCuelet(
    HDEVINFO endpointDevices,
    const wchar_t* endpointId,
    const std::set<std::wstring>& ownedParents,
    std::wstring& parent)
{
    if (endpointDevices == INVALID_HANDLE_VALUE ||
        endpointId == nullptr || endpointId[0] == L'\0') {
        return false;
    }
    std::wstring instanceId = endpointId;
    if (!startsWithInsensitive(instanceId, L"SWD\\MMDEVAPI\\")) {
        instanceId = L"SWD\\MMDEVAPI\\" + instanceId;
    }
    SP_DEVINFO_DATA data{sizeof(data)};
    if (!::SetupDiOpenDeviceInfoW(
            endpointDevices, instanceId.c_str(), nullptr, 0, &data)) {
        return false;
    }
    parent =
        readDevProperty(endpointDevices, data, DEVPKEY_Device_Parent)
            .value_or(L"");
    return !parent.empty() &&
           ownedParents.find(lower(parent)) != ownedParents.end();
}

void enumerateCueletEndpoints(DriverStatus& status)
{
    auto roots = cueletDevices(true);
    std::set<std::wstring> ownedParents;
    for (const auto& root : roots) {
        if (!root.instanceId.empty()) {
            ownedParents.insert(lower(root.instanceId));
        }
    }
    closeDeviceRecords(roots);
    if (ownedParents.empty()) return;

    const auto endpointDevices = ::SetupDiGetClassDevsW(
        &audioEndpointClass, nullptr, nullptr, DIGCF_PRESENT);
    if (endpointDevices == INVALID_HANDLE_VALUE) return;

    const auto initialized = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool uninitialize = SUCCEEDED(initialized);
    IMMDeviceEnumerator* enumerator = nullptr;
    if (FAILED(::CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&enumerator)))) {
        ::SetupDiDestroyDeviceInfoList(endpointDevices);
        if (uninitialize) ::CoUninitialize();
        return;
    }
    IMMDeviceCollection* devices = nullptr;
    if (SUCCEEDED(enumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE, &devices))) {
        UINT count = 0;
        devices->GetCount(&count);
        struct EndpointCandidate {
            std::wstring id;
            std::wstring parent;
        };
        std::vector<EndpointCandidate> renderEndpoints;
        std::vector<EndpointCandidate> captureEndpoints;
        for (UINT index = 0; index < count; ++index) {
            IMMDevice* device = nullptr;
            IMMEndpoint* endpoint = nullptr;
            LPWSTR endpointId = nullptr;
            if (SUCCEEDED(devices->Item(index, &device)) &&
                SUCCEEDED(device->QueryInterface(IID_PPV_ARGS(&endpoint))) &&
                SUCCEEDED(device->GetId(&endpointId)) && endpointId) {
                std::wstring endpointParent;
                EDataFlow flow = eAll;
                if (SUCCEEDED(endpoint->GetDataFlow(&flow)) &&
                    endpointBelongsToCuelet(
                        endpointDevices, endpointId,
                        ownedParents, endpointParent)) {
                    if (flow == eRender) {
                        renderEndpoints.push_back(
                            {endpointId, endpointParent});
                    } else if (flow == eCapture) {
                        captureEndpoints.push_back(
                            {endpointId, endpointParent});
                    }
                }
            }
            if (endpointId) ::CoTaskMemFree(endpointId);
            if (endpoint) endpoint->Release();
            if (device) device->Release();
        }
        status.renderEndpointPresent = renderEndpoints.size() == 1;
        status.captureEndpointPresent = captureEndpoints.size() == 1;
        if (status.renderEndpointPresent) {
            status.renderEndpointId = renderEndpoints.front().id;
        }
        if (status.captureEndpointPresent) {
            status.captureEndpointId = captureEndpoints.front().id;
        }
        status.endpointPairValid =
            status.renderEndpointPresent && status.captureEndpointPresent &&
            !renderEndpoints.front().parent.empty() &&
            lower(renderEndpoints.front().parent) ==
                lower(captureEndpoints.front().parent);
        devices->Release();
    }
    enumerator->Release();
    ::SetupDiDestroyDeviceInfoList(endpointDevices);
    if (uninitialize) ::CoUninitialize();
}

std::optional<std::wstring> catalogFromInf(const std::filesystem::path& inf)
{
    std::wifstream stream(inf);
    stream.imbue(std::locale(""));
    if (!stream) return std::nullopt;
    std::wstring line;
    while (std::getline(stream, line)) {
        const auto normalized = lower(line);
        const auto equals = line.find(L'=');
        if (normalized.rfind(L"catalogfile", 0) == 0 && equals != std::wstring::npos) {
            auto value = line.substr(equals + 1);
            value.erase(0, value.find_first_not_of(L" \t"));
            value.erase(value.find_last_not_of(L" \t\r\n") + 1);
            if (!value.empty()) return value;
        }
    }
    return std::nullopt;
}

std::optional<std::wstring> versionFromInf(const std::filesystem::path& inf)
{
    std::wifstream stream(inf);
    stream.imbue(std::locale(""));
    if (!stream) return std::nullopt;
    std::wstring line;
    while (std::getline(stream, line)) {
        const auto normalized = lower(line);
        const auto equals = line.find(L'=');
        const auto comma = line.find(L',');
        if (normalized.rfind(L"driverver", 0) == 0 &&
            equals != std::wstring::npos &&
            comma != std::wstring::npos &&
            comma > equals) {
            auto value = line.substr(comma + 1);
            const auto first = value.find_first_not_of(L" \t");
            const auto last = value.find_last_not_of(L" \t\r\n");
            if (first != std::wstring::npos && last != std::wstring::npos) {
                return value.substr(first, last - first + 1);
            }
        }
    }
    return std::nullopt;
}

std::optional<std::array<unsigned long, 4>> parsedVersion(
    std::wstring_view text)
{
    std::array<unsigned long, 4> result{};
    std::size_t start = 0;
    for (std::size_t index = 0; index < result.size(); ++index) {
        const auto end = text.find(L'.', start);
        const auto part = text.substr(
            start, end == std::wstring_view::npos ? text.size() - start
                                                  : end - start);
        if (part.empty() ||
            part.find_first_not_of(L"0123456789") != std::wstring_view::npos) {
            return std::nullopt;
        }
        try {
            result[index] = std::stoul(std::wstring(part));
        } catch (...) {
            return std::nullopt;
        }
        if (index + 1 < result.size()) {
            if (end == std::wstring_view::npos) return std::nullopt;
            start = end + 1;
        } else if (end != std::wstring_view::npos) {
            return std::nullopt;
        }
    }
    return result;
}

bool infBelongsToCuelet(const std::filesystem::path& inf, std::wstring& diagnostic)
{
    std::wifstream stream(inf);
    stream.imbue(std::locale(""));
    if (!stream) {
        diagnostic = L"The bundled INF could not be opened.";
        return false;
    }
    std::wostringstream contents;
    contents << stream.rdbuf();
    const auto text = lower(contents.str());
    std::wstring compactText;
    compactText.reserve(text.size());
    std::copy_if(
        text.begin(), text.end(), std::back_inserter(compactText),
        [](wchar_t character) { return !std::iswspace(character); });
    const bool directProvider =
        compactText.find(L"provider=%cuelet%") != std::wstring::npos;
    const bool namedProvider =
        compactText.find(L"provider=%providername%") != std::wstring::npos &&
        (compactText.find(L"providername=\"cuelet\"") != std::wstring::npos ||
         compactText.find(L"providername=cuelet") != std::wstring::npos);
    if ((!directProvider && !namedProvider) ||
        text.find(L"root\\cueletvirtualaudio") == std::wstring::npos ||
        text.find(L"cuelet virtual microphone input") == std::wstring::npos ||
        text.find(L"cuelet virtual microphone") == std::wstring::npos) {
        diagnostic = L"The bundled INF does not contain Cuelet's expected provider, hardware ID, and endpoints.";
        return false;
    }
    return true;
}

bool verifySignature(const std::filesystem::path& inf, std::wstring& diagnostic)
{
    const auto catalogName = catalogFromInf(inf);
    if (!catalogName) {
        diagnostic = L"The INF does not declare a catalog.";
        return false;
    }
    const auto catalog = inf.parent_path() / *catalogName;
    if (!std::filesystem::is_regular_file(catalog)) {
        diagnostic = L"The catalog declared by the INF is missing.";
        return false;
    }
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = catalog.c_str();
    WINTRUST_DATA trust{};
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &file;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    DRIVER_VER_INFO driverPolicy{};
    driverPolicy.cbStruct = sizeof(driverPolicy);
    driverPolicy.dwPlatform = VER_PLATFORM_WIN32_NT;
    trust.pPolicyCallbackData = &driverPolicy;
    GUID action = DRIVER_ACTION_VERIFY;
    const auto result = ::WinVerifyTrust(nullptr, &action, &trust);
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    ::WinVerifyTrust(nullptr, &action, &trust);
    if (driverPolicy.pcSignerCertContext) {
        ::CertFreeCertificateContext(driverPolicy.pcSignerCertContext);
    }
    if (result != ERROR_SUCCESS) {
        diagnostic = L"The bundled catalog failed Windows driver-signing policy: " +
                     win32Message(static_cast<DWORD>(result));
        return false;
    }
    return true;
}

DriverStatus currentStatus()
{
    DriverStatus status;
    auto devices = cueletDevices(true);
    status.packageInstalled = !devices.empty();
    if (!devices.empty()) {
        status.publishedInf = devices.front().publishedInf;
        status.installedVersion = devices.front().driverVersion;
    }
    closeDeviceRecords(devices);
    std::wstring diagnostic;
    const auto inf = bundledInf();
    status.bundledVersion = versionFromInf(inf).value_or(L"");
    const bool bundledVersionAllowed =
        !cuelet::virtual_audio::isKnownUnsafeDriverVersion(
            status.bundledVersion);
    status.signatureTrusted =
        bundledVersionAllowed &&
        std::filesystem::is_regular_file(inf) &&
        verifySignature(inf, diagnostic);
    const auto installedVersion = parsedVersion(status.installedVersion);
    const auto bundledVersion = parsedVersion(status.bundledVersion);
    if (installedVersion && bundledVersion) {
        status.updateAvailable = *installedVersion < *bundledVersion;
        status.newerDriverInstalled = *installedVersion > *bundledVersion;
    }
    enumerateCueletEndpoints(status);
    return status;
}

bool createRootDevice(std::wstring& diagnostic)
{
    auto existing = cueletDevices(true);
    if (!existing.empty()) {
        closeDeviceRecords(existing);
        return true;
    }
    const auto set = ::SetupDiCreateDeviceInfoList(&GUID_DEVCLASS_MEDIA, nullptr);
    if (set == INVALID_HANDLE_VALUE) {
        diagnostic = L"SetupDiCreateDeviceInfoList failed: " + win32Message(::GetLastError());
        return false;
    }
    SP_DEVINFO_DATA data{sizeof(data)};
    bool success = false;
    if (::SetupDiCreateDeviceInfoW(
            set, L"CueletVirtualAudio", &GUID_DEVCLASS_MEDIA, nullptr, nullptr,
            DICD_GENERATE_ID, &data)) {
        const std::wstring ids = std::wstring(hardwareId) + L'\0' + L'\0';
        if (::SetupDiSetDeviceRegistryPropertyW(
                set, &data, SPDRP_HARDWAREID,
                reinterpret_cast<BYTE const*>(ids.data()),
                static_cast<DWORD>(ids.size() * sizeof(wchar_t))) &&
            ::SetupDiCallClassInstaller(DIF_REGISTERDEVICE, set, &data)) {
            success = true;
        }
    }
    if (!success) {
        diagnostic = L"Windows could not create the Cuelet root device: " +
                     win32Message(::GetLastError());
    }
    ::SetupDiDestroyDeviceInfoList(set);
    return success;
}

void removeCueletDevices(std::set<std::wstring>& publishedInfs, bool& reboot, std::wstring& diagnostic)
{
    auto devices = cueletDevices(false);
    for (auto& device : devices) {
        if (!device.publishedInf.empty()) publishedInfs.insert(device.publishedInf);
        SP_REMOVEDEVICE_PARAMS removal{};
        removal.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        removal.ClassInstallHeader.InstallFunction = DIF_REMOVE;
        removal.Scope = DI_REMOVEDEVICE_GLOBAL;
        removal.HwProfile = 0;
        if (!::SetupDiSetClassInstallParamsW(
                device.set, &device.data, &removal.ClassInstallHeader, sizeof(removal)) ||
            !::SetupDiCallClassInstaller(DIF_REMOVE, device.set, &device.data)) {
            diagnostic = L"Windows could not remove a Cuelet device: " +
                         win32Message(::GetLastError());
            closeDeviceRecords(devices);
            return;
        }
        SP_DEVINSTALL_PARAMS_W install{};
        install.cbSize = sizeof(install);
        if (::SetupDiGetDeviceInstallParamsW(device.set, &device.data, &install) &&
            (install.Flags & (DI_NEEDREBOOT | DI_NEEDRESTART))) {
            reboot = true;
        }
    }
    closeDeviceRecords(devices);
}

bool uninstallPublishedInfs(
    const std::set<std::wstring>& publishedInfs,
    std::wstring& diagnostic)
{
    for (const auto& publishedInf : publishedInfs) {
        const auto filename = std::filesystem::path(publishedInf).filename().wstring();
        if (!startsWithInsensitive(filename, L"oem") ||
            lower(std::filesystem::path(filename).extension().wstring()) != L".inf") {
            diagnostic = L"Cuelet refused an unexpected published INF identity: " +
                         publishedInf;
            return false;
        }
        if (!::SetupUninstallOEMInfW(filename.c_str(), SUOI_FORCEDELETE, nullptr)) {
            const auto error = ::GetLastError();
            if (error != ERROR_FILE_NOT_FOUND) {
                diagnostic = L"Windows could not remove the Cuelet driver package: " +
                             win32Message(error);
                return false;
            }
        }
    }
    return true;
}

bool removeDeferredCueletService(std::wstring& diagnostic)
{
    const auto manager = ::OpenSCManagerW(
        nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) {
        diagnostic = L"Windows could not open the service manager: " +
                     win32Message(::GetLastError());
        return false;
    }
    const auto service = ::OpenServiceW(
        manager, driverServiceName, SERVICE_QUERY_CONFIG | DELETE);
    if (!service) {
        const auto error = ::GetLastError();
        ::CloseServiceHandle(manager);
        if (error == ERROR_SERVICE_DOES_NOT_EXIST) return true;
        if (error == ERROR_SERVICE_MARKED_FOR_DELETE) return true;
        diagnostic = L"Windows could not inspect the Cuelet driver service: " +
                     win32Message(error);
        return false;
    }

    DWORD bytesRequired = 0;
    ::QueryServiceConfigW(service, nullptr, 0, &bytesRequired);
    const auto sizeError = ::GetLastError();
    if (sizeError != ERROR_INSUFFICIENT_BUFFER ||
        bytesRequired < sizeof(QUERY_SERVICE_CONFIGW)) {
        diagnostic = L"Windows could not size the Cuelet service configuration: " +
                     win32Message(sizeError);
        ::CloseServiceHandle(service);
        ::CloseServiceHandle(manager);
        return false;
    }
    std::vector<BYTE> storage(bytesRequired);
    auto* configuration =
        reinterpret_cast<QUERY_SERVICE_CONFIGW*>(storage.data());
    if (!::QueryServiceConfigW(
            service, configuration, bytesRequired, &bytesRequired)) {
        diagnostic = L"Windows could not read the Cuelet service configuration: " +
                     win32Message(::GetLastError());
        ::CloseServiceHandle(service);
        ::CloseServiceHandle(manager);
        return false;
    }

    const auto imagePath = lower(
        configuration->lpBinaryPathName
            ? configuration->lpBinaryPathName
            : L"");
    const auto expectedBinary = lower(driverBinaryName);
    const bool ownedImage =
        imagePath.find(
            L"\\driverstore\\filerepository\\cueletvirtualaudio.inf_") !=
            std::wstring::npos &&
        imagePath.size() >= expectedBinary.size() &&
        imagePath.compare(
            imagePath.size() - expectedBinary.size(),
            expectedBinary.size(), expectedBinary) == 0;
    if (!ownedImage) {
        diagnostic =
            L"Cuelet refused to delete a service whose image path is not "
            L"the Cuelet DriverStore package: " +
            std::wstring(
                configuration->lpBinaryPathName
                    ? configuration->lpBinaryPathName
                    : L"(empty)");
        ::CloseServiceHandle(service);
        ::CloseServiceHandle(manager);
        return false;
    }

    const bool deleted = ::DeleteService(service) != FALSE;
    const auto deleteError = deleted ? ERROR_SUCCESS : ::GetLastError();
    ::CloseServiceHandle(service);
    ::CloseServiceHandle(manager);
    if (!deleted && deleteError != ERROR_SERVICE_MARKED_FOR_DELETE) {
        diagnostic =
            L"Windows could not delete the deferred Cuelet driver service: " +
            win32Message(deleteError);
        return false;
    }
    return true;
}

Result installOrRepair(std::wstring_view operation, bool allowTestPackage)
{
    Result result;
    result.operation = operation;
    if (!isElevated()) {
        result.code = ExitCode::NotElevated;
        result.message = L"Administrator permission is required.";
        return result;
    }
    if (!supportedWindowsBuild(result.diagnostic)) {
        result.code = ExitCode::UnsupportedPlatform;
        result.message = L"This Windows version is not supported by the bundled Cuelet driver.";
        return result;
    }
    const auto inf = bundledInf();
    if (!std::filesystem::is_regular_file(inf)) {
        result.code = ExitCode::PackageMissing;
        result.message = L"The Cuelet driver package is not bundled with this build.";
        result.diagnostic = inf.wstring();
        return result;
    }
    if (!infBelongsToCuelet(inf, result.diagnostic)) {
        result.code = ExitCode::PackageRejected;
        result.message = L"The bundled driver package failed Cuelet ownership validation.";
        return result;
    }
    const auto packageVersion = versionFromInf(inf);
    if (!packageVersion || !parsedVersion(*packageVersion)) {
        result.code = ExitCode::PackageRejected;
        result.message =
            L"The bundled driver package has an invalid version.";
        result.diagnostic =
            L"Cuelet requires a four-part numeric DriverVer value.";
        return result;
    }
    if (cuelet::virtual_audio::isKnownUnsafeDriverVersion(
            *packageVersion)) {
        result.code = ExitCode::PackageRejected;
        result.message =
            L"Cuelet refused to install a retired crash build.";
        result.diagnostic =
            L"Driver version 20.37.42.726 is preserved only as crash "
            L"evidence and must not be reinstalled.";
        return result;
    }
    if (!packageArchitectureMatchesNative(inf, result.diagnostic)) {
        result.code = ExitCode::UnsupportedPlatform;
        result.message = L"The bundled Cuelet driver does not match this Windows architecture.";
        return result;
    }
    const bool signatureTrusted = verifySignature(inf, result.diagnostic);
    if (!signatureTrusted && !allowTestPackage) {
        result.code = ExitCode::SignatureRejected;
        result.message = L"Windows does not trust the Cuelet driver package.";
        return result;
    }

    const auto initialStatus = currentStatus();
    if (initialStatus.newerDriverInstalled) {
        result.status = initialStatus;
        result.code = ExitCode::InstallationFailed;
        result.message =
            L"A newer Cuelet virtual-audio driver is already installed.";
        result.diagnostic =
            L"Cuelet refused to downgrade the installed driver package.";
        return result;
    }
    if (initialStatus.endpointPairValid && !initialStatus.updateAvailable) {
        result.status = initialStatus;
        result.status.signatureTrusted = signatureTrusted;
        result.message = operation == L"repair"
            ? L"Cuelet Virtual Microphone is healthy; no repair was required."
            : L"Cuelet Virtual Microphone is already installed and connected.";
        return result;
    }

    wchar_t destinationInf[MAX_PATH]{};
    DWORD required = 0;
    const bool stagedNow = ::SetupCopyOEMInfW(
            inf.c_str(), inf.parent_path().c_str(), SPOST_PATH, 0,
            destinationInf, static_cast<DWORD>(std::size(destinationInf)),
            &required, nullptr) != FALSE;
    const auto stagingError = stagedNow ? ERROR_SUCCESS : ::GetLastError();
    if (!stagedNow && stagingError != ERROR_FILE_EXISTS) {
        result.code = ExitCode::InstallationFailed;
        result.message = L"Windows could not stage the Cuelet driver package.";
        result.diagnostic = win32Message(stagingError);
        return result;
    }
    std::set<std::wstring> stagedInfs;
    if (stagedNow && destinationInf[0] != L'\0') {
        stagedInfs.insert(std::filesystem::path(destinationInf).filename().wstring());
    }
    if (!createRootDevice(result.diagnostic)) {
        std::wstring rollbackDiagnostic;
        uninstallPublishedInfs(stagedInfs, rollbackDiagnostic);
        if (!rollbackDiagnostic.empty()) {
            result.diagnostic += L" Rollback: " + rollbackDiagnostic;
        }
        result.code = ExitCode::InstallationFailed;
        result.message = L"Windows could not create the Cuelet audio device.";
        return result;
    }
    BOOL reboot = FALSE;
    DWORD const updateFlags =
        operation == L"repair" &&
                !initialStatus.installedVersion.empty() &&
                initialStatus.installedVersion == initialStatus.bundledVersion
            ? INSTALLFLAG_FORCE
            : 0;
    if (!::UpdateDriverForPlugAndPlayDevicesW(
            nullptr, hardwareId, inf.c_str(), updateFlags, &reboot)) {
        const auto error = ::GetLastError();
        std::set<std::wstring> rollbackInfs = stagedInfs;
        bool rollbackReboot = false;
        std::wstring rollbackDiagnostic;
        removeCueletDevices(rollbackInfs, rollbackReboot, rollbackDiagnostic);
        std::wstring packageRollbackDiagnostic;
        uninstallPublishedInfs(rollbackInfs, packageRollbackDiagnostic);
        result.code = ExitCode::InstallationFailed;
        result.message = L"Windows rejected the Cuelet driver installation.";
        result.diagnostic = win32Message(error);
        if (!rollbackDiagnostic.empty() || !packageRollbackDiagnostic.empty()) {
            result.diagnostic += L" Rollback: " + rollbackDiagnostic +
                                 L" " + packageRollbackDiagnostic;
        }
        return result;
    }
    result.status.restartRequired = reboot != FALSE;
    for (int attempt = 0; attempt < 60; ++attempt) {
        result.status = currentStatus();
        result.status.restartRequired = result.status.restartRequired || reboot != FALSE;
        if (result.status.endpointPairValid) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    // Development bypass never turns an untrusted package into a trusted status.
    result.status.signatureTrusted = signatureTrusted;
    if (result.status.restartRequired && !result.status.endpointPairValid) {
        result.message = L"Cuelet Virtual Microphone was installed and Windows requires a restart.";
        return result;
    }
    if (!result.status.packageInstalled ||
        !result.status.renderEndpointPresent ||
        !result.status.captureEndpointPresent ||
        !result.status.endpointPairValid) {
        std::set<std::wstring> rollbackInfs = stagedInfs;
        bool rollbackReboot = false;
        std::wstring rollbackDiagnostic;
        removeCueletDevices(rollbackInfs, rollbackReboot, rollbackDiagnostic);
        std::wstring packageRollbackDiagnostic;
        uninstallPublishedInfs(rollbackInfs, packageRollbackDiagnostic);
        result.code = ExitCode::VerificationFailed;
        result.message = L"Windows did not create the complete Cuelet endpoint pair.";
        result.diagnostic =
            L"Both paired endpoints were not verified within 30 seconds. "
            L"Cuelet rolled back the partial installation.";
        if (!rollbackDiagnostic.empty() || !packageRollbackDiagnostic.empty()) {
            result.diagnostic += L" Rollback: " + rollbackDiagnostic +
                                 L" " + packageRollbackDiagnostic;
        }
        result.status = currentStatus();
        return result;
    }
    result.message = L"Cuelet Virtual Microphone is installed and connected.";
    return result;
}

Result uninstall()
{
    Result result;
    result.operation = L"uninstall";
    if (!isElevated()) {
        result.code = ExitCode::NotElevated;
        result.message = L"Administrator permission is required.";
        return result;
    }
    std::set<std::wstring> publishedInfs;
    bool reboot = false;
    removeCueletDevices(publishedInfs, reboot, result.diagnostic);
    if (!result.diagnostic.empty()) {
        result.code = ExitCode::UninstallFailed;
        result.message = L"Windows could not remove the Cuelet audio device.";
        return result;
    }
    if (!uninstallPublishedInfs(publishedInfs, result.diagnostic)) {
        result.code = ExitCode::UninstallFailed;
        result.message = L"Windows could not remove the Cuelet driver package.";
        return result;
    }
    if (!removeDeferredCueletService(result.diagnostic)) {
        result.code = ExitCode::UninstallFailed;
        result.message =
            L"Windows could not remove the deferred Cuelet driver service.";
        return result;
    }
    result.status = currentStatus();
    result.status.restartRequired = reboot;
    if (result.status.packageInstalled) {
        result.code = ExitCode::UninstallFailed;
        result.message = L"The Cuelet device is still present after uninstall.";
        return result;
    }
    result.message = reboot
        ? L"Cuelet Virtual Microphone was removed. Windows requires a restart."
        : L"Cuelet Virtual Microphone was removed.";
    return result;
}

Result status()
{
    Result result;
    result.operation = L"status";
    result.status = currentStatus();
    if (cuelet::virtual_audio::isKnownUnsafeDriverVersion(
            result.status.bundledVersion)) {
        result.diagnostic =
            L"The adjacent 20.37.42.726 package is retired crash evidence "
            L"and cannot be installed.";
    }
    if (!result.status.packageInstalled) {
        result.message = L"Cuelet Virtual Microphone is not installed.";
    } else if (result.status.endpointPairValid) {
        result.message = result.status.updateAvailable
            ? L"A Cuelet Virtual Microphone driver update is available."
            : L"Cuelet Virtual Microphone is connected.";
    } else {
        result.message = L"The Cuelet driver package is present, but the endpoint pair needs repair.";
    }
    return result;
}

void printJson(const Result& result)
{
    const auto json = toJson(result);
    const auto required = ::WideCharToMultiByte(
        CP_UTF8, 0, json.data(), static_cast<int>(json.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(required), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, json.data(), static_cast<int>(json.size()),
        utf8.data(), required, nullptr, nullptr);
    DWORD written = 0;
    ::WriteFile(::GetStdHandle(STD_OUTPUT_HANDLE), utf8.data(),
                static_cast<DWORD>(utf8.size()), &written, nullptr);
}

} // namespace

int wmain(int argc, wchar_t** argv)
{
    std::wstring operation;
    std::filesystem::path resultPath;
    bool allowTestPackage = false;
    if (argc >= 2) operation = lower(argv[1]);
    for (int index = 2; index < argc; ++index) {
        const std::wstring argument = argv[index];
        if (argument == L"--result-file" && index + 1 < argc) {
            resultPath = argv[++index];
        } else if (argument == L"--json") {
            // JSON is always the stdout format.
        } else if (argument == L"--allow-test-package") {
#if defined(_DEBUG) || defined(CUELET_TEST_BUNDLE_LAYOUT)
            // ShellExecute's runas path does not reliably carry process-local
            // environment changes across UAC. The argument is already an
            // explicit developer opt-in. The isolated bundle build is also
            // limited to its parent bundle's hash-locked DriverPackage.
            allowTestPackage = true;
#else
            Result rejected;
            rejected.code = ExitCode::InvalidArguments;
            rejected.operation = operation;
            rejected.message = L"Developer driver options are unavailable in Release builds.";
            printJson(rejected);
            return static_cast<int>(rejected.code);
#endif
        } else {
            Result invalid;
            invalid.code = ExitCode::InvalidArguments;
            invalid.operation = operation;
            invalid.message = L"Unknown installer argument.";
            invalid.diagnostic = argument;
            printJson(invalid);
            return static_cast<int>(invalid.code);
        }
    }
    Result result;
    if (operation == L"status") {
        result = status();
    } else if (operation == L"install" || operation == L"repair") {
        result = installOrRepair(operation, allowTestPackage);
    } else if (operation == L"uninstall") {
        result = uninstall();
    } else {
        result.code = ExitCode::InvalidArguments;
        result.operation = operation;
        result.message = L"Usage: Cuelet.VirtualAudio.Installer.exe install|repair|uninstall|status [--json]";
    }
    if (!resultPath.empty() && !writeResult(resultPath, result)) {
        result.code = ExitCode::ResultWriteFailed;
        result.message = L"The installer could not write its constrained result file.";
    }
    printJson(result);
    return static_cast<int>(result.code);
}
