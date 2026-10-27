#include "matcher.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct Item make_item(const char *id, const char *name,
                             const char *category, const char *description,
                             const char *location, const char *date) {
    struct Item item = {0};
    snprintf(item.id, sizeof(item.id), "%s", id);
    snprintf(item.item_name, sizeof(item.item_name), "%s", name);
    snprintf(item.category, sizeof(item.category), "%s", category);
    snprintf(item.description, sizeof(item.description), "%s", description);
    snprintf(item.location, sizeof(item.location), "%s", location);
    snprintf(item.item_date, sizeof(item.item_date), "%s", date);
    return item;
}

int main(void) {
    struct Item lost = make_item("lost-1", "Black leather wallet", "Wallet",
        "Small black wallet with a student card", "Central Library", "2026-10-20");
    struct Item exact = make_item("found-1", "Black leather wallet", "Wallet",
        "Small black wallet with a student card", "Central Library", "2026-10-20");
    struct Item partial = make_item("found-2", "Wallet", "Wallet",
        "Brown wallet found near the library", "Central Library", "2026-10-21");
    struct Item case_change = make_item("found-3", "BLACK LEATHER WALLET", "Wallet",
        "Small black wallet with student card", "Central Library", "2026-10-20");
    struct Item different_category = make_item("found-4", "Black leather wallet", "Keys",
        "Small black wallet with a student card", "Central Library", "2026-10-20");
    struct Item distant = make_item("found-5", "Umbrella", "Clothing",
        "Blue umbrella", "North Parking Lot", "2026-09-01");
    struct Item empty = {0};
    struct MatchScore parts;
    struct Match ordered[3] = {0};
    int exact_score;
    int different_score;

    exact_score = unilost_calculate_score(&lost, &exact, &parts);
    assert(exact_score == 100);
    assert(parts.category == 30 && parts.name == 20 && parts.description == 20);
    assert(parts.location == 15 && parts.date == 15);

    assert(unilost_calculate_score(&lost, &partial, &parts) >= 40);
    assert(parts.category == 30 && parts.location == 15);

    assert(unilost_calculate_score(&lost, &case_change, &parts) == 97);
    assert(parts.name == 20);

    different_score = unilost_calculate_score(&lost, &different_category, &parts);
    assert(parts.category == 0);
    assert(different_score < exact_score);

    assert(unilost_calculate_score(&lost, &distant, &parts) <= 20);
    assert(parts.location == 0 && parts.date == 0);

    assert(unilost_calculate_score(&empty, &empty, &parts) == 0);
    assert(unilost_calculate_score(NULL, &lost, &parts) == 0);
    assert(parts.total == 0);
    assert(unilost_valid_date("2024-02-29"));
    assert(!unilost_valid_date("2025-02-29"));
    assert(!unilost_valid_date("2026-13-01"));
    assert(!unilost_valid_date("not-a-date"));
    assert(!unilost_valid_date("1800-01-01"));

    assert(exact_score >= 0 && exact_score <= 100);
    assert(different_score >= 0 && different_score <= 100);

    ordered[0].score.total = 40;
    ordered[0].lost = lost;
    ordered[0].found = partial;
    ordered[1].score.total = 90;
    ordered[1].lost = lost;
    ordered[1].found = case_change;
    ordered[2].score.total = 70;
    ordered[2].lost = lost;
    ordered[2].found = different_category;
    unilost_sort_matches(ordered, 3);
    assert(ordered[0].score.total == 90);
    assert(ordered[1].score.total == 70);
    assert(ordered[2].score.total == 40);

    assert(unilost_match_pair_exists(ordered, 3, "lost-1", "found-2"));
    assert(!unilost_match_pair_exists(ordered, 3, "lost-1", "found-9"));
    assert(strcmp(unilost_match_strength(75), "Strong potential match") == 0);
    assert(strcmp(unilost_match_strength(55), "Possible match") == 0);
    assert(strcmp(unilost_match_strength(20), "Low match") == 0);

    puts("All matcher tests passed.");
    return 0;
}
