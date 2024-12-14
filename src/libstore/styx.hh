#pragma once
///@file

#include <string>

namespace nix {

typedef enum {
    StyxDisable,
    StyxMount,
    StyxMaterialize
} StyxMode;

void makeStyxMount(const std::string upstream, const std::string storePath, const std::string mountPoint, int narSize);
void makeStyxMaterialize(const std::string upstream, const std::string storePath, const std::string dest, int narSize);
bool isStyxMount(const std::string path);
void deleteStyxMount(const std::string path);
void tryStyxGC();

}  // namespace nix
