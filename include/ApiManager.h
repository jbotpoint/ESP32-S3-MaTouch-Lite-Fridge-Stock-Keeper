#ifndef API_SERVER_H
#define API_SERVER_H

#include <WebServer.h>
#include <ArduinoJson.h>
#include "GroceryManager.h"
#include "Meals.h"

class ApiServer {
public:
    ApiServer(GroceryManager &groceryMgr, MealsManager &mealsMgr, int port = 80);
    void begin();
    void handle();

private:
    WebServer server;
    GroceryManager &groceryManager;
    MealsManager &mealsManager;

    void handleGetStock();
    void handlePostStock();
    void handleGetMeals();
    void handlePostMeals();
    void handleNotFound();
    void handleDeleteStock();
    void handleDeleteMeals();
};

#endif // API_SERVER_H