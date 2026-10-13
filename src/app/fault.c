/* Pattern from jarvis-jhk/gtetrinet-ng: release binaries post one anonymous line
 * (name, version, platform) to a public ntfy topic; a GitHub Action in this repo turns
 * it into an issue and forwards it with a secret it alone holds. No logs, no user data. */
#include "fault.h"
#include "version.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DM_FAULT_TOPIC "https://ntfy.sh/drmario64-vs-faults-9c41d7a2e05b4f18"

#if defined(__GLIBC__)
#include <execinfo.h>
#include <sys/wait.h>
#include <unistd.h>
static void on_crash(int sig)
{
    void *frames[48];
    char head[96];
    int n = snprintf(head, sizeof head, "CRASH: %s %s, signal %d\n", DM_APP_NAME, DM_VERSION, sig);
    if (write(2, head, (size_t)n) < 0) {}
    int k = backtrace(frames, 48);
    backtrace_symbols_fd(frames, k, 2);
    signal(sig, SIG_DFL);
    _exit(128 + sig);
}
#elif defined(_WIN32)
#include <windows.h>
static LONG WINAPI on_exception(EXCEPTION_POINTERS *e)
{
    fprintf(stderr, "CRASH: %s %s, exception 0x%08lx at %p\n", DM_APP_NAME, DM_VERSION,
            (unsigned long)e->ExceptionRecord->ExceptionCode, e->ExceptionRecord->ExceptionAddress);
    fflush(stderr);
    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

void dm_fault_install(void)
{
#ifdef __SANITIZE_ADDRESS__
    return; /* keep the sanitizer's own handler in diagnostic builds */
#endif
#if defined(__GLIBC__)
    signal(SIGSEGV, on_crash);
    signal(SIGBUS, on_crash);
    signal(SIGFPE, on_crash);
    signal(SIGILL, on_crash);
    signal(SIGABRT, on_crash);
#elif defined(_WIN32)
    SetUnhandledExceptionFilter(on_exception);
#endif
}

/* Signal handlers only write diagnostics; a crash is reported by the launcher after the
 * process exits. Only ordinary faults start a helper from here. */
static void report_fault(void)
{
    static int sent;
    const char *off = getenv("DM_NO_REPORT");
    if (!DM_FAULT_REPORTS || sent || (off && !strcmp(off, "1"))) return;
    sent = 1;
#ifdef _WIN32
    char command[] = "curl.exe -fsS --max-time 8 -d \"" DM_APP_NAME " " DM_VERSION " Windows application fault\" " DM_FAULT_TOPIC;
    STARTUPINFOA start = {0};
    PROCESS_INFORMATION process = {0};
    start.cb = sizeof start;
    if (CreateProcessA(NULL, command, NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, NULL, &start, &process)) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    }
#elif defined(__GLIBC__)
    /* double fork: no zombies while the game keeps running */
    pid_t child = fork();
    if (child == 0) {
        if (fork() == 0)
            execlp("curl", "curl", "-fsS", "--max-time", "8", "-o", "/dev/null", "-d",
                   DM_APP_NAME " " DM_VERSION " Linux application fault", DM_FAULT_TOPIC, (char *)NULL);
        _exit(0);
    }
    if (child > 0) waitpid(child, NULL, 0);
#endif
}

void dm_fault(const char *what, const char *detail)
{
    fprintf(stderr, "FAULT: %s: %s\n", what, detail ? detail : "");
    fflush(stderr);
    report_fault();
}
