#include "multicast.h"

#include <cstring>
#include <iostream>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>

namespace feedhandler {

// ============================================================================
// MulticastReceiver
// ============================================================================

MulticastReceiver::MulticastReceiver(const std::string& group, uint16_t port,
                                     const std::string& interface, size_t buffer_size)
    : group_(group), port_(port), interface_(interface), buffer_size_(buffer_size) {
    buffer_.resize(buffer_size_);
}

MulticastReceiver::~MulticastReceiver() {
    stop();
}

bool MulticastReceiver::start() {
    if (running_) return true;
    
    // Create UDP socket
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Allow multiple listeners on same port
    int reuse = 1;
    if (setsockopt(socket_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        std::cerr << "Failed to set SO_REUSEADDR: " << strerror(errno) << std::endl;
        close(socket_fd_);
        return false;
    }
    
    // Set receive buffer size
    int rcvbuf = static_cast<int>(buffer_size_);
    setsockopt(socket_fd_, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));
    
    // Bind to port
    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    if (bind(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Failed to bind: " << strerror(errno) << std::endl;
        close(socket_fd_);
        return false;
    }
    
    // Join multicast group
    struct ip_mreq mreq{};
    mreq.imr_multiaddr.s_addr = inet_addr(group_.c_str());
    mreq.imr_interface.s_addr = inet_addr(interface_.c_str());
    
    if (setsockopt(socket_fd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0) {
        std::cerr << "Failed to join multicast group: " << strerror(errno) << std::endl;
        close(socket_fd_);
        return false;
    }
    
    running_ = true;
    std::cout << "Multicast receiver started on " << group_ << ":" << port_ << std::endl;
    return true;
}

void MulticastReceiver::stop() {
    if (!running_) return;
    
    running_ = false;
    
    if (socket_fd_ >= 0) {
        // Leave multicast group
        struct ip_mreq mreq{};
        mreq.imr_multiaddr.s_addr = inet_addr(group_.c_str());
        mreq.imr_interface.s_addr = inet_addr(interface_.c_str());
        setsockopt(socket_fd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
        
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

bool MulticastReceiver::receive(MessageCallback callback) {
    if (!running_) return false;
    
    ssize_t len = recv(socket_fd_, buffer_.data(), buffer_.size(), 0);
    if (len < 0) {
        if (errno == EINTR) return true;
        std::cerr << "Receive error: " << strerror(errno) << std::endl;
        return false;
    }
    
    if (len > 0 && callback) {
        callback(buffer_.data(), static_cast<size_t>(len));
    }
    
    return true;
}

int MulticastReceiver::poll(int timeout_ms) {
    struct pollfd pfd{};
    pfd.fd = socket_fd_;
    pfd.events = POLLIN;
    
    int ret = ::poll(&pfd, 1, timeout_ms);
    if (ret > 0 && (pfd.revents & POLLIN)) {
        return 1;
    }
    return ret;
}

ssize_t MulticastReceiver::read(uint8_t* buffer, size_t max_size) {
    return recv(socket_fd_, buffer, max_size, 0);
}

// ============================================================================
// MulticastSender
// ============================================================================

MulticastSender::MulticastSender(const std::string& group, uint16_t port,
                                 const std::string& interface, int ttl)
    : group_(group), port_(port), interface_(interface), ttl_(ttl) {
}

MulticastSender::~MulticastSender() {
    stop();
}

bool MulticastSender::start() {
    if (running_) return true;
    
    // Create UDP socket
    socket_fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (socket_fd_ < 0) {
        std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Set TTL
    if (setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl_, sizeof(ttl_)) < 0) {
        std::cerr << "Failed to set TTL: " << strerror(errno) << std::endl;
        close(socket_fd_);
        return false;
    }
    
    // Set outgoing interface
    struct in_addr iface{};
    iface.s_addr = inet_addr(interface_.c_str());
    if (setsockopt(socket_fd_, IPPROTO_IP, IP_MULTICAST_IF, &iface, sizeof(iface)) < 0) {
        std::cerr << "Failed to set interface: " << strerror(errno) << std::endl;
        close(socket_fd_);
        return false;
    }
    
    // Set up destination address
    memset(&dest_addr_, 0, sizeof(dest_addr_));
    dest_addr_.sin_family = AF_INET;
    dest_addr_.sin_port = htons(port_);
    dest_addr_.sin_addr.s_addr = inet_addr(group_.c_str());
    
    running_ = true;
    std::cout << "Multicast sender started on " << group_ << ":" << port_ << std::endl;
    return true;
}

void MulticastSender::stop() {
    if (!running_) return;
    
    running_ = false;
    
    if (socket_fd_ >= 0) {
        close(socket_fd_);
        socket_fd_ = -1;
    }
}

bool MulticastSender::send(const uint8_t* data, size_t length) {
    if (!running_) return false;
    
    ssize_t sent = sendto(socket_fd_, data, length, 0,
                          reinterpret_cast<struct sockaddr*>(&dest_addr_),
                          sizeof(dest_addr_));
    
    if (sent < 0) {
        std::cerr << "Send error: " << strerror(errno) << std::endl;
        return false;
    }
    
    return static_cast<size_t>(sent) == length;
}

bool MulticastSender::send(const std::vector<uint8_t>& data) {
    return send(data.data(), data.size());
}

} // namespace feedhandler
