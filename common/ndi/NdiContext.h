#pragma once

#include <Processing.NDI.Lib.h>

class NdiContext {
public:
    NdiContext();
    ~NdiContext();

    NdiContext(const NdiContext&) = delete;
    NdiContext& operator=(const NdiContext&) = delete;

    bool isValid() const { return valid_; }
    const char* version() const;

private:
    bool valid_ = false;
};
