
#include "error.hh"
#include "file-system.hh"
#include "globals.hh"
#include "logging.hh"
#include "styx.hh"

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
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30);

    std::string resData;
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resData);

    //printMsg(lvlNotice, "asking styx to do '%s'", postData);
    CURLcode curlRes = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (curlRes != CURLE_OK) {
        throw Error("request to styx failed: curl code %d", curlRes);
    }
    //printMsg(lvlNotice, "styx response '%s'", trim(resData));
    nlohmann::json res = nlohmann::json::parse(resData);
    if (res.at("Success") != true) {
        std::string error = res.at("Error");
        throw Error("request to styx failed: %s", error);
    }
}


void makeStyxMount(const std::string upstream, const std::string storePath, const std::string mountPoint)
{
    deletePath(mountPoint);
    createDirs(mountPoint);

    nlohmann::json req = {
        {"Upstream", upstream},
        {"StorePath", storePath},
        {"MountPoint", mountPoint},
    };
    styxRequest("/mount", req);
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
        nlohmann::json req = { };
        styxRequest("/gc", req);
    } catch (...) {
        // ignore
    }
}

}  // namespace nix
