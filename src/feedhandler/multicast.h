#pragma once

#include <functional>
#include <string>
#include <cstdint>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

namespace feedhandler {

class MulticastReceiver {
public:
    using MessageCallback = std::function<void(const uint8_t*, size_t)>;
    
    MulticastReceiver(const std::string& group, uint16_t port, 
                      const std::string& interface = "0.0.0.0",
                      size_t buffer_size = 65536);
    ~MulticastReceiver();
    
    // Non-copyable
    MulticastReceiver(const MulticastReceiver&) = delete;
    MulticastReceiver& operator=(const MulticastReceiver&) = delete;
    
    bool start();
    void stop();
    
    // Blocking receive - returns false on error/stop
    bool receive(MessageCallback callback);
    
    // Poll with timeout (ms), returns bytes received or -1
    int poll(int timeout_ms);
    
    // Read data after poll
    ssize_t read(uint8_t* buffer, size_t max_size);
    
    int getFd() const { return socket_fd_; }
    bool isRunning() const { return running_; }
    
private:
    std::string group_;
    uint16_t port_;
    std::string interface_;
    size_t buffer_size_;
    int socket_fd_ = -1;
    bool running_ = false;
    std::vector<uint8_t> buffer_;
};

class MulticastSender {
public:
    MulticastSender(const std::string& group, uint16_t port,
                    const std::string& interface = "0.0.0.0",
                    int ttl = 1);
    ~MulticastSender();
    
    // Non-copyable
    MulticastSender(const MulticastSender&) = delete;
    MulticastSender& operator=(const MulticastSender&) = delete;
    
    bool start();
    void stop();
    
    // Send data
    bool send(const uint8_t* data, size_t length);
    bool send(const std::vector<uint8_t>& data);
    
    int getFd() const { return socket_fd_; }
    bool isRunning() const { return running_; }
    
private:
    std::string group_;
    uint16_t port_;
    std::string interface_;
    int ttl_;
    int socket_fd_ = -1;
    bool running_ = false;
    struct sockaddr_in dest_addr_;
};

} // namespace feedhandler
