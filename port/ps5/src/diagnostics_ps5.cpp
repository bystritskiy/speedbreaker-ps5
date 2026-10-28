// SpeedBreaker PS5: request the built-in report without navigating the UI.
#include "ps5_host.h"
#include <report/report.h>
#include <user/paths.h>
#include <SDL3/SDL.h>
#include <unistd.h>

void ps5::diagnostics::Poll()
{
    static Uint64 last = 0;
    const Uint64 now = SDL_GetTicks();
    if (now - last < 500)
        return;
    last = now;
    if (report::GetBugReportState() == report::BugReportState::Saving)
        return;
    // Publish by FTP rename; consume before starting work, so one request
    // produces one archive. SaveBugReport must run on this main/UI thread.
    const auto request = GetUserPath() / ".request-bug-report";
    if (unlink(request.c_str()) == 0)
        report::SaveBugReport();
}
