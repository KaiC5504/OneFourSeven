#include "bonjour_browser.h"

// Contract stub: agent D owns this file.

namespace {
class NullBrowser : public BonjourBrowser {
public:
    bool supported() const override { return false; }
    void start(FoundCallback, FailedCallback) override {}
    void stop() override {}
    void poll() override {}
};
}  // namespace

std::unique_ptr<BonjourBrowser> BonjourBrowser::create(const std::string &) {
    return std::make_unique<NullBrowser>();
}
