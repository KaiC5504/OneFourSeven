#pragma once

#include <godot_cpp/classes/logger.hpp>
#include <godot_cpp/classes/script_backtrace.hpp>
#include <godot_cpp/variant/typed_array.hpp>

// Forwards Godot's own errors and warnings (engine, GDScript, shaders) into our log as
// app.error. The Log autoload registers one with OS.add_logger().
class OfsEngineLogger : public godot::Logger {
    GDCLASS(OfsEngineLogger, godot::Logger)

protected:
    static void _bind_methods();

public:
    void _log_error(const godot::String &function, const godot::String &file, int32_t line,
                    const godot::String &code, const godot::String &rationale, bool editor_notify,
                    int32_t error_type,
                    const godot::TypedArray<godot::Ref<godot::ScriptBacktrace>> &script_backtraces) override;
    void _log_message(const godot::String &message, bool error) override;
};
