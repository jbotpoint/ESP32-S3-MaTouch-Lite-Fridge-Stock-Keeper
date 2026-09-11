#include "GroceryManager.h"

#define SPOILED_REMOVE_AFTER_SEC (7UL * 24UL * 60UL * 60UL)

void GroceryManager::begin()
{
    if (!LittleFS.begin(true)) {
        Serial.println("ERROR: LittleFS mount failed.");
    }

    loadItems();
}

void GroceryManager::loadItems()
{
    itemCount = 0;

    fs::File f = LittleFS.open(ITEMS_FILE_PATH, "r");

    if (!f) {
        Serial.println("No items file yet - starting with empty list.");
        return;
    }

    int count = 0;
    f.read((uint8_t *)&count, sizeof(count));

    if (count < 0 || count > MAX_ITEMS) {
        Serial.println("Items file looked corrupt - ignoring.");
        f.close();
        return;
    }

    size_t expected = sizeof(GroceryItem) * count;
    size_t got = f.read((uint8_t *)items, expected);

    f.close();

    if (got == expected) {
        itemCount = count;
        Serial.printf("Loaded %d item(s) from flash.\n", itemCount);
    }
    else {
        Serial.println("Items file size mismatch - ignoring.");
        itemCount = 0;
    }
}

void GroceryManager::saveItems()
{
    fs::File f = LittleFS.open(ITEMS_FILE_PATH, "w");

    if (!f) {
        Serial.println("ERROR: could not open items file for writing.");
        return;
    }

    f.write((uint8_t *)&itemCount, sizeof(itemCount));
    f.write((uint8_t *)items, sizeof(GroceryItem) * itemCount);

    f.close();
}

void GroceryManager::addNewItem(
    const char *name,
    uint8_t category,
    int days,
    int qty,
    int editingIndex
)
{
    if (strlen(name) == 0) {
        Serial.println("Item name is empty - not saving.");
        return;
    }

    if (editingIndex >= 0) {
        GroceryItem &it = items[editingIndex];

        strncpy(it.name, name, NAME_MAX_LEN - 1);
        it.name[NAME_MAX_LEN - 1] = '\0';

        it.category = category;
        it.dateAdded = time(nullptr);
        it.spoilDate =
            it.dateAdded + ((time_t)days * 24 * 60 * 60);
        it.spoiled = false;
        it.qty = qty;

        saveItems();
        sendItemToHomeAssistant(it, "updated");
    }
    else {
        int foundIndex = -1;

        for (int i = 0; i < itemCount; i++) {
            if (strcasecmp(items[i].name, name) == 0) {
                foundIndex = i;
                break;
            }
        }

        if (foundIndex >= 0) {
            items[foundIndex].qty += qty;

            saveItems();
            sendItemToHomeAssistant(items[foundIndex], "updated");
        }
        else {
            if (itemCount >= MAX_ITEMS) {
                Serial.println("Item list full - cannot add more.");
                return;
            }

            GroceryItem &it = items[itemCount];

            strncpy(it.name, name, NAME_MAX_LEN - 1);
            it.name[NAME_MAX_LEN - 1] = '\0';

            it.category = category;
            it.dateAdded = time(nullptr);
            it.spoilDate =
                it.dateAdded + ((time_t)days * 24 * 60 * 60);
            it.spoiled = false;
            it.qty = qty;

            itemCount++;

            saveItems();
            sendItemToHomeAssistant(it, "added");
        }
    }
}

void GroceryManager::deleteItem(int index)
{
    if (index < 0 || index >= itemCount) return;

    sendItemToHomeAssistant(items[index], "removed");

    for (int j = index; j < itemCount - 1; j++) {
        items[j] = items[j + 1];
    }

    itemCount--;

    saveItems();
}

int GroceryManager::getDaysUntil(time_t t)
{
    time_t now = time(nullptr);
    long diffSec = (long)(t - now);

    return diffSec / (60 * 60 * 24);
}

bool GroceryManager::checkSpoilage()
{
    time_t now = time(nullptr);
    bool changed = false;

    for (int i = 0; i < itemCount; /* no auto-increment */) {
        GroceryItem &it = items[i];

        if (!it.spoiled && now >= it.spoilDate) {
            it.spoiled = true;
            changed = true;

            sendItemToHomeAssistant(it, "spoiled");
        }

        if (
            it.spoiled &&
            now >= (
                it.spoilDate +
                (time_t)SPOILED_REMOVE_AFTER_SEC
            )
        ) {
            sendItemToHomeAssistant(it, "removed");

            for (int j = i; j < itemCount - 1; j++) {
                items[j] = items[j + 1];
            }

            itemCount--;
            changed = true;
            continue;
        }

        i++;
    }

    if (changed) {
        saveItems();
    }

    return changed;
}

void GroceryManager::sendItemToHomeAssistant(
    const GroceryItem &item,
    const char *eventType
)
{
    Serial.printf(
        "[HA STUB] event=%s item=\"%s\" category=%s\n",
        eventType,
        item.name,
        categoryNames[item.category]
    );
}
