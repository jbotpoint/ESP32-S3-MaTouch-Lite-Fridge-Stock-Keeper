#pragma once

#include "AppTypes.h"
#include <LittleFS.h>

class GroceryManager {
private:
    GroceryItem items[MAX_ITEMS];
    int itemCount = 0;
    const char *ITEMS_FILE_PATH = "/items.dat";

    void sendItemToHomeAssistant(
        const GroceryItem &item,
        const char *eventType
    );

public:
    void begin();
    void loadItems();
    void saveItems();

    void addNewItem(
        const char *name,
        uint8_t category,
        int days,
        int qty,
        int editingIndex = -1
    );

    bool checkSpoilage();
    int getDaysUntil(time_t t);

    GroceryItem *getItems() { return items; }
    int getItemCount() const { return itemCount; }

    void deleteItem(int index);
};
