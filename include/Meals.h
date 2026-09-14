#pragma once

#include "AppTypes.h"
#include <Arduino.h>
#include <time.h>
#include <string>
#include <vector>
#include <cstring>

#define MEAL_NAME_MAX_LEN 32
#define MAX_MEALS 50
#define MAX_INGREDIENTS 10
#define INGREDIENT_NAME_MAX_LEN 32

enum MealCategory {
    MEAL_BREAKFAST,
    MEAL_LUNCH,
    MEAL_DINNER,
    MEAL_SNACK
};

const char *const mealCategoryNames[] = {
    "Breakfast",
    "Lunch",
    "Dinner",
    "Snack"
};

const size_t MEAL_CATEGORY_COUNT =
    sizeof(mealCategoryNames) / sizeof(mealCategoryNames[0]);

struct MealItem {
    char name[MEAL_NAME_MAX_LEN];
    MealCategory category;
    time_t dateCreated;
    bool isCompleted;
    char ingredients[MAX_INGREDIENTS][INGREDIENT_NAME_MAX_LEN];
    int ingredientCount;
    int timesMade;
};

class MealsManager {
private:
    MealItem meals[MAX_MEALS];
    int mealCount = 0;
    const char *ITEMS_FILE_PATH = "/meals.dat";

public:
    void begin();
    void loadMeals();
    void saveMeals();

    void addMeal(const char *name, MealCategory category, const char newIngredients[][INGREDIENT_NAME_MAX_LEN], int ingCount);    bool completeMeal(int index);
    void addToMealCount(int mealindex, int count);
    bool uncompleteMeal(int index);
    void deleteMeal(int index);

    const MealItem *getMeals();
    int getMealCount() const;

    bool hasMeal(const char *name) const;
};