#ifndef UNILOST_DATABASE_H
#define UNILOST_DATABASE_H

#include "matcher.h"

#include <libpq-fe.h>
#include <stddef.h>

#define UNILOST_PROFILE_NAME_LEN 101
#define UNILOST_PROFILE_PHONE_LEN 33

struct Profile {
    char full_name[UNILOST_PROFILE_NAME_LEN];
    char phone[UNILOST_PROFILE_PHONE_LEN];
    char created_at[UNILOST_CREATED_LEN];
};

struct DashboardSummary {
    long lost_reports;
    long found_reports;
    long matches;
};

#define UNILOST_CLAIM_ID_LEN 64
#define UNILOST_CLAIM_MESSAGE_LEN 501
#define UNILOST_CLAIM_CONTACT_LEN 257
struct Claim {
    char id[UNILOST_CLAIM_ID_LEN];
    char item_id[UNILOST_ID_LEN];
    char item_name[UNILOST_NAME_LEN];
    char report_type[8];
    char category[UNILOST_CATEGORY_LEN];
    char message[UNILOST_CLAIM_MESSAGE_LEN];
    char requester_contact[UNILOST_CLAIM_CONTACT_LEN];
    char status[UNILOST_STATUS_LEN];
    char created_at[UNILOST_CREATED_LEN];
};

struct Database {
    PGconn *connection;
};

enum DatabaseResult {
    DATABASE_ERROR = 0,
    DATABASE_OK = 1,
    DATABASE_NOT_FOUND = -1,
    DATABASE_CONFLICT = -2
};

void database_init(struct Database *database);
void database_close(struct Database *database);
int database_is_ready(struct Database *database);
int database_count_open_items(struct Database *database, const char *report_type,
                              long *count);
int database_get_dashboard_summary(struct Database *database, const char *user_id,
                                   struct DashboardSummary *summary);

int database_list_items(struct Database *database, const char *report_type,
                        const char *category, const char *search,
                        const char *location, const char *owner_id,
                        const char *viewer_id, const char *status,
                        const char *date_from, const char *date_to,
                        const char *sort, int limit, int offset,
                        struct Item items[], size_t capacity, size_t *count,
                        size_t *total);
int database_create_item(struct Database *database, const struct Item *item,
                         const char *contact_name, const char *contact_info,
                         const char *owner_id,
                         char created_id[UNILOST_ID_LEN]);
int database_get_item(struct Database *database, const char *id,
                      const char *viewer_id, struct Item *item);
int database_get_open_lost(struct Database *database, const char *id,
                           const char *owner_id,
                           struct Item *item);
int database_get_open_found(struct Database *database, struct Item items[],
                            size_t capacity, size_t *count);
int database_store_match(struct Database *database, const char *lost_id,
                         const char *found_id, int score);
int database_update_item_status(struct Database *database, const char *id,
                                const char *owner_id, const char *status);
int database_get_profile(struct Database *database, const char *user_id,
                         struct Profile *profile);
int database_save_profile(struct Database *database, const char *user_id,
                          const char *full_name, const char *verified_phone);
int database_create_claim(struct Database *database, const char *item_id,
                          const char *requester_id, const char *message,
                          const char *requester_contact,
                          char claim_id[UNILOST_CLAIM_ID_LEN]);
int database_list_claims(struct Database *database, const char *user_id,
                         int received, struct Claim claims[], size_t capacity,
                         size_t *count);
int database_update_claim(struct Database *database, const char *claim_id,
                          const char *owner_id, const char *status);

#endif
