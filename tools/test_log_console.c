#include <windows.h>
#include <wchar.h>
#include <assert.h>
static wchar_t seen_command[2048];
static DWORD seen_flags;
static int launches;
static BOOL WINAPI test_create_process(LPCWSTR app, LPWSTR command, LPSECURITY_ATTRIBUTES pa,
    LPSECURITY_ATTRIBUTES ta, BOOL inherit, DWORD flags, LPVOID env, LPCWSTR cwd,
    LPSTARTUPINFOW startup, LPPROCESS_INFORMATION process) {
    (void)pa; (void)ta; (void)env; (void)cwd; (void)startup;
    assert(wcsstr(app, L"\\WindowsPowerShell\\v1.0\\powershell.exe") && !inherit);
    wcscpy(seen_command, command); seen_flags = flags; launches++;
    process->hProcess = CreateEventW(NULL, FALSE, FALSE, NULL);
    process->hThread = CreateEventW(NULL, FALSE, FALSE, NULL);
    return TRUE;
}
static DWORD WINAPI test_console_processes(LPDWORD list, DWORD count) { (void)list; (void)count; return 2; }
#define CreateProcessW test_create_process
#define GetConsoleProcessList test_console_processes
#include "../runtime/platform/windows.c"
#undef CreateProcessW
#undef GetConsoleProcessList

int main(void) {
    strcpy(g_log_path, "C:\\Test O'Name\\skyrecomp.log");
    _putenv("SKYRECOMP_CONSOLE_OPEN="); _putenv("SKYRECOMP_CONSOLE=1");
    open_log_console();
    assert(launches == 1 && seen_flags == CREATE_NEW_CONSOLE);
    assert(wcsstr(seen_command, L"O''Name") && wcsstr(seen_command, L"-LiteralPath"));
    open_log_console(); assert(launches == 1);
    _putenv("SKYRECOMP_CONSOLE_OPEN="); _putenv("SKYRECOMP_CONSOLE=0");
    open_log_console(); assert(launches == 1);
    puts("diagnostic console: separate process, quoted log path, no duplicate window and opt-out passed");
}
