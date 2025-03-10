#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <bf_rt/bf_rt.hpp>
extern "C" {
#include <bf_switchd/bf_switchd.h>
}

// Use to unpack cookie/context in Learn filter callback
struct learnInterface {
  volatile bool hasNewData = false;
  std::vector<uint64_t> mLearnDataVec;
  std::shared_ptr<const bfrt::BfRtLearn> mLearn;
};

class ControlPlane {
public:
  ControlPlane(std::string programName);
  ~ControlPlane();

  std::shared_ptr<const bfrt::BfRtTable> getTable(std::string name);
  std::shared_ptr<const bfrt::BfRtLearn> getLearnFilter(std::string name);
  std::shared_ptr<bfrt::BfRtSession> getSession();
  bf_rt_target_t getDeviceTarget();

  void addEntry(std::shared_ptr<const bfrt::BfRtTable> table,
                std::vector<std::pair<std::string, std::uint64_t>> keys,
                std::vector<std::pair<std::string, std::uint64_t>> data,
                std::string action = std::string());
  std::unordered_map<std::string, std::uint64_t>
  getEntry(std::shared_ptr<const bfrt::BfRtTable> table,
           std::vector<std::pair<std::string, std::uint64_t>> keys,
           std::string action = std::string());

  learnInterface mLearnInterface;

private:
  bf_switchd_context_t *mSwitchContext;
  std::shared_ptr<bfrt::BfRtSession> mSession;
  std::shared_ptr<const bfrt::BfRtInfo> mInfo;
  bf_rt_target_t mDeviceTarget;
};
