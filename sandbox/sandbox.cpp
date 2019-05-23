
#include <ctime>     // for strftime()
#include <array>
#include <cstdint>   // for int16_t, [u]int32_t
#include <optional>
#include <iostream>
#include <exception> // for terminate()
#include <stdexcept> // for runtime_error()

#include <gsl/gsl_assert>

#include <clara.hpp>

#include <Windows.h>


void win32Check(DWORD errorCode)
{
    if (errorCode != ERROR_SUCCESS)
        throw std::system_error(std::error_code(errorCode, std::system_category())); // TODO: does this work correctly?
}
void win32Assert(BOOL success)
{
    if (!success)
        win32Check(GetLastError());
}


struct BatteryThresholdData
{
    std::int32_t p0[2];
    std::int32_t threshold;
    std::int32_t p1[5];
};

enum class BatteryChargeStatus : std::int32_t
{
    inactive = 0,
    charging = 1,
    discharging = 2
};
std::ostream& operator <<(std::ostream& stream, BatteryChargeStatus chargeStatus)
{
    switch (chargeStatus)
    {
    case BatteryChargeStatus::inactive:
        return stream << "inactive";
    case BatteryChargeStatus::charging:
        return stream << "charging";
    case BatteryChargeStatus::discharging:
        return stream << "discharging";
    default:
        std::terminate();
    }
}

enum class BatteryChemistry : std::int32_t
{
    LiIon = 256,
    NiMH = 16,
    NiCd = 1,
    LiPolymer = 4096,
    Unknown = -1,
    SilverZinc = 65536
};
std::ostream& operator <<(std::ostream& stream, BatteryChemistry batteryChemistry)
{
    switch (batteryChemistry)
    {
    case BatteryChemistry::LiIon:
        return stream << "Lithium-Ion";
    case BatteryChemistry::NiMH:
        return stream << "NiMH";
    case BatteryChemistry::NiCd:
        return stream << "NiCd";
    case BatteryChemistry::LiPolymer:
        return stream << "Lithium-Polymer";
    case BatteryChemistry::Unknown:
        return stream << "unknown";
    case BatteryChemistry::SilverZinc:
        return stream << "Silver-Zinc";
    default:
        return stream << "-";
    }
}

using IntBool = std::int32_t;

#pragma pack(push, 1)
struct SmartBatteryStatus
{
    /*   0 */ std::uint32_t size; // set to 309
    /*   4 */ IntBool canReportChargeStatus;
    /*   8 */ BatteryChargeStatus chargeStatus;
    /* ... */ char padding0[4];
    /*  16 */ std::int32_t acDischarge;
    /* ... */ char padding1[12];
    /*  32 */ IntBool canReportRemainingCharge;
    /*  36 */ std::uint32_t remainingCharge; // in %
    /*  40 */ IntBool canReportRemainingCapacity;
    /*  44 */ std::uint32_t remainingCapacity; // in mWh
    /*  48 */ IntBool canReportRemainingTime; // also assuming canReportChargeStatus and chargeStatus != 1
    /*  52 */ std::uint32_t remainingTime; // in minutes
    /*  56 */ IntBool canReportChargeCompletionTime; // also assuming canReportChargeStatus and chargeStatus == 1
    /*  60 */ std::uint32_t chargeCompletionTime; // in minutes
    /*  64 */ IntBool canReportVoltage;
    /*  68 */ std::uint32_t voltage; // in mV
    /*  72 */ IntBool canReportCurrent;
    /*  76 */ std::int32_t current; // in mA
    /*  80 */ IntBool canReportTemperature;
    /*  84 */ std::uint32_t temperature; // in °C
    /*  88 */ IntBool canReportCycleCount;
    /*  92 */ std::uint32_t cycleCount;
    /*  96 */ IntBool canReportFullChargeCapacity;
    /* 100 */ std::uint32_t fullChargeCapacity; // in mWh
    /* 104 */ IntBool canReportDesignCapacity;
    /* 108 */ std::uint32_t designCapacity; // in mWh
    /* 112 */ IntBool canReportDesignVoltage;
    /* 116 */ std::uint32_t designVoltage; // in mV
    /* 120 */ IntBool canReportDeviceChemistry;
    /* 124 */ BatteryChemistry deviceChemistry;
    /* 128 */ IntBool canReportSerialNumber;
    /* 132 */ std::uint32_t serialNumber;
    /* 136 */ IntBool canReportManufactureDate;
    /* 140 */ char manufactureDate[19+1];
    /* 160 */ IntBool canReportManufacturer;
    /* 164 */ char manufacturer[26+1];
    /* 191 */ IntBool canReportDeviceName;
    /* 195 */ char deviceName[26+1];
    /* 222 */ IntBool canReportBarCodeNumber;
    /* 226 */ char barCodeNumber[26+1];
    /* 253 */ IntBool canReportFirstUseDate;
    /* 257 */ char firstUseDate[19+1];
    /* ... */ char padding2[32];
};
static_assert(sizeof(SmartBatteryStatus) == 309);
struct SmartBatteryStatusEx
{
    std::uint32_t size; // set to 64
    char padding0[60];
};
static_assert(sizeof(SmartBatteryStatusEx) == 64);
#pragma pack(pop)

using SM_ChargeCapacityThresholdFunc = std::int32_t (__stdcall * )(std::int32_t batteryId, BatteryThresholdData* thresholdData);
using SM_GetSmartBatteryStatusFunc = std::int16_t (__stdcall * )(std::int32_t batteryId, SmartBatteryStatus* batteryStatus);
using SM_GetSmartBatteryStatusExFunc = std::int16_t (__stdcall * )(std::int32_t batteryId, SmartBatteryStatus* batteryStatus, SmartBatteryStatusEx* batteryStatusEx);


struct ProgramArgs
{
    bool setThreshold = false;
    bool detailed = false;
    int batteryId = 0;
    int startThreshold = 0;
    int stopThreshold = 0;
    bool disableThresholds = false;
};

clara::Parser makeCommandLineParser(ProgramArgs& args)
{
    return clara::Opt(args.setThreshold)
           ["-s"]["--set"]
           ("set battery threshold (calling without arguments prints charge thresholds of installed batteries)")
         | clara::Opt(args.detailed)
           ["-d"]["--detailed"]
           ("print detailed battery info")
         | clara::Opt(args.batteryId, "id")
           ["-b"]["--battery"]
           ("battery id")
         | clara::Opt(args.startThreshold, "1..100")
           ["--start"]
           ("charging start threshold (in %)")
         | clara::Opt(args.stopThreshold, "1..100")
           ["--stop"]
           ("charging stop threshold (in %)")
         | clara::Opt(args.disableThresholds)
           ["--disable"]
           ("disable charging thresholds (always charge fully)");
}

template <typename HandleT, typename HandleValueT, HandleValueT InvalidHandle, BOOL (WINAPI *FreeHandleFunc)(HandleT)>
    struct GenericWin32Handle
{
private:
    HandleT handle_;

public:
    HandleT handle(void) const noexcept { return handle_; }
    HandleT release(void) noexcept { return std::exchange(handle_, InvalidHandle); }

    explicit operator bool(void) const noexcept { return handle_ != (HandleT) InvalidHandle; }

    explicit GenericWin32Handle(HandleT _handle)
        : handle_(_handle)
    {
        win32Assert(handle_ != (HandleT) InvalidHandle);
    }
    GenericWin32Handle(GenericWin32Handle&& rhs) noexcept
        : handle_(rhs.release())
    {
        Expects(handle_ != (HandleT) InvalidHandle);
    }
    GenericWin32Handle& operator =(GenericWin32Handle&& rhs) noexcept
    {
        Expects(rhs.handle_ != (HandleT) InvalidHandle);
        if (handle_ != (HandleT) InvalidHandle)
            FreeHandleFunc(handle_);
        handle_ = rhs.release();
        return *this;
    }
    ~GenericWin32Handle(void)
    {
        if (handle_ != (HandleT) InvalidHandle)
            FreeHandleFunc(handle_);
    }
};
using Win32Handle = GenericWin32Handle<HANDLE, LONG_PTR, -1, CloseHandle>;
using Win32Module = GenericWin32Handle<HMODULE, HMODULE, nullptr, FreeLibrary>;

class SMInterface
{
private:
    Win32Module hLib_;
    SM_ChargeCapacityThresholdFunc getChargeCapacityStartThreshold_;
    SM_ChargeCapacityThresholdFunc setChargeCapacityStartThreshold_;
    SM_ChargeCapacityThresholdFunc getChargeCapacityStopThreshold_;
    SM_ChargeCapacityThresholdFunc setChargeCapacityStopThreshold_;
    SM_GetSmartBatteryStatusExFunc getSmartBatteryStatusEx_;
    SM_GetSmartBatteryStatusFunc getSmartBatteryStatus_;

public:
    SMInterface(const wchar_t* libPath)
        : hLib_(LoadLibraryW(libPath))
    {
        getChargeCapacityStartThreshold_ = (SM_ChargeCapacityThresholdFunc) GetProcAddress(hLib_.handle(), "SM_GetChargeStartCapacityThreshold");
        win32Assert(getChargeCapacityStartThreshold_ != NULL);
        setChargeCapacityStartThreshold_ = (SM_ChargeCapacityThresholdFunc) GetProcAddress(hLib_.handle(), "SM_SetChargeStartCapacityThreshold");
        win32Assert(setChargeCapacityStartThreshold_ != NULL);
        getChargeCapacityStopThreshold_ = (SM_ChargeCapacityThresholdFunc) GetProcAddress(hLib_.handle(), "SM_GetChargeStopCapacityThreshold");
        win32Assert(getChargeCapacityStopThreshold_ != NULL);
        setChargeCapacityStopThreshold_ = (SM_ChargeCapacityThresholdFunc) GetProcAddress(hLib_.handle(), "SM_SetChargeStopCapacityThreshold");
        win32Assert(setChargeCapacityStopThreshold_ != NULL);
        getSmartBatteryStatusEx_ = (SM_GetSmartBatteryStatusExFunc) GetProcAddress(hLib_.handle(), "SM_GetSmartBatteryStatusEx");
        win32Assert(getSmartBatteryStatusEx_!= NULL);
        getSmartBatteryStatus_ = (SM_GetSmartBatteryStatusFunc) GetProcAddress(hLib_.handle(), "SM_GetSmartBatteryStatus");
        win32Assert(getSmartBatteryStatus_!= NULL);
    }

    std::optional<std::array<std::int32_t, 2>> tryGetThresholds(std::int32_t batteryId) const
    {
        auto batteryData = BatteryThresholdData{ };
        auto result = getChargeCapacityStartThreshold_(batteryId, &batteryData);
        if (result != 0)
            return std::nullopt;
        auto startThreshold = batteryData.threshold;
        batteryData = BatteryThresholdData{ };
        result = getChargeCapacityStopThreshold_(batteryId, &batteryData);
        if (result != 0)
            return std::nullopt;
        auto stopThreshold = batteryData.threshold;
        return std::array{ startThreshold, stopThreshold };
    }
    bool trySetThresholds(std::int32_t batteryId, std::int32_t startThreshold, std::int32_t stopThreshold) const
    {
        auto batteryData = BatteryThresholdData{ };
        batteryData.threshold = startThreshold;
        auto result = setChargeCapacityStartThreshold_(batteryId, &batteryData);
        if (result != 0)
            return false;
        batteryData = BatteryThresholdData{ };
        batteryData.threshold = stopThreshold;
        result = setChargeCapacityStopThreshold_(batteryId, &batteryData);
        if (result != 0)
            return false;
        return true;
    }
    bool tryGetSmartBatteryStatus(std::int32_t batteryId, SmartBatteryStatus& status) const
    {
        status = { };
        status.size = sizeof(SmartBatteryStatus);
        auto result = getSmartBatteryStatus_(batteryId, &status);
        return result == 0;
    }
    bool tryGetSmartBatteryStatusEx(std::int32_t batteryId, SmartBatteryStatus& status, SmartBatteryStatusEx& statusEx) const
    {
        status = { };
        status.size = sizeof(SmartBatteryStatus);
        statusEx = { };
        statusEx.size = sizeof(SmartBatteryStatusEx);
        auto result = getSmartBatteryStatusEx_(batteryId, &status, &statusEx);
        return result == 0;
    }
};

void reportBatteryThresholds(std::ostream& stream, std::int32_t startThreshold, std::int32_t stopThreshold)
{
    char indent[] = "    ";

    stream << indent << "Charge thresholds: ";
    if (startThreshold == 0 && stopThreshold == 0)
    {
        stream << "disabled";
    }
    else
    {
        stream << "charge from " << startThreshold + 1 << "% to " << stopThreshold << "%";
    }
    stream << '\n';
}
void reportBatteryStatus(std::ostream& stream, const SmartBatteryStatus& status, bool detailed)
{
    char indent[] = "    ";

    if (status.canReportChargeStatus)
    {
        stream << indent << "Status: " << status.chargeStatus;
        if (status.chargeStatus == BatteryChargeStatus::discharging && status.acDischarge)
            stream << " (AC)";
        stream << '\n';
    }
    if (status.canReportRemainingCharge)
    {
        stream << indent << "Current charge: " << status.remainingCharge << '%';
        if (status.canReportRemainingTime && status.chargeStatus != BatteryChargeStatus::charging)
        {
            char timestr[10];
            auto timerec = std::tm{ };
            timerec.tm_hour = status.remainingTime / 60;
            timerec.tm_min = status.remainingTime % 60;
            strftime(timestr, sizeof(timestr), "%H:%M", &timerec);
            stream << " (" << timestr << " left)";
        }
        else if (status.canReportChargeCompletionTime && status.chargeStatus == BatteryChargeStatus::charging)
        {
            char timestr[10];
            auto timerec = std::tm{ };
            timerec.tm_hour = status.chargeCompletionTime / 60;
            timerec.tm_min = status.chargeCompletionTime % 60;
            strftime(timestr, sizeof(timestr), "%H:%M", &timerec);
            stream << " (" << timestr << " until charging complete)";
        }
        stream << '\n';
    }
    if (status.canReportRemainingCapacity)
    {
        stream << indent << "Current capacity: " << status.remainingCapacity << " mWh";
        if (status.canReportFullChargeCapacity)
        {
            stream << " of " << status.fullChargeCapacity << " mWh";
        }
        if (status.canReportDesignCapacity)
        {
            stream << " (design capacity " << status.designCapacity << " mWh)";
        }
        stream << '\n';
    }
    if (status.canReportCycleCount)
        stream << indent << "Number of charge/discharge cycles: " << status.cycleCount << '\n';
    if (status.canReportVoltage && status.canReportCurrent)
    {
        stream << indent << "Power consumption: " << ((status.voltage * status.current) / 1000) << " mW\n";
    }
    if (status.canReportVoltage)
    {
        stream << indent << "Voltage: " << status.voltage << " mV";
        if (status.canReportDesignVoltage)
        {
            stream << " (design voltage " << status.designVoltage << " mV)";
        }
        stream << '\n';
    }
    if (status.canReportCurrent)
    {
        stream << indent << "Current: " << status.current << " mA\n";
    }
    if (status.canReportTemperature)
        stream << indent << "Temperature: " << status.temperature << " °C\n";
    if (detailed)
    {
        if (status.canReportManufacturer)
            stream << indent << "Manufacturer: " << status.manufacturer << '\n';
        if (status.canReportDeviceChemistry)
            stream << indent << "Chemistry: " << status.deviceChemistry << '\n';
        if (status.canReportDeviceName)
            stream << indent << "FRU: " << status.deviceName << '\n';
        if (status.canReportSerialNumber)
            stream << indent << "Serial number: " << status.serialNumber << '\n';
        if (status.canReportBarCodeNumber)
            stream << indent << "Bar code number: " << status.barCodeNumber << '\n';
        if (status.canReportManufactureDate)
            stream << indent << "Date of manufacture: " << status.manufactureDate << '\n';
        if (status.canReportFirstUseDate)
            stream << indent << "Date of first use: " << status.firstUseDate << '\n';
    }
}


int run(ProgramArgs& args)
{
    auto smInterface = SMInterface(L"C:\\Program Files (x86)\\ThinkPad\\Utilities\\PWMIF32V.DLL");

    if (args.setThreshold)
    {
        if (args.batteryId < 1)
        {
            std::cerr << "invalid battery id\n";
            return 1;
        }
        auto maybeOldThresholds = smInterface.tryGetThresholds(args.batteryId);
        if (!maybeOldThresholds)
        {
            std::cerr << "invalid battery id\n";
            return 1;
        }
        auto [oldStartThreshold, oldStopThreshold] = *maybeOldThresholds;

        if (args.disableThresholds)
        {
            args.startThreshold = 1;
            args.stopThreshold = 0;
        }
        else
        {
            if (args.startThreshold < 0 || args.startThreshold > 100)
            {
                std::cerr << "invalid charging start threshold (value must be between 1 and 100)\n";
                return 1;
            }
            if (args.stopThreshold < 0 || args.stopThreshold > 100)
            {
                std::cerr << "invalid charging stop threshold (value must be between 1 and 100)\n";
                return 1;
            }
            if (args.stopThreshold <= args.startThreshold)
            {
                std::cerr << "charging stop threshold must be greater than charging start threshold\n";
                return 1;
            }
        }

        bool succeeded = smInterface.trySetThresholds(args.batteryId, args.startThreshold - 1, args.stopThreshold);
        if (!succeeded)
        {
            std::cerr << "Failed to set battery thresholds " << args.startThreshold << "%.." << args.stopThreshold << "% for battery #" << args.batteryId << '\n';
            return 1;
        }

        if (args.disableThresholds)
        {
            std::cout << "Disable charging thresholds for battery #" << args.batteryId << '\n';
        }
        else
        {
            std::cout << "Set thresholds for battery #" << args.batteryId << ": charge from " << args.startThreshold << "% to " << args.stopThreshold << "%\n";
        }
        
        auto maybeThresholds = smInterface.tryGetThresholds(args.batteryId);
        Expects(maybeThresholds);
        auto [startThreshold, stopThreshold] = *maybeThresholds;
        if ((args.disableThresholds && (startThreshold != 0 || stopThreshold != 0))
            || (!args.disableThresholds && (startThreshold != args.startThreshold - 1 || stopThreshold != args.stopThreshold)))
        {
            std::cerr << "Unknown error setting battery thresholds\n";
            reportBatteryThresholds(std::cerr, startThreshold, stopThreshold);
            return 1;
        }
    }
    else if (args.batteryId != 0)
    {
        auto maybeThresholds = smInterface.tryGetThresholds(args.batteryId);
        if (maybeThresholds)
        {
            auto [startThreshold, stopThreshold] = *maybeThresholds;
            std::cout << "Battery #" << args.batteryId << ":\n";
            reportBatteryThresholds(std::cout, startThreshold, stopThreshold);

            auto status = SmartBatteryStatus{ };
            if (smInterface.tryGetSmartBatteryStatus(args.batteryId, status))
            {
                reportBatteryStatus(std::cout, status, args.detailed);
            }
        }
        else
        {
            std::cerr << "invalid battery id";
            return 1;
        }
    }
    else
    {
        for (std::int32_t batteryId = 1; ; ++batteryId)
        {
            auto maybeThresholds = smInterface.tryGetThresholds(batteryId);
            if (!maybeThresholds)
                break;
            auto [startThreshold, stopThreshold] = *maybeThresholds;
            std::cout << "\nBattery #" << batteryId << ":\n";
            reportBatteryThresholds(std::cout, startThreshold, stopThreshold);

            auto status = SmartBatteryStatus{ };
            if (smInterface.tryGetSmartBatteryStatus(batteryId, status))
            {
                reportBatteryStatus(std::cout, status, args.detailed);
            }
        }
    }
    return 0;
}

#pragma region Entry point
static void terminateHandler(void)
{
    std::cerr << "terminate() was called.\n";
    std::abort();
}
int main(int argc, char* argv[])
{
    std::set_terminate(terminateHandler);

    try
    {
        ProgramArgs args{ };

        bool showHelp = false;
        auto cliArgs = clara::Args(argc, argv);
        auto cli = clara::Help(showHelp)
                 | makeCommandLineParser(args);
        auto result = cli.parse(cliArgs);

        if (!result)
        {
            std::cerr << "Error in command line: " << result.errorMessage() << "\n"
                      << "Call '" << cliArgs.exeName() << " -h' to see the available options\n";
            return 1;
        }
        if (showHelp)
        {
            std::cout << cli << '\n';
            return 0;
        }
    
        return run(args);
    }
    catch (const std::runtime_error& e)
    {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
#pragma endregion Entry point
