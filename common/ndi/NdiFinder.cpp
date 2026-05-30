#include "NdiFinder.h"

NdiFinder::NdiFinder(uint32_t waitMs)
    : waitMs_(waitMs) {
    NDIlib_find_create_t desc{};
    desc.show_local_sources = true;
    desc.p_groups = groups_.empty() ? nullptr : groups_.c_str();
    finder_ = NDIlib_find_create_v2(&desc);
}

NdiFinder::~NdiFinder() {
    if (finder_) {
        NDIlib_find_destroy(finder_);
    }
}

void NdiFinder::setGroups(const std::string& groups) {
    if (groups_ == groups) {
        return;
    }
    groups_ = groups;
    if (finder_) {
        NDIlib_find_destroy(finder_);
    }
    NDIlib_find_create_t desc{};
    desc.show_local_sources = true;
    desc.p_groups = groups_.empty() ? nullptr : groups_.c_str();
    finder_ = NDIlib_find_create_v2(&desc);
}

std::vector<NdiSourceInfo> NdiFinder::refresh() {
    sources_.clear();
    if (!finder_) {
        return sources_;
    }

    // wait_for_sources 仅在源数量变化时返回 true；超时仍应读取当前快照。
    NDIlib_find_wait_for_sources(finder_, waitMs_);

    uint32_t count = 0;
    const NDIlib_source_t* raw = NDIlib_find_get_current_sources(finder_, &count);
    sources_.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        NdiSourceInfo info;
        if (raw[i].p_ndi_name) {
            info.name = raw[i].p_ndi_name;
        }
        if (raw[i].p_url_address) {
            info.urlAddress = raw[i].p_url_address;
        }
        info.source = raw[i];
        info.source.p_ndi_name = info.name.empty() ? nullptr : info.name.c_str();
        info.source.p_url_address = info.urlAddress.empty() ? nullptr : info.urlAddress.c_str();
        sources_.push_back(std::move(info));
    }
    return sources_;
}
