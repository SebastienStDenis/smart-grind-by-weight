/**
 * WiFi and HTTP for the host.
 *
 * The Mac is already on a network, so association is instantaneous and always
 * succeeds; TrainDataClient's own connect/poll/retry state machine runs
 * unchanged on top. HTTP GETs go out over libcurl to the real gateway.
 */

#include <HTTPClient.h>
#include <WiFi.h>

#include <curl/curl.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <mutex>

SimWiFiClass WiFi;

namespace {

std::once_flag curl_init_flag;

void ensure_curl_initialised() {
    std::call_once(curl_init_flag, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

size_t append_body(void* contents, size_t size, size_t count, void* user_data) {
    auto* out = static_cast<std::string*>(user_data);
    out->append(static_cast<char*>(contents), size * count);
    return size * count;
}

/** First non-loopback IPv4 address on the host. */
String host_ipv4() {
    struct ifaddrs* interfaces = nullptr;
    if (getifaddrs(&interfaces) != 0) return String("0.0.0.0");

    String address("0.0.0.0");
    for (struct ifaddrs* it = interfaces; it != nullptr; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET) continue;
        if (it->ifa_flags & IFF_LOOPBACK) continue;

        char buffer[INET_ADDRSTRLEN] = {0};
        auto* sin = reinterpret_cast<struct sockaddr_in*>(it->ifa_addr);
        if (inet_ntop(AF_INET, &sin->sin_addr, buffer, sizeof(buffer))) {
            address = String(buffer);
            break;
        }
    }

    freeifaddrs(interfaces);
    return address;
}

}  // namespace

bool SimWiFiClass::mode(wifi_mode_t mode) {
    (void)mode;
    return true;
}

wl_status_t SimWiFiClass::begin(const char* ssid, const char* password) {
    (void)ssid;
    (void)password;

    connected_ = true;
    emit(ARDUINO_EVENT_WIFI_STA_CONNECTED);
    emit(ARDUINO_EVENT_WIFI_STA_GOT_IP);
    return WL_CONNECTED;
}

bool SimWiFiClass::disconnect(bool wifi_off, bool erase_ap) {
    (void)wifi_off;
    (void)erase_ap;

    if (connected_) {
        connected_ = false;
        emit(ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    }
    return true;
}

bool SimWiFiClass::isConnected() const {
    return connected_;
}

wl_status_t SimWiFiClass::status() const {
    return connected_ ? WL_CONNECTED : WL_DISCONNECTED;
}

IPAddress SimWiFiClass::localIP() const {
    return connected_ ? IPAddress(host_ipv4()) : IPAddress();
}

void SimWiFiClass::onEvent(EventCallback callback) {
    callback_ = callback;
}

void SimWiFiClass::emit(WiFiEvent_t event, uint8_t reason) {
    if (!callback_) return;

    WiFiEventInfo_t info = {};
    info.wifi_sta_disconnected.reason = reason;
    callback_(event, info);
}

bool HTTPClient::begin(const String& url) {
    ensure_curl_initialised();
    url_ = url.c_str();
    payload_.clear();
    return !url_.empty();
}

int HTTPClient::GET() {
    if (url_.empty()) return -1;

    CURL* handle = curl_easy_init();
    if (!handle) return -1;

    payload_.clear();
    curl_easy_setopt(handle, CURLOPT_URL, url_.c_str());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, append_body);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &payload_);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, (long)timeout_ms_);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, (long)connect_timeout_ms_);
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);

    CURLcode result = curl_easy_perform(handle);
    long status = -1;
    if (result == CURLE_OK) {
        curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    }

    curl_easy_cleanup(handle);
    return (int)status;
}

String HTTPClient::getString() {
    return String(payload_);
}

void HTTPClient::end() {
    payload_.clear();
    url_.clear();
}
