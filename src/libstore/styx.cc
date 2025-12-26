
#include "nix/util/error.hh"
#include "nix/util/file-system.hh"
#include "nix/store/globals.hh"
#include "nix/util/logging.hh"
#include "nix/store/styx.hh"

#include <regex>
#include <map>
#include <sys/vfs.h>

#include <nlohmann/json.hpp>
#include <curl/curl.h>

namespace nix {

static size_t writeCallback(char *ptr, size_t size, size_t nmemb, void *userdata) {
    ((std::string*)userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

static void styxRequest(const std::string path, const nlohmann::json & req) {
    auto postData = req.dump();

    CURL *curl = curl_easy_init();
    if (!curl) {
        throw Error("curl init failed");
    }

    std::string url = "http://unix" + path;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_UNIX_SOCKET_PATH, settings.styxSockPath.get().c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postData.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, postData.size());
    // TODO: this doesn't seem to work for unix sockets?
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 1);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 300);

    std::string resData;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resData);

    CURLcode curlRes = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (curlRes != CURLE_OK) {
        throw Error("styx error: curl: %d", curlRes);
    }
    nlohmann::json res = nlohmann::json::parse(resData);
    if (!res.value("Success", false)) {
        throw Error(res.value("Error", "<missing>"));
    }
}

StyxMode canUseStyx(const std::string storeUri, int narSize, std::string name) {
    auto trim = [](std::string_view s) {
        return s.back() == '/' ? s.substr(0, s.size() - 1) : s;
    };
    // always use styx on fake cache, even if nar is too small
    // TODO: maybe easier to do this a different way so we don't need a
    // special case here
    if (trim(storeUri) != trim("http://localhost:7444")) {
        bool useStyx = false;
        for (auto & uri : settings.styxSubstituters.get())
            if (trim(uri) == trim(storeUri)) { useStyx = true; break; }
        if (!useStyx || narSize < settings.styxMinSize)
            return StyxDisable;
    }
    // TODO: compile these only once
    for (auto & exc : settings.styxExclude.get())
        if (std::regex_match(name, std::regex(exc)))
            return StyxDisable;
    for (auto & inc : settings.styxOndemand.get())
        if (std::regex_match(name, std::regex(inc)))
            return StyxMount;
    for (auto & inc : settings.styxMaterialize.get())
        if (std::regex_match(name, std::regex(inc)))
            return StyxMaterialize;
    return StyxDisable;
}

void makeStyxMount(const std::string upstream, const std::string storePath, const std::string mountPoint, int narSize)
{
    deletePath(mountPoint);
    createDirs(mountPoint);

    nlohmann::json req = {
        {"Upstream", upstream},
        {"StorePath", storePath},
        {"MountPoint", mountPoint},
        {"NarSize", narSize},
    };
    styxRequest("/mount", req);
}


void makeStyxMaterialize(const std::string upstream, const std::string storePath, const std::string dest, int narSize)
{
    nlohmann::json req = {
        {"Upstream", upstream},
        {"StorePath", storePath},
        {"DestPath", dest},
        {"NarSize", narSize},
    };
    styxRequest("/materialize", req);
}


bool isStyxMount(const std::string mountPoint)
{
    struct statfs st;
    if (statfs(mountPoint.c_str(), &st))
        throw SysError("getting filesystem info about '%s'", mountPoint);
    return st.f_type == 0xE0F5E1E2;
}


void deleteStyxMount(const std::string storePath)
{
    nlohmann::json req = {
        {"StorePath", storePath},
    };
    styxRequest("/umount", req);
    // note: this does not delete the mountpoint, but collectGarbage will do
    // that right after calling this
}

void tryStyxGC()
{
    try {
        std::map<int, bool> byState;
        byState[5] = true;
        nlohmann::json req = {
            {"GcByState", byState},
        };
        styxRequest("/gc", req);
    } catch (...) {
        // ignore
    }
}

}  // namespace nix
