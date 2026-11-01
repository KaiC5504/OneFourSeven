#include "ofs/log.hpp"

// Contract stub: agent A replaces every body in this file.

namespace ofs::log {

std::string_view level_name(Level) { return "info"; }
bool parse_level(std::string_view, Level&) { return false; }
std::string to_jsonl(const Event&) { return {}; }
bool from_jsonl(std::string_view, Event&) { return false; }
std::string new_session_id() { return "00000000-000000-000000"; }

struct FileSink::Impl {
    std::string path;
};
FileSink::FileSink(std::string, std::string, std::size_t) : impl_(std::make_unique<Impl>()) {}
FileSink::~FileSink() = default;
void FileSink::write(const Event&, const std::string&) {}
void FileSink::flush() {}
const std::string& FileSink::path() const { return impl_->path; }

struct RingBuffer::Impl {};
RingBuffer::RingBuffer(std::size_t) : impl_(std::make_unique<Impl>()) {}
RingBuffer::~RingBuffer() = default;
void RingBuffer::write(const Event&, const std::string&) {}
std::vector<std::string> RingBuffer::take(std::size_t) { return {}; }
void RingBuffer::restore(std::vector<std::string>) {}
std::size_t RingBuffer::size_lines() const { return 0; }
std::uint64_t RingBuffer::dropped() const { return 0; }

struct Logger::Impl {
    Context context;
};
Logger::Logger() : impl_(std::make_unique<Impl>()) {}
Logger& Logger::instance() {
    static Logger logger;
    return logger;
}
void Logger::configure(Context context) { impl_->context = std::move(context); }
Context Logger::context() const { return impl_->context; }
void Logger::add_sink(std::shared_ptr<Sink>) {}
void Logger::clear_sinks() {}
void Logger::emit(std::string_view, Level, std::string_view, nlohmann::json) {}
bool Logger::emit_line(std::string_view) { return false; }
void Logger::flush() {}

}  // namespace ofs::log
