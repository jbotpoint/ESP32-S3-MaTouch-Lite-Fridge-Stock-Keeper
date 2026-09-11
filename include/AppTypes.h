#pragma once

#include <Arduino.h>
#include <time.h>

#define SCREEN_W 480
#define SCREEN_H 320
#define NAME_MAX_LEN 24
#define MAX_ITEMS 100

enum WifiState {
    WIFI_STATE_IDLE,
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_FAILED
};

enum AppScreen {
    SCREEN_DASHBOARD,
    SCREEN_EDIT_STOCK,
    SCREEN_FETCH_UPDATES,
    SCREEN_MEALS,
    SCREEN_ADD_ITEM,
    SCREEN_KEYBOARD,
    SCREEN_CATEGORY_PICKER,
    SCREEN_WIFI_SETTINGS,
    SCREEN_WIFI_SCAN_LIST
};

enum KeyboardTarget {
    KB_TARGET_ITEM_NAME,
    KB_TARGET_WIFI_PASSWORD
};

struct GroceryItem {
    char name[NAME_MAX_LEN];
    time_t dateAdded;
    time_t spoilDate;
    uint8_t category;
    bool spoiled;
    int qty;
};

struct Button {
    int x, y, w, h;
    const char *label;
};

extern const char *const categoryNames[];
extern const size_t CATEGORY_COUNT;
