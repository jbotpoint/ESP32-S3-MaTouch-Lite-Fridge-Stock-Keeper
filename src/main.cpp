#include <Arduino.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include <time.h>
#include <ctype.h>
#include "NS2009.h"
#include "AppTypes.h"
#include "GroceryManager.h"
#include "NetworkManager.h"
#include "Meals.h"

// ============================================================
// MaTouch Lite ESP32-S3 3.95" ST7796 - FRIDGE GROCERY STOCK APP
// ============================================================

#define TOUCH_SDA_PIN 38
#define TOUCH_SCL_PIN 39

#define TOUCH_I2C_ADDRESS 0x48

#define TOUCH_OFFSET_X 8
#define TOUCH_OFFSET_Y 8

#define SCREEN_W 480
#define SCREEN_H 320

#define HEADER_H 40

#define BURGER_X 0
#define BURGER_Y 0
#define BURGER_W 40
#define BURGER_H 40

#define WIFI_ICON_X (SCREEN_W - 40)
#define WIFI_ICON_Y 0
#define WIFI_ICON_W 30
#define WIFI_ICON_H 30

#define MENU_W 220
#define MENU_BTN_H 55
#define MENU_BTN_GAP 12
#define MENU_BTN_X (10)
#define MENU_BTN_W (MENU_W - 20)

#ifndef CATEGORY_COUNT
#define CATEGORY_COUNT 8
#endif

TFT_eSPI tft = TFT_eSPI();
NS2009 touch(TOUCH_I2C_ADDRESS, false, false);

GroceryManager groceryManager;
NetworkManager networkManager;
MealsManager mealsManager;

unsigned long lastWifiRetryMillis = 0;
const unsigned long WIFI_RETRY_INTERVAL_MS = 5UL * 60UL * 1000UL;
char pendingSSID[33] = "";

// ============================================================
// APP STATE
// ============================================================

AppScreen currentScreen = SCREEN_DASHBOARD;
bool menuOpen = false;
bool wasTouched = false;

bool itemMenuPopupOpen = false;
int itemMenuX = 0;
int itemMenuY = 0;
int selectedItemIndex = -1;

char newItemName[NAME_MAX_LEN] = "";
uint8_t newItemCategory = 0;
int newItemDays = 7;
int newItemQty = 1;
int editingItemIndex = -1;

#define KB_BUFFER_MAX_LEN 65
char kbBuffer[KB_BUFFER_MAX_LEN];
int kbBufferLen = 0;
AppScreen kbReturnScreen = SCREEN_ADD_ITEM;
KeyboardTarget kbTarget = KB_TARGET_ITEM_NAME;
bool kbShiftActive = false;

unsigned long lastSpoilCheckMillis = 0;
const unsigned long SPOIL_CHECK_INTERVAL_MS = 60UL * 60UL * 1000UL;

// --- SCROLL STATE FOR MEAL SCREENS ---
int mealScrollOffset = 0;
int lastTouchY = 0;

// Meal Details uses the original scroll area.
// Add Meal has a different layout because Name + Category occupy the top.
#define MEAL_DETAILS_SCROLL_TOP    (HEADER_H + 85)
#define MEAL_DETAILS_SCROLL_BOTTOM (SCREEN_H - 70)

#define ADD_MEAL_SCROLL_TOP        205
#define ADD_MEAL_SCROLL_BOTTOM     255


// ============================================================
// BUTTON / HIT-TESTING HELPER
// ============================================================

bool hitTest(const Button &b, int x, int y)
{
    return (x >= b.x && x <= b.x + b.w && y >= b.y && y <= b.y + b.h);
}

const Button burgerButton = {BURGER_X, BURGER_Y, BURGER_W, BURGER_H, ""};
const Button wifiIconButton = {WIFI_ICON_X, WIFI_ICON_Y, WIFI_ICON_W, WIFI_ICON_H, ""};
const Button homeButton = {SCREEN_W - 85, 0, 30, HEADER_H, ""}; 

Button menuButtons[4] = {
    {MENU_BTN_X, 0, MENU_BTN_W, MENU_BTN_H, "Add Item"},
    {MENU_BTN_X, 0, MENU_BTN_W, MENU_BTN_H, "Edit Stock"},
    {MENU_BTN_X, 0, MENU_BTN_W, MENU_BTN_H, "Fetch Updates"},
    {MENU_BTN_X, 0, MENU_BTN_W, MENU_BTN_H, "Meals"},
};

const Button backButton = {20, SCREEN_H - 60, 120, 40, "Back"};

const Button nameFieldBtn     = {20,  65,  440, 35, ""};
const Button categoryFieldBtn = {20,  115, 440, 35, ""};

const Button daysMinusBtn     = {180, 160, 40,  35, "-"};
const Button daysPlusBtn      = {320, 160, 40,  35, "+"};

const Button qtyMinusBtn      = {180, 205, 40,  35, "-"};
const Button qtyPlusBtn       = {320, 205, 40,  35, "+"};

const Button saveItemBtn      = {30,  265, 150, 40, "Save Item"};
const Button cancelItemBtn    = {300, 265, 150, 40, "Cancel"};

Button dashboardItemButtons[MAX_ITEMS];
Button ctxAddBtn, ctxRefreshBtn, ctxDeleteBtn;

Button categoryButtons[CATEGORY_COUNT];

#define MAX_KB_KEYS 40
Button kbKeys[MAX_KB_KEYS];
char kbChars[MAX_KB_KEYS];
int kbKeyCount = 0;

Button wifiScanBtn   = {30, 0, 420, 45, "Scan for Networks"};
Button wifiRetryBtn  = {30, 0, 420, 45, "Retry Connection"};
Button wifiForgetBtn = {30, 0, 420, 45, "Forget This Network"};
bool wifiRetryBtnVisible = false;
bool wifiForgetBtnVisible = false;

Button wifiScanButtons[MAX_WIFI_SCAN_RESULTS];

int selectedMealIndex = -1;
MealCategory newMealCategory = MEAL_DINNER;
char newMealIngredients[MAX_INGREDIENTS][INGREDIENT_NAME_MAX_LEN];
int newMealIngCount = 0;

Button addMealNavBtn = {30, 50, 420, 40, "Create New Meal"};
const Button mealCategoryFieldBtn = {20, 120, 440, 30, ""};
const Button addIngredientBtn = {400, 160, 26, 26, "+"};
Button mealListButtons[MAX_MEALS];
Button mealAddBtn = {400, HEADER_H + 35, 26, 26, "+"};
const Button mealMinusBtn = {440, HEADER_H + 35, 26, 26, "-"};

// ============================================================
// FORWARD DECLARATIONS
// ============================================================

void drawDashboard();
void drawHeader(const char *title);
void drawBurgerIcon();
void drawWifiIcon();
void openMenu();
void closeMenu();
void drawMenuOverlay();
void drawPlaceholderScreen(const char *title);
void drawBackButtonUI();
void drawAddItemScreen();
void openKeyboardFor(const char *initialText, AppScreen returnScreen, KeyboardTarget target);
void drawKeyboardScreen();
void redrawKeyboardBuffer();
void openCategoryPicker();
void drawCategoryPicker();
void handleTouch(int x, int y);
uint16_t daysColor(int daysLeft, bool spoiled);
void refreshWifiStatusUI();
void drawWifiSettingsScreen();
void startWifiScan();
void drawWifiScanList(int n);
void drawMealsScreen();
void drawMealDetailsScreen();
void drawAddMealScreen();
void drawHomeButton();


// ============================================================
// STOCK / SPOIL COLOR CODING
// ============================================================

uint16_t daysColor(int daysLeft, bool spoiled)
{
    if (spoiled) return TFT_RED;
    if (daysLeft <= 2) return TFT_ORANGE;
    return TFT_GREEN;
}


// ============================================================
// DRAWING: HEADER
// ============================================================

void drawBurgerIcon()
{
    tft.fillRect(BURGER_X, BURGER_Y, BURGER_W, BURGER_H, TFT_DARKCYAN);
    int lineY = BURGER_Y + 9;
    for (int i = 0; i < 3; i++) {
        tft.fillRect(BURGER_X + 7, lineY, BURGER_W - 14, 4, TFT_WHITE);
        lineY += 11;
    }
}

void drawWifiIcon()
{
    tft.fillRect(WIFI_ICON_X, WIFI_ICON_Y, WIFI_ICON_W, WIFI_ICON_H, TFT_DARKCYAN);

    uint16_t col;
    switch (networkManager.getWifiState()) {
        case WIFI_STATE_CONNECTED:  col = TFT_GREEN;  break;
        case WIFI_STATE_CONNECTING: col = TFT_YELLOW; break;
        case WIFI_STATE_FAILED:     col = TFT_RED;    break;
        default:                    col = TFT_DARKGREY; break;
    }

    tft.fillRect(WIFI_ICON_X + 6,  WIFI_ICON_Y + 26, 6, 10, col);
    tft.fillRect(WIFI_ICON_X + 16, WIFI_ICON_Y + 18, 6, 18, col);
    tft.fillRect(WIFI_ICON_X + 26, WIFI_ICON_Y + 8,  6, 28, col);
}

void refreshWifiStatusUI()
{
    if (menuOpen) return;
    if (currentScreen == SCREEN_WIFI_SETTINGS) drawWifiSettingsScreen();
    else drawWifiIcon();
}

void drawHeader(const char *title)
{
    // Fill header background
    tft.fillRect(0, 0, SCREEN_W, HEADER_H, TFT_DARKCYAN);
    
    drawBurgerIcon();
    
    // Logic: Only draw the home button if we are NOT on the Dashboard screen.
    if (currentScreen != SCREEN_DASHBOARD) {
        drawHomeButton();
    }

    drawWifiIcon();

    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setTextSize(2);
    tft.setCursor(65, 16);
    tft.print(title);
}


// ============================================================
// DRAWING: DASHBOARD
// ============================================================

void drawDashboard()
{
    tft.fillScreen(TFT_BLACK);
    drawHeader("Fridge Stock");

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, HEADER_H + 10);
    tft.print("Item");
    tft.setCursor(280, HEADER_H + 10);
    tft.print("Category");
    tft.setCursor(410, HEADER_H + 10);
    tft.print("Days");

    tft.drawFastHLine(15, HEADER_H + 34, SCREEN_W - 30, TFT_DARKGREY);

    if (groceryManager.getItemCount() == 0) {
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.setCursor(20, HEADER_H + 60);
        tft.print("No items yet. Use the menu to add one.");
        return;
    }

    int rowY = HEADER_H + 44;
    int rowH = 28;

    for (int i = 0; i < groceryManager.getItemCount() && rowY + rowH < SCREEN_H; i++) {
        int daysLeft = groceryManager.getDaysUntil(groceryManager.getItems()[i].spoilDate);
        
        dashboardItemButtons[i] = {10, rowY - 2, SCREEN_W - 20, rowH + 4, ""};

        tft.fillCircle(28, rowY + (rowH / 2), 6, daysColor(daysLeft, groceryManager.getItems()[i].spoiled));

        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.setTextSize(2);
        
        char nameBuf[64];
        if (groceryManager.getItems()[i].qty > 1) {
            sprintf(nameBuf, "%s (x%d)", groceryManager.getItems()[i].name, groceryManager.getItems()[i].qty);
        } else {
            strcpy(nameBuf, groceryManager.getItems()[i].name);
        }
        tft.setCursor(45, rowY + 3);
        tft.print(nameBuf);

        tft.setCursor(280, rowY + 3);
        tft.print(categoryNames[groceryManager.getItems()[i].category]);

        tft.setCursor(410, rowY + 3);
        if (groceryManager.getItems()[i].spoiled) {
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.print("OLD");
        } else {
            tft.print(daysLeft);
        }

        rowY += rowH;
    }
    
    if (itemMenuPopupOpen) {
        int menuW = 160;
        int menuH = 135;
        int px = itemMenuX;
        int py = itemMenuY;
        
        if (px + menuW > SCREEN_W) px = SCREEN_W - menuW - 10;
        if (py + menuH > SCREEN_H) py = SCREEN_H - menuH - 10;
        
        tft.fillRoundRect(px, py, menuW, menuH, 8, TFT_DARKGREY);
        tft.drawRoundRect(px, py, menuW, menuH, 8, TFT_WHITE);
        
        ctxAddBtn = {px + 10, py + 10, 140, 33, "Edit Item"};
        ctxRefreshBtn = {px + 10, py + 50, 140, 33, "Refresh"};
        ctxDeleteBtn = {px + 10, py + 90, 140, 33, "Delete"};
        
        Button* btns[] = {&ctxAddBtn, &ctxRefreshBtn, &ctxDeleteBtn};
        for(int k=0; k<3; k++) {
            tft.fillRoundRect(btns[k]->x, btns[k]->y, btns[k]->w, btns[k]->h, 5, TFT_DARKCYAN);
            tft.drawRoundRect(btns[k]->x, btns[k]->y, btns[k]->w, btns[k]->h, 5, TFT_WHITE);
            tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
            tft.setTextSize(2);
            tft.setCursor(btns[k]->x + 10, btns[k]->y + 8);
            tft.print(btns[k]->label);
        }
    }
}


// ============================================================
// DRAWING: SLIDE-OUT MENU OVERLAY
// ============================================================

void drawMenuOverlay()
{
    tft.fillRect(0, HEADER_H, MENU_W, SCREEN_H - HEADER_H, TFT_DARKGREY);
    tft.drawFastVLine(MENU_W, HEADER_H, SCREEN_H - HEADER_H, TFT_WHITE);

    int btnY = HEADER_H + 20;
    for (int i = 0; i < 4; i++) {
        menuButtons[i].y = btnY;

        tft.fillRoundRect(menuButtons[i].x, menuButtons[i].y,
                           menuButtons[i].w, menuButtons[i].h, 8, TFT_DARKCYAN);
        tft.drawRoundRect(menuButtons[i].x, menuButtons[i].y,
                           menuButtons[i].w, menuButtons[i].h, 8, TFT_WHITE);

        tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
        tft.setTextSize(2);
        tft.setCursor(menuButtons[i].x + 12, menuButtons[i].y + 17);
        tft.print(menuButtons[i].label);

        btnY += MENU_BTN_H + MENU_BTN_GAP;
    }
}

void openMenu()
{
    menuOpen = true;
    itemMenuPopupOpen = false; 
    drawMenuOverlay();
}

void closeMenu()
{
    menuOpen = false;
    
    // Switch statement ensures that when the menu closes, 
    // the appropriate screen is redrawn based on the current context.
    switch (currentScreen) {
        case SCREEN_DASHBOARD: 
            drawDashboard(); // Redraws dashboard and clears item popups/menu overlays
            break;

        case SCREEN_MEALS: 
            drawMealsScreen(); // Redraws meals list when menu closes
            break;

        case SCREEN_ADD_ITEM: 
            // Redraws add item screen to clear any temporary state drawn while building it.
            drawAddItemScreen(); 
            break;

        case SCREEN_WIFI_SETTINGS:
        case SCREEN_WIFI_SCAN_LIST:
            // Redraws header/status bar after menu closure
            refreshWifiStatusUI();
            break;

        // Note: Depending on your full project structure, you may want to add handlers for 
        // other screens here (e.g., SCREEN_MEAL_DETAILS, etc.) if they require specific cleanup.
        
        default: 
            // Fallback to dashboard redraw for safety in case of unknown screen states
            drawDashboard(); 
            break;
    }
}


// ============================================================
// SHARED "Back" BUTTON WIDGET
// ============================================================

void drawBackButtonUI()
{
    tft.fillRoundRect(backButton.x, backButton.y, backButton.w, backButton.h, 8, TFT_DARKCYAN);
    tft.drawRoundRect(backButton.x, backButton.y, backButton.w, backButton.h, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setTextSize(2);
    tft.setCursor(backButton.x + 18, backButton.y + 10);
    tft.print("Back");
}


// ============================================================
// DRAWING: PLACEHOLDER SCREENS
// ============================================================

void drawPlaceholderScreen(const char *title)
{
    tft.fillScreen(TFT_BLACK);
    drawHeader(title);

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(30, 130);
    tft.print("Coming soon.");

    drawBackButtonUI();
}


// ============================================================
// DRAWING: ADD ITEM SCREEN
// ============================================================

void drawAddItemScreen()
{
    tft.fillScreen(TFT_BLACK);
    drawHeader(editingItemIndex >= 0 ? "Edit Item" : "Add Item");

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextSize(2);
    
    tft.setCursor(20, 50);
    tft.print("Name:");
    tft.fillRoundRect(nameFieldBtn.x, nameFieldBtn.y, nameFieldBtn.w, nameFieldBtn.h, 6, TFT_DARKCYAN);
    tft.drawRoundRect(nameFieldBtn.x, nameFieldBtn.y, nameFieldBtn.w, nameFieldBtn.h, 6, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setCursor(nameFieldBtn.x + 10, nameFieldBtn.y + 10);
    tft.print(strlen(newItemName) > 0 ? newItemName : "Tap to enter name");

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(20, 100);
    tft.print("Category:");
    tft.fillRoundRect(categoryFieldBtn.x, categoryFieldBtn.y, categoryFieldBtn.w, categoryFieldBtn.h, 6, TFT_DARKCYAN);
    tft.drawRoundRect(categoryFieldBtn.x, categoryFieldBtn.y, categoryFieldBtn.w, categoryFieldBtn.h, 6, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setCursor(categoryFieldBtn.x + 10, categoryFieldBtn.y + 10);
    tft.print(categoryNames[newItemCategory]);

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(20, 168);
    tft.print("Spoils in:");

    tft.fillRoundRect(daysMinusBtn.x, daysMinusBtn.y, daysMinusBtn.w, daysMinusBtn.h, 6, TFT_DARKCYAN);
    tft.drawRoundRect(daysMinusBtn.x, daysMinusBtn.y, daysMinusBtn.w, daysMinusBtn.h, 6, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setCursor(daysMinusBtn.x + 14, daysMinusBtn.y + 10);
    tft.print("-");

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(235, 168);
    tft.printf("%d d", newItemDays);

    tft.fillRoundRect(daysPlusBtn.x, daysPlusBtn.y, daysPlusBtn.w, daysPlusBtn.h, 6, TFT_DARKCYAN);
    tft.drawRoundRect(daysPlusBtn.x, daysPlusBtn.y, daysPlusBtn.w, daysPlusBtn.h, 6, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setCursor(daysPlusBtn.x + 14, daysPlusBtn.y + 10);
    tft.print("+");

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(20, 213);
    tft.print("Quantity:");

    tft.fillRoundRect(qtyMinusBtn.x, qtyMinusBtn.y, qtyMinusBtn.w, qtyMinusBtn.h, 6, TFT_DARKCYAN);
    tft.drawRoundRect(qtyMinusBtn.x, qtyMinusBtn.y, qtyMinusBtn.w, qtyMinusBtn.h, 6, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setCursor(qtyMinusBtn.x + 14, qtyMinusBtn.y + 10);
    tft.print("-");

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(235, 213);
    tft.printf("%d", newItemQty);

    tft.fillRoundRect(qtyPlusBtn.x, qtyPlusBtn.y, qtyPlusBtn.w, qtyPlusBtn.h, 6, TFT_DARKCYAN);
    tft.drawRoundRect(qtyPlusBtn.x, qtyPlusBtn.y, qtyPlusBtn.w, qtyPlusBtn.h, 6, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setCursor(qtyPlusBtn.x + 14, qtyPlusBtn.y + 10);
    tft.print("+");

    tft.fillRoundRect(saveItemBtn.x, saveItemBtn.y, saveItemBtn.w, saveItemBtn.h, 8, TFT_DARKGREEN);
    tft.drawRoundRect(saveItemBtn.x, saveItemBtn.y, saveItemBtn.w, saveItemBtn.h, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.setCursor(saveItemBtn.x + 20, saveItemBtn.y + 12);
    tft.print("Save Item");

    tft.fillRoundRect(cancelItemBtn.x, cancelItemBtn.y, cancelItemBtn.w, cancelItemBtn.h, 8, TFT_MAROON);
    tft.drawRoundRect(cancelItemBtn.x, cancelItemBtn.y, cancelItemBtn.w, cancelItemBtn.h, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_MAROON);
    tft.setCursor(cancelItemBtn.x + 35, cancelItemBtn.y + 12);
    tft.print("Cancel");
}


// ============================================================
// DRAWING: ON-SCREEN KEYBOARD
// ============================================================

void openKeyboardFor(const char *initialText, AppScreen returnScreen, KeyboardTarget target)
{
    strncpy(kbBuffer, initialText, KB_BUFFER_MAX_LEN - 1);
    kbBuffer[KB_BUFFER_MAX_LEN - 1] = '\0';
    kbBufferLen = strlen(kbBuffer);
    kbReturnScreen = returnScreen;
    kbTarget = target;
    kbShiftActive = false;
    currentScreen = SCREEN_KEYBOARD;
    drawKeyboardScreen();
}

void redrawKeyboardBuffer()
{
    tft.fillRect(15, 55, SCREEN_W - 30, 30, TFT_BLACK);
    tft.drawRect(15, 55, SCREEN_W - 30, 30, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 62);

    if (kbTarget == KB_TARGET_WIFI_PASSWORD) {
        char mask[KB_BUFFER_MAX_LEN];
        int n = kbBufferLen;
        for (int i = 0; i < n; i++) mask[i] = '*';
        mask[n] = '\0';
        tft.print(mask);
    } else {
        tft.print(kbBuffer);
    }
}

void drawKeyboardScreen()
{
    tft.fillScreen(TFT_BLACK);
    drawHeader(kbTarget == KB_TARGET_WIFI_PASSWORD ? "WiFi Password" : "Enter Name");
    redrawKeyboardBuffer();

    kbKeyCount = 0;

    const char *rowChars[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM"};
    int rowIndent[4] = {10, 10, 25, 80};
    int rowY[4] = {95, 134, 173, 212};
    int keyW = 42, keyH = 36, gap = 2;

    for (int r = 0; r < 4; r++) {
        int x = rowIndent[r];
        for (int c = 0; rowChars[r][c] != '\0'; c++) {
            char baseChar = rowChars[r][c];
            bool isLetter = (r > 0);

            kbKeys[kbKeyCount] = {x, rowY[r], keyW, keyH, ""};
            kbChars[kbKeyCount] = baseChar; 

            tft.fillRoundRect(x, rowY[r], keyW, keyH, 5, TFT_DARKCYAN);
            tft.drawRoundRect(x, rowY[r], keyW, keyH, 5, TFT_WHITE);
            tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
            tft.setTextSize(2);
            tft.setCursor(x + 13, rowY[r] + 8);
            tft.print((isLetter && !kbShiftActive) ? (char)tolower((unsigned char)baseChar) : baseChar);

            x += keyW + gap;
            kbKeyCount++;
        }
    }

    int bottomY = 254;

    kbKeys[kbKeyCount] = {10, bottomY, 70, 44, ""};
    kbChars[kbKeyCount] = '\x01';
    tft.fillRoundRect(10, bottomY, 70, 44, 6, kbShiftActive ? TFT_ORANGE : TFT_DARKCYAN);
    tft.drawRoundRect(10, bottomY, 70, 44, 6, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, kbShiftActive ? TFT_ORANGE : TFT_DARKCYAN);
    tft.setCursor(20, bottomY + 14);
    tft.print("SHIFT");
    kbKeyCount++;

    kbKeys[kbKeyCount] = {85, bottomY, 70, 44, ""};
    kbChars[kbKeyCount] = '\b';
    tft.fillRoundRect(85, bottomY, 70, 44, 6, TFT_MAROON);
    tft.drawRoundRect(85, bottomY, 70, 44, 6, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_MAROON);
    tft.setCursor(105, bottomY + 14);
    tft.print("<-");
    kbKeyCount++;

    kbKeys[kbKeyCount] = {160, bottomY, 220, 44, ""};
    kbChars[kbKeyCount] = ' ';
    tft.fillRoundRect(160, bottomY, 220, 44, 6, TFT_DARKCYAN);
    tft.drawRoundRect(160, bottomY, 220, 44, 6, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setCursor(250, bottomY + 14);
    tft.print("SPACE");
    kbKeyCount++;

    kbKeys[kbKeyCount] = {385, bottomY, 80, 44, ""};
    kbChars[kbKeyCount] = '\r';
    tft.fillRoundRect(385, bottomY, 80, 44, 6, TFT_DARKGREEN);
    tft.drawRoundRect(385, bottomY, 80, 44, 6, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.setCursor(400, bottomY + 14);
    tft.print("DONE");
    kbKeyCount++;
}


// ============================================================
// DRAWING: CATEGORY PICKER
// ============================================================

void openCategoryPicker()
{
    currentScreen = SCREEN_CATEGORY_PICKER;
    drawCategoryPicker();
}

void drawCategoryPicker()
{
    tft.fillScreen(TFT_BLACK);
    drawHeader("Select Category");

    int rowY = HEADER_H + 15;
    int rowH = 32;

    for (unsigned int i = 0; i < CATEGORY_COUNT; i++) {
        categoryButtons[i] = {20, rowY, 440, rowH - 4, categoryNames[i]};

        bool selected = (i == newItemCategory);
        uint16_t bg = selected ? TFT_DARKCYAN : TFT_BLACK;

        tft.fillRoundRect(categoryButtons[i].x, categoryButtons[i].y,
                           categoryButtons[i].w, categoryButtons[i].h, 5, bg);
        tft.drawRoundRect(categoryButtons[i].x, categoryButtons[i].y,
                           categoryButtons[i].w, categoryButtons[i].h, 5, TFT_WHITE);

        tft.setTextColor(TFT_WHITE, bg);
        tft.setTextSize(2);
        tft.setCursor(categoryButtons[i].x + 12, categoryButtons[i].y + 6);
        tft.print(categoryNames[i]);

        rowY += rowH;
    }
}


// ============================================================
// DRAWING: WIFI SETTINGS + SCAN LIST
// ============================================================

void drawWifiSettingsScreen()
{
    tft.fillScreen(TFT_BLACK);
    drawHeader("WiFi Settings");

    tft.setTextSize(2);
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(20, HEADER_H + 15);
    tft.print("Status:");

    tft.setCursor(120, HEADER_H + 15);
    switch (networkManager.getWifiState()) {
        case WIFI_STATE_CONNECTED:
            tft.setTextColor(TFT_GREEN, TFT_BLACK);
            tft.printf("Connected (%s)", networkManager.getSavedSSID());
            break;
        case WIFI_STATE_CONNECTING:
            tft.setTextColor(TFT_YELLOW, TFT_BLACK);
            tft.print("Connecting...");
            break;
        case WIFI_STATE_FAILED:
            tft.setTextColor(TFT_RED, TFT_BLACK);
            tft.print("Connection failed");
            break;
        default:
            tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
            tft.print(networkManager.hasSavedCredentials() ? "Not connected" : "No network set up");
            break;
    }

    int btnY = HEADER_H + 55;

    wifiScanBtn.y = btnY;
    tft.fillRoundRect(wifiScanBtn.x, wifiScanBtn.y, wifiScanBtn.w, wifiScanBtn.h, 8, TFT_DARKCYAN);
    tft.drawRoundRect(wifiScanBtn.x, wifiScanBtn.y, wifiScanBtn.w, wifiScanBtn.h, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setCursor(wifiScanBtn.x + 20, wifiScanBtn.y + 14);
    tft.print("Scan for Networks");
    btnY += 55;

    wifiRetryBtnVisible = networkManager.hasSavedCredentials();
    wifiForgetBtnVisible = networkManager.hasSavedCredentials();

    if (wifiRetryBtnVisible) {
        wifiRetryBtn.y = btnY;
        tft.fillRoundRect(wifiRetryBtn.x, wifiRetryBtn.y, wifiRetryBtn.w, wifiRetryBtn.h, 8, TFT_DARKCYAN);
        tft.drawRoundRect(wifiRetryBtn.x, wifiRetryBtn.y, wifiRetryBtn.w, wifiRetryBtn.h, 8, TFT_WHITE);
        tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
        tft.setCursor(wifiRetryBtn.x + 20, wifiRetryBtn.y + 14);
        tft.print("Retry Connection");
        btnY += 55;
    }

    if (wifiForgetBtnVisible) {
        wifiForgetBtn.y = btnY;
        tft.fillRoundRect(wifiForgetBtn.x, wifiForgetBtn.y, wifiForgetBtn.w, wifiForgetBtn.h, 8, TFT_MAROON);
        tft.drawRoundRect(wifiForgetBtn.x, wifiForgetBtn.y, wifiForgetBtn.w, wifiForgetBtn.h, 8, TFT_WHITE);
        tft.setTextColor(TFT_WHITE, TFT_MAROON);
        tft.setCursor(wifiForgetBtn.x + 20, wifiForgetBtn.y + 14);
        tft.print("Forget This Network");
    }

    drawBackButtonUI();
}

void startWifiScan()
{
    currentScreen = SCREEN_WIFI_SCAN_LIST;
    tft.fillScreen(TFT_BLACK);
    drawHeader("WiFi Networks");
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(30, HEADER_H + 20);
    tft.print("Scanning...");
    int n = networkManager.scanNetworks();
    drawWifiScanList(n);
}

void drawWifiScanList(int n)
{
    tft.fillScreen(TFT_BLACK);
    drawHeader("WiFi Networks");
    int rowY = HEADER_H + 15;
    int rowH = 32;
    if (n <= 0) {
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(30, rowY);
        tft.print("No networks found.");
    } else {
        int maxShow = (n < MAX_WIFI_SCAN_RESULTS) ? n : MAX_WIFI_SCAN_RESULTS;
        for (int i = 0; i < maxShow; i++) {
            wifiScanButtons[i] = {20, rowY, 440, rowH - 4, ""};
            tft.fillRoundRect(20, rowY, 440, rowH - 4, 5, TFT_DARKCYAN);
            tft.drawRoundRect(20, rowY, 440, rowH - 4, 5, TFT_WHITE);
            tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
            tft.setTextSize(2);
            tft.setCursor(32, rowY + 4);
            tft.print(networkManager.getScanSSID(i));
            if (!networkManager.isScanNetworkOpen(i)) {
                tft.setCursor(430, rowY + 4);
                tft.print("*");
            }
            rowY += rowH;
        }
    }
    drawBackButtonUI();
}


// ============================================================
// TOUCH ROUTING
// ============================================================

void handleTouch(int x, int y)
{
    // 1. Calculate drag delta BEFORE updating lastTouchY
    int deltaY = lastTouchY - y;
    if (!wasTouched) {
        deltaY = 0; // Reset delta on initial touch press
    }
    lastTouchY = y;

    // --- 2. GLOBAL HOME BUTTON CHECK ---
    if (hitTest(homeButton, x, y)) 
    {
        currentScreen = SCREEN_DASHBOARD;
        drawDashboard();
        return;
    }

    // --- 3. BURGER MENU TOGGLE CHECK ---
    if (hitTest(burgerButton, x, y)) 
    {
        itemMenuPopupOpen = false;
        if (menuOpen) closeMenu();
        else openMenu();
        return;
    }

    // --- 4. MENU OVERLAY HANDLING ---
    if (menuOpen) 
    {
        if (hitTest(menuButtons[0], x, y)) 
        {
            currentScreen = SCREEN_ADD_ITEM;
            menuOpen = false;
            newItemName[0] = '\0';
            newItemCategory = 0;
            newItemDays = 7;
            newItemQty = 1;
            editingItemIndex = -1;
            drawAddItemScreen();
            return;
        } else if (hitTest(menuButtons[1], x, y)) 
        {
            currentScreen = SCREEN_DASHBOARD;
            menuOpen = false;
            drawDashboard();
            return;
        } else if (hitTest(menuButtons[2], x, y)) 
        {
            currentScreen = SCREEN_FETCH_UPDATES;
            menuOpen = false;
            drawPlaceholderScreen("Fetch Updates");
            return;
        } else if (hitTest(menuButtons[3], x, y)) 
        {
            currentScreen = SCREEN_MEALS;
            menuOpen = false;
            drawMealsScreen();
            return;
        }
        closeMenu();
        return;
    }

    // --- 5. SCREEN-SPECIFIC TOUCH HANDLING ---
    switch (currentScreen) 
    {

        case SCREEN_DASHBOARD:
            if (wasTouched) return; // Ignore drag motions on dashboard
            if (itemMenuPopupOpen) 
            {
                if (hitTest(ctxAddBtn, x, y)) 
                {
                    strncpy(newItemName, groceryManager.getItems()[selectedItemIndex].name, NAME_MAX_LEN);
                    newItemCategory = groceryManager.getItems()[selectedItemIndex].category;
                    newItemDays = (groceryManager.getItems()[selectedItemIndex].spoilDate - groceryManager.getItems()[selectedItemIndex].dateAdded) / (24*3600);
                    if (newItemDays <= 0) newItemDays = 7;
                    newItemQty = groceryManager.getItems()[selectedItemIndex].qty;
                    editingItemIndex = selectedItemIndex;
                    itemMenuPopupOpen = false;
                    currentScreen = SCREEN_ADD_ITEM;
                    drawAddItemScreen();
                } else if (hitTest(ctxRefreshBtn, x, y)) 
                {
                    time_t now = time(nullptr);
                    time_t originalDuration = groceryManager.getItems()[selectedItemIndex].spoilDate - groceryManager.getItems()[selectedItemIndex].dateAdded;
                    groceryManager.getItems()[selectedItemIndex].dateAdded = now;
                    groceryManager.getItems()[selectedItemIndex].spoilDate = now + originalDuration;
                    groceryManager.getItems()[selectedItemIndex].spoiled = false;
                    groceryManager.saveItems();
                    itemMenuPopupOpen = false;
                    drawDashboard();
                } else if (hitTest(ctxDeleteBtn, x, y)) 
                {
                    groceryManager.deleteItem(selectedItemIndex);
                    itemMenuPopupOpen = false;
                    selectedItemIndex = -1;
                    drawDashboard();
                } else 
                {
                    itemMenuPopupOpen = false;
                    drawDashboard();
                }
            } else 
            {
                for (int i = 0; i < groceryManager.getItemCount(); i++) 
                {
                    if (hitTest(dashboardItemButtons[i], x, y)) 
                    {
                        selectedItemIndex = i;
                        itemMenuPopupOpen = true;
                        itemMenuX = x;
                        itemMenuY = y;
                        drawDashboard();
                        break;
                    }
                }
            }
            break;

        case SCREEN_ADD_ITEM:
            if (wasTouched) return;
            if (hitTest(nameFieldBtn, x, y))
            {
                openKeyboardFor(newItemName, SCREEN_ADD_ITEM, KB_TARGET_ITEM_NAME);
            } else if (hitTest(categoryFieldBtn, x, y))
            {
                openCategoryPicker();
            } else if (hitTest(daysMinusBtn, x, y))
            {
                if (newItemDays > 1) newItemDays--;
                drawAddItemScreen();
            } else if (hitTest(daysPlusBtn, x, y))
            {
                newItemDays++;
                drawAddItemScreen();
            } else if (hitTest(qtyMinusBtn, x, y))
            {
                if (newItemQty > 1) newItemQty--;
                drawAddItemScreen();
            } else if (hitTest(qtyPlusBtn, x, y))
            {
                newItemQty++;
                drawAddItemScreen();
            } else if (hitTest(saveItemBtn, x, y))
            {
                if (editingItemIndex >= 0)
                {
                    GroceryItem &it = groceryManager.getItems()[editingItemIndex];
                    strncpy(it.name, newItemName, NAME_MAX_LEN - 1);
                    it.name[NAME_MAX_LEN - 1] = '\0';
                    it.category = newItemCategory;
                    it.qty = newItemQty;
                    it.dateAdded = time(nullptr);
                    it.spoilDate = it.dateAdded + (time_t)newItemDays * 24 * 3600;
                    it.spoiled = false;
                    groceryManager.saveItems();
                } else
                {
                    groceryManager.addNewItem(newItemName, newItemCategory, newItemDays, newItemQty);
                }
                editingItemIndex = -1;
                currentScreen = SCREEN_DASHBOARD;
                drawDashboard();
            } else if (hitTest(cancelItemBtn, x, y))
            {
                editingItemIndex = -1;
                currentScreen = SCREEN_DASHBOARD;
                drawDashboard();
            } else if (hitTest(backButton, x, y))
            {
                editingItemIndex = -1;
                currentScreen = SCREEN_DASHBOARD;
                drawDashboard();
            }
            break;

        case SCREEN_EDIT_STOCK:
        case SCREEN_FETCH_UPDATES:
            if (wasTouched) return;
            if (hitTest(backButton, x, y)) 
            {
                currentScreen = SCREEN_DASHBOARD;
                drawDashboard();
            } else {
                if (menuOpen) closeMenu();
            }
            break;

        case SCREEN_MEALS:
            if (wasTouched) return;
            if (hitTest(addMealNavBtn, x, y))
            {
                newItemName[0] = '\0';
                newMealIngCount = 0;
                newMealCategory = MEAL_DINNER;
                mealScrollOffset = 0;
                currentScreen = SCREEN_ADD_MEAL;
                drawAddMealScreen();
                return;
            }

            if (hitTest(backButton, x, y))
            {
                currentScreen = SCREEN_DASHBOARD;
                drawDashboard();
                return;
            }

            for (int i = 0; i < mealsManager.getMealCount(); i++)
            {
                if (hitTest(mealListButtons[i], x, y))
                {
                    selectedMealIndex = i;
                    mealScrollOffset = 0;
                    currentScreen = SCREEN_MEAL_DETAILS;
                    drawMealDetailsScreen();
                    return;
                }
            }

            if (menuOpen) closeMenu();
            return;

        case SCREEN_ADD_MEAL:
        {
            const int scrollTop = ADD_MEAL_SCROLL_TOP;
            const int scrollBottom = ADD_MEAL_SCROLL_BOTTOM;

            if (y >= scrollTop && y <= scrollBottom) 
            {
                if (wasTouched && (deltaY > 2 || deltaY < -2)) 
                {
                    mealScrollOffset += deltaY;

                    const MealItem &meal = mealsManager.getMeals()[selectedMealIndex];
                    int contentHeight = newMealIngCount * 22;
                    int viewportHeight = scrollBottom - scrollTop;
                    int maxOff = contentHeight - viewportHeight;
                    if (maxOff < 0) maxOff = 0;

                    if (mealScrollOffset < 0) mealScrollOffset = 0;
                    if (mealScrollOffset > maxOff) mealScrollOffset = maxOff;

                    drawAddMealScreen();
                    return;
                }
            }

            if (wasTouched) return;

            if (hitTest(nameFieldBtn, x, y))
            {
                openKeyboardFor(newItemName, SCREEN_ADD_MEAL, KB_TARGET_MEAL_NAME);
                return;
            }

            if (hitTest(mealCategoryFieldBtn, x, y))
            {
                newMealCategory = (MealCategory)((newMealCategory + 1) % MEAL_CATEGORY_COUNT);
                drawAddMealScreen();
                return;
            }

            if (hitTest(addIngredientBtn, x, y))
            {
                if (newMealIngCount < MAX_INGREDIENTS)
                {
                    openKeyboardFor("", SCREEN_ADD_MEAL, KB_TARGET_INGREDIENT);
                }
                return;
            }

            if (hitTest(saveItemBtn, x, y))
            {
                mealsManager.addMeal(newItemName, newMealCategory, newMealIngredients, newMealIngCount);
                newItemName[0] = '\0';
                newMealIngCount = 0;
                newMealCategory = MEAL_DINNER;
                mealScrollOffset = 0;
                currentScreen = SCREEN_MEALS;
                drawMealsScreen();
                return;
            }

            if (hitTest(cancelItemBtn, x, y))
            {
                newItemName[0] = '\0';
                newMealIngCount = 0;
                newMealCategory = MEAL_DINNER;
                mealScrollOffset = 0;
                currentScreen = SCREEN_MEALS;
                drawMealsScreen();
                return;
            }
            return;
        }

        case SCREEN_MEAL_DETAILS:
        {
            int scrollTop = MEAL_DETAILS_SCROLL_TOP;
            int scrollBottom = MEAL_DETAILS_SCROLL_BOTTOM;

            if (y >= scrollTop && y <= scrollBottom) 
            {
                if (wasTouched && (deltaY > 2 || deltaY < -2)) 
                {
                    mealScrollOffset += deltaY;

                    const MealItem &meal = mealsManager.getMeals()[selectedMealIndex];
                    int contentHeight = meal.ingredientCount * 22;
                    int viewportHeight = scrollBottom - scrollTop;
                    int maxOff = contentHeight - viewportHeight;
                    if (maxOff < 0) maxOff = 0;

                    if (mealScrollOffset < 0) mealScrollOffset = 0;
                    if (mealScrollOffset > maxOff) mealScrollOffset = maxOff;

                    drawMealDetailsScreen();
                    return;
                }
            }

            if (wasTouched) return;

            if (hitTest(mealAddBtn, x, y)) 
            {
                const MealItem &m = mealsManager.getMeals()[selectedMealIndex];
                mealsManager.addToMealCount(selectedMealIndex, 1);
                drawMealDetailsScreen();
            } else if (hitTest(mealMinusBtn, x, y)) 
            {
                const MealItem &m = mealsManager.getMeals()[selectedMealIndex];
                mealsManager.addToMealCount(selectedMealIndex, -1);
                drawMealDetailsScreen();
            } else if (hitTest(backButton, x, y)) 
            {
                currentScreen = SCREEN_MEALS;
                drawMealsScreen();
            } else 
            {
                if (menuOpen && y >= scrollTop && y <= scrollBottom) closeMenu();
            }

            if (currentScreen == SCREEN_MEAL_DETAILS) drawMealDetailsScreen();
            break;
        }

        case SCREEN_KEYBOARD:
            if (wasTouched) return;
            for (int i = 0; i < kbKeyCount; i++) 
            {
                if (!hitTest(kbKeys[i], x, y)) continue;

                char raw = kbChars[i];

                if (raw == '\x01') 
                {
                    kbShiftActive = !kbShiftActive;
                    drawKeyboardScreen();
                } else if (raw == '\r') 
                {
                    if (kbTarget == KB_TARGET_ITEM_NAME) 
                    {
                        strncpy(newItemName, kbBuffer, NAME_MAX_LEN - 1);
                        newItemName[NAME_MAX_LEN - 1] = '\0';
                        currentScreen = kbReturnScreen;
                        drawAddItemScreen();
                    } else if (kbTarget == KB_TARGET_MEAL_NAME) 
                    {
                        strncpy(newItemName, kbBuffer, NAME_MAX_LEN - 1);
                        newItemName[NAME_MAX_LEN - 1] = '\0';
                        currentScreen = kbReturnScreen;
                        drawAddMealScreen();
                    } else if (kbTarget == KB_TARGET_INGREDIENT) 
                    {
                        if (strlen(kbBuffer) > 0 && newMealIngCount < MAX_INGREDIENTS) 
                        {
                            strncpy(newMealIngredients[newMealIngCount], kbBuffer, INGREDIENT_NAME_MAX_LEN - 1);
                            newMealIngredients[newMealIngCount][INGREDIENT_NAME_MAX_LEN - 1] = '\0';
                            newMealIngCount++;
                        }
                        currentScreen = kbReturnScreen;
                        drawAddMealScreen();
                    } else 
                    {
                        networkManager.saveWifiCredentials(pendingSSID, kbBuffer);
                        networkManager.startWifiConnect();
                        currentScreen = SCREEN_WIFI_SETTINGS;
                        drawWifiSettingsScreen();
                    }
                } else if (raw == '\b') 
                {
                    if (kbBufferLen > 0) 
                    {
                        kbBuffer[--kbBufferLen] = '\0';
                        redrawKeyboardBuffer();
                    }
                } else 
                {
                    char typed = raw;
                    if (isalpha((unsigned char)typed) && !kbShiftActive) 
                    {
                        typed = (char)tolower((unsigned char)typed);
                    }
                    if (kbBufferLen < KB_BUFFER_MAX_LEN - 1) 
                    {
                        kbBuffer[kbBufferLen++] = typed;
                        kbBuffer[kbBufferLen] = '\0';
                        redrawKeyboardBuffer();
                    }
                }
                break;
            }
            break;

        case SCREEN_CATEGORY_PICKER:
            if (wasTouched) return;
            for (unsigned int i = 0; i < CATEGORY_COUNT; i++) 
            {
                if (hitTest(categoryButtons[i], x, y)) 
                {
                    newItemCategory = i;
                    currentScreen = SCREEN_ADD_ITEM;
                    drawAddItemScreen();
                    break;
                }
            }
            break;

        case SCREEN_WIFI_SETTINGS:
            if (wasTouched) return;
            if (hitTest(wifiScanBtn, x, y)) 
            {
                startWifiScan();
            } else if (wifiRetryBtnVisible && hitTest(wifiRetryBtn, x, y)) 
            {
                networkManager.startWifiConnect();
                drawWifiSettingsScreen();
            } else if (wifiForgetBtnVisible && hitTest(wifiForgetBtn, x, y)) 
            {
                networkManager.forgetWifiCredentials();
                drawWifiSettingsScreen();
            } else if (hitTest(backButton, x, y)) 
            {
                currentScreen = SCREEN_DASHBOARD;
                drawDashboard();
            }
            break;

        case SCREEN_WIFI_SCAN_LIST:
            if (wasTouched) return;
            bool matched = false;
            for (int i = 0; i < networkManager.getScanCount(); i++) 
            {
                if (hitTest(wifiScanButtons[i], x, y)) 
                {
                    matched = true;
                    strncpy(pendingSSID, networkManager.getScanSSID(i), sizeof(pendingSSID) - 1);
                    pendingSSID[sizeof(pendingSSID) - 1] = '\0';

                    if (networkManager.isScanNetworkOpen(i)) 
                    {
                        networkManager.saveWifiCredentials(pendingSSID, "");
                        networkManager.startWifiConnect();
                        currentScreen = SCREEN_WIFI_SETTINGS;
                        drawWifiSettingsScreen();
                    } else 
                    {
                        openKeyboardFor("", SCREEN_WIFI_SETTINGS, KB_TARGET_WIFI_PASSWORD);
                    }
                    break;
                }
            }
            if (!matched && hitTest(backButton, x, y)) 
            {
                currentScreen = SCREEN_WIFI_SETTINGS;
                drawWifiSettingsScreen();
            }
            break;
    }
}


// ============================================================
// SETUP
// ============================================================

void setup()
{
    Serial.begin(115200);
    delay(1000);
    Serial.println();
    Serial.println("====================================");
    Serial.println("MaTouch Lite - Fridge Stock App");
    Serial.println("====================================");
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    Serial.println("Initializing LCD...");
    tft.init();
    tft.setRotation(1);
    Serial.println("LCD initialized.");
    Serial.println("Initializing NS2009...");
    Wire1.begin(TOUCH_SDA_PIN, TOUCH_SCL_PIN);
    Wire1.setClock(100000);
    touch.Calibrate(CALIBRATE_MIN_X, CALIBRATE_MAX_X, CALIBRATE_MIN_Y, CALIBRATE_MAX_Y);
    Serial.println("NS2009 initialized.");
    Serial.println("Mounting LittleFS...");
    groceryManager.begin();
    mealsManager.begin();
    networkManager.loadLastSyncEpoch();
    groceryManager.checkSpoilage();
    lastSpoilCheckMillis = millis();
    drawDashboard();
    networkManager.loadWifiCredentials();
    if (networkManager.hasSavedCredentials()) networkManager.startWifiConnect();
    else Serial.println("No WiFi configured.");
}


// ============================================================
// LOOP
// ============================================================

void loop()
{
    bool wifiUiChanged = networkManager.updateWifiState();
    if (wifiUiChanged) refreshWifiStatusUI();

    if (networkManager.consumeTimeSyncEvent()) {
        if (groceryManager.checkSpoilage() &&
            currentScreen == SCREEN_DASHBOARD && !menuOpen) {
            drawDashboard();
        }
    }

    touch.Scan();
    if (touch.Touched) {
        int rawX = touch.X;
        int rawY = touch.Y;
        int x = rawY;
        int y = 320 - rawX;
        x = constrain(x + TOUCH_OFFSET_X, 0, SCREEN_W - 1);
        y = constrain(y - TOUCH_OFFSET_Y, 0, SCREEN_H - 1);

        // Call handleTouch continuously during movement to process dragging
        handleTouch(x, y); 
        wasTouched = true;
    } else {
        wasTouched = false;
    }

    if (millis() - lastSpoilCheckMillis >= SPOIL_CHECK_INTERVAL_MS) {
        if (groceryManager.checkSpoilage() &&
            currentScreen == SCREEN_DASHBOARD && !menuOpen) {
            drawDashboard();
        }
        lastSpoilCheckMillis = millis();
    }

    if (networkManager.getWifiState() == WIFI_STATE_FAILED &&
        networkManager.hasSavedCredentials() &&
        millis() - lastWifiRetryMillis >= WIFI_RETRY_INTERVAL_MS) {
        networkManager.startWifiConnect();
        lastWifiRetryMillis = millis();
    }

    delay(20);
}

void drawMealsScreen()
{
    tft.fillScreen(TFT_BLACK);
    drawHeader("Meals");

    // Create New Meal button lives up top so it never collides
    // with the Back button at the bottom of the screen.
    tft.fillRoundRect(addMealNavBtn.x, addMealNavBtn.y, addMealNavBtn.w, addMealNavBtn.h, 8, TFT_DARKGREEN);
    tft.drawRoundRect(addMealNavBtn.x, addMealNavBtn.y, addMealNavBtn.w, addMealNavBtn.h, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.setTextSize(2);
    tft.setCursor(addMealNavBtn.x + 110, addMealNavBtn.y + 12);
    tft.print(addMealNavBtn.label);

    int rowY = addMealNavBtn.y + addMealNavBtn.h + 15;
    int rowH = 35;

    if (mealsManager.getMealCount() == 0) {
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.setCursor(20, rowY);
        tft.print("No meals yet.");
    }

    // Stop drawing rows well clear of the Back button
    for (int i = 0; i < mealsManager.getMealCount() && rowY + rowH < backButton.y - 10; i++) {
        mealListButtons[i] = {20, rowY, 440, rowH, ""};
        const MealItem &m = mealsManager.getMeals()[i];

        tft.fillRoundRect(20, rowY, 440, rowH, 5, TFT_DARKCYAN);
        tft.drawRoundRect(20, rowY, 440, rowH, 5, TFT_WHITE);

        // Green dot = completed, orange = not yet made
        tft.fillCircle(35, rowY + rowH / 2, 6, m.isCompleted ? TFT_GREEN : TFT_ORANGE);

        tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
        tft.setCursor(50, rowY + 9);
        tft.print(m.name);

        rowY += rowH + 5;
    }

    drawBackButtonUI();
}

void drawMealDetailsScreen()
{
    tft.fillScreen(TFT_BLACK);
    const MealItem &meal = mealsManager.getMeals()[selectedMealIndex];
    drawHeader(meal.name);

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, HEADER_H + 10);
    tft.printf("Category: %s", mealCategoryNames[meal.category]);

    tft.setCursor(20, HEADER_H + 35);
    tft.setTextColor(meal.isCompleted ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
    tft.print("Made: " + String(meal.timesMade));

    tft.fillRect(0, MEAL_DETAILS_SCROLL_TOP, SCREEN_W, MEAL_DETAILS_SCROLL_BOTTOM - MEAL_DETAILS_SCROLL_TOP, TFT_BLACK);

    // 2. Render ingredients strictly within bounds
    int yBase = MEAL_DETAILS_SCROLL_TOP - mealScrollOffset;

    for (int i = 0; i < meal.ingredientCount && i < MAX_INGREDIENTS; i++) {
        int yPos = yBase + (i * 22);

        // Skip any item whose top is above the scroll area or bottom exceeds the scroll area
        if (yPos < MEAL_DETAILS_SCROLL_TOP) continue;
        if (yPos + 20 > MEAL_DETAILS_SCROLL_BOTTOM) break;

        tft.fillRect(30, yPos + 2, SCREEN_W - 60, 18, TFT_DARKGREY);
        tft.drawRect(30, yPos, SCREEN_W - 60, 20, TFT_WHITE);
        
        char temp[INGREDIENT_NAME_MAX_LEN];
        int len = (strlen(meal.ingredients[i]) > INGREDIENT_NAME_MAX_LEN - 1) ? 
                  INGREDIENT_NAME_MAX_LEN - 1 : strlen(meal.ingredients[i]);
        strncpy(temp, meal.ingredients[i], len);
        temp[len] = '\0';
        
        tft.setTextColor(TFT_WHITE, TFT_DARKGREY);
        tft.setCursor(38, yPos + 3);
        tft.print(temp);
    }

    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(20, HEADER_H + 60);
    tft.print("Ingredients:");

    tft.fillRoundRect(mealAddBtn.x, mealAddBtn.y, mealAddBtn.w, mealAddBtn.h, 8, TFT_DARKCYAN);
    tft.drawRoundRect(mealAddBtn.x, mealAddBtn.y, mealAddBtn.w, mealAddBtn.h, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setCursor(mealAddBtn.x + 8, mealAddBtn.y + 8);
    tft.print(mealAddBtn.label);

    tft.fillRoundRect(mealMinusBtn.x, mealMinusBtn.y, mealMinusBtn.w, mealMinusBtn.h, 8, TFT_MAROON);
    tft.drawRoundRect(mealMinusBtn.x, mealMinusBtn.y, mealMinusBtn.w, mealMinusBtn.h, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_MAROON);
    tft.setCursor(mealMinusBtn.x + 8, mealMinusBtn.y + 8);
    tft.print(mealMinusBtn.label);

    drawBackButtonUI();
}

void drawAddMealScreen()
{
    tft.fillScreen(TFT_BLACK);
    drawHeader("New Meal");

    // ------------------------------------------------------------
    // Meal name
    // ------------------------------------------------------------
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 50);
    tft.print("Name:");

    tft.fillRoundRect(
        nameFieldBtn.x, nameFieldBtn.y,
        nameFieldBtn.w, nameFieldBtn.h,
        6, TFT_DARKCYAN
    );
    tft.drawRoundRect(
        nameFieldBtn.x, nameFieldBtn.y,
        nameFieldBtn.w, nameFieldBtn.h,
        6, TFT_WHITE
    );

    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setCursor(nameFieldBtn.x + 10, nameFieldBtn.y + 10);
    tft.print(strlen(newItemName) > 0 ? newItemName : "Tap to name meal");

    // ------------------------------------------------------------
    // Meal category
    // ------------------------------------------------------------
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(20, 105);
    tft.print("Category (tap to change):");

    tft.fillRoundRect(
        mealCategoryFieldBtn.x, mealCategoryFieldBtn.y,
        mealCategoryFieldBtn.w, mealCategoryFieldBtn.h,
        6, TFT_DARKCYAN
    );
    tft.drawRoundRect(
        mealCategoryFieldBtn.x, mealCategoryFieldBtn.y,
        mealCategoryFieldBtn.w, mealCategoryFieldBtn.h,
        6, TFT_WHITE
    );

    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setCursor(mealCategoryFieldBtn.x + 10, mealCategoryFieldBtn.y + 6);
    tft.print(mealCategoryNames[newMealCategory]);

    // ------------------------------------------------------------
    // Add ingredient button
    // ------------------------------------------------------------
    tft.fillRoundRect(
        addIngredientBtn.x, addIngredientBtn.y,
        addIngredientBtn.w, addIngredientBtn.h,
        8, TFT_DARKCYAN
    );
    tft.drawRoundRect(
        addIngredientBtn.x, addIngredientBtn.y,
        addIngredientBtn.w, addIngredientBtn.h,
        8, TFT_WHITE
    );
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setTextSize(2);
    tft.setCursor(addIngredientBtn.x + 8, addIngredientBtn.y + 8);
    tft.print("+");

    // ------------------------------------------------------------
    // Ingredients
    //
    // The old code started this list at SCROLL_AREA_TOP (105),
    // which is inside the Category field. The Add Meal list now
    // starts at 205, below the Add Ingredient button.
    // ------------------------------------------------------------
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(20, 165);
    tft.print("Ingredients:");

    const int listTop = ADD_MEAL_SCROLL_TOP;
    const int listBottom = ADD_MEAL_SCROLL_BOTTOM;
    const int rowH = 20;

    tft.setTextColor(TFT_WHITE, TFT_BLACK);

    for (int i = 0; i < newMealIngCount && i < MAX_INGREDIENTS; i++)
    {
        int yPos = listTop + (i * rowH) - mealScrollOffset;

        // Do not draw rows outside the ingredient viewport.
        if (yPos + rowH < listTop) continue;
        if (yPos > listBottom) break;

        tft.fillRect(30, yPos + 2, SCREEN_W - 60, 18, TFT_DARKGREY);
        tft.drawRect(30, yPos, SCREEN_W - 60, 20, TFT_WHITE);

        char temp[INGREDIENT_NAME_MAX_LEN];
        size_t len = strlen(newMealIngredients[i]);
        if (len >= INGREDIENT_NAME_MAX_LEN)
            len = INGREDIENT_NAME_MAX_LEN - 1;

        strncpy(temp, newMealIngredients[i], len);
        temp[len] = '\0';

        tft.setCursor(38, yPos + 3);
        tft.print(temp);
    }

    // ------------------------------------------------------------
    // Save / Cancel
    // ------------------------------------------------------------
    tft.fillRoundRect(
        saveItemBtn.x, saveItemBtn.y,
        saveItemBtn.w, saveItemBtn.h,
        8, TFT_DARKGREEN
    );
    tft.drawRoundRect(
        saveItemBtn.x, saveItemBtn.y,
        saveItemBtn.w, saveItemBtn.h,
        8, TFT_WHITE
    );
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.setCursor(saveItemBtn.x + 20, saveItemBtn.y + 12);
    tft.print("Save Meal");

    tft.fillRoundRect(
        cancelItemBtn.x, cancelItemBtn.y,
        cancelItemBtn.w, cancelItemBtn.h,
        8, TFT_MAROON
    );
    tft.drawRoundRect(
        cancelItemBtn.x, cancelItemBtn.y,
        cancelItemBtn.w, cancelItemBtn.h,
        8, TFT_WHITE
    );
    tft.setTextColor(TFT_WHITE, TFT_MAROON);
    tft.setCursor(cancelItemBtn.x + 35, cancelItemBtn.y + 12);
    tft.print("Cancel");
}

void drawHomeButton()
{
    // 1. Draw the background rectangle for the Home Button
    tft.fillRect(homeButton.x, homeButton.y, homeButton.w, homeButton.h, TFT_DARKCYAN);
    
    // 2. Calculate Center X
    // We assume a standard character width of roughly 9-10 pixels at font size 2.
    // Formula: (ButtonWidth - CharWidth) / 2 gives us the offset from the left edge to center the char visually.
    int charWidth = 10; 
    int centerX = homeButton.x + (homeButton.w - charWidth) / 2;
    
    // 3. Set cursor and draw 'H'
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setTextSize(3);
    int centerY = homeButton.y + (homeButton.h / 2) - 4; // Approx vertical center
    
    tft.setCursor(centerX, centerY);
    tft.print("H");
    tft.setTextSize(2);
}
