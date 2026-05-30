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
    explicit NdiFinder(uint32_t waitMs = 3000);
    ~NdiFinder();

    NdiFinder(const NdiFinder&) = delete;
    NdiFinder& operator=(const NdiFinder&) = delete;

    void setGroups(const std::string& groups);

    std::vector<NdiSourceInfo> refresh();
    const std::vector<NdiSourceInfo>& sources() const { return sources_; }

private:
    NDIlib_find_instance_t finder_ = nullptr;
    uint32_t waitMs_;
    std::string groups_;
    std::vector<NdiSourceInfo> sources_;
};
