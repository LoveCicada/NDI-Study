#pragma once

#include <Processing.NDI.Lib.h>

#include <string>
#include <vector>

struct NdiSourceInfo {
    std::string name;
    std::string urlAddress;
    NDIlib_source_t source{};
};

class NdiFinder {
public:
    explicit NdiFinder(uint32_t defaultWaitMs = 250);
    ~NdiFinder();

    NdiFinder(const NdiFinder&) = delete;
    NdiFinder& operator=(const NdiFinder&) = delete;

    void setGroups(const std::string& groups);

    // waitMs == 0 时使用构造时的 defaultWaitMs_
    std::vector<NdiSourceInfo> refresh(uint32_t waitMs = 0);
    const std::vector<NdiSourceInfo>& sources() const { return sources_; }

private:
    NDIlib_find_instance_t finder_ = nullptr;
    uint32_t defaultWaitMs_;
    std::string groups_;
    std::vector<NdiSourceInfo> sources_;
};
