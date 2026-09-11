#pragma once
#include <TFT_eSPI.h>
#include "AppTypes.h"
#include "GroceryManager.h"
#include "NetworkManager.h"

class UIManager {
private:
    TFT_eSPI tft;
    GroceryManager* groceryMgr;
    NetworkManager* networkMgr;
    
    AppScreen currentScreen;
    bool menuOpen;
    
    // Keyboard and item state buffers
    char kbBuffer[65];
    char newItemName[NAME_MAX_LEN];

public:
    UIManager(GroceryManager* gm, NetworkManager* nm);
    void begin();
    
    void drawDashboard();
    void drawHeader(const char *title);
    void drawWifiIcon();
    void drawMenuOverlay();
    void drawAddItemScreen();
    void drawKeyboardScreen();
    void drawWifiSettingsScreen();
    
    void handleTouch(int x, int y);
    void refresh();
};