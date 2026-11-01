#pragma once

#include <functional>
#include <memory>
#include <string>

// Finds tools/devlink.py on the local network: NWBrowser on iOS
// (gdext/ios/bonjour_browser.mm), a stub that never finds anything elsewhere
// (bonjour_browser_stub.cpp; desktop builds talk to 127.0.0.1 instead).
class BonjourBrowser {
public:
    struct Found {
        std::string name;
        std::string host;  // IPv4 dotted quad
        int port = 0;
    };
    // Called on the Godot main thread (results are queued and delivered from poll()).
    using FoundCallback = std::function<void(const Found &)>;
    // error is human readable; local_network_denied is true when iOS refused the
    // local network permission (NWError kDNSServiceErr_PolicyDenied).
    using FailedCallback = std::function<void(const std::string &error, bool local_network_denied)>;

    static std::unique_ptr<BonjourBrowser> create(const std::string &service_type);
    virtual ~BonjourBrowser() = default;
    virtual bool supported() const = 0;
    virtual void start(FoundCallback found, FailedCallback failed) = 0;
    virtual void stop() = 0;
    // Delivers queued results; call from _process.
    virtual void poll() = 0;
};
