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
#include "ApiManager.h"
#include <LittleFS.h>

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
// Instantiate server passing existing managers
ApiServer apiServer(groceryManager, mealsManager);
bool apiServerStarted = false;

unsigned long lastWifiRetryMillis = 0;
const unsigned long WIFI_RETRY_INTERVAL_MS = 5UL * 60UL * 1000UL;
char pendingSSID[33] = "";

// ============================================================
// APP STATE
// ============================================================

// --- SCREEN REFRESH TIMER ---
unsigned long lastScreenRefreshMillis = 0;
const unsigned long SCREEN_REFRESH_INTERVAL_MS = 30000; // Refresh interval (e.g. every 10 seconds)

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

// --- PAGING STATE FOR THE DASHBOARD INVENTORY ---
// Six grocery items fit cleanly above the bottom navigation controls.
#define DASHBOARD_ITEMS_PER_PAGE 6
int dashboardPageIndex = 0;

// --- PAGING STATE FOR THE MEALS LIST (3 meals shown per page) ---
#define MEALS_PER_PAGE 3
int mealsPageIndex = 0;

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
    {MENU_BTN_X, 0, MENU_BTN_W, MENU_BTN_H, "Calendar"},
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
const Button mealDeleteBtn = {380, SCREEN_H - 60, 80, 40, "Del"};

// Pagination controls for the dashboard inventory - bottom-right,
// in-line with the same navigation area used by the Meals screen.
const Button dashboardPrevPageBtn = {330, SCREEN_H - 60, 40, 40, "<"};
const Button dashboardNextPageBtn = {420, SCREEN_H - 60, 40, 40, ">"};

// Pagination controls for the meals list - sit bottom-right, in-line with
// the Back button (same y/height), mirroring its left-side margin.
const Button mealsPrevPageBtn = {330, SCREEN_H - 60, 40, 40, "<"};
const Button mealsNextPageBtn = {420, SCREEN_H - 60, 40, 40, ">"};


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
void refreshCurrentScreen();


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
// CALENDAR: MEAL PLANNER
// ============================================================

#define CAL_MAX_ENTRIES   120
#define CAL_FILE_PATH     "/calendar.bin"

#define CAL_GRID_X        12
#define CAL_GRID_Y        92
#define CAL_CELL_W        65
#define CAL_CELL_H        36

#define CAL_DAY_LIST_TOP    115
#define CAL_DAY_LIST_BOTTOM 255
#define CAL_DAY_ROW_H       22

// One meal assigned to one calendar day. The meal is stored by NAME (not
// index) so deleting other meals never shifts assignments onto the wrong day.
struct CalendarEntry {
    uint16_t year;
    uint8_t  month;   // 0-11
    uint8_t  day;     // 1-31
    char     meal[MEAL_NAME_MAX_LEN];
};

CalendarEntry calEntries[CAL_MAX_ENTRIES];
int calEntryCount = 0;

int calViewYear  = 2026;
int calViewMonth = 0;      // month currently shown in the grid (0-11)

int calSelYear   = 2026;   // day currently open on the day screen
int calSelMonth  = 0;
int calSelDay    = 1;
int calPickedMeal = -1;    // index into mealsManager, -1 = no meal
bool calOrphan   = false;  // day had a meal that no longer exists

const char *calMonthNames[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};
const char *calDayLabels[7] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};

const Button calPrevBtn     = {10,  44, 50, 28, "<"};
const Button calNextBtn     = {420, 44, 50, 28, ">"};
const Button calMealPrevBtn = {20,  48, 40, 36, "<"};
const Button calMealNextBtn = {420, 48, 40, 36, ">"};

// --- date helpers (no timezone dependence) ---

bool calIsLeap(int y)
{
    return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

int calDaysInMonth(int y, int m0)
{
    static const int dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m0 == 1 && calIsLeap(y)) return 29;
    return dim[m0];
}

// 0 = Sunday ... 6 = Saturday (Sakamoto's algorithm). m0 is 0-11.
int calDayOfWeek(int y, int m0, int d)
{
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m0 < 2) y -= 1;
    return (y + y / 4 - y / 100 + y / 400 + t[m0] + d) % 7;
}

void calGetToday(int &y, int &m0, int &d)
{
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    y  = t.tm_year + 1900;
    m0 = t.tm_mon;
    d  = t.tm_mday;
}

// --- storage ---

void calSave()
{
    if (LittleFS.exists(CAL_FILE_PATH)) LittleFS.remove(CAL_FILE_PATH);

    fs::File f = LittleFS.open(CAL_FILE_PATH, "w");
    if (!f) {
        Serial.println("ERROR: could not open calendar file for writing.");
        return;
    }

    f.write((uint8_t *)&calEntryCount, sizeof(calEntryCount));
    f.write((uint8_t *)calEntries, sizeof(CalendarEntry) * calEntryCount);
    f.flush();
    f.close();
}

void calLoad()
{
    calEntryCount = 0;

    fs::File f = LittleFS.open(CAL_FILE_PATH, "r");
    if (!f) {
        Serial.println("No calendar file yet - starting empty.");
        return;
    }

    int count = 0;
    if (f.read((uint8_t *)&count, sizeof(count)) != sizeof(count) ||
        count < 0 || count > CAL_MAX_ENTRIES) {
        Serial.println("Calendar file looked corrupt - ignoring.");
        f.close();
        return;
    }

    size_t expected = sizeof(CalendarEntry) * count;
    size_t got = f.read((uint8_t *)calEntries, expected);
    f.close();

    if (got == expected) {
        calEntryCount = count;
        Serial.printf("Loaded %d calendar entr(ies) from flash.\n", calEntryCount);
    } else {
        Serial.println("Calendar file size mismatch - ignoring.");
    }
}

int calFind(int y, int m0, int d)
{
    for (int i = 0; i < calEntryCount; i++) {
        if (calEntries[i].year == y && calEntries[i].month == m0 && calEntries[i].day == d) {
            return i;
        }
    }
    return -1;
}

// Assign a meal to a day. A null/empty mealName clears the day.
void calAssign(int y, int m0, int d, const char *mealName)
{
    int idx = calFind(y, m0, d);
    bool clear = (mealName == nullptr || mealName[0] == '\0');

    if (clear) {
        if (idx < 0) return;
        for (int j = idx; j < calEntryCount - 1; j++) calEntries[j] = calEntries[j + 1];
        calEntryCount--;
        calSave();
        return;
    }

    if (idx < 0) {
        if (calEntryCount >= CAL_MAX_ENTRIES) {
            Serial.println("Calendar full - cannot assign more days.");
            return;
        }
        idx = calEntryCount++;
        calEntries[idx].year  = y;
        calEntries[idx].month = m0;
        calEntries[idx].day   = d;
    }

    strncpy(calEntries[idx].meal, mealName, MEAL_NAME_MAX_LEN - 1);
    calEntries[idx].meal[MEAL_NAME_MAX_LEN - 1] = '\0';
    calSave();
}

int calMealIndexByName(const char *name)
{
    for (int i = 0; i < mealsManager.getMealCount(); i++) {
        if (strcasecmp(mealsManager.getMeals()[i].name, name) == 0) return i;
    }
    return -1;
}

// An ingredient counts as "in stock" if a matching, unspoiled item with
// quantity > 0 exists (case-insensitive exact name match).
bool calIngredientInStock(const char *ingredient)
{
    for (int i = 0; i < groceryManager.getItemCount(); i++) {
        const auto &it = groceryManager.getItems()[i];
        if (strcasecmp(it.name, ingredient) == 0 && it.qty > 0 && !it.spoiled) {
            return true;
        }
    }
    return false;
}

void calOpenDay(int y, int m0, int d)
{
    calSelYear  = y;
    calSelMonth = m0;
    calSelDay   = d;
    calPickedMeal = -1;
    calOrphan = false;
    mealScrollOffset = 0;

    int e = calFind(y, m0, d);
    if (e >= 0) {
        calPickedMeal = calMealIndexByName(calEntries[e].meal);
        if (calPickedMeal < 0) calOrphan = true;
    }
}

// --- drawing ---

void calDrawArrowBtn(const Button &b, bool filled = true)
{
    tft.fillRoundRect(b.x, b.y, b.w, b.h, 6, TFT_DARKCYAN);
    tft.drawRoundRect(b.x, b.y, b.w, b.h, 6, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setTextSize(2);
    tft.setCursor(b.x + (b.w - 12) / 2, b.y + (b.h - 16) / 2);
    tft.print(b.label);
}

void drawCalendarScreen()
{
    tft.fillScreen(TFT_BLACK);
    drawHeader("Calendar");

    int todayY, todayM, todayD;
    calGetToday(todayY, todayM, todayD);

    // Month navigation row
    calDrawArrowBtn(calPrevBtn);
    calDrawArrowBtn(calNextBtn);

    char title[24];
    snprintf(title, sizeof(title), "%s %d", calMonthNames[calViewMonth], calViewYear);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor((SCREEN_W - (int)strlen(title) * 12) / 2, 51);
    tft.print(title);

    // Weekday labels
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    for (int c = 0; c < 7; c++) {
        tft.setCursor(CAL_GRID_X + c * CAL_CELL_W + 20, 75);
        tft.print(calDayLabels[c]);
    }

    // Day cells
    int firstDow = calDayOfWeek(calViewYear, calViewMonth, 1);
    int dim = calDaysInMonth(calViewYear, calViewMonth);

    for (int day = 1; day <= dim; day++) {
        int slot = firstDow + day - 1;
        int col = slot % 7;
        int row = slot / 7;

        int cx = CAL_GRID_X + col * CAL_CELL_W;
        int cy = CAL_GRID_Y + row * CAL_CELL_H;
        int cw = CAL_CELL_W - 2;
        int ch = CAL_CELL_H - 2;

        bool hasMeal = calFind(calViewYear, calViewMonth, day) >= 0;
        bool isToday = (calViewYear == todayY && calViewMonth == todayM && day == todayD);

        if (hasMeal) {
            // Green outline (2px) for days with a meal
            tft.drawRect(cx, cy, cw, ch, TFT_GREEN);
            tft.drawRect(cx + 1, cy + 1, cw - 2, ch - 2, TFT_GREEN);
        } else {
            tft.drawRect(cx, cy, cw, ch, TFT_DARKGREY);
        }

        tft.setTextColor(isToday ? TFT_YELLOW : TFT_WHITE, TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(cx + 7, cy + 9);
        tft.print(day);
    }
}

void drawCalendarDayScreen()
{
    tft.fillScreen(TFT_BLACK);

    char title[28];
    snprintf(title, sizeof(title), "%s %d, %d",
             calMonthNames[calSelMonth], calSelDay, calSelYear);
    drawHeader(title);

    int mealCount = mealsManager.getMealCount();
    if (calPickedMeal >= mealCount) calPickedMeal = -1;

    // Meal selector:  [<]  meal name  [>]
    calDrawArrowBtn(calMealPrevBtn);
    calDrawArrowBtn(calMealNextBtn);

    tft.fillRoundRect(65, 48, 350, 36, 6, TFT_DARKCYAN);
    tft.drawRoundRect(65, 48, 350, 36, 6, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKCYAN);
    tft.setTextSize(2);
    tft.setCursor(75, 58);

    char nameBuf[32];
    if (calPickedMeal >= 0) {
        snprintf(nameBuf, sizeof(nameBuf), "%.28s", mealsManager.getMeals()[calPickedMeal].name);
    } else {
        snprintf(nameBuf, sizeof(nameBuf), "%s", mealCount > 0 ? "No meal (tap arrows)" : "No meals created");
    }
    tft.print(nameBuf);

    // Ingredients
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.setCursor(20, 94);

    if (calPickedMeal >= 0) {
        tft.print("Ingredients (red = not in stock)");

        const MealItem &meal = mealsManager.getMeals()[calPickedMeal];

        for (int i = 0; i < meal.ingredientCount && i < MAX_INGREDIENTS; i++) {
            int yPos = CAL_DAY_LIST_TOP + (i * CAL_DAY_ROW_H) - mealScrollOffset;

            if (yPos < CAL_DAY_LIST_TOP) continue;
            if (yPos + 20 > CAL_DAY_LIST_BOTTOM) break;

            bool inStock = calIngredientInStock(meal.ingredients[i]);

            tft.fillRect(30, yPos + 2, SCREEN_W - 60, 18, TFT_DARKGREY);
            tft.drawRect(30, yPos, SCREEN_W - 60, 20, TFT_WHITE);

            char temp[INGREDIENT_NAME_MAX_LEN];
            strncpy(temp, meal.ingredients[i], INGREDIENT_NAME_MAX_LEN - 1);
            temp[INGREDIENT_NAME_MAX_LEN - 1] = '\0';

            tft.setTextColor(inStock ? TFT_WHITE : TFT_RED, TFT_DARKGREY);
            tft.setCursor(38, yPos + 3);
            tft.print(temp);
        }

        if (meal.ingredientCount == 0) {
            tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
            tft.setCursor(30, CAL_DAY_LIST_TOP + 4);
            tft.print("This meal has no ingredients.");
        }
    } else {
        tft.setTextColor(calOrphan ? TFT_ORANGE : TFT_DARKGREY, TFT_BLACK);
        tft.print(calOrphan ? "Assigned meal was deleted." : "Choose a meal with the arrows.");
    }

    // Save / Cancel (same positions as the other edit screens)
    tft.fillRoundRect(saveItemBtn.x, saveItemBtn.y, saveItemBtn.w, saveItemBtn.h, 8, TFT_DARKGREEN);
    tft.drawRoundRect(saveItemBtn.x, saveItemBtn.y, saveItemBtn.w, saveItemBtn.h, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    tft.setTextSize(2);
    tft.setCursor(saveItemBtn.x + 45, saveItemBtn.y + 12);
    tft.print("Save");

    tft.fillRoundRect(cancelItemBtn.x, cancelItemBtn.y, cancelItemBtn.w, cancelItemBtn.h, 8, TFT_MAROON);
    tft.drawRoundRect(cancelItemBtn.x, cancelItemBtn.y, cancelItemBtn.w, cancelItemBtn.h, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_MAROON);
    tft.setCursor(cancelItemBtn.x + 35, cancelItemBtn.y + 12);
    tft.print("Cancel");
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

    int itemCount = groceryManager.getItemCount();
    int totalPages = (itemCount + DASHBOARD_ITEMS_PER_PAGE - 1) /
                     DASHBOARD_ITEMS_PER_PAGE;
    if (totalPages < 1) totalPages = 1;

    // Clamp the page if items were deleted or otherwise removed.
    if (dashboardPageIndex >= totalPages) dashboardPageIndex = totalPages - 1;
    if (dashboardPageIndex < 0) dashboardPageIndex = 0;

    int startIndex = dashboardPageIndex * DASHBOARD_ITEMS_PER_PAGE;
    int endIndex = startIndex + DASHBOARD_ITEMS_PER_PAGE;
    if (endIndex > itemCount) endIndex = itemCount;

    if (itemCount == 0) {
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.setCursor(20, HEADER_H + 60);
        tft.print("No items yet. Use the menu to add one.");
    } else {
        int rowY = HEADER_H + 44;
        int rowH = 28;

        // Only draw the items belonging to the current page.
        for (int i = startIndex;
             i < endIndex && rowY + rowH < dashboardPrevPageBtn.y - 10;
             i++) {

            int row = i - startIndex;
            dashboardItemButtons[row] =
                {10, rowY - 2, SCREEN_W - 20, rowH + 4, ""};

            int daysLeft =
                groceryManager.getDaysUntil(groceryManager.getItems()[i].spoilDate);

            tft.fillCircle(
                28, rowY + (rowH / 2), 6,
                daysColor(daysLeft, groceryManager.getItems()[i].spoiled)
            );

            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.setTextSize(2);

            char nameBuf[64];
            if (groceryManager.getItems()[i].qty > 1) {
                snprintf(
                    nameBuf, sizeof(nameBuf), "%s (x%d)",
                    groceryManager.getItems()[i].name,
                    groceryManager.getItems()[i].qty
                );
            } else {
                strncpy(
                    nameBuf,
                    groceryManager.getItems()[i].name,
                    sizeof(nameBuf) - 1
                );
                nameBuf[sizeof(nameBuf) - 1] = '\0';
            }

            char displayBuf[25];
            int nameLen = strlen(nameBuf);

            if (nameLen > 24) {
                // Add trailing spaces so the end and start don't collide.
                char paddedName[80];
                snprintf(
                    paddedName, sizeof(paddedName), "%s    ", nameBuf
                );
                int paddedLen = strlen(paddedName);

                // Advance one character every 7000 ms. Scroll. Character wrap around.
                int offset = (millis() / 7000) % paddedLen;

                for (int c = 0; c < 24; c++) {
                    displayBuf[c] = paddedName[(offset + c) % paddedLen];
                }
                displayBuf[24] = '\0';
            } else {
                strncpy(displayBuf, nameBuf, sizeof(displayBuf));
                displayBuf[24] = '\0';
            }

            tft.setCursor(45, rowY + 3);
            tft.print(displayBuf);

            tft.setCursor(280, rowY + 3);
            tft.setTextColor(TFT_WHITE, TFT_BLACK);
            tft.print(categoryNames[groceryManager.getItems()[i].category]);

            tft.setCursor(410, rowY + 3);
            if (groceryManager.getItems()[i].spoiled) {
                tft.setTextColor(TFT_RED, TFT_BLACK);
            } else {
                tft.setTextColor(TFT_WHITE, TFT_BLACK);
            }
            tft.print(daysLeft);

            rowY += rowH;
        }
    }

    // --- Pagination: "< pg# >" bottom-right, matching the Meals screen ---
    bool hasPrev = dashboardPageIndex > 0;
    bool hasNext = dashboardPageIndex < totalPages - 1;

    tft.fillRoundRect(
        dashboardPrevPageBtn.x, dashboardPrevPageBtn.y,
        dashboardPrevPageBtn.w, dashboardPrevPageBtn.h, 8,
        hasPrev ? TFT_DARKCYAN : TFT_DARKGREY
    );
    tft.drawRoundRect(
        dashboardPrevPageBtn.x, dashboardPrevPageBtn.y,
        dashboardPrevPageBtn.w, dashboardPrevPageBtn.h, 8, TFT_WHITE
    );
    tft.setTextColor(
        hasPrev ? TFT_WHITE : TFT_LIGHTGREY,
        hasPrev ? TFT_DARKCYAN : TFT_DARKGREY
    );
    tft.setTextSize(2);
    tft.setCursor(
        dashboardPrevPageBtn.x + 14,
        dashboardPrevPageBtn.y + 12
    );
    tft.print(dashboardPrevPageBtn.label);

    char pageLabel[16];
    snprintf(
        pageLabel, sizeof(pageLabel),
        "%d/%d", dashboardPageIndex + 1, totalPages
    );

    int pageLabelX =
        dashboardPrevPageBtn.x + dashboardPrevPageBtn.w +
        ((dashboardNextPageBtn.x -
          (dashboardPrevPageBtn.x + dashboardPrevPageBtn.w)) / 2) -
        (strlen(pageLabel) * 6);

    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(pageLabelX, dashboardPrevPageBtn.y + 12);
    tft.print(pageLabel);

    tft.fillRoundRect(
        dashboardNextPageBtn.x, dashboardNextPageBtn.y,
        dashboardNextPageBtn.w, dashboardNextPageBtn.h, 8,
        hasNext ? TFT_DARKCYAN : TFT_DARKGREY
    );
    tft.drawRoundRect(
        dashboardNextPageBtn.x, dashboardNextPageBtn.y,
        dashboardNextPageBtn.w, dashboardNextPageBtn.h, 8, TFT_WHITE
    );
    tft.setTextColor(
        hasNext ? TFT_WHITE : TFT_LIGHTGREY,
        hasNext ? TFT_DARKCYAN : TFT_DARKGREY
    );
    tft.setCursor(
        dashboardNextPageBtn.x + 14,
        dashboardNextPageBtn.y + 12
    );
    tft.print(dashboardNextPageBtn.label);

    // Keep the existing Back UI call for consistency. On the Dashboard it
    // is not normally visible/used, but this does not change its behavior.
    // drawBackButtonUI();

    // Existing item context popup is still drawn on top of the current page.
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
        for (int k = 0; k < 3; k++) {
            tft.fillRoundRect(
                btns[k]->x, btns[k]->y,
                btns[k]->w, btns[k]->h, 5, TFT_DARKCYAN
            );
            tft.drawRoundRect(
                btns[k]->x, btns[k]->y,
                btns[k]->w, btns[k]->h, 5, TFT_WHITE
            );
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

        case SCREEN_CALENDAR:
            drawCalendarScreen();
            break;

        case SCREEN_CALENDAR_DAY:
            drawCalendarDayScreen();
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

    if (hitTest(wifiIconButton, x, y)) 
    {
        itemMenuPopupOpen = false;
        if (menuOpen) closeMenu();
        currentScreen = SCREEN_WIFI_SETTINGS;
        drawWifiSettingsScreen();
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
            int todayY, todayM, todayD;
            calGetToday(todayY, todayM, todayD);
            calViewYear = todayY;
            calViewMonth = todayM;
            currentScreen = SCREEN_CALENDAR;
            menuOpen = false;
            drawCalendarScreen();
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

            // Pagination controls must be checked before item rows.
            if (hitTest(dashboardPrevPageBtn, x, y)) {
                if (dashboardPageIndex > 0) {
                    dashboardPageIndex--;
                    itemMenuPopupOpen = false;
                    selectedItemIndex = -1;
                    drawDashboard();
                }
                return;
            }

            if (hitTest(dashboardNextPageBtn, x, y)) {
                int itemCount = groceryManager.getItemCount();
                int totalPages =
                    (itemCount + DASHBOARD_ITEMS_PER_PAGE - 1) /
                    DASHBOARD_ITEMS_PER_PAGE;
                if (totalPages < 1) totalPages = 1;

                if (dashboardPageIndex < totalPages - 1) {
                    dashboardPageIndex++;
                    itemMenuPopupOpen = false;
                    selectedItemIndex = -1;
                    drawDashboard();
                }
                return;
            }

            if (itemMenuPopupOpen)
            {
                if (hitTest(ctxAddBtn, x, y))
                {
                    strncpy(
                        newItemName,
                        groceryManager.getItems()[selectedItemIndex].name,
                        NAME_MAX_LEN
                    );
                    newItemCategory =
                        groceryManager.getItems()[selectedItemIndex].category;
                    newItemDays =
                        (groceryManager.getItems()[selectedItemIndex].spoilDate -
                         groceryManager.getItems()[selectedItemIndex].dateAdded) /
                        (24 * 3600);
                    if (newItemDays <= 0) newItemDays = 7;
                    newItemQty =
                        groceryManager.getItems()[selectedItemIndex].qty;
                    editingItemIndex = selectedItemIndex;
                    itemMenuPopupOpen = false;
                    currentScreen = SCREEN_ADD_ITEM;
                    drawAddItemScreen();
                }
                else if (hitTest(ctxRefreshBtn, x, y))
                {
                    time_t now = time(nullptr);
                    time_t originalDuration =
                        groceryManager.getItems()[selectedItemIndex].spoilDate -
                        groceryManager.getItems()[selectedItemIndex].dateAdded;
                    groceryManager.getItems()[selectedItemIndex].dateAdded = now;
                    groceryManager.getItems()[selectedItemIndex].spoilDate =
                        now + originalDuration;
                    groceryManager.getItems()[selectedItemIndex].spoiled = false;
                    groceryManager.saveItems();
                    itemMenuPopupOpen = false;
                    drawDashboard();
                }
                else if (hitTest(ctxDeleteBtn, x, y))
                {
                    groceryManager.deleteItem(selectedItemIndex);
                    itemMenuPopupOpen = false;
                    selectedItemIndex = -1;
                    drawDashboard();
                }
                else
                {
                    itemMenuPopupOpen = false;
                    drawDashboard();
                }
            }
            else
            {
                int startIndex =
                    dashboardPageIndex * DASHBOARD_ITEMS_PER_PAGE;
                int endIndex =
                    startIndex + DASHBOARD_ITEMS_PER_PAGE;
                if (endIndex > groceryManager.getItemCount()) {
                    endIndex = groceryManager.getItemCount();
                }

                for (int i = startIndex; i < endIndex; i++)
                {
                    int row = i - startIndex;
                    if (hitTest(dashboardItemButtons[row], x, y))
                    {
                        selectedItemIndex = i; // Keep absolute inventory index.
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

        case SCREEN_CALENDAR:
        {
            if (wasTouched) return;

            if (hitTest(calPrevBtn, x, y))
            {
                if (--calViewMonth < 0) { calViewMonth = 11; calViewYear--; }
                drawCalendarScreen();
                return;
            }

            if (hitTest(calNextBtn, x, y))
            {
                if (++calViewMonth > 11) { calViewMonth = 0; calViewYear++; }
                drawCalendarScreen();
                return;
            }

            int col = (x - CAL_GRID_X) / CAL_CELL_W;
            int row = (y - CAL_GRID_Y) / CAL_CELL_H;

            if (x >= CAL_GRID_X && y >= CAL_GRID_Y && col >= 0 && col < 7 && row >= 0 && row < 6)
            {
                int day = row * 7 + col - calDayOfWeek(calViewYear, calViewMonth, 1) + 1;

                if (day >= 1 && day <= calDaysInMonth(calViewYear, calViewMonth))
                {
                    calOpenDay(calViewYear, calViewMonth, day);
                    currentScreen = SCREEN_CALENDAR_DAY;
                    drawCalendarDayScreen();
                }
            }
            return;
        }

        case SCREEN_CALENDAR_DAY:
        {
            // Drag-scroll the ingredient list
            if (y >= CAL_DAY_LIST_TOP && y <= CAL_DAY_LIST_BOTTOM &&
                wasTouched && (deltaY > 2 || deltaY < -2) && calPickedMeal >= 0)
            {
                mealScrollOffset += deltaY;

                const MealItem &meal = mealsManager.getMeals()[calPickedMeal];
                int contentHeight = meal.ingredientCount * CAL_DAY_ROW_H;
                int maxOff = contentHeight - (CAL_DAY_LIST_BOTTOM - CAL_DAY_LIST_TOP);
                if (maxOff < 0) maxOff = 0;

                if (mealScrollOffset < 0) mealScrollOffset = 0;
                if (mealScrollOffset > maxOff) mealScrollOffset = maxOff;

                drawCalendarDayScreen();
                return;
            }

            if (wasTouched) return;

            int mealCount = mealsManager.getMealCount();

            if (hitTest(calMealPrevBtn, x, y))
            {
                if (mealCount > 0) {
                    if (--calPickedMeal < -1) calPickedMeal = mealCount - 1;
                    mealScrollOffset = 0;
                    drawCalendarDayScreen();
                }
                return;
            }

            if (hitTest(calMealNextBtn, x, y))
            {
                if (mealCount > 0) {
                    if (++calPickedMeal >= mealCount) calPickedMeal = -1;
                    mealScrollOffset = 0;
                    drawCalendarDayScreen();
                }
                return;
            }

            if (hitTest(saveItemBtn, x, y))
            {
                // "No meal" selected clears the day
                const char *name = (calPickedMeal >= 0)
                    ? mealsManager.getMeals()[calPickedMeal].name : "";
                calAssign(calSelYear, calSelMonth, calSelDay, name);

                mealScrollOffset = 0;
                currentScreen = SCREEN_CALENDAR;
                drawCalendarScreen();
                return;
            }

            if (hitTest(cancelItemBtn, x, y))
            {
                mealScrollOffset = 0;
                currentScreen = SCREEN_CALENDAR;
                drawCalendarScreen();
                return;
            }
            return;
        }

        case SCREEN_EDIT_STOCK:
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

            if (hitTest(mealsPrevPageBtn, x, y))
            {
                if (mealsPageIndex > 0) {
                    mealsPageIndex--;
                    drawMealsScreen();
                }
                return;
            }

            if (hitTest(mealsNextPageBtn, x, y))
            {
                int mealCount = mealsManager.getMealCount();
                int totalPages = (mealCount + MEALS_PER_PAGE - 1) / MEALS_PER_PAGE;
                if (totalPages < 1) totalPages = 1;
                if (mealsPageIndex < totalPages - 1) {
                    mealsPageIndex++;
                    drawMealsScreen();
                }
                return;
            }

            {
                int startIndex = mealsPageIndex * MEALS_PER_PAGE;
                int endIndex = startIndex + MEALS_PER_PAGE;
                if (endIndex > mealsManager.getMealCount()) endIndex = mealsManager.getMealCount();

                for (int i = startIndex; i < endIndex; i++)
                {
                    int row = i - startIndex;
                    if (hitTest(mealListButtons[row], x, y))
                    {
                        selectedMealIndex = i;
                        mealScrollOffset = 0;
                        currentScreen = SCREEN_MEAL_DETAILS;
                        drawMealDetailsScreen();
                        return;
                    }
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
            } else if (hitTest(mealDeleteBtn, x, y)) 
            {
                const MealItem &m = mealsManager.getMeals()[selectedMealIndex];
                mealsManager.deleteMeal(selectedMealIndex);
                currentScreen = SCREEN_MEALS;
                drawMealsScreen();

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

#ifdef CLEAR_FLASH_ON_BOOT
    Serial.println("!!! CLEAR_FLASH_ON_BOOT defined: Formatting LittleFS... !!!");
    
    // Mount LittleFS first; if it fails, pass true to auto-format
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS mount failed, auto-formatted!");
    } else {
        Serial.println("LittleFS mounted. Formatting now...");
        LittleFS.format();
        Serial.println("Format complete!");
    }

    Serial.println("!!! Flash cleared. Remove build flag and reflash. !!!");
    while (1) { delay(1000); } // Halt execution
#endif

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
    calLoad();
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

    // Start API server once connected to WiFi
    if (networkManager.getWifiState() == WIFI_STATE_CONNECTED) {
        if (!apiServerStarted) {
            apiServer.begin();
            apiServerStarted = true;
        }
        apiServer.handle();
    } else {
        apiServerStarted = false;
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

    // --- PERIODIC SCREEN REFRESH ---
    if (millis() - lastScreenRefreshMillis >= SCREEN_REFRESH_INTERVAL_MS) {
        refreshCurrentScreen();
        lastScreenRefreshMillis = millis();
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

    int mealCount = mealsManager.getMealCount();
    int totalPages = (mealCount + MEALS_PER_PAGE - 1) / MEALS_PER_PAGE;
    if (totalPages < 1) totalPages = 1;

    // Clamp in case the count shrank (e.g. a meal was deleted) since the
    // page was last set.
    if (mealsPageIndex >= totalPages) mealsPageIndex = totalPages - 1;
    if (mealsPageIndex < 0) mealsPageIndex = 0;

    int startIndex = mealsPageIndex * MEALS_PER_PAGE;

    if (mealCount == 0) {
        tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
        tft.setCursor(20, rowY);
        tft.print("No meals yet.");
    }

    // Only draw this page's slice (up to MEALS_PER_PAGE meals), still
    // staying clear of the Back button.
    int endIndex = startIndex + MEALS_PER_PAGE;
    if (endIndex > mealCount) endIndex = mealCount;

    for (int i = startIndex; i < endIndex && rowY + rowH < backButton.y - 10; i++) {
        int row = i - startIndex;
        mealListButtons[row] = {20, rowY, 440, rowH, ""};
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

    // --- Pagination: "< pg# >" bottom-right, in-line with the Back button ---
    bool hasPrev = mealsPageIndex > 0;
    bool hasNext = mealsPageIndex < totalPages - 1;

    tft.fillRoundRect(mealsPrevPageBtn.x, mealsPrevPageBtn.y, mealsPrevPageBtn.w, mealsPrevPageBtn.h, 8,
                       hasPrev ? TFT_DARKCYAN : TFT_DARKGREY);
    tft.drawRoundRect(mealsPrevPageBtn.x, mealsPrevPageBtn.y, mealsPrevPageBtn.w, mealsPrevPageBtn.h, 8, TFT_WHITE);
    tft.setTextColor(hasPrev ? TFT_WHITE : TFT_LIGHTGREY, hasPrev ? TFT_DARKCYAN : TFT_DARKGREY);
    tft.setTextSize(2);
    tft.setCursor(mealsPrevPageBtn.x + 14, mealsPrevPageBtn.y + 12);
    tft.print(mealsPrevPageBtn.label);

    char pageLabel[16];
    snprintf(pageLabel, sizeof(pageLabel), "%d/%d", mealsPageIndex + 1, totalPages);
    int pageLabelX = mealsPrevPageBtn.x + mealsPrevPageBtn.w +
        ((mealsNextPageBtn.x - (mealsPrevPageBtn.x + mealsPrevPageBtn.w)) / 2) - (strlen(pageLabel) * 6);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setCursor(pageLabelX, mealsPrevPageBtn.y + 12);
    tft.print(pageLabel);

    tft.fillRoundRect(mealsNextPageBtn.x, mealsNextPageBtn.y, mealsNextPageBtn.w, mealsNextPageBtn.h, 8,
                       hasNext ? TFT_DARKCYAN : TFT_DARKGREY);
    tft.drawRoundRect(mealsNextPageBtn.x, mealsNextPageBtn.y, mealsNextPageBtn.w, mealsNextPageBtn.h, 8, TFT_WHITE);
    tft.setTextColor(hasNext ? TFT_WHITE : TFT_LIGHTGREY, hasNext ? TFT_DARKCYAN : TFT_DARKGREY);
    tft.setCursor(mealsNextPageBtn.x + 14, mealsNextPageBtn.y + 12);
    tft.print(mealsNextPageBtn.label);

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

    tft.fillRoundRect(mealDeleteBtn.x, mealDeleteBtn.y, mealDeleteBtn.w, mealDeleteBtn.h, 8, TFT_MAROON);
    tft.drawRoundRect(mealDeleteBtn.x, mealDeleteBtn.y, mealDeleteBtn.w, mealDeleteBtn.h, 8, TFT_WHITE);
    tft.setTextColor(TFT_WHITE, TFT_MAROON);
    tft.setCursor(mealDeleteBtn.x + 18, mealDeleteBtn.y + 10);
    tft.print(mealDeleteBtn.label);

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

void refreshCurrentScreen()
{
    // Skip auto-refresh if menu/popups are active, keyboard is open, or screen is currently touched
    if (wasTouched || menuOpen || itemMenuPopupOpen || currentScreen == SCREEN_KEYBOARD) {
        return;
    }

    switch (currentScreen) {
        case SCREEN_DASHBOARD:
            drawDashboard();
            break;

        case SCREEN_MEALS:
            drawMealsScreen();
            break;

        case SCREEN_MEAL_DETAILS:
            drawMealDetailsScreen();
            break;

        case SCREEN_CALENDAR:
            drawCalendarScreen();
            break;

        case SCREEN_CALENDAR_DAY:
            drawCalendarDayScreen();
            break;

        case SCREEN_WIFI_SETTINGS:
            drawWifiSettingsScreen();
            break;

        case SCREEN_ADD_ITEM:
            drawAddItemScreen();
            break;

        case SCREEN_ADD_MEAL:
            drawAddMealScreen();
            break;

        default:
            break;
    }
}