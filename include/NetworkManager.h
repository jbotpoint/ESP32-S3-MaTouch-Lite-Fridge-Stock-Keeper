#pragma once

#include "AppTypes.h"
#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include <time.h>

#define MAX_WIFI_SCAN_RESULTS 6

class NetworkManager {
private:
    Preferences wifiPrefs;
    Preferences timePrefs;

    char savedSSID[33] = "";
    char savedPassword[65] = "";
    bool credentialsLoaded = false;

    WifiState wifiState = WIFI_STATE_IDLE;
    unsigned long wifiConnectStartMillis = 0;

    bool timeSyncedOnce = false;
    bool timeSyncEventPending = false;

    char wifiScanSSIDs[MAX_WIFI_SCAN_RESULTS][33];
    bool wifiScanIsOpen[MAX_WIFI_SCAN_RESULTS];
    int wifiScanCount = 0;

public:
    void loadWifiCredentials();
    void saveWifiCredentials(const char *ssid, const char *pass);
    void forgetWifiCredentials();

    void loadLastSyncEpoch();
    void saveLastSyncEpoch();

    void startWifiConnect();

    // Returns true when the WiFi UI should be refreshed because
    // the connection state changed.
    bool updateWifiState();

    // Returns true once when NTP has successfully produced a real
    // timestamp after a connection.
    bool consumeTimeSyncEvent();

    int scanNetworks();

    WifiState getWifiState() const { return wifiState; }
    bool hasSavedCredentials() const { return credentialsLoaded; }
    const char *getSavedSSID() const { return savedSSID; }
    const char *getSavedPassword() const { return savedPassword; }

    int getScanCount() const { return wifiScanCount; }
    const char *getScanSSID(int index) const;
    bool isScanNetworkOpen(int index) const;
};
