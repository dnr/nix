#pragma once
///@file

#include <string>

namespace nix {

void makeStyxMount(const std::string upstream, const std::string storePath, const std::string mountPoint, int narSize);
bool isStyxMount(const std::string path);
void deleteStyxMount(const std::string path);
void tryStyxGC();

}  // namespace nix
