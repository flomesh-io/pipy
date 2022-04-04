/*
MIT License
Copyright (c) 2019 win32ports
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#ifndef __SYS_WAIT_H_B07AB843_5FF0_4FCF_B6F0_8CB97D28F337__
#define __SYS_WAIT_H_B07AB843_5FF0_4FCF_B6F0_8CB97D28F337__

#ifndef _WIN32

#pragma message("this sys/wait.h implementation is for Windows only!")

#else /* _WIN32 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef __GNUC__
#include <sys/time.h> /* for timeval */
#endif /* __GNUC__  */

#include <errno.h>
#include <string.h>

#ifndef _INC_WINDOWS

typedef unsigned long DWORD;
typedef DWORD * LPDWORD;

#ifdef _WIN64

typedef long long LONG_PTR;
typedef unsigned long long ULONG_PTR;

#else /* _WIN64 */

typedef long LONG_PTR;
typedef unsigned long ULONG_PTR;

#endif

typedef long LONG;
typedef wchar_t WCHAR;
typedef int BOOL;
typedef void VOID;
typedef void * HANDLE;

#ifndef MAX_PATH
#define MAX_PATH 260
#endif /* MAX_PATH */

#ifndef WINAPI
#define WINAPI __stdcall
#endif /* WINAPI */

#ifndef DECLSPEC_DLLIMPORT
#ifdef _MSC_VER
#define DECLSPEC_DLLIMPORT __declspec(dllimport)
#else /* _MSC_VER */
#define DECLSPEC_DLLIMPORT
#endif /* _MSC_VER */
#endif /* DECLSPEC_DLLIMPORT */

#ifndef WINBASEAPI
#define WINBASEAPI DECLSPEC_DLLIMPORT
#endif /* WINBASEAPI */

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(LONG_PTR)-1)
#endif /* INVALID_HANDLE_VALUE */

#ifndef SYNCHRONIZE
#define SYNCHRONIZE (0x00100000L)
#endif /* SYNCHRONIZE */

#ifndef PROCESS_QUERY_INFORMATION
#define PROCESS_QUERY_INFORMATION (0x0400)
#endif /* PROCESS_QUERY_INFORMATION */

#ifndef INFINITE
#define INFINITE 0xFFFFFFFF
#endif /* INFINITE */

#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0 0
#endif /* WAIT_OBJECT_0 */

#ifndef WAIT_TIMEOUT
#define WAIT_TIMEOUT 258L
#endif /* WAIT_TIMEOUT */

/*
FIXME: causes FILETIME to conflict
#ifndef _FILETIME_
#define _FILETIME_
typedef struct FILETIME {
    DWORD dwLowDateTime;
    DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;
#endif
*/

WINBASEAPI DWORD WINAPI GetCurrentProcessId(VOID);
WINBASEAPI BOOL WINAPI CloseHandle(HANDLE hObject);
WINBASEAPI HANDLE WINAPI OpenProcess(DWORD dwDesiredAccess, BOOL bInheritHandle, DWORD dwProcessId);
WINBASEAPI DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds);
WINBASEAPI BOOL WINAPI GetExitCodeProcess(HANDLE hProcess, LPDWORD lpExitCode);

/*
FIXME: causes FILETIME to conflict
WINBASEAPI BOOL WINAPI GetProcessTimes(HANDLE hProcess, LPFILETIME lpCreationTime, LPFILETIME lpExitTime, LPFILETIME lpKernelTime, LPFILETIME lpUserTime);
*/

#endif /* _INC_WINDOWS */

#ifndef _WINSOCKAPI_

struct private_timeval {
    long    tv_sec;
    long    tv_usec;
};

#define timeval private_timeval

#endif /* _WINSOCKAPI_ */

#ifndef _INC_TOOLHELP32

typedef struct tagPROCESSENTRY32W
{
    DWORD   dwSize;
    DWORD   cntUsage;
    DWORD   th32ProcessID;
    ULONG_PTR th32DefaultHeapID;
    DWORD   th32ModuleID;
    DWORD   cntThreads;
    DWORD   th32ParentProcessID;
    LONG    pcPriClassBase;
    DWORD   dwFlags;
    WCHAR   szExeFile[MAX_PATH];
} PROCESSENTRY32W;
typedef PROCESSENTRY32W *  PPROCESSENTRY32W;
typedef PROCESSENTRY32W *  LPPROCESSENTRY32W;

#ifndef TH32CS_SNAPPROCESS
#define TH32CS_SNAPPROCESS 2
#endif /* TH32CS_SNAPPROCESS */

HANDLE WINAPI CreateToolhelp32Snapshot(DWORD dwFlags, DWORD th32ProcessID);
BOOL WINAPI Process32FirstW(HANDLE hSnapshot, LPPROCESSENTRY32W lppe);
BOOL WINAPI Process32NextW(HANDLE hSnapshot, LPPROCESSENTRY32W lppe);

#endif /* _INC_TOOLHELP32 */

#ifndef WNOHANG
#define WNOHANG 1
#endif /* WNOHANG */

#ifndef WUNTRACED
#define WUNTRACED 2
#endif /* WUNTRACED */

#ifndef __WEXITSTATUS
#define __WEXITSTATUS(status) (((status) & 0xFF00) >> 8)
#endif /* __WEXITSTATUS */

#ifndef __WIFEXITED
#define __WIFEXITED(status) (__WTERMSIG(status) == 0)
#endif /* __WIFEXITED */

#ifndef __WTERMSIG
#define __WTERMSIG(status) ((status) & 0x7F)
#endif /* __WTERMSIG */

#ifndef __WIFSIGNALED
#define __WIFSIGNALED(status) (((signed char)(__WTERMSIG(status) + 1) >> 1) > 0)
#endif /* __WIFSIGNALED */

#ifndef __WIFSTOPPED
#define __WIFSTOPPED(status) (((status) & 0xFF) == 0x7F)
#endif /* __WIFSTOPPED */

#ifndef __WSTOPSIG
#define __WSTOPSIG(status) __WEXITSTATUS(status)
#endif /* __WSTOPSIG */

#ifndef __WCONTINUED
#define __WCONTINUED 8
#endif /* __WCONTINUED */

#ifndef __WNOWAIT
#define __WNOWAIT 0x01000000
#endif /* __WNOWAIT */

#ifndef WEXITSTATUS
#define WEXITSTATUS(status) __WEXITSTATUS(status)
#endif /* WEXITSTATUS */

#ifndef WIFEXITED
#define WIFEXITED(status) __WIFEXITED(status)
#endif /* WIFEXITED */

#ifndef WIFSIGNALED
#define WIFSIGNALED(status) __WIFSIGNALED(status)
#endif /* WIFSIGNALED */

#ifndef WTERMSIG
#define WTERMSIG(status) __WTERMSIG(status)
#endif /* WTERMSIG */

#ifndef WIFSTOPPED
#define WIFSTOPPED(status) __WIFSTOPPED(status)
#endif /* WIFSTOPPED */

#ifndef WSTOPSIG
#define WSTOPSIG(status) __WSTOPSIG(status)
#endif /* WSTOPSIG */

#if !defined(__pid_t_defined) && !defined(_PID_T_) && !defined(pid_t)
#define __pid_t_defined 1
#define _PID_T_
typedef int __pid_t;
typedef __pid_t pid_t;
#endif /* !defined(__pid_t_defined) && !defined(_PID_T_) && !defined(pid_t) */

#ifndef __id_t_defined
#define __id_t_defined 1
typedef unsigned __id_t;
typedef __id_t id_t;
#endif /* __id_t_defined */

#ifndef __uid_t_defined
#define __uid_t_defined 1
typedef unsigned __uid_t;
typedef __uid_t uid_t;
#endif /* __uid_t_defined */

#ifndef __siginfo_t_defined
#define __siginfo_t_defined 1
typedef struct
{
    int si_signo; /* signal number */
    int si_code; /* signal code */
    int si_errno; /* if non-zero, errno associated with this signal, as defined in <errno.h> */
    pid_t si_pid; /* sending process ID */
    uid_t si_uid; /* real user ID of sending process */
    void * si_addr; /* address of faulting instruction */
    int si_status; /* exit value of signal */
    long si_band; /* band event of SIGPOLL */
}
siginfo_t;
#endif /* __siginfo_t_defined */

struct rusage
{
    struct timeval ru_utime; /* user time used */
    struct timeval ru_stime; /* system time used */
};

#ifdef _XOPEN_SOURCE

#ifndef __W_CONTINUED
#define __W_CONTINUED 0xFFFF
#endif /* __W_CONTINUED */

#ifndef __WIFCONTINUED
#define __WIFCONTINUED(status) ((status) == __W_CONTINUED)
#endif /* __WIFCONTINUED */

#ifndef WIFCONTINUED
#define WIFCONTINUED(status) __WIFCONTINUED(status)
#endif /* WIFCONTINUED */

#ifndef WSTOPPED
#define WSTOPPED 2
#endif /* WSTOPPED */

#ifndef WEXITED
#define WEXITED 4
#endif /* WEXITED */

#ifndef WCONTINUED
#define WCONTINUED __WCONTINUED
#endif /* WCONTINUED */

#ifndef WNOWAIT
#define WNOWAIT __WNOWAIT
#endif /* WNOWAIT */

typedef enum
{
    P_ALL,
    P_PID,
    P_PGID
}
idtype_t;

#endif /* _XOPEN_SOURCE  */

static int __filter_anychild(PROCESSENTRY32W * pe, DWORD pid)
{
    return pe->th32ParentProcessID == GetCurrentProcessId();
}

static int __filter_pid(PROCESSENTRY32W * pe, DWORD pid)
{
    return pe->th32ProcessID == pid;
}

/*
FIXME: causes FILETIME to conflict
static void __filetime2timeval(FILETIME time, struct timeval * out)
{
    unsigned long long value = time.dwHighDateTime;
    value = (value << 32) | time.dwLowDateTime;
    out->tv_sec = (long)(value / 1000000);
    out->tv_usec = (long)(value % 1000000);
}
*/

static int __waitpid_internal(pid_t pid, int * status, int options, siginfo_t * infop, struct rusage * rusage)
{
    int saved_status = 0;
    HANDLE hProcess = INVALID_HANDLE_VALUE, hSnapshot = INVALID_HANDLE_VALUE;
    int (*filter)(PROCESSENTRY32W*, DWORD);
    PROCESSENTRY32W pe;
    DWORD wait_status = 0, exit_code = 0;
    int nohang = WNOHANG == (WNOHANG & options);
    options &= ~(WUNTRACED | __WNOWAIT | __WCONTINUED | WNOHANG);
    if (options)
    {
        errno = -EINVAL;
        return -1;
    }

    if (pid == -1)
    {
        /* wait for any child */
        filter = __filter_anychild;
    }
    else if (pid < -1)
    {
        /* wait for any process from the group */
        abort(); /* not implemented */
    }
    else if (pid == 0)
    {
        /* wait for any process from the current group */
        abort(); /* not implemented */
    }
    else
    {
        /* wait for process with given pid */
        filter = __filter_pid;
    }

    hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (INVALID_HANDLE_VALUE == hSnapshot)
    {
        errno = ECHILD;
        return -1;
    }
    pe.dwSize = sizeof(pe);
    if (!Process32FirstW(hSnapshot, &pe))
    {
        CloseHandle(hSnapshot);
        errno = ECHILD;
        return -1;
    }
    do
    {
        if (filter(&pe, pid))
        {    
            hProcess = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION, 0, pe.th32ProcessID);
            if (INVALID_HANDLE_VALUE == hProcess)
            {
                CloseHandle(hSnapshot);
                errno = ECHILD;
                return -1;
            }
            break;
        }
    }
    while (Process32NextW(hSnapshot, &pe));
    if (INVALID_HANDLE_VALUE == hProcess)
    {
        CloseHandle(hSnapshot);
        errno = ECHILD;
        return -1;
    }

    wait_status = WaitForSingleObject(hProcess, nohang ? 0 : INFINITE);

    if (WAIT_OBJECT_0 == wait_status)
    {
        if (GetExitCodeProcess(hProcess, &exit_code))
            saved_status |= (exit_code & 0xFF) << 8;
    }
    else if (WAIT_TIMEOUT == wait_status && nohang)
    {
        return 0;
    }
    else
    {
        CloseHandle(hProcess);
        CloseHandle(hSnapshot);
        errno = ECHILD;
        return -1;
    }
    if (rusage)
    {
        memset(rusage, 0, sizeof(*rusage));
        /*
        FIXME: causes FILETIME to conflict
        FILETIME creation_time, exit_time, kernel_time, user_time;
        if (GetProcessTimes(hProcess, &creation_time, &exit_time, &kernel_time, &user_time))
        {
             __filetime2timeval(kernel_time, &rusage->ru_stime);
             __filetime2timeval(user_time, &rusage->ru_utime);
        }
        */
    }
    if (infop)
    {
        memset(infop, 0, sizeof(*infop));
    }

    CloseHandle(hProcess);
    CloseHandle(hSnapshot);

    if (status)
        *status = saved_status;

    return pe.th32ParentProcessID;
}

static int waitpid(pid_t pid, int * status, int options)
{
    return __waitpid_internal(pid, status, options, NULL, NULL);
}

static int wait(int *status)
{
    return __waitpid_internal(-1, status, 0, NULL, NULL);
}

#ifdef _XOPEN_SOURCE

static int waitid(idtype_t idtype, id_t id, siginfo_t * infop, int options)
{
    pid_t pid;
    switch (idtype)
    {
    case P_PID: pid = id; break;
    case P_PGID: pid = -(pid_t)id; break;
    case P_ALL: pid = 0; break;
    default: errno = EINVAL; return -1;
    }
    return __waitpid_internal(pid, NULL, options, infop, NULL);
}

static pid_t wait3(int * status, int options, struct rusage * rusage)
{
    return __waitpid_internal(-1, status, options, NULL, rusage);
}

static pid_t wait4(pid_t pid, int * status, int options, struct rusage * rusage)
{
    return __waitpid_internal(pid, status, options, NULL, rusage);
}

#endif /* _XOPEN_SOURCE */

#ifndef _INC_WINDOWS

#undef WAIT_OBJECT_0

#undef FILETIME
#undef PFILETIME
#undef LPFILETIME

#endif /* _INC_WINDOWS */

#ifndef _WINSOCKAPI_

#undef timeval

#endif /* _WINSOCKAPI_ */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _WIN32 */

#endif /* __SYS_WAIT_H_B07AB843_5FF0_4FCF_B6F0_8CB97D28F337__ */