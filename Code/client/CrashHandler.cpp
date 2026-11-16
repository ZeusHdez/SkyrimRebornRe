#include <BranchInfo.h>
#include "CrashHandler.h"
#include <DbgHelp.h>
#include <Windows.h>
#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <strsafe.h>

using time_point = std::chrono::system_clock::time_point;

std::string SerializeTimePoint(const time_point& time, const std::string& format)
{
    std::time_t tt = std::chrono::system_clock::to_time_t(time);
    std::tm tm = *std::gmtime(&tt); // GMT (UTC)
    // std::tm tm = *std::localtime(&tt); //Locale time-zone, usually UTC by default.
    std::stringstream ss;
    ss << std::put_time(&tm, format.c_str());
    return ss.str();
}

// Logs an address together with the module it belongs to, so that it can be looked up without a dump
static void LogCodeAddress(const char* acpLabel, const uintptr_t aAddress, const MEMORY_BASIC_INFORMATION& acInfo)
{
    char modulePath[MAX_PATH] = "";
    const uintptr_t cBase = reinterpret_cast<uintptr_t>(acInfo.AllocationBase);
    if (!cBase || !GetModuleFileNameA(static_cast<HMODULE>(acInfo.AllocationBase), modulePath, sizeof(modulePath)))
        StringCchCopyA(modulePath, sizeof(modulePath), "unknown module");

    spdlog::error("\t{}: {:#x} ({} + {:#x})", acpLabel, aAddress, std::filesystem::path(modulePath).filename().string(), aAddress - cBase);
}

// The dump often does not make it to us, so write the faulting address and everything on the stack that
// points into code to the log. A jump to a null function pointer leaves its caller at the top of that list.
static void LogCrashContext(const PEXCEPTION_POINTERS apExceptionInfo)
{
    constexpr uint32_t kMaxScannedSlots = 1024;
    constexpr uint32_t kMaxLoggedSlots = 48;
    constexpr DWORD kExecutable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;

    const EXCEPTION_RECORD* pRecord = apExceptionInfo->ExceptionRecord;
    const CONTEXT* pContext = apExceptionInfo->ContextRecord;

    if (pRecord->NumberParameters >= 2)
        spdlog::error("\taccess type {} on memory address {:#x}", pRecord->ExceptionInformation[0], pRecord->ExceptionInformation[1]);

    spdlog::error("\tmain module is loaded at {:#x}", reinterpret_cast<uintptr_t>(GetModuleHandleA(nullptr)));

    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<void*>(pContext->Rip), &info, sizeof(info)))
        LogCodeAddress("crashed at", pContext->Rip, info);

    // The handler runs on the thread that crashed
    const NT_TIB* pThreadInfo = reinterpret_cast<const NT_TIB*>(NtCurrentTeb());
    const uintptr_t stackLow = reinterpret_cast<uintptr_t>(pThreadInfo->StackLimit);
    const uintptr_t stackHigh = reinterpret_cast<uintptr_t>(pThreadInfo->StackBase);
    if (pContext->Rsp < stackLow || pContext->Rsp >= stackHigh)
    {
        spdlog::error("\tstack pointer {:#x} is outside of the thread stack, not reading it", pContext->Rsp);
        return;
    }

    spdlog::error("\tcode addresses on the stack, the callers are among them:");

    const uintptr_t* pStack = reinterpret_cast<const uintptr_t*>(pContext->Rsp & ~static_cast<uintptr_t>(7));
    uint32_t logged = 0;
    for (uint32_t i = 0; i < kMaxScannedSlots && logged < kMaxLoggedSlots; ++i)
    {
        if (reinterpret_cast<uintptr_t>(pStack + i + 1) > stackHigh)
            break;

        const uintptr_t cValue = pStack[i];
        if (!VirtualQuery(reinterpret_cast<void*>(cValue), &info, sizeof(info)))
            continue;
        if (info.State != MEM_COMMIT || !(info.Protect & kExecutable))
            continue;

        char label[32];
        StringCchPrintfA(label, sizeof(label), "rsp+%#x", i * 8);
        LogCodeAddress(label, cValue, info);
        ++logged;
    }
}

LONG WINAPI VectoredExceptionHandler(PEXCEPTION_POINTERS pExceptionInfo)
{
    static int alreadyCrashed = 0;
    auto retval = EXCEPTION_CONTINUE_SEARCH;

    // Serialize 
    static std::mutex singleThreaded;
    const std::lock_guard lock{singleThreaded};

    // Check for severe, not continuable and not software-originated exception
    if (pExceptionInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        alreadyCrashed++ == 0)
    {
        spdlog::critical (__FUNCTION__ ": crash occurred!"); 
        
        spdlog::error(__FUNCTION__ ": exception code is {:x}, at address {}, flags {:x} ",
                      pExceptionInfo->ExceptionRecord->ExceptionCode,
                      pExceptionInfo->ExceptionRecord->ExceptionAddress,
                      pExceptionInfo->ExceptionRecord->ExceptionFlags);

        LogCrashContext(pExceptionInfo);

#if (IS_MASTER)
        volatile static bool bMiniDump = false;
#else
        volatile static bool bMiniDump = true;
#endif
        if (bMiniDump)
        {
            HANDLE hDumpFile = INVALID_HANDLE_VALUE;
            std::string dumpFilePath;
            BOOL dumpWritten = FALSE;
            try
            {
                MINIDUMP_EXCEPTION_INFORMATION M;
                char dumpPath[MAX_PATH];

                M.ThreadId = GetCurrentThreadId();
                M.ExceptionPointers = pExceptionInfo;
                M.ClientPointers = 0;

                std::ostringstream oss;
                oss << "crash_" << SerializeTimePoint(std::chrono::system_clock::now(), "UTC_%Y-%m-%d_%H-%M-%S")
                    << ".dmp";

                GetModuleFileNameA(NULL, dumpPath, sizeof(dumpPath));
                std::filesystem::path modulePath(dumpPath);
                auto subPath = modulePath.parent_path();

                CrashHandler::RemovePreviousDump(subPath);

                subPath /= oss.str();
                dumpFilePath = subPath.string();

                hDumpFile = CreateFileA(subPath.string().c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL, NULL);

                // baseline settings from https://stackoverflow.com/a/63123214/5273909
                auto dumpSettings = MiniDumpWithDataSegs | MiniDumpWithProcessThreadData | MiniDumpWithHandleData |
                                    MiniDumpWithThreadInfo |
                                    /*
                                    //MiniDumpWithPrivateReadWriteMemory | // this one gens bad dump
                                    MiniDumpWithUnloadedModules |
                                    MiniDumpWithFullMemoryInfo |
                                    MiniDumpWithTokenInformation |
                                    MiniDumpWithPrivateWriteCopyMemory |
                                    */
                                    0;

                dumpWritten = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hDumpFile, (MINIDUMP_TYPE)dumpSettings,
                                  (pExceptionInfo) ? &M : NULL, NULL, NULL);
            }
            catch (...) // Mini-dump is best effort only.
            {
            }

            // CreateFile reports failure with INVALID_HANDLE_VALUE, not with a null handle
            if (hDumpFile != INVALID_HANDLE_VALUE)
                CloseHandle(hDumpFile);

            if (hDumpFile == INVALID_HANDLE_VALUE || !dumpWritten)
                spdlog::critical(__FUNCTION__ ": coredump failed, could not write {}", dumpFilePath);
            else
                spdlog::critical(__FUNCTION__ ": coredump created at {} -> flush logs.", dumpFilePath);
        }

        // Something in STR breaks top-level unhandled exception filters.
        // The Win API for them is pretty clunky (non-atomic, not chainable), 
        // but they can do some important things. If someone actually set one
        // they probably meant it; make sure it actually runs.
        // This will make more CrashLogger mods work with STR.

        // Get the current unhandled exception filter. If it has changed
        // from when STR started up, invoke it here.
        LPTOP_LEVEL_EXCEPTION_FILTER pCurrentUnhandledExceptionFilter = SetUnhandledExceptionFilter(CrashHandler::GetOriginalUnhandledExceptionFilter());
        SetUnhandledExceptionFilter(pCurrentUnhandledExceptionFilter);
        if (pCurrentUnhandledExceptionFilter != CrashHandler::GetOriginalUnhandledExceptionFilter())
        {
            spdlog::critical(__FUNCTION__ ": UnhandledExceptionFilter() workaround triggered.");

            singleThreaded.unlock();        // Might reenter, but is safe at this point.
            if ((*pCurrentUnhandledExceptionFilter)(pExceptionInfo) == EXCEPTION_CONTINUE_EXECUTION)
                retval = EXCEPTION_CONTINUE_EXECUTION;
            singleThreaded.lock();
        }

        spdlog::shutdown();
    }
    return retval;
}

LPTOP_LEVEL_EXCEPTION_FILTER CrashHandler::m_pUnhandled;
CrashHandler::CrashHandler()
{
    // Record the original (or as close as we can get) top-level unhandled exception handler.
    // We grab this so we can see if it is changed, presumably by a mod or even graphics drivers.
    // Something in STR breaks unhandled exception handling, so we'll fake it if necessary.
    // This is the only way to get the current setting, but the race is small.
    m_pUnhandled = SetUnhandledExceptionFilter(NULL);
    SetUnhandledExceptionFilter(m_pUnhandled);

    m_handler = AddVectoredExceptionHandler(1, &VectoredExceptionHandler);
}

CrashHandler::~CrashHandler()
{
}

void CrashHandler::RemovePreviousDump(std::filesystem::path path)
{
    for (auto& entry : std::filesystem::directory_iterator(path))
    {
        if (entry.path().string().find("crash") != std::string::npos)
        {
            DeleteFileA(entry.path().string().c_str());
        }
    }
}
