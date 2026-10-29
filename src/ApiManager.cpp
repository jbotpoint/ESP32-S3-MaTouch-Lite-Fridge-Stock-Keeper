#include "ApiManager.h"

ApiServer::ApiServer(GroceryManager &groceryMgr, MealsManager &mealsMgr, int port)
    : server(port), groceryManager(groceryMgr), mealsManager(mealsMgr) {}

void ApiServer::begin() {
    server.on("/api/stock", HTTP_GET, std::bind(&ApiServer::handleGetStock, this));
    server.on("/api/stock", HTTP_POST, std::bind(&ApiServer::handlePostStock, this));
    server.on("/api/stock", HTTP_DELETE, std::bind(&ApiServer::handleDeleteStock, this));
    
    server.on("/api/meals", HTTP_GET, std::bind(&ApiServer::handleGetMeals, this));
    server.on("/api/meals", HTTP_POST, std::bind(&ApiServer::handlePostMeals, this));
    server.on("/api/meals", HTTP_DELETE, std::bind(&ApiServer::handleDeleteMeals, this));
    
    server.onNotFound(std::bind(&ApiServer::handleNotFound, this));
    server.begin();
    Serial.println("API WebServer started on port 80.");
}

void ApiServer::handle() {
    server.handleClient();
}

// ------------------------------------------------------------
// STOCK HANDLERS
// ------------------------------------------------------------

void ApiServer::handleGetStock() {
    Serial.println("handleGetStock called");
    DynamicJsonDocument doc(8192);
    JsonArray itemsArr = doc.createNestedArray("items");

    int count = groceryManager.getItemCount();
    doc["count"] = count;

    for (int i = 0; i < count; i++) {
        const auto &item = groceryManager.getItems()[i];
        JsonObject obj = itemsArr.createNestedObject();
        obj["name"] = item.name;
        obj["category"] = item.category;
        
        int daysLeft = groceryManager.getDaysUntil(item.spoilDate);
        obj["days"] = daysLeft > 0 ? daysLeft : 0;
        obj["qty"] = item.qty;
        obj["spoiled"] = item.spoiled;
    }

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void ApiServer::handlePostStock() {
    Serial.println("handlePostStock called");
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    JsonArray itemsArr = doc["items"].as<JsonArray>();
    if (itemsArr.isNull()) {
        server.send(400, "application/json", "{\"error\":\"Missing 'items' array\"}");
        return;
    }

    const uint8_t MAX_CATEGORY = 7; // Indices 0 to 7 (8 categories total)
    int added = 0;

    for (JsonObject obj : itemsArr) {
        const char* name = obj["name"] | "";
        uint8_t cat = obj["category"] | 0;
        int days = obj["days"] | 7;
        int qty = obj["qty"] | 1;

        // Data Sanitization
        if (cat > MAX_CATEGORY) {
            Serial.printf("Warning: Invalid category %d for '%s'. Defaulting to 0.\n", cat, name);
            cat = 0; // Clamps to default category (e.g., "Other" or "General")
        }

        if (days < 0) days = 0;
        if (qty < 1) qty = 1;

        if (strlen(name) > 0) {
            groceryManager.addNewItem(name, cat, days, qty);
            added++;
        }
    }

    StaticJsonDocument<256> respDoc;
    respDoc["status"] = "ok";
    respDoc["added"] = added;
    String response;
    serializeJson(respDoc, response);
    server.send(200, "application/json", response);
}

// Removes stock items by name. Accepts either a single object
// {"name":"Milk"} or a batch {"items":[{"name":"Milk"},{"name":"Eggs"}]}.
// Only the first match for each name is removed.
void ApiServer::handleDeleteStock() {
    Serial.println("handleDeleteStock called");
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    int removed = 0;
    int notFound = 0;

    auto removeByName = [&](const char* name) {
        if (!name || strlen(name) == 0) return;
        int count = groceryManager.getItemCount();
        for (int i = 0; i < count; i++) {
            if (strcmp(groceryManager.getItems()[i].name, name) == 0) {
                groceryManager.deleteItem(i);
                removed++;
                return;
            }
        }
        notFound++;
    };

    JsonArray itemsArr = doc["items"].as<JsonArray>();
    if (!itemsArr.isNull()) {
        for (JsonObject obj : itemsArr) {
            removeByName(obj["name"] | "");
        }
    } else {
        removeByName(doc["name"] | "");
    }

    if (removed == 0 && notFound == 0) {
        server.send(400, "application/json", "{\"error\":\"No item name(s) provided\"}");
        return;
    }

    StaticJsonDocument<256> respDoc;
    respDoc["status"] = "ok";
    respDoc["removed"] = removed;
    respDoc["notFound"] = notFound;
    String response;
    serializeJson(respDoc, response);
    server.send(200, "application/json", response);
}

// ------------------------------------------------------------
// MEALS HANDLERS
// ------------------------------------------------------------

void ApiServer::handleGetMeals() {
    Serial.println("handleGetMeals called");
    DynamicJsonDocument doc(8192);
    JsonArray mealsArr = doc.createNestedArray("meals");

    int count = mealsManager.getMealCount();
    doc["count"] = count;

    for (int i = 0; i < count; i++) {
        const auto &meal = mealsManager.getMeals()[i];
        JsonObject obj = mealsArr.createNestedObject();
        obj["name"] = meal.name;
        obj["category"] = meal.category;
        obj["timesMade"] = meal.timesMade;
        obj["isCompleted"] = meal.isCompleted;

        JsonArray ingArr = obj.createNestedArray("ingredients");
        for (int j = 0; j < meal.ingredientCount; j++) {
            ingArr.add(meal.ingredients[j]);
        }
    }

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void ApiServer::handlePostMeals() {
    Serial.println("handlePostMeals called");
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    DynamicJsonDocument doc(8192);
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    JsonArray mealsArr = doc["meals"].as<JsonArray>();
    if (mealsArr.isNull()) {
        server.send(400, "application/json", "{\"error\":\"Missing 'meals' array\"}");
        return;
    }

    // Append incoming items without clearing existing list
    int added = 0;
    for (JsonObject obj : mealsArr) {
        const char* name = obj["name"] | "";
        MealCategory cat = (MealCategory)(obj["category"] | 0);
        
        char ingredients[MAX_INGREDIENTS][INGREDIENT_NAME_MAX_LEN];
        int ingCount = 0;

        JsonArray ingArr = obj["ingredients"].as<JsonArray>();
        for (JsonVariant ing : ingArr) {
            if (ingCount < MAX_INGREDIENTS) {
                strncpy(ingredients[ingCount], ing.as<const char*>(), INGREDIENT_NAME_MAX_LEN - 1);
                ingredients[ingCount][INGREDIENT_NAME_MAX_LEN - 1] = '\0';
                ingCount++;
            }
        }

        if (strlen(name) > 0) {
            mealsManager.addMeal(name, cat, ingredients, ingCount);
            added++;
        }
    }

    StaticJsonDocument<256> respDoc;
    respDoc["status"] = "ok";
    respDoc["added"] = added;
    String response;
    serializeJson(respDoc, response);
    server.send(200, "application/json", response);
}

// Removes meals by name. Accepts either a single object {"name":"Tacos"}
// or a batch {"meals":[{"name":"Tacos"},{"name":"Chili"}]}.
// Only the first match for each name is removed.
void ApiServer::handleDeleteMeals() {
    Serial.println("handleDeleteMeals called");
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    StaticJsonDocument<1024> doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    int removed = 0;
    int notFound = 0;

    auto removeByName = [&](const char* name) {
        if (!name || strlen(name) == 0) return;
        int count = mealsManager.getMealCount();
        for (int i = 0; i < count; i++) {
            if (strcmp(mealsManager.getMeals()[i].name, name) == 0) {
                mealsManager.deleteMeal(i);
                removed++;
                return;
            }
        }
        notFound++;
    };

    JsonArray mealsArr = doc["meals"].as<JsonArray>();
    if (!mealsArr.isNull()) {
        for (JsonObject obj : mealsArr) {
            removeByName(obj["name"] | "");
        }
    } else {
        removeByName(doc["name"] | "");
    }

    if (removed == 0 && notFound == 0) {
        server.send(400, "application/json", "{\"error\":\"No meal name(s) provided\"}");
        return;
    }

    StaticJsonDocument<256> respDoc;
    respDoc["status"] = "ok";
    respDoc["removed"] = removed;
    respDoc["notFound"] = notFound;
    String response;
    serializeJson(respDoc, response);
    server.send(200, "application/json", response);
}

void ApiServer::handleNotFound() {
    server.send(404, "application/json", "{\"error\":\"Endpoint not found\"}");
}