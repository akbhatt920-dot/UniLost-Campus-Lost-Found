#include "matcher.h"

#include <ctype.h>
#include <string.h>

#define TOKEN_COUNT_MAX 64
#define TOKEN_LENGTH_MAX 48

static int is_word_character(unsigned char ch) {
    return isalnum(ch) || ch >= 128;
}

/* Convert punctuation to spaces, lowercase letters, and collapse whitespace. */
static void normalize_text(const char *source, char *target, size_t target_size) {
    size_t read_index = 0;
    size_t write_index = 0;
    int previous_was_space = 1;

    if (target_size == 0) return;
    target[0] = '\0';
    if (source == NULL) return;

    while (source[read_index] != '\0' && write_index + 1 < target_size) {
        unsigned char ch = (unsigned char) source[read_index++];
        if (is_word_character(ch)) {
            target[write_index++] = (char) tolower(ch);
            previous_was_space = 0;
        } else if (!previous_was_space && write_index + 1 < target_size) {
            target[write_index++] = ' ';
            previous_was_space = 1;
        }
    }

    if (write_index > 0 && target[write_index - 1] == ' ') write_index--;
    target[write_index] = '\0';
}

static int token_exists(char tokens[][TOKEN_LENGTH_MAX], int count,
                        const char *token) {
    int i;
    for (i = 0; i < count; i++) {
        if (strcmp(tokens[i], token) == 0) return 1;
    }
    return 0;
}

/* Build a small set of unique normalized words from a field. */
static int collect_tokens(const char *text,
                          char tokens[][TOKEN_LENGTH_MAX], int capacity) {
    char normalized[UNILOST_DESCRIPTION_LEN + UNILOST_LOCATION_LEN];
    char *word;
    char *save_pointer = NULL;
    int count = 0;

    normalize_text(text, normalized, sizeof(normalized));
    word = strtok_r(normalized, " ", &save_pointer);
    while (word != NULL && count < capacity) {
        if (!token_exists(tokens, count, word)) {
            size_t length = strlen(word);
            if (length >= TOKEN_LENGTH_MAX) length = TOKEN_LENGTH_MAX - 1;
            memcpy(tokens[count], word, length);
            tokens[count][length] = '\0';
            count++;
        }
        word = strtok_r(NULL, " ", &save_pointer);
    }
    return count;
}

/* Jaccard overlap: shared unique words divided by all unique words. */
static int token_similarity(const char *first, const char *second,
                            int maximum_points) {
    char first_tokens[TOKEN_COUNT_MAX][TOKEN_LENGTH_MAX] = {{0}};
    char second_tokens[TOKEN_COUNT_MAX][TOKEN_LENGTH_MAX] = {{0}};
    int first_count = collect_tokens(first, first_tokens, TOKEN_COUNT_MAX);
    int second_count = collect_tokens(second, second_tokens, TOKEN_COUNT_MAX);
    int shared = 0;
    int union_count;
    int i;

    if (first_count == 0 || second_count == 0) return 0;
    for (i = 0; i < first_count; i++) {
        if (token_exists(second_tokens, second_count, first_tokens[i])) shared++;
    }
    union_count = first_count + second_count - shared;
    return (shared * maximum_points + union_count / 2) / union_count;
}

static int is_leap_year(int year) {
    return year % 400 == 0 || (year % 4 == 0 && year % 100 != 0);
}

static int parse_date(const char *date, int *year, int *month, int *day) {
    int i;
    static const int days_per_month[] = {0, 31, 28, 31, 30, 31, 30,
                                         31, 31, 30, 31, 30, 31};

    if (date == NULL || strlen(date) != 10 || date[4] != '-' || date[7] != '-')
        return 0;
    for (i = 0; i < 10; i++) {
        if (i != 4 && i != 7 && !isdigit((unsigned char) date[i])) return 0;
    }

    *year = (date[0] - '0') * 1000 + (date[1] - '0') * 100 +
            (date[2] - '0') * 10 + (date[3] - '0');
    *month = (date[5] - '0') * 10 + (date[6] - '0');
    *day = (date[8] - '0') * 10 + (date[9] - '0');

    if (*year < 1900 || *year > 2100 || *month < 1 || *month > 12) return 0;
    if (*month == 2 && is_leap_year(*year)) return *day >= 1 && *day <= 29;
    return *day >= 1 && *day <= days_per_month[*month];
}

int unilost_valid_date(const char *date) {
    int year, month, day;
    return parse_date(date, &year, &month, &day);
}

static int date_ordinal(int year, int month, int day) {
    static const int days_before_month[] = {0, 0, 31, 59, 90, 120, 151,
                                            181, 212, 243, 273, 304, 334};
    int prior_years = year - 1;
    int ordinal = prior_years * 365 + prior_years / 4 - prior_years / 100 +
                  prior_years / 400;
    ordinal += days_before_month[month] + day;
    if (month > 2 && is_leap_year(year)) ordinal++;
    return ordinal;
}

static int date_points(const char *first, const char *second) {
    int first_year, first_month, first_day;
    int second_year, second_month, second_day;
    int difference;

    if (!parse_date(first, &first_year, &first_month, &first_day) ||
        !parse_date(second, &second_year, &second_month, &second_day)) return 0;

    difference = date_ordinal(first_year, first_month, first_day) -
                 date_ordinal(second_year, second_month, second_day);
    if (difference < 0) difference = -difference;
    if (difference == 0) return 15;
    if (difference <= 1) return 12;
    if (difference <= 3) return 9;
    if (difference <= 7) return 5;
    if (difference <= 14) return 2;
    return 0;
}

int unilost_calculate_score(const struct Item *lost, const struct Item *found,
                            struct MatchScore *parts) {
    struct MatchScore result = {0, 0, 0, 0, 0, 0};
    char lost_category[UNILOST_CATEGORY_LEN * 2];
    char found_category[UNILOST_CATEGORY_LEN * 2];

    if (lost == NULL || found == NULL) {
        if (parts != NULL) *parts = result;
        return 0;
    }

    normalize_text(lost->category, lost_category, sizeof(lost_category));
    normalize_text(found->category, found_category, sizeof(found_category));
    result.category = lost_category[0] != '\0' &&
                      strcmp(lost_category, found_category) == 0 ? 30 : 0;
    result.name = token_similarity(lost->item_name, found->item_name, 20);
    result.description = token_similarity(lost->description, found->description, 20);
    result.location = token_similarity(lost->location, found->location, 15);
    result.date = date_points(lost->item_date, found->item_date);
    result.total = result.category + result.name + result.description +
                   result.location + result.date;
    if (result.total < 0) result.total = 0;
    if (result.total > 100) result.total = 100;
    if (parts != NULL) *parts = result;
    return result.total;
}

const char *unilost_match_strength(int score) {
    if (score >= 70) return "Strong potential match";
    if (score >= 40) return "Possible match";
    return "Low match";
}

void unilost_sort_matches(struct Match matches[], size_t count) {
    size_t end;
    for (end = count; end > 1; end--) {
        size_t i;
        int swapped = 0;
        for (i = 0; i + 1 < end; i++) {
            if (matches[i].score.total < matches[i + 1].score.total) {
                struct Match temporary = matches[i];
                matches[i] = matches[i + 1];
                matches[i + 1] = temporary;
                swapped = 1;
            }
        }
        if (!swapped) break;
    }
}

int unilost_match_pair_exists(const struct Match matches[], size_t count,
                              const char *lost_id, const char *found_id) {
    size_t i;
    if (matches == NULL || lost_id == NULL || found_id == NULL) return 0;
    for (i = 0; i < count; i++) {
        if (strcmp(matches[i].lost.id, lost_id) == 0 &&
            strcmp(matches[i].found.id, found_id) == 0) return 1;
    }
    return 0;
}
