#include "Log.hpp"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <vector>

namespace
{

bool g_ready = false;

} // namespace

namespace logging
{

void init()
{
    if (g_ready)
        return;

    try
    {
        auto console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console->set_level(spdlog::level::info);

        auto file = std::make_shared<spdlog::sinks::basic_file_sink_mt>("raytracer.log", true);
        file->set_level(spdlog::level::debug);

        std::vector<spdlog::sink_ptr> sinks{console, file};
        auto logger = std::make_shared<spdlog::logger>("raytracer", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::debug);
        logger->flush_on(spdlog::level::warn);
        spdlog::set_default_logger(logger);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        spdlog::info("logging started");
        g_ready = true;
    }
    catch (const spdlog::spdlog_ex &error)
    {
        // Keep the process alive if the log file cannot be opened.
        auto console = spdlog::stdout_color_mt("raytracer");
        spdlog::set_default_logger(console);
        spdlog::warn("logging file sink unavailable ({}); console only", error.what());
        g_ready = true;
    }
}

void shutdown()
{
    if (!g_ready)
        return;
    spdlog::info("logging stopped");
    spdlog::shutdown();
    g_ready = false;
}

} // namespace logging
