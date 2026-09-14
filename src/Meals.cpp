#include "Meals.h"
#include <LittleFS.h>

void MealsManager::begin()
{
    loadMeals();
}

void MealsManager::loadMeals()
{
    mealCount = 0;

    fs::File f = LittleFS.open(ITEMS_FILE_PATH, "r");

    if (!f) {
        Serial.println("No meals file yet - starting with empty list.");
        return;
    }

    int count = 0;
    f.read((uint8_t *)&count, sizeof(count));

    if (count < 0 || count > MAX_MEALS) {
        Serial.println("Meals file looked corrupt - ignoring.");
        f.close();
        return;
    }

    size_t expected = sizeof(MealItem) * count;
    size_t got = f.read((uint8_t *)meals, expected);

    f.close();

    if (got == expected) {
        mealCount = count;
        Serial.printf("Loaded %d meal(s) from flash.\n", mealCount);
    }
    else {
        Serial.println("Meals file size mismatch - ignoring.");
        mealCount = 0;
    }
}

void MealsManager::saveMeals()
{
    fs::File f = LittleFS.open(ITEMS_FILE_PATH, "w");

    if (!f) {
        Serial.println("ERROR: could not open meals file for writing.");
        return;
    }

    f.write((uint8_t *)&mealCount, sizeof(mealCount));
    f.write((uint8_t *)meals, sizeof(MealItem) * mealCount);

    f.close();
}

void MealsManager::addMeal(const char *name, MealCategory category, const char newIngredients[][INGREDIENT_NAME_MAX_LEN], int ingCount)
{
    if (strlen(name) == 0 || mealCount >= MAX_MEALS) return;

    MealItem &it = meals[mealCount];
    strncpy(it.name, name, MEAL_NAME_MAX_LEN - 1);
    it.name[MEAL_NAME_MAX_LEN - 1] = '\0';
    it.category = category;
    it.dateCreated = time(nullptr);
    it.isCompleted = false;
    
    it.ingredientCount = (ingCount > MAX_INGREDIENTS) ? MAX_INGREDIENTS : ingCount;
    for (int i = 0; i < it.ingredientCount; i++) {
        strncpy(it.ingredients[i], newIngredients[i], INGREDIENT_NAME_MAX_LEN - 1);
        it.ingredients[i][INGREDIENT_NAME_MAX_LEN - 1] = '\0';
    }

    mealCount++;
    saveMeals();
}

bool MealsManager::completeMeal(int index)
{
    if (index < 0 || index >= mealCount) return false;

    meals[index].isCompleted = true;
    saveMeals();
    Serial.printf("Completed meal: %s\n", meals[index].name);
    return true;
}

void MealsManager::addToMealCount(int mealindex, int count)
{
    if (mealindex < 0 || mealindex >= mealCount) return;

    meals[mealindex].timesMade += count;
    saveMeals();
    Serial.printf("Updated times made for meal: %s to %d\n", meals[mealindex].name, meals[mealindex].timesMade);
}

bool MealsManager::uncompleteMeal(int index)
{
    if (index < 0 || index >= mealCount) return false;

    meals[index].isCompleted = false;
    saveMeals();
    Serial.printf("Uncompleted meal: %s\n", meals[index].name);
    return true;
}

void MealsManager::deleteMeal(int index)
{
    if (index < 0 || index >= mealCount) return;

    // Shift remaining meals down
    for (int j = index; j < mealCount - 1; j++) {
        meals[j] = meals[j + 1];
    }

    mealCount--;

    saveMeals();
    Serial.printf("Deleted meal: %s\n", meals[index].name);
}

const MealItem *MealsManager::getMeals()
{
    return meals;
}

int MealsManager::getMealCount() const
{
    return mealCount;
}

bool MealsManager::hasMeal(const char *name) const
{
    for (int i = 0; i < mealCount; i++) {
        if (strcasecmp(meals[i].name, name) == 0) {
            return true;
        }
    }
    return false;
}