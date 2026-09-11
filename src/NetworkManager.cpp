#include "NetworkManager.h"

#define GMT_OFFSET_SEC      0
#define DAYLIGHT_OFFSET_SEC 0
#define NTP_SERVER "pool.ntp.org"

static const unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

void NetworkManager::loadWifiCredentials()
{
    wifiPrefs.begin("wifi", true);

    String ssid = wifiPrefs.getString("ssid", "");
    String pass = wifiPrefs.getString("pass", "");

    wifiPrefs.end();

    if (ssid.length() > 0) {
        ssid.toCharArray(savedSSID, sizeof(savedSSID));
        pass.toCharArray(savedPassword, sizeof(savedPassword));
        credentialsLoaded = true;

        Serial.printf("Loaded saved WiFi network \"%s\".\n", savedSSID);
    } else {
        credentialsLoaded = false;
        Serial.println("No WiFi network configured yet.");
    }
}

void NetworkManager::saveWifiCredentials(const char *ssid, const char *pass)
{
    wifiPrefs.begin("wifi", false);
    wifiPrefs.putString("ssid", ssid);
    wifiPrefs.putString("pass", pass);
    wifiPrefs.end();

    strncpy(savedSSID, ssid, sizeof(savedSSID) - 1);
    savedSSID[sizeof(savedSSID) - 1] = '\0';

    strncpy(savedPassword, pass, sizeof(savedPassword) - 1);
    savedPassword[sizeof(savedPassword) - 1] = '\0';

    credentialsLoaded = true;
}

void NetworkManager::forgetWifiCredentials()
{
    wifiPrefs.begin("wifi", false);
    wifiPrefs.clear();
    wifiPrefs.end();

    credentialsLoaded = false;
    savedSSID[0] = '\0';
    savedPassword[0] = '\0';

    WiFi.disconnect(true);
    wifiState = WIFI_STATE_IDLE;
}

void NetworkManager::loadLastSyncEpoch()
{
    timePrefs.begin("time", true);
    uint64_t v = timePrefs.getULong64("epoch", 0);
    timePrefs.end();

    if (v > 0) {
        struct timeval tv;
        tv.tv_sec = (time_t)v;
        tv.tv_usec = 0;
        settimeofday(&tv, nullptr);

        Serial.println(
            "Seeded clock from last known synced time "
            "(approximate until NTP succeeds)."
        );
    }
}

void NetworkManager::saveLastSyncEpoch()
{
    timePrefs.begin("time", false);
    timePrefs.putULong64("epoch", (uint64_t)time(nullptr));
    timePrefs.end();
}

void NetworkManager::startWifiConnect()
{
    if (!credentialsLoaded) return;

    Serial.printf("Connecting to WiFi \"%s\"...\n", savedSSID);

    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID, savedPassword);

    wifiState = WIFI_STATE_CONNECTING;
    wifiConnectStartMillis = millis();
}

bool NetworkManager::updateWifiState()
{
    bool uiChanged = false;

    if (wifiState == WIFI_STATE_CONNECTING) {
        if (WiFi.status() == WL_CONNECTED) {
            wifiState = WIFI_STATE_CONNECTED;

            Serial.println("WiFi connected.");

            configTime(
                GMT_OFFSET_SEC,
                DAYLIGHT_OFFSET_SEC,
                NTP_SERVER
            );

            timeSyncedOnce = false;
            uiChanged = true;
        }
        else if (millis() - wifiConnectStartMillis > WIFI_CONNECT_TIMEOUT_MS) {
            wifiState = WIFI_STATE_FAILED;

            Serial.println("WiFi connection timed out.");

            uiChanged = true;
        }
    }
    else if (wifiState == WIFI_STATE_CONNECTED) {
        if (WiFi.status() != WL_CONNECTED) {
            wifiState = WIFI_STATE_FAILED;

            Serial.println("WiFi connection lost.");

            uiChanged = true;
        }
        else if (!timeSyncedOnce && time(nullptr) > 1700000000) {
            timeSyncedOnce = true;

            saveLastSyncEpoch();

            Serial.println("Time synced via NTP.");

            timeSyncEventPending = true;
        }
    }

    return uiChanged;
}

bool NetworkManager::consumeTimeSyncEvent()
{
    if (!timeSyncEventPending) return false;

    timeSyncEventPending = false;
    return true;
}

int NetworkManager::scanNetworks()
{
    wifiScanCount = 0;

    int n = WiFi.scanNetworks();

    if (n > 0) {
        int maxShow =
            (n < MAX_WIFI_SCAN_RESULTS)
                ? n
                : MAX_WIFI_SCAN_RESULTS;

        for (int i = 0; i < maxShow; i++) {
            String ssid = WiFi.SSID(i);

            ssid.toCharArray(
                wifiScanSSIDs[wifiScanCount],
                sizeof(wifiScanSSIDs[wifiScanCount])
            );

            wifiScanIsOpen[wifiScanCount] =
                (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);

            wifiScanCount++;
        }
    }

    WiFi.scanDelete();

    return n;
}

const char *NetworkManager::getScanSSID(int index) const
{
    if (index < 0 || index >= wifiScanCount) return "";
    return wifiScanSSIDs[index];
}

bool NetworkManager::isScanNetworkOpen(int index) const
{
    if (index < 0 || index >= wifiScanCount) return false;
    return wifiScanIsOpen[index];
}
