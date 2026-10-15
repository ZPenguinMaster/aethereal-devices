/*---------------------------------------------------------*\
| service_main.cpp                                          |
|                                                           |
|   Entry point for the Aethereal Device Service: OpenRGB's |
|   device drivers and SDK server, with no window. Started  |
|   by the Aethereal PC app, or run as a Windows service.   |
|                                                           |
|   Based on OpenRGB's startup/main_Windows.cpp and         |
|   startup/main_FreeBSD_Linux_MacOS.cpp. Modified for      |
|   Aethereal (see AETHEREAL.md).                           |
|                                                           |
|   This file is part of the OpenRGB project                |
|   SPDX-License-Identifier: GPL-2.0-or-later               |
\*---------------------------------------------------------*/

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include "DetectionManager.h"
#include "LogManager.h"
#include "NetworkServer.h"
#include "ResourceManager.h"
#include "SettingsManager.h"
#include "StringUtils.h"
#include "filesystem.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/types.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif

using namespace std::chrono_literals;

/*---------------------------------------------------------*\
| What the service is, and where it listens                 |
|                                                           |
|   Its own port, so it never collides with OpenRGB itself  |
|   (6742) if the person also runs that. Only this PC.      |
\*---------------------------------------------------------*/
#define AETHEREAL_SERVICE_NAME          "Aethereal Device Service"
#define AETHEREAL_SERVICE_ID            "AetherealDevices"
#define AETHEREAL_SERVICE_HOST          "127.0.0.1"
#define AETHEREAL_SERVICE_DEFAULT_PORT  6743

static volatile bool            stop_requested      = false;
static std::mutex               stop_mutex;
static std::condition_variable  stop_cv;

static void RequestStop()
{
    stop_requested = true;
    stop_cv.notify_one();
}

/*---------------------------------------------------------*\
| Options                                                   |
\*---------------------------------------------------------*/
struct ServiceOptions
{
    unsigned short      port        = AETHEREAL_SERVICE_DEFAULT_PORT;
    filesystem::path    config_dir;
    bool                no_smbus    = false;
    long                parent_pid  = 0;
    bool                verbose     = false;
};

static void PrintHelp()
{
    std::cout
        << AETHEREAL_SERVICE_NAME << " " << VERSION_STRING << "\n"
        << "A modified OpenRGB (https://gitlab.com/CalcProgrammer1/OpenRGB): its device drivers and SDK\n"
        << "server, with no window. GPL-2.0-or-later; source: https://github.com/ZPenguinMaster/aethereal-devices\n"
        << "\n"
        << "Options:\n"
        << "  --port N        the SDK server's port on 127.0.0.1 (default " << AETHEREAL_SERVICE_DEFAULT_PORT << ")\n"
        << "  --config DIR    where settings and logs are kept\n"
        << "  --no-smbus      leave the SMBus alone: no RAM, and no older motherboards reached through it\n"
        << "  --parent PID    stop when that process ends (when the app starts the service itself)\n"
        << "  -v, --verbose   more in the log\n"
        << "  -V, --version   the version\n"
        << "  -h, --help      this\n";
}

/*---------------------------------------------------------*\
| ParseOptions                                              |
|                                                           |
|   Returns false (with a message) on a bad option          |
\*---------------------------------------------------------*/
static bool ParseOptions(int argc, char* argv[], ServiceOptions& options)
{
    for(int arg_index = 1; arg_index < argc; arg_index++)
    {
        std::string option   = argv[arg_index];
        bool        has_next = arg_index + 1 < argc;

        if(option == "--port" && has_next)
        {
            int port = atoi(argv[++arg_index]);

            if(port < 1024 || port > 65535)
            {
                std::cerr << "--port: expected 1024 to 65535\n";
                return false;
            }
            options.port = (unsigned short)port;
        }
        else if(option == "--config" && has_next)
        {
            options.config_dir = filesystem::u8path(argv[++arg_index]);
        }
        else if(option == "--no-smbus")
        {
            options.no_smbus = true;
        }
        else if(option == "--parent" && has_next)
        {
            options.parent_pid = atol(argv[++arg_index]);
        }
        else if(option == "-v" || option == "--verbose")
        {
            options.verbose = true;
        }
        else if(option == "-V" || option == "--version")
        {
            std::cout << AETHEREAL_SERVICE_NAME << " " << VERSION_STRING << "\n"
                      << "Commit: " << GIT_COMMIT_ID << " (" << GIT_COMMIT_DATE << ")\n";
            exit(EXIT_SUCCESS);
        }
        else if(option == "-h" || option == "--help")
        {
            PrintHelp();
            exit(EXIT_SUCCESS);
        }
        else
        {
            std::cerr << "Unknown option: " << option << "\n\n";
            PrintHelp();
            return false;
        }
    }

    return true;
}

/*---------------------------------------------------------*\
| DefaultConfigDirectory                                    |
|                                                           |
|   Our own place, never OpenRGB's (so a person who also    |
|   runs OpenRGB keeps their settings apart from ours)      |
\*---------------------------------------------------------*/
static filesystem::path DefaultConfigDirectory()
{
#ifdef _WIN32
    const wchar_t* appdata = _wgetenv(L"APPDATA");
    if(appdata != NULL)
    {
        return(filesystem::path(appdata) / "Aethereal" / "devices");
    }
#else
    const char* xdg_config_home = getenv("XDG_CONFIG_HOME");
    const char* home            = getenv("HOME");
    if(xdg_config_home != NULL)
    {
        return(filesystem::path(xdg_config_home) / "aethereal-devices");
    }
    if(home != NULL)
    {
        return(filesystem::path(home) / ".config" / "aethereal-devices");
    }
#endif
    return(filesystem::path("."));
}

/*---------------------------------------------------------*\
| PointConfigAt                                             |
|                                                           |
|   ResourceManager makes its configuration directory as    |
|   soon as it's created (%APPDATA%\OpenRGB, or             |
|   ~/.config/OpenRGB). Pointing the variable it reads at   |
|   our directory first keeps it from touching OpenRGB's.   |
|   (Settings end up in <dir>/OpenRGB/OpenRGB.json.)        |
\*---------------------------------------------------------*/
static void PointConfigAt(const filesystem::path& dir)
{
    filesystem::create_directories(dir);
#ifdef _WIN32
    _wputenv_s(L"APPDATA", dir.wstring().c_str());
#else
    setenv("XDG_CONFIG_HOME", dir.string().c_str(), 1);
#endif
}

/*---------------------------------------------------------*\
| WatchParent                                               |
|                                                           |
|   When the app starts the service itself, the service     |
|   ends with it, even if the app is killed                 |
\*---------------------------------------------------------*/
static void WatchParent(long parent_pid)
{
#ifdef _WIN32
    HANDLE parent = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)parent_pid);

    if(parent == NULL)
    {
        LOG_WARNING("[Aethereal] Parent process %ld not found; stopping", parent_pid);
        RequestStop();
        return;
    }

    std::thread([parent]()
    {
        WaitForSingleObject(parent, INFINITE);
        CloseHandle(parent);
        LOG_INFO("[Aethereal] Parent process ended; stopping");
        RequestStop();
    }).detach();
#else
#ifdef __linux__
    prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
    std::thread([parent_pid]()
    {
        while(!stop_requested)
        {
            if(getppid() != (pid_t)parent_pid)
            {
                LOG_INFO("[Aethereal] Parent process ended; stopping");
                RequestStop();
                return;
            }
            std::this_thread::sleep_for(1s);
        }
    }).detach();
#endif
}

#ifdef _WIN32
/*---------------------------------------------------------*\
| Windows service                                           |
|                                                           |
|   From OpenRGB's main_Windows.cpp: the same service       |
|   control, under our own name                             |
\*---------------------------------------------------------*/
static void WINAPI ServiceMain(DWORD dwArgc, LPTSTR *lpszArgv);

static char                  service_name[]             = AETHEREAL_SERVICE_ID;
static SERVICE_TABLE_ENTRY   service_dispatch_table[]   = { { service_name, ServiceMain }, { NULL, NULL } };
static DWORD                 service_checkpoint         = 1;
static SERVICE_STATUS_HANDLE service_status_handle;
static SERVICE_STATUS        service_status;
static std::mutex            service_status_mutex;
static bool                  started_as_service         = false;
static bool                  have_console               = false;

static int                   main_argc;
static char**                main_argv;

/*---------------------------------------------------------*\
| Detection progress goes from 0 to 100 twice, passing an   |
| estimate based on that confuses the service manager       |
\*---------------------------------------------------------*/
static int                   detection_pass;
static unsigned int          lastpercent                = 101;

static void ReportServiceStatus(DWORD dwCurrentState, DWORD dwWin32ExitCode, DWORD dwWaitHint)
{
    std::lock_guard<std::mutex> lock(service_status_mutex);

    /*-----------------------------------------------------*\
    | Never report going back (a stop that arrived before   |
    | start finished, say)                                  |
    \*-----------------------------------------------------*/
    if(service_status.dwCurrentState == SERVICE_STOPPED)
    {
        return;
    }
    if(service_status.dwCurrentState == SERVICE_STOP_PENDING && dwCurrentState != SERVICE_STOPPED)
    {
        return;
    }

    service_status.dwCurrentState   = dwCurrentState;
    service_status.dwWin32ExitCode  = dwWin32ExitCode;
    service_status.dwWaitHint       = dwWaitHint;

    if(dwCurrentState == SERVICE_START_PENDING)
    {
        service_status.dwControlsAccepted = 0;
    }
    else
    {
        service_status.dwControlsAccepted = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PRESHUTDOWN;
    }

    if((dwCurrentState == SERVICE_RUNNING) || (dwCurrentState == SERVICE_STOPPED))
    {
        service_status.dwCheckPoint = 0;
    }
    else
    {
        service_status.dwCheckPoint = service_checkpoint++;
    }

    SetServiceStatus(service_status_handle, &service_status);
}

static void ServiceResourceManagerCallback(void *, unsigned int update_reason)
{
    if(update_reason != RESOURCEMANAGER_UPDATE_REASON_DETECTION_PROGRESS_CHANGED)
    {
        return;
    }

    unsigned int percent = std::clamp(ResourceManager::get()->GetDetectionPercent(), 0u, 100u);

    if(lastpercent > percent)
    {
        detection_pass += 1;
    }
    lastpercent = percent;

    switch(detection_pass)
    {
        case 0:  percent = 0;                   break;
        case 1:  percent = percent * 4 / 5;     break;
        case 2:  percent = percent / 5 + 80;    break;
        default: percent = 100;                 break;
    }

    ReportServiceStatus(SERVICE_START_PENDING, NO_ERROR, ((100 - percent) / 5 + 10) * 1000);
}

static DWORD WINAPI ServiceControlHandler(DWORD dwControl, DWORD, LPVOID, LPVOID)
{
    switch(dwControl)
    {
        case SERVICE_CONTROL_INTERROGATE:
            return NO_ERROR;

        case SERVICE_CONTROL_STOP:
        case SERVICE_CONTROL_PRESHUTDOWN:
            ReportServiceStatus(SERVICE_STOP_PENDING, NO_ERROR, 10000);
            RequestStop();
            return NO_ERROR;

        default:
            return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

/*---------------------------------------------------------*\
| LogEvent                                                  |
|                                                           |
|   Startup and shutdown problems, to the event log when    |
|   there's no console                                      |
\*---------------------------------------------------------*/
static void LogEvent(const char* message)
{
    if(have_console)
    {
        fprintf(stderr, "%s\n", message);
        return;
    }

    HANDLE event_source = RegisterEventSource(NULL, service_name);

    if(event_source)
    {
        LPCSTR strings[2] = { service_name, message };
        ReportEvent(event_source, EVENTLOG_ERROR_TYPE, 0, 1, NULL, 2, 0, strings, NULL);
        DeregisterEventSource(event_source);
    }
}

/*---------------------------------------------------------*\
| TimerResolutionThread                                     |
|                                                           |
|   Windows' default timer is 15.6 ms; devices' timing      |
|   needs 0.5 ms (as in OpenRGB)                            |
\*---------------------------------------------------------*/
static void TimerResolutionThread()
{
    typedef unsigned int NTSTATUS;
    typedef NTSTATUS (*NTSETTIMERRESOLUTION)(ULONG DesiredResolution, BOOLEAN SetResolution, PULONG CurrentResolution);

    PROCESS_POWER_THROTTLING_STATE  throttling { PROCESS_POWER_THROTTLING_CURRENT_VERSION, 4, 0 };
    ULONG                           current;
    HMODULE                         ntdll               = LoadLibrary("ntdll.dll");
    NTSETTIMERRESOLUTION            NtSetTimerResolution = (NTSETTIMERRESOLUTION)GetProcAddress(ntdll, "NtSetTimerResolution");

    SetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &throttling, sizeof(throttling));

    while(NtSetTimerResolution)
    {
        NtSetTimerResolution(5000, TRUE, &current);
        std::this_thread::sleep_for(500ms);
    }
}

static BOOL WINAPI ConsoleCtrlHandler(DWORD)
{
    RequestStop();
    return TRUE;
}
#else
static void SignalHandler(int)
{
    RequestStop();
}
#endif

/*---------------------------------------------------------*\
| RunService                                                |
|                                                           |
|   Detect devices, serve them, and wait to be told to stop |
\*---------------------------------------------------------*/
static int RunService(int argc, char* argv[])
{
    ServiceOptions options;

    if(!ParseOptions(argc, argv, options))
    {
        return EXIT_FAILURE;
    }

    if(options.config_dir.empty())
    {
        options.config_dir = DefaultConfigDirectory();
    }
    PointConfigAt(options.config_dir);

    if(options.verbose)
    {
        LogManager::get()->SetVerbosity(LL_VERBOSE);
    }

    LOG_INFO("[Aethereal] %s %s, port %u, settings in %s", AETHEREAL_SERVICE_NAME, VERSION_STRING,
             (unsigned int)options.port, options.config_dir.generic_u8string().c_str());

    /*-----------------------------------------------------*\
    | Settings: the SMBus (RAM, older motherboards) on or   |
    | off, from the command line each time                  |
    \*-----------------------------------------------------*/
    SettingsManager* settings   = ResourceManager::get()->GetSettingsManager();
    json detector_settings      = settings->GetSettings("Detectors");
    detector_settings["smbus_disabled"] = options.no_smbus;
    settings->SetSettings("Detectors", detector_settings);

    if(options.no_smbus)
    {
        LOG_INFO("[Aethereal] SMBus off: no RAM, no motherboards reached through it");
    }

    ResourceManager::get()->SetDefaultServerHost(AETHEREAL_SERVICE_HOST);
    ResourceManager::get()->SetDefaultServerPort(options.port);

    if(options.parent_pid > 0)
    {
        WatchParent(options.parent_pid);
    }

#ifdef _WIN32
    std::thread(TimerResolutionThread).detach();

    if(started_as_service)
    {
        ResourceManager::get()->RegisterResourceManagerCallback(ServiceResourceManagerCallback, NULL);
    }
    else
    {
        SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
    }
#else
    std::signal(SIGINT,  SignalHandler);
    std::signal(SIGTERM, SignalHandler);
#endif

    /*-----------------------------------------------------*\
    | Detect devices and start the server: no local         |
    | connection attempt, no CLI actions, no window         |
    \*-----------------------------------------------------*/
    ResourceManager::get()->Initialize(false, true, true, false, false);
    ResourceManager::get()->WaitForInitialization();

#ifdef _WIN32
    if(started_as_service)
    {
        ResourceManager::get()->UnregisterResourceManagerCallback(ServiceResourceManagerCallback, NULL);
    }
#endif

    int             exitval = EXIT_SUCCESS;
    NetworkServer*  server  = ResourceManager::get()->GetServer();

    if(server && server->GetOnline())
    {
        server->SetName(std::string(AETHEREAL_SERVICE_NAME) + " " + VERSION_STRING);

#ifdef _WIN32
        if(started_as_service)
        {
            ReportServiceStatus(SERVICE_RUNNING, NO_ERROR, 0);
        }
#endif

        std::unique_lock<std::mutex> lock(stop_mutex);
        while(server->GetOnline() && !stop_requested)
        {
            stop_cv.wait_for(lock, 1s);
        }
        server->StopServer();
    }
    else
    {
        LOG_ERROR("[Aethereal] The SDK server didn't start (is port %u taken?)", (unsigned int)options.port);
        exitval = EXIT_FAILURE;
    }

    /*-----------------------------------------------------*\
    | Let devices go back to how they were, then close them |
    \*-----------------------------------------------------*/
    ResourceManager::get()->ServiceShutdown();
    DetectionManager::get()->Cleanup();

    LOG_INFO("[Aethereal] Stopped with exit code %d", exitval);

    return exitval;
}

#ifdef _WIN32
static void WINAPI ServiceMain(DWORD, LPTSTR*)
{
    service_status.dwServiceType                = SERVICE_WIN32_OWN_PROCESS;
    service_status.dwServiceSpecificExitCode    = 0;

    started_as_service                          = true;
    service_status_handle                       = RegisterServiceCtrlHandlerEx(service_name, ServiceControlHandler, NULL);

    if(service_status_handle == 0)
    {
        LogEvent("RegisterServiceCtrlHandlerEx failed");
        return;
    }

    ReportServiceStatus(SERVICE_START_PENDING, NO_ERROR, 30000);

    /*-----------------------------------------------------*\
    | The options are the ones in the service's command     |
    | line (set by the installer), not the start arguments  |
    \*-----------------------------------------------------*/
    if(RunService(main_argc, main_argv) != EXIT_SUCCESS)
    {
        LogEvent(AETHEREAL_SERVICE_NAME " stopped with an error; see its log");
    }

    ReportServiceStatus(SERVICE_STOPPED, NO_ERROR, 0);
}

int main(int argc, char* argv[])
{
    main_argc = argc;
    main_argv = argv;

    /*-----------------------------------------------------*\
    | This calls ServiceMain() if we were started as a      |
    | service, and fails straight away if not               |
    \*-----------------------------------------------------*/
    if(StartServiceCtrlDispatcher(service_dispatch_table))
    {
        return(started_as_service ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    if(GetLastError() != ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)
    {
        LogEvent("StartServiceCtrlDispatcher failed");
        return EXIT_FAILURE;
    }

    if(AttachConsole(ATTACH_PARENT_PROCESS))
    {
        freopen("CONIN$",  "r", stdin);
        freopen("CONOUT$", "w", stdout);
        freopen("CONOUT$", "w", stderr);
        have_console = true;
    }

    return RunService(argc, argv);
}
#else
int main(int argc, char* argv[])
{
    return RunService(argc, argv);
}
#endif
