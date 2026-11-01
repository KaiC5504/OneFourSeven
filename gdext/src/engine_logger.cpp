#include "engine_logger.h"

#include <string>

#include "ofs/log.hpp"

using namespace godot;

namespace {

// Godot calls loggers from any thread, and anything below might itself raise an engine
// error (a bad UTF-8 conversion, say), which would land back here.
thread_local bool t_in_logger = false;

class ReentryGuard {
public:
    ReentryGuard() : entered_(!t_in_logger) { t_in_logger = true; }
    ~ReentryGuard() {
        if (entered_) {
            t_in_logger = false;
        }
    }
    bool entered() const { return entered_; }

private:
    bool entered_;
};

std::string to_std(const String &text) {
    const CharString utf8 = text.utf8();
    return std::string(utf8.get_data(), size_t(utf8.length()));
}

const char *kind_name(int32_t error_type) {
    switch (error_type) {
        case Logger::ERROR_TYPE_WARNING:
            return "warning";
        case Logger::ERROR_TYPE_SCRIPT:
            return "script";
        case Logger::ERROR_TYPE_SHADER:
            return "shader";
        default:
            return "error";
    }
}

}  // namespace

void OfsEngineLogger::_bind_methods() {}

void OfsEngineLogger::_log_error(const String &function, const String &file, int32_t line, const String &code,
                                 const String &rationale, bool, int32_t error_type,
                                 const TypedArray<Ref<ScriptBacktrace>> &script_backtraces) {
    ReentryGuard guard;
    if (!guard.entered()) {
        return;
    }
    nlohmann::json stack = nlohmann::json::array();
    for (int64_t i = 0; i < script_backtraces.size(); ++i) {
        const Ref<ScriptBacktrace> backtrace = script_backtraces[i];
        if (backtrace.is_null()) {
            continue;
        }
        const int32_t frames = backtrace->get_frame_count();
        for (int32_t frame = 0; frame < frames; ++frame) {
            stack.push_back(to_std(backtrace->get_frame_file(frame)) + ":" +
                            std::to_string(backtrace->get_frame_line(frame)) + " " +
                            to_std(backtrace->get_frame_function(frame)));
        }
    }
    nlohmann::json data = {
        {"kind", kind_name(error_type)},
        {"message", to_std(rationale.is_empty() ? code : rationale)},
        {"file", to_std(file)},
        {"line", int64_t(line)},
        {"function", to_std(function)},
        {"stack", std::move(stack)},
    };
    const auto level = error_type == ERROR_TYPE_WARNING ? ofs::log::Level::warn : ofs::log::Level::error;
    ofs::log::Logger::instance().emit("app", level, "app.error", std::move(data));
}

void OfsEngineLogger::_log_message(const String &message, bool error) {
    // Ordinary print() output is chatter; only printerr() and friends are worth a line.
    if (!error) {
        return;
    }
    ReentryGuard guard;
    if (!guard.entered()) {
        return;
    }
    ofs::log::Logger::instance().emit("app", ofs::log::Level::error, "app.error",
                                      {{"kind", "message"}, {"message", to_std(message.strip_edges(false, true))}});
}
