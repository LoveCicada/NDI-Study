#include "NdiContext.h"

NdiContext::NdiContext() {
    if (!NDIlib_initialize()) {
        valid_ = false;
        return;
    }
    valid_ = true;
}

NdiContext::~NdiContext() {
    if (valid_) {
        NDIlib_destroy();
    }
}

const char* NdiContext::version() const {
    return valid_ ? NDIlib_version() : nullptr;
}
