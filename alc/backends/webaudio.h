#ifndef BACKENDS_WEBAUDIO_H
#define BACKENDS_WEBAUDIO_H

#include "base.h"

struct WebAudioBackendFactory final : public BackendFactory {
public:
    bool init() override;

    bool querySupport(BackendType type) override;

    std::string probe(BackendType type) override;

    BackendPtr createBackend(DeviceBase *device, BackendType type) override;

    static BackendFactory &getFactory();
};

#endif /* BACKENDS_WEBAUDIO_H */
