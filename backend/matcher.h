#ifndef UNILOST_MATCHER_H
#define UNILOST_MATCHER_H

#include <stddef.h>

#define UNILOST_ID_LEN 64
#define UNILOST_NAME_LEN 121
#define UNILOST_CATEGORY_LEN 51
#define UNILOST_DESCRIPTION_LEN 401
#define UNILOST_LOCATION_LEN 121
#define UNILOST_DATE_LEN 11
#define UNILOST_STATUS_LEN 16
#define UNILOST_CREATED_LEN 40
#define UNILOST_PHOTO_PATH_LEN 256

struct Item {
    char id[UNILOST_ID_LEN];
    char report_type[8];
    char item_name[UNILOST_NAME_LEN];
    char category[UNILOST_CATEGORY_LEN];
    char description[UNILOST_DESCRIPTION_LEN];
    char location[UNILOST_LOCATION_LEN];
    char item_date[UNILOST_DATE_LEN];
    char status[UNILOST_STATUS_LEN];
    char created_at[UNILOST_CREATED_LEN];
    char photo_path[UNILOST_PHOTO_PATH_LEN];
    int is_owner;  /* Set by the C API for the authenticated caller only. */
    int is_claimable;  /* An account owner exists to receive a private request. */
};

struct MatchScore {
    int category;
    int name;
    int description;
    int location;
    int date;
    int total;
};

struct Match {
    struct Item lost;
    struct Item found;
    struct MatchScore score;
};

/* Return 1 for a real YYYY-MM-DD date between 1900 and 2100, else 0. */
int unilost_valid_date(const char *date);

/* Fill the five explainable score parts and return the total (0 to 100). */
int unilost_calculate_score(const struct Item *lost, const struct Item *found,
                            struct MatchScore *parts);

const char *unilost_match_strength(int score);

/* Beginner-friendly bubble sort, highest scores first. */
void unilost_sort_matches(struct Match matches[], size_t count);

int unilost_match_pair_exists(const struct Match matches[], size_t count,
                              const char *lost_id, const char *found_id);

#endif
