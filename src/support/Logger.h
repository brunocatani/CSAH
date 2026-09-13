#pragma once

#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

namespace csah::logging
{
    inline std::shared_ptr<spdlog::logger> instance;
    inline std::filesystem::path runtimeLogDirectory;

    inline void init()
    {
        auto directory = F4SE::log::log_directory();
        const std::string_view expectedGamePath =
            REL::Module::IsVR() ? "Fallout4VR/F4SE" : "Fallout4/F4SE";
        if (!directory.value().generic_string().ends_with(expectedGamePath)) {
            directory = directory.value().parent_path().append(expectedGamePath);
        }
        *directory /= "CSAH.log";
        runtimeLogDirectory = directory->parent_path();
        auto sink =
            std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                directory->string(),
                5 * 1024 * 1024,
                3,
                true);
        instance = std::make_shared<spdlog::logger>(
            "CSAH",
            std::move(sink));
        instance->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");
        instance->set_level(spdlog::level::info);
        instance->flush_on(spdlog::level::info);
        spdlog::set_default_logger(instance);
    }

    [[nodiscard]] inline const std::filesystem::path& outputDirectory() noexcept
    {
        return runtimeLogDirectory;
    }

    template <class... Args>
    void info(
        spdlog::format_string_t<Args...> format,
        Args&&... args) noexcept
    {
        try {
            if (instance) {
                instance->info(format, std::forward<Args>(args)...);
            }
        } catch (...) {
        }
    }

    template <class... Args>
    void warn(
        spdlog::format_string_t<Args...> format,
        Args&&... args) noexcept
    {
        try {
            if (instance) {
                instance->warn(format, std::forward<Args>(args)...);
            }
        } catch (...) {
        }
    }

    template <class... Args>
    void error(
        spdlog::format_string_t<Args...> format,
        Args&&... args) noexcept
    {
        try {
            if (instance) {
                instance->error(format, std::forward<Args>(args)...);
            }
        } catch (...) {
        }
    }

    template <class... Args>
    void critical(
        spdlog::format_string_t<Args...> format,
        Args&&... args) noexcept
    {
        try {
            if (instance) {
                instance->critical(format, std::forward<Args>(args)...);
            }
        } catch (...) {
        }
    }
}
