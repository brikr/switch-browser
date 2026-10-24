#pragma once

// Minimal IPv4 resolver (A records over UDP), so the loader doesn't depend on glibc's NSS modules,
// which a static binary has to load at runtime (needing QEMU_LD_PREFIX under qemu).
// Order: numeric address, /etc/hosts, then the first IPv4 nameserver in /etc/resolv.conf
// (override with NXB_DNS=<ip>).

#include <netinet/in.h>

#include <string>
#include <vector>

namespace nxb {

// Returns the IPv4 addresses for `name` (empty if it doesn't resolve). Sets `*h_err` to a
// netdb h_errno value on failure.
std::vector<in_addr> resolve_ipv4(const std::string& name, int* h_err);

}  // namespace nxb
