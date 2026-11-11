#pragma once

#ifndef _WIN32

#include <windows.h>

#include "log/log.hpp"
#include <directx-native/dxn_event.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#define THREAD_PRIORITY_TIME_CRITICAL 15

inline HANDLE GetCurrentProcess() {
  mullion::Logger::warn("GetCurrentProcess not implemented.");
  return nullptr;
}

inline HANDLE GetCurrentThread() {
  mullion::Logger::warn("GetCurrentThread not implemented.");
  return nullptr;
}

inline DWORD GetCurrentProcessId() {
  return static_cast<DWORD>(getpid());
}

inline DWORD GetCurrentThreadId() {
  uint64_t id = 0;
  pthread_threadid_np(nullptr, &id);
  return static_cast<DWORD>(id);
}

inline BOOL QueryPerformanceFrequency(LARGE_INTEGER *frequency) {
  frequency->QuadPart = 1000000000;
  return TRUE;
}

inline BOOL QueryPerformanceCounter(LARGE_INTEGER *counter) {
  counter->QuadPart = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
  return TRUE;
}

inline void OutputDebugStringA(const char *message) {
  mullion::Logger::debug(message);
}

inline BOOL SetEvent(HANDLE handle) {
  if (!handle || !dxn_event_is_event(handle))
    return FALSE;
  dxn_event_signal(handle);
  return TRUE;
}

inline HANDLE CreateEventW(SECURITY_ATTRIBUTES *, BOOL manual_reset, BOOL initial_state, LPCWSTR) {
  return dxn_event_create(manual_reset, initial_state);
}

constexpr DWORD GENERIC_ALL = 0x10000000;
constexpr DWORD GENERIC_READ = 0x80000000;
constexpr DWORD GENERIC_WRITE = 0x40000000;

constexpr DWORD MEM_COMMIT = 0x1000;
constexpr DWORD MEM_RESERVE = 0x2000;
constexpr DWORD MEM_RELEASE = 0x8000;
constexpr DWORD MEM_WRITE_WATCH = 0x200000;
constexpr DWORD PAGE_READWRITE = 0x04;

inline uint64_t GetTickCount64() {
  return clock_gettime_nsec_np(CLOCK_MONOTONIC_RAW) / 1000000;
}

inline void DebugBreak() { __builtin_debugtrap(); }

struct SYSTEM_INFO {
  DWORD dwPageSize;
  DWORD dwAllocationGranularity;
};

inline void GetSystemInfo(SYSTEM_INFO *info) {
  info->dwPageSize = getpagesize();
  info->dwAllocationGranularity = 65536;
}

void *VirtualAlloc(void *address, size_t length, DWORD flags, DWORD protection);
BOOL VirtualFree(void *address, size_t length, DWORD flags);

inline BOOL ProcessIdToSessionId(DWORD pid, DWORD *id) {
  mullion::Logger::warn("ProcessIdToSessionId not implemented.");
  *id = 0;
  return FALSE;
}

inline BOOL SetThreadPriority(HANDLE hThread, int nPriority) {
  mullion::Logger::warn("SetThreadPriority not implemented.");
  return FALSE;
}

inline HANDLE GetModuleHandle(LPCSTR lpModuleName) {
  mullion::Logger::warn("GetModuleHandle not implemented.");
  return nullptr;
}

inline void ExitThread(DWORD dwExitCode) {
  mullion::Logger::warn("ExitThread not implemented.");
}

#define WM_SIZE 0

inline LONG SendMessage(HWND hWnd, UINT Msg, UINT_PTR wParam, LONG_PTR lParam) {
  mullion::Logger::warn("SendMessage not implemented.");
  return 0;
}

inline DWORD GetProcessId(HANDLE Process) {
  mullion::Logger::warn("GetProcessId not implemented.");
  return 0;
}

inline DWORD GetWindowThreadProcessId(HWND hWnd, LPDWORD lpdwProcessId) {
  mullion::Logger::warn("GetWindowThreadProcessId not implemented.");
  *lpdwProcessId = 0;
  return 0;
}

inline DWORD MAKELONG(WORD a, WORD b) {
  return (((DWORD)a) << 16) | ((DWORD)b);
}

typedef SECURITY_ATTRIBUTES *LPSECURITY_ATTRIBUTES;

inline HANDLE CreateSemaphore(LPSECURITY_ATTRIBUTES lpSemaphoreAttributes,
                              LONG                  lInitialCount,
                              LONG                  lMaximumCount,
                              LPCSTR                lpName) {
  mullion::Logger::warn("CreateSemaphore not implemented.");
  return nullptr;
}

inline BOOL ReleaseSemaphore(HANDLE hSemaphore,
                             LONG   lReleaseCount,
                             LPLONG lpPreviousCount) {
  mullion::Logger::warn("ReleaseSemaphore not implemented.");
  return FALSE;
}

inline BOOL CloseHandle(HANDLE hObject) {
  if (!hObject || !dxn_event_is_event(hObject))
    return FALSE;
  dxn_event_close(hObject);
  return TRUE;
}

inline BOOL DuplicateHandle(HANDLE   hSourceProcessHandle,
                            HANDLE   hSourceHandle,
                            HANDLE   hTargetProcessHandle,
                            LPHANDLE lpTargetHandle,
                            DWORD    dwDesiredAccess,
                            BOOL     bInheritHandle,
                            DWORD    dwOptions)
{
  *lpTargetHandle = hSourceHandle ? dxn_event_duplicate(hSourceHandle) : nullptr;
  return *lpTargetHandle != nullptr;
}


#define ARRAYSIZE(a) (sizeof(a)/sizeof(*(a)))

#endif
