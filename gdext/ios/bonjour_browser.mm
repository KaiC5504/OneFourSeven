#include "bonjour_browser.h"

#import <Foundation/Foundation.h>
#include <Network/Network.h>
#include <arpa/inet.h>
#include <dns_sd.h>
#include <netinet/in.h>

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

// Ported from BiliFast's LogServerDiscovery.swift. devlink.py speaks plain HTTP, so each
// Bonjour result is resolved to an address and port by a short connection. Network work runs
// on a private serial queue; results cross to the Godot main thread through a locked queue.

namespace {

struct Event {
    uint64_t generation = 0;
    bool failed = false;
    BonjourBrowser::Found found;
    std::string error;
    bool local_network_denied = false;
};

struct Resolving {
    nw_connection_t connection = nil;
    uint64_t id = 0;
};

// Shared with every handler block, so it outlives the browser object until cancellation.
struct Shared {
    std::string service_type;
    dispatch_queue_t queue = nil;
    std::atomic<uint64_t> generation{0};

    std::mutex mutex;
    std::vector<Event> events;

    // Touched only on queue.
    nw_browser_t browser = nil;
    std::map<std::string, int> seen;  // a service shows once per interface
    std::map<std::string, Resolving> resolving;
    std::map<std::string, BonjourBrowser::Found> found;
    uint64_t next_connection_id = 0;
};

using SharedPtr = std::shared_ptr<Shared>;

bool is_policy_denied(nw_error_t error) {
    return error != nil && nw_error_get_error_domain(error) == nw_error_domain_dns &&
           nw_error_get_error_code(error) == kDNSServiceErr_PolicyDenied;
}

std::string describe(nw_error_t error, const char *what) {
    std::string text = what;
    if (error == nil) {
        return text;
    }
    NSError *ns = (__bridge_transfer NSError *)nw_error_copy_cf_error(error);
    text += ": ";
    text += ns.localizedDescription.UTF8String ? ns.localizedDescription.UTF8String : "unknown error";
    text += " (" + std::to_string(nw_error_get_error_code(error)) + ")";
    return text;
}

void push(const SharedPtr &s, Event event) {
    std::lock_guard<std::mutex> lock(s->mutex);
    s->events.push_back(std::move(event));
}

void push_failure(const SharedPtr &s, uint64_t generation, nw_error_t error, const char *what) {
    Event event;
    event.generation = generation;
    event.failed = true;
    event.error = describe(error, what);
    event.local_network_denied = is_policy_denied(error);
    push(s, std::move(event));
}

void finish(const SharedPtr &s, const std::string &name, uint64_t id) {
    auto it = s->resolving.find(name);
    if (it == s->resolving.end() || it->second.id != id) {
        return;
    }
    nw_connection_t connection = it->second.connection;
    s->resolving.erase(it);
    nw_connection_cancel(connection);
}

void connection_changed(const SharedPtr &s, uint64_t generation, const std::string &name, uint64_t id,
                        nw_connection_state_t state, nw_error_t error) {
    if (s->generation.load() != generation) {
        return;
    }
    auto it = s->resolving.find(name);
    if (it == s->resolving.end() || it->second.id != id) {
        return;
    }
    switch (state) {
        case nw_connection_state_ready: {
            nw_path_t path = nw_connection_copy_current_path(it->second.connection);
            nw_endpoint_t remote = path ? nw_path_copy_effective_remote_endpoint(path) : nil;
            if (remote != nil && nw_endpoint_get_type(remote) == nw_endpoint_type_address) {
                const struct sockaddr *address = nw_endpoint_get_address(remote);
                if (address != nullptr && address->sa_family == AF_INET) {
                    char text[INET_ADDRSTRLEN] = {};
                    const auto *v4 = reinterpret_cast<const struct sockaddr_in *>(address);
                    if (inet_ntop(AF_INET, &v4->sin_addr, text, sizeof(text)) != nullptr) {
                        BonjourBrowser::Found found{name, text, int(nw_endpoint_get_port(remote))};
                        s->found[name] = found;
                        Event event;
                        event.generation = generation;
                        event.found = found;
                        push(s, std::move(event));
                    }
                }
            }
            finish(s, name, id);
            break;
        }
        // Waiting means the network refused it (local network permission, nothing listening);
        // it is tried again when the browse results next change.
        case nw_connection_state_waiting:
        case nw_connection_state_failed:
            if (is_policy_denied(error)) {
                push_failure(s, generation, error, "resolving the PC failed");
            }
            finish(s, name, id);
            break;
        case nw_connection_state_cancelled:
            finish(s, name, id);
            break;
        default:
            break;
    }
}

void resolve(const SharedPtr &s, uint64_t generation, const std::string &name, nw_endpoint_t endpoint) {
    nw_parameters_t parameters =
        nw_parameters_create_secure_tcp(NW_PARAMETERS_DISABLE_PROTOCOL, NW_PARAMETERS_DEFAULT_CONFIGURATION);
    // devlink.py advertises IPv4 only, and a link-local IPv6 address would need a zone in the URL.
    nw_protocol_stack_t stack = nw_parameters_copy_default_protocol_stack(parameters);
    nw_protocol_options_t ip = nw_protocol_stack_copy_internet_protocol(stack);
    if (ip != nil) {
        nw_ip_options_set_version(ip, nw_ip_version_4);
    }
    nw_connection_t connection = nw_connection_create(endpoint, parameters);
    const uint64_t id = ++s->next_connection_id;
    s->resolving[name] = Resolving{connection, id};
    nw_connection_set_queue(connection, s->queue);
    // The handler looks the connection up by id instead of capturing it, so there is no cycle.
    nw_connection_set_state_changed_handler(connection, ^(nw_connection_state_t state, nw_error_t error) {
      connection_changed(s, generation, name, id, state, error);
    });
    nw_connection_start(connection);
}

void result_changed(const SharedPtr &s, uint64_t generation, nw_browse_result_t old_result,
                    nw_browse_result_t new_result) {
    if (s->generation.load() != generation) {
        return;
    }
    const nw_browse_result_change_t changes = nw_browse_result_get_changes(old_result, new_result);
    if (changes & nw_browse_result_change_result_removed) {
        nw_endpoint_t endpoint = nw_browse_result_copy_endpoint(old_result);
        const char *service = endpoint ? nw_endpoint_get_bonjour_service_name(endpoint) : nullptr;
        if (service != nullptr) {
            const std::string name = service;
            if (--s->seen[name] <= 0) {
                s->seen.erase(name);
                s->found.erase(name);
                auto it = s->resolving.find(name);
                if (it != s->resolving.end()) {
                    finish(s, name, it->second.id);
                }
            }
        }
    }
    if (changes & nw_browse_result_change_result_added) {
        nw_endpoint_t endpoint = nw_browse_result_copy_endpoint(new_result);
        const char *service = endpoint ? nw_endpoint_get_bonjour_service_name(endpoint) : nullptr;
        if (service != nullptr) {
            const std::string name = service;
            ++s->seen[name];
            if (s->found.count(name) == 0 && s->resolving.count(name) == 0) {
                resolve(s, generation, name, endpoint);
            }
        }
    }
}

void cancel_all(const SharedPtr &s) {
    if (s->browser != nil) {
        nw_browser_cancel(s->browser);
        s->browser = nil;
    }
    for (auto &entry : s->resolving) {
        nw_connection_cancel(entry.second.connection);
    }
    s->resolving.clear();
    s->seen.clear();
    s->found.clear();
}

void start_on_queue(const SharedPtr &s, uint64_t generation) {
    if (s->generation.load() != generation) {
        return;
    }
    cancel_all(s);
    nw_browse_descriptor_t descriptor =
        nw_browse_descriptor_create_bonjour_service(s->service_type.c_str(), nullptr);
    nw_parameters_t parameters =
        nw_parameters_create_secure_tcp(NW_PARAMETERS_DISABLE_PROTOCOL, NW_PARAMETERS_DEFAULT_CONFIGURATION);
    nw_browser_t browser = nw_browser_create(descriptor, parameters);
    nw_browser_set_queue(browser, s->queue);
    nw_browser_set_browse_results_changed_handler(
        browser, ^(nw_browse_result_t old_result, nw_browse_result_t new_result, bool) {
          result_changed(s, generation, old_result, new_result);
        });
    nw_browser_set_state_changed_handler(browser, ^(nw_browser_state_t state, nw_error_t error) {
      if (s->generation.load() != generation) {
          return;
      }
      if (state == nw_browser_state_failed) {
          push_failure(s, generation, error, "Bonjour browsing failed");
          cancel_all(s);
      } else if (state == nw_browser_state_waiting) {
          push_failure(s, generation, error, "Bonjour browsing is waiting");
      }
    });
    s->browser = browser;
    nw_browser_start(browser);
}

class NWBonjourBrowser : public BonjourBrowser {
public:
    explicit NWBonjourBrowser(const std::string &service_type) : shared_(std::make_shared<Shared>()) {
        shared_->service_type = service_type;
        shared_->queue = dispatch_queue_create("com.kaichuan.onefourseven.bonjour", DISPATCH_QUEUE_SERIAL);
    }

    ~NWBonjourBrowser() override { stop(); }

    bool supported() const override { return true; }

    void start(FoundCallback found, FailedCallback failed) override {
        found_ = std::move(found);
        failed_ = std::move(failed);
        const uint64_t generation = ++shared_->generation;
        SharedPtr s = shared_;
        dispatch_async(s->queue, ^{
          start_on_queue(s, generation);
        });
    }

    void stop() override {
        ++shared_->generation;
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            shared_->events.clear();
        }
        SharedPtr s = shared_;
        dispatch_async(s->queue, ^{
          cancel_all(s);
        });
    }

    void poll() override {
        std::vector<Event> events;
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            events.swap(shared_->events);
        }
        const uint64_t generation = shared_->generation.load();
        for (const Event &event : events) {
            if (event.generation != generation) {
                continue;
            }
            if (event.failed) {
                if (failed_) {
                    failed_(event.error, event.local_network_denied);
                }
            } else if (found_) {
                found_(event.found);
            }
        }
    }

private:
    SharedPtr shared_;
    FoundCallback found_;
    FailedCallback failed_;
};

}  // namespace

std::unique_ptr<BonjourBrowser> BonjourBrowser::create(const std::string &service_type) {
    return std::make_unique<NWBonjourBrowser>(service_type);
}
