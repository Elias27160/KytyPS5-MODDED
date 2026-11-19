#include "common/profiler.h"

#include "common/emulatorConfig.h"

#include <common/TracyProtocol.hpp>
#include <common/TracyVersion.hpp>
#include <cstdio>
#include <cstring>
#include <string>
#include <tracy/Tracy.hpp>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Profiler {

void SetThreadName(const char* name) {
#if defined(_WIN32)
	// Lite: also name the OS thread, so the overlay and external profilers can tell threads apart.
	if (name != nullptr) {
		std::wstring wide(name, name + std::strlen(name));
		SetThreadDescription(GetCurrentThread(), wide.c_str());
	}
#endif
	if (tracy::ProfilerAvailable() && name != nullptr) {
		tracy::SetThreadName(name);
	}
}

void Initialize() {
	if (Config::ProfilerEnabled() && !tracy::ProfilerAvailable()) {
		tracy::StartupProfiler();
		TracySetProgramName("KytyPS5");
		::printf("Tracy profiler enabled: client %d.%d.%d, protocol %u, "
		         "broadcast %u, connect to 127.0.0.1:8086\n",
		         tracy::Version::Major, tracy::Version::Minor, tracy::Version::Patch,
		         tracy::ProtocolVersion, tracy::BroadcastVersion);
	}
}

void Shutdown() {
	if (tracy::ProfilerAvailable()) {
		tracy::ShutdownProfiler();
	}
}

} // namespace Profiler
