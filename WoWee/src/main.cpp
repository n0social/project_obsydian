#include "core/application.hpp"
#include "core/config_paths.hpp"
#include "core/logger.hpp"
#include <exception>
#include <csignal>
#include <cstdlib>
#include <cctype>
#include <filesystem>
#include <string>
#include <SDL2/SDL.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <libgen.h>
#include <unistd.h>
#endif
#if defined(__linux__) && !defined(__ANDROID__)
#include <X11/Xlib.h>
#include <execinfo.h>
#include <unistd.h>
#include <libgen.h>
#include <cstring>

// Keep a persistent X11 connection for emergency mouse release in signal handlers.
// XOpenDisplay inside a signal handler is unreliable, so we open it once at startup.
static Display* g_emergencyDisplay = nullptr;

static void releaseMouseGrab() {
    if (g_emergencyDisplay) {
        XUngrabPointer(g_emergencyDisplay, CurrentTime);
        XUngrabKeyboard(g_emergencyDisplay, CurrentTime);
        XFlush(g_emergencyDisplay);
    }
}
#elif defined(__ANDROID__)
#include <unistd.h>
#include <libgen.h>
#include <cstring>
static void releaseMouseGrab() {}
#else
static void releaseMouseGrab() {}
#endif

#if defined(__linux__) && !defined(__ANDROID__)
static void crashHandlerSigaction(int sig, siginfo_t* info, void* /*ucontext*/) {
    releaseMouseGrab();
    void* frames[64];
    int n = backtrace(frames, 64);
    const char* sigName = (sig == SIGSEGV) ? "SIGSEGV" :
                          (sig == SIGABRT) ? "SIGABRT" :
                          (sig == SIGFPE)  ? "SIGFPE"  : "UNKNOWN";
    void* faultAddr = info ? info->si_addr : nullptr;
    fprintf(stderr, "\n=== CRASH: signal %s (%d) faultAddr=%p ===\n",
            sigName, sig, faultAddr);
    backtrace_symbols_fd(frames, n, STDERR_FILENO);
    FILE* f = fopen("/tmp/wowee_debug.log", "a");
    if (f) {
        fprintf(f, "\n=== CRASH: signal %s (%d) faultAddr=%p ===\n",
                sigName, sig, faultAddr);
        fflush(f);
        backtrace_symbols_fd(frames, n, fileno(f));
        fclose(f);
    }
    // Re-raise with default handler
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_DFL;
    sigaction(sig, &sa, nullptr);
    raise(sig);
}
#else
static void crashHandler(int sig) {
    releaseMouseGrab();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
#endif

static wowee::core::LogLevel readLogLevelFromEnv() {
    const char* raw = std::getenv("WOWEE_LOG_LEVEL");
    if (!raw || !*raw) return wowee::core::LogLevel::WARNING;
    std::string level(raw);
    for (char& c : level) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (level == "debug") return wowee::core::LogLevel::DEBUG;
    if (level == "info") return wowee::core::LogLevel::INFO;
    if (level == "warn" || level == "warning") return wowee::core::LogLevel::WARNING;
    if (level == "error") return wowee::core::kLogLevelError;
    if (level == "fatal") return wowee::core::LogLevel::FATAL;
    return wowee::core::LogLevel::WARNING;
}

#ifdef __APPLE__
static void selectMacUserDataPath() {
    if (std::getenv("WOW_DATA_PATH")) return;

    const char* home = std::getenv("HOME");
    if (!home || !*home) return;

    namespace fs = std::filesystem;
    const fs::path dataRoot = fs::path(home) / "Library/Application Support/Wowee/Data";
    std::error_code ec;
    bool hasManifest = fs::is_regular_file(dataRoot / "manifest.json", ec);

    const fs::path expansions = dataRoot / "expansions";
    if (!hasManifest && fs::is_directory(expansions, ec)) {
        for (fs::directory_iterator it(expansions, ec), end; it != end && !ec; it.increment(ec)) {
            if (fs::is_regular_file(it->path() / "manifest.json", ec)) {
                hasManifest = true;
                break;
            }
        }
    }

    if (hasManifest) {
        setenv("WOW_DATA_PATH", dataRoot.c_str(), 0);
    }
}
#endif

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
#if defined(__linux__) && !defined(__ANDROID__)
    g_emergencyDisplay = XOpenDisplay(nullptr);
    // Use sigaction for SIGSEGV/SIGABRT/SIGFPE to get si_addr (faulting address)
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_sigaction = crashHandlerSigaction;
        sa.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &sa, nullptr);
        sigaction(SIGABRT, &sa, nullptr);
        sigaction(SIGFPE,  &sa, nullptr);
    }
    std::signal(SIGTERM, [](int) { std::_Exit(1); });
    std::signal(SIGINT,  [](int) { std::_Exit(1); });
#else
    std::signal(SIGSEGV, crashHandler);
    std::signal(SIGABRT, crashHandler);
    std::signal(SIGFPE,  crashHandler);
    std::signal(SIGTERM, crashHandler);
    std::signal(SIGINT,  crashHandler);
#endif
    // Change working directory so relative asset paths resolve from any launch
    // location. A signed macOS bundle keeps data in Contents/Resources because
    // Contents/MacOS may contain code only.
#ifdef __APPLE__
    {
        uint32_t bufSize = 0;
        _NSGetExecutablePath(nullptr, &bufSize);
        std::string exePath(bufSize, '\0');
        _NSGetExecutablePath(exePath.data(), &bufSize);
        const std::filesystem::path executableDir =
            std::filesystem::path(exePath.c_str()).parent_path();
        const std::filesystem::path resourceDir =
            executableDir.parent_path() / "Resources";
        const std::filesystem::path runtimeDir =
            std::filesystem::is_directory(resourceDir / "assets")
                ? resourceDir
                : executableDir;
        if (chdir(runtimeDir.c_str()) != 0) {}
    }
    selectMacUserDataPath();
#elif defined(__ANDROID__)
    {
        // Prefer the directory that owns WOW_DATA_PATH (Obsidian sets this to
        // getExternalFilesDir()/Data). Relative assets/shaders then resolve next
        // to Data/ on the same writable tree adb can push into.
        if (const char* dataPath = std::getenv("WOW_DATA_PATH"); dataPath && *dataPath) {
            const std::filesystem::path parent = std::filesystem::path(dataPath).parent_path();
            if (!parent.empty()) {
                if (chdir(parent.string().c_str()) != 0) {}
            }
        } else if (const char* internal = SDL_AndroidGetInternalStoragePath();
                   internal && *internal) {
            if (chdir(internal) != 0) {}
            const std::string fallback = std::string(internal) + "/Data";
            setenv("WOW_DATA_PATH", fallback.c_str(), 1);
        }
    }
#elif defined(__linux__)
    {
        char buf[4096];
        ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
        if (len > 0) { buf[len] = '\0'; if (chdir(dirname(buf)) != 0) {} }
    }
#endif

    try {
        wowee::core::Logger::getInstance().setLogLevel(readLogLevelFromEnv());
        LOG_INFO("=== Wowee Native Client ===");
        LOG_INFO("Starting application...");

        // Seed portable config from the per-user location on first portable launch.
        wowee::core::migratePortableConfigIfNeeded();

        wowee::core::Application app;

        if (!app.initialize()) {
            LOG_FATAL("Failed to initialize application");
            return 1;
        }

        app.run();
        app.shutdown();

        LOG_INFO("Application exited successfully");
#if defined(__linux__) && !defined(__ANDROID__)
        if (g_emergencyDisplay) { XCloseDisplay(g_emergencyDisplay); g_emergencyDisplay = nullptr; }
#endif
        return 0;
    }
    catch (const std::exception& e) {
        releaseMouseGrab();
        LOG_FATAL("Unhandled exception: ", e.what());
        return 1;
    }
    catch (...) {
        releaseMouseGrab();
        LOG_FATAL("Unknown exception occurred");
        return 1;
    }
}
