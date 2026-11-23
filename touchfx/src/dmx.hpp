#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

enum DmxProtocol { DMXP_OFF, DMXP_SACN, DMXP_ARTNET, DMXP_COUNT };

// Receives one DMX universe over sACN (E1.31) or Art-Net on a background thread.
class DmxInput {
 public:
  ~DmxInput();
  // universe: sACN 1..63999, Art-Net port-address 0..32767.
  // adapterIp / adapterMask (host order): listen only on that network card; 0 = all network cards.
  void start(int protocol, int universe, uint32_t adapterIp = 0, uint32_t adapterMask = 0);
  void stop();
  int protocol() const { return protocol_; }
  int universe() const { return universe_; }
  const std::string& error() const { return error_; }

  // Copies the newest 512 channels. Returns false if nothing has been received yet.
  bool snapshot(uint8_t out[512]);
  double secondsSinceData() const;  // large if never received
  int packetsPerSecond() const { return pps_.load(); }
  std::string sourceInfo();         // "192.168.1.10 (grandMA3)"
  // Art-Net: the console that last polled TouchFX (it is listed as a node there). Empty if none yet.
  std::string polledBy();
  double secondsSincePoll() const;

 private:
  void threadMain();
  void handlePacket(const uint8_t* p, int n, uint32_t fromIp);

  int protocol_ = DMXP_OFF, universe_ = 0;
  uint32_t adapterIp_ = 0, adapterMask_ = 0;
  std::string error_;
  std::thread thread_;
  std::atomic<bool> quit_{false};
  std::mutex mtx_;
  uint8_t data_[512] = {};
  bool have_ = false;
  std::atomic<int64_t> lastMs_{-1};
  std::atomic<int> pps_{0};
  int count_ = 0;
  int64_t countStart_ = 0;
  std::string source_;
  int sacnPriority_ = -1;
  int64_t sacnPriorityMs_ = 0;
  intptr_t sock_ = -1;
  void replyArtPoll(uint32_t toIp);
  std::string pollerName_;
  uint32_t pollerIp_ = 0;
  std::atomic<int64_t> lastPollMs_{-1};
  struct NodeName {
    uint32_t ip;
    std::string name;
  };
  std::vector<NodeName> nodeNames_;  // names from other nodes' ArtPollReply (e.g. the console)
};
