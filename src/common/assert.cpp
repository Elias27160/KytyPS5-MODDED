#include "common/assert.h"

#include "common/subsystems.h"
#include "kytyGitVersion.h"

#include <cstdio>
#include <cstdlib>
#include <fmt/format.h>

namespace Common {

[[noreturn]] static void DbgReport(const char* title, std::string_view text, const char* file,
                                   int line) {
	const auto report = fmt::format("--- Build ---\n{}\n{}\n{} in {}:{}\n", KYTY_BUILD_LABEL, title,
	                                text, file, line);
	Log::WriteToConsoleAndLog(report);
	// Lite: the launcher runs the emulator with logging off, so keep the cause of a crash in the
	// working directory where the player can find it.
	if (FILE* crash = std::fopen("kyty_crash.txt", "a"); crash != nullptr) {
		std::fputs(report.c_str(), crash);
		std::fclose(crash);
	}
	Subsystems::EmergencyShutdownActive();
	std::fflush(nullptr);
	std::_Exit(321);
}

void DbgExitIfHandler(const char* expr, const char* file, int line) {
	DbgReport("--- Fatal Error ---", fmt::format("Error: condition ({}) is true", expr), file,
	          line);
}

void DbgNotImplementedHandler(const char* expr, const char* file, int line) {
	DbgReport("--- Fatal Error ---", fmt::format("Not implemented ({})", expr), file, line);
}

void DbgExitHandler(const char* file, int line, std::string_view text) {
	DbgReport("--- Error ---", text, file, line);
}

} // namespace Common
