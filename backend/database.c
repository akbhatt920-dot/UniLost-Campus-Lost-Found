#include "database.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DATABASE_URL_MAX 4096
#define ITEM_QUERY_LIMIT 200

static int secure_connection_string(const char *source,
                                   char destination[DATABASE_URL_MAX]) {
    const char *ssl_mode;
    size_t length;

    if (source == NULL || source[0] == '\0') return 0;
    length = strlen(source);
    if (length >= DATABASE_URL_MAX - 32) return 0;

    ssl_mode = strstr(source, "sslmode=");
    if (ssl_mode != NULL) {
        char mode[24];
        size_t mode_length = 0;
        const char *value = ssl_mode + strlen("sslmode=");
        while (value[mode_length] != '\0' && value[mode_length] != '&' &&
               value[mode_length] != ' ' && value[mode_length] != '\'') {
            if (mode_length + 1 >= sizeof(mode)) return 0;
            mode[mode_length] = value[mode_length];
            mode_length++;
        }
        mode[mode_length] = '\0';
        if (strcmp(mode, "require") != 0 && strcmp(mode, "verify-ca") != 0 &&
            strcmp(mode, "verify-full") != 0) return 0;
        memcpy(destination, source, length + 1);
        return 1;
    }

    memcpy(destination, source, length);
    if (strncmp(source, "postgres://", 11) == 0 ||
        strncmp(source, "postgresql://", 13) == 0) {
        snprintf(destination + length, DATABASE_URL_MAX - length,
                 "%csslmode=require", strchr(source, '?') == NULL ? '?' : '&');
    } else {
        snprintf(destination + length, DATABASE_URL_MAX - length,
                 " sslmode=require");
    }
    return 1;
}

static PGconn *get_connection(struct Database *database) {
    char connection_string[DATABASE_URL_MAX];
    const char *url = getenv("DATABASE_URL");

    if (database == NULL) return NULL;
    if (database->connection != NULL &&
        PQstatus(database->connection) == CONNECTION_OK) return database->connection;
    if (database->connection != NULL) {
        PQfinish(database->connection);
        database->connection = NULL;
    }
    if (!secure_connection_string(url, connection_string)) return NULL;

    database->connection = PQconnectdb(connection_string);
    if (database->connection == NULL ||
        PQstatus(database->connection) != CONNECTION_OK ||
        PQsslInUse(database->connection) != 1) {
        if (database->connection != NULL) PQfinish(database->connection);
        database->connection = NULL;
        return NULL;
    }
    return database->connection;
}

static void close_bad_connection(struct Database *database) {
    if (database != NULL && database->connection != NULL &&
        PQstatus(database->connection) != CONNECTION_OK) {
        PQfinish(database->connection);
        database->connection = NULL;
    }
}

static void copy_column(PGresult *result, int row, int column,
                        char *destination, size_t destination_size) {
    if (destination_size == 0) return;
    destination[0] = '\0';
    if (!PQgetisnull(result, row, column))
        snprintf(destination, destination_size, "%s", PQgetvalue(result, row, column));
}

static void read_item(PGresult *result, int row, struct Item *item) {
    memset(item, 0, sizeof(*item));
    copy_column(result, row, 0, item->id, sizeof(item->id));
    copy_column(result, row, 1, item->report_type, sizeof(item->report_type));
    copy_column(result, row, 2, item->item_name, sizeof(item->item_name));
    copy_column(result, row, 3, item->category, sizeof(item->category));
    copy_column(result, row, 4, item->description, sizeof(item->description));
    copy_column(result, row, 5, item->location, sizeof(item->location));
    copy_column(result, row, 6, item->item_date, sizeof(item->item_date));
    copy_column(result, row, 7, item->status, sizeof(item->status));
    copy_column(result, row, 8, item->created_at, sizeof(item->created_at));
    if (PQnfields(result) > 9)
        copy_column(result, row, 9, item->photo_path, sizeof(item->photo_path));
}

static int query_succeeded(PGresult *result) {
    ExecStatusType status;
    if (result == NULL) return 0;
    status = PQresultStatus(result);
    return status == PGRES_TUPLES_OK || status == PGRES_COMMAND_OK;
}

static int append_filter(char *query, size_t query_size, const char *format,
                         int parameter) {
    char filter[160];
    int written = snprintf(filter, sizeof(filter), format, parameter);
    if (written < 0 || (size_t) written >= sizeof(filter) ||
        strlen(query) + (size_t) written + 1 >= query_size) return 0;
    strcat(query, filter);
    return 1;
}

void database_init(struct Database *database) {
    if (database != NULL) database->connection = NULL;
}

void database_close(struct Database *database) {
    if (database != NULL && database->connection != NULL) {
        PQfinish(database->connection);
        database->connection = NULL;
    }
}

int database_is_ready(struct Database *database) {
    PGconn *connection = get_connection(database);
    PGresult *result;
    int ready;
    if (connection == NULL) return 0;
    result = PQexec(connection, "SELECT 1");
    ready = query_succeeded(result);
    if (result != NULL) PQclear(result);
    close_bad_connection(database);
    return ready;
}

int database_count_open_items(struct Database *database, const char *report_type,
                              long *count) {
    static const char query[] =
        "SELECT count(*)::bigint FROM public.items "
        "WHERE report_type=$1 AND status='open'";
    const char *values[1] = {report_type};
    PGconn *connection;
    PGresult *result;
    if (database == NULL || report_type == NULL || count == NULL ||
        (strcmp(report_type, "lost") != 0 && strcmp(report_type, "found") != 0))
        return DATABASE_ERROR;
    connection = get_connection(database);
    if (connection == NULL) return DATABASE_ERROR;
    result = PQexecParams(connection, query, 1, NULL, values, NULL, NULL, 0);
    if (!query_succeeded(result) || PQntuples(result) != 1) {
        if (result != NULL) PQclear(result);
        close_bad_connection(database);
        return DATABASE_ERROR;
    }
    *count = strtol(PQgetvalue(result, 0, 0), NULL, 10);
    PQclear(result);
    close_bad_connection(database);
    return DATABASE_OK;
}

int database_get_dashboard_summary(struct Database *database, const char *user_id,
                                   struct DashboardSummary *summary) {
    static const char query[] =
        "SELECT count(*) FILTER (WHERE report_type='lost')::bigint, "
        "count(*) FILTER (WHERE report_type='found')::bigint, "
        "(SELECT count(*)::bigint FROM public.matches m "
        "JOIN public.items lost ON lost.id=m.lost_item_id "
        "WHERE lost.user_id::text=$1) "
        "FROM public.items WHERE user_id::text=$1";
    const char *values[1] = {user_id};
    PGconn *connection;
    PGresult *result;
    if (database == NULL || user_id == NULL || summary == NULL)
        return DATABASE_ERROR;
    connection = get_connection(database);
    if (connection == NULL) return DATABASE_ERROR;
    result = PQexecParams(connection, query, 1, NULL, values, NULL, NULL, 0);
    if (!query_succeeded(result) || PQntuples(result) != 1) {
        if (result != NULL) PQclear(result);
        close_bad_connection(database);
        return DATABASE_ERROR;
    }
    summary->lost_reports = strtol(PQgetvalue(result, 0, 0), NULL, 10);
    summary->found_reports = strtol(PQgetvalue(result, 0, 1), NULL, 10);
    summary->matches = strtol(PQgetvalue(result, 0, 2), NULL, 10);
    PQclear(result);
    close_bad_connection(database);
    return DATABASE_OK;
}

int database_list_items(struct Database *database, const char *report_type,
                        const char *category, const char *search,
                        const char *location, const char *owner_id,
                        const char *viewer_id, const char *status,
                        const char *date_from, const char *date_to,
                        const char *sort, int limit, int offset,
                        struct Item items[], size_t capacity, size_t *count,
                        size_t *total) {
    char query[1500] =
        "SELECT id::text, report_type, item_name, category, description, location, "
        "TO_CHAR(item_date, 'YYYY-MM-DD'), status, "
        "TO_CHAR(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS'), photo_path, user_id::text, "
        "count(*) OVER()::bigint "
        "FROM items WHERE 1=1";
    const char *values[10];
    char search_pattern[UNILOST_DESCRIPTION_LEN + UNILOST_NAME_LEN + 3];
    char location_pattern[UNILOST_LOCATION_LEN + 3];
    char limit_text[12], offset_text[16];
    const char *order_clause = " ORDER BY created_at DESC, id DESC";
    size_t value_count = 0;
    size_t row_count;
    PGconn *connection;
    PGresult *result;
    int row;

    if (count != NULL) *count = 0;
    if (total != NULL) *total = 0;
    if (database == NULL || items == NULL || count == NULL || total == NULL || limit < 1 ||
        limit > ITEM_QUERY_LIMIT || offset < 0) return DATABASE_ERROR;
    connection = get_connection(database);
    if (connection == NULL) return DATABASE_ERROR;

    if (report_type != NULL && report_type[0] != '\0') {
        if (!append_filter(query, sizeof(query), " AND report_type=$%d",
                           (int) value_count + 1)) return DATABASE_ERROR;
        values[value_count++] = report_type;
    }
    if (category != NULL && category[0] != '\0') {
        if (!append_filter(query, sizeof(query), " AND category=$%d",
                           (int) value_count + 1)) return DATABASE_ERROR;
        values[value_count++] = category;
    }
    if (search != NULL && search[0] != '\0') {
        char filter[160];
        int parameter = (int) value_count + 1;
        snprintf(search_pattern, sizeof(search_pattern), "%%%s%%", search);
        snprintf(filter, sizeof(filter),
                 " AND (item_name ILIKE $%d OR description ILIKE $%d)",
                 parameter, parameter);
        if (strlen(query) + strlen(filter) + 1 >= sizeof(query)) return DATABASE_ERROR;
        strcat(query, filter);
        values[value_count++] = search_pattern;
    }
    if (location != NULL && location[0] != '\0') {
        snprintf(location_pattern, sizeof(location_pattern), "%%%s%%", location);
        if (!append_filter(query, sizeof(query), " AND location ILIKE $%d",
                           (int) value_count + 1)) return DATABASE_ERROR;
        values[value_count++] = location_pattern;
    }
    if (owner_id != NULL && owner_id[0] != '\0') {
        if (!append_filter(query, sizeof(query), " AND user_id::text=$%d",
                           (int) value_count + 1)) return DATABASE_ERROR;
        values[value_count++] = owner_id;
    }
    if (status != NULL && status[0] != '\0') {
        if (!append_filter(query, sizeof(query), " AND status=$%d",
                           (int) value_count + 1)) return DATABASE_ERROR;
        values[value_count++] = status;
    }
    if (date_from != NULL && date_from[0] != '\0') {
        if (!append_filter(query, sizeof(query), " AND item_date >= $%d::date",
                           (int) value_count + 1)) return DATABASE_ERROR;
        values[value_count++] = date_from;
    }
    if (date_to != NULL && date_to[0] != '\0') {
        if (!append_filter(query, sizeof(query), " AND item_date <= $%d::date",
                           (int) value_count + 1)) return DATABASE_ERROR;
        values[value_count++] = date_to;
    }

    if (sort != NULL && strcmp(sort, "oldest") == 0)
        order_clause = " ORDER BY created_at ASC, id ASC";
    else if (sort != NULL && strcmp(sort, "name") == 0)
        order_clause = " ORDER BY lower(item_name) ASC, created_at DESC, id DESC";
    else if (sort != NULL && strcmp(sort, "date") == 0)
        order_clause = " ORDER BY item_date DESC, created_at DESC, id DESC";
    if (strlen(query) + strlen(order_clause) + 1 >= sizeof(query))
        return DATABASE_ERROR;
    strcat(query, order_clause);
    snprintf(limit_text, sizeof(limit_text), "%d", limit);
    snprintf(offset_text, sizeof(offset_text), "%d", offset);
    if (!append_filter(query, sizeof(query), " LIMIT $%d::integer",
                       (int) value_count + 1)) return DATABASE_ERROR;
    values[value_count++] = limit_text;
    if (!append_filter(query, sizeof(query), " OFFSET $%d::integer",
                       (int) value_count + 1)) return DATABASE_ERROR;
    values[value_count++] = offset_text;
    result = PQexecParams(connection, query, (int) value_count, NULL, values,
                          NULL, NULL, 0);
    if (!query_succeeded(result)) {
        if (result != NULL) PQclear(result);
        close_bad_connection(database);
        return DATABASE_ERROR;
    }

    row_count = (size_t) PQntuples(result);
    if (row_count > 0)
        *total = (size_t) strtoull(PQgetvalue(result, 0, 11), NULL, 10);
    if (row_count > capacity) row_count = capacity;
    for (row = 0; row < (int) row_count; row++) {
        read_item(result, row, &items[row]);
        items[row].is_owner = !PQgetisnull(result, row, 10) &&
            PQgetvalue(result, row, 10)[0] != '\0' && viewer_id != NULL &&
            strcmp(PQgetvalue(result, row, 10), viewer_id) == 0;
        items[row].is_claimable = !PQgetisnull(result, row, 10) &&
            PQgetvalue(result, row, 10)[0] != '\0' && !items[row].is_owner;
    }
    *count = row_count;
    PQclear(result);
    close_bad_connection(database);
    return DATABASE_OK;
}

int database_create_item(struct Database *database, const struct Item *item,
                         const char *contact_name, const char *contact_info,
                         const char *owner_id,
                         char created_id[UNILOST_ID_LEN]) {
    static const char query[] =
        "INSERT INTO items (report_type, item_name, category, description, location, "
        "item_date, contact_name, contact_info, status, photo_path, user_id) "
        "VALUES ($1, $2, $3, $4, $5, $6::date, $7, $8, 'open', $9, $10::uuid) "
        "RETURNING id::text";
    const char *values[10];
    PGconn *connection;
    PGresult *result;

    if (database == NULL || item == NULL || owner_id == NULL ||
        created_id == NULL) return DATABASE_ERROR;
    connection = get_connection(database);
    if (connection == NULL) return DATABASE_ERROR;
    values[0] = item->report_type;
    values[1] = item->item_name;
    values[2] = item->category;
    values[3] = item->description;
    values[4] = item->location;
    values[5] = item->item_date;
    values[6] = contact_name;
    values[7] = contact_info;
    values[8] = item->photo_path[0] == '\0' ? NULL : item->photo_path;
    values[9] = owner_id;
    result = PQexecParams(connection, query, 10, NULL, values, NULL, NULL, 0);
    if (!query_succeeded(result) || PQntuples(result) != 1) {
        if (result != NULL) PQclear(result);
        close_bad_connection(database);
        return DATABASE_ERROR;
    }
    copy_column(result, 0, 0, created_id, UNILOST_ID_LEN);
    PQclear(result);
    close_bad_connection(database);
    return DATABASE_OK;
}

int database_get_item(struct Database *database, const char *id,
                      const char *viewer_id, struct Item *item) {
    static const char query[] =
        "SELECT id::text, report_type, item_name, category, description, location, "
        "TO_CHAR(item_date, 'YYYY-MM-DD'), status, "
        "TO_CHAR(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS'), photo_path, user_id::text "
        "FROM items WHERE id::text=$1";
    const char *values[1] = {id};
    PGconn *connection;
    PGresult *result;
    if (database == NULL || id == NULL || viewer_id == NULL || item == NULL)
        return DATABASE_ERROR;
    connection = get_connection(database);
    if (connection == NULL) return DATABASE_ERROR;
    result = PQexecParams(connection, query, 1, NULL, values, NULL, NULL, 0);
    if (!query_succeeded(result)) {
        if (result != NULL) PQclear(result);
        close_bad_connection(database);
        return DATABASE_ERROR;
    }
    if (PQntuples(result) == 0) {
        PQclear(result);
        close_bad_connection(database);
        return DATABASE_NOT_FOUND;
    }
    read_item(result, 0, item);
    item->is_owner = !PQgetisnull(result, 0, 10) &&
        PQgetvalue(result, 0, 10)[0] != '\0' &&
        strcmp(PQgetvalue(result, 0, 10), viewer_id) == 0;
    item->is_claimable = !PQgetisnull(result, 0, 10) &&
        PQgetvalue(result, 0, 10)[0] != '\0' && !item->is_owner;
    PQclear(result);
    close_bad_connection(database);
    return DATABASE_OK;
}

static int database_get_one_open_item(struct Database *database, const char *id,
                                      const char *report_type,
                                      const char *owner_id,
                                      struct Item *item) {
    static const char query[] =
        "SELECT id::text, report_type, item_name, category, description, location, "
        "TO_CHAR(item_date, 'YYYY-MM-DD'), status, "
        "TO_CHAR(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS'), photo_path "
        "FROM items WHERE id::text=$1 AND report_type=$2 AND status='open' "
        "AND user_id::text=$3";
    const char *values[3] = {id, report_type, owner_id};
    PGconn *connection = get_connection(database);
    PGresult *result;

    if (connection == NULL) return DATABASE_ERROR;
    result = PQexecParams(connection, query, 3, NULL, values, NULL, NULL, 0);
    if (!query_succeeded(result)) {
        if (result != NULL) PQclear(result);
        close_bad_connection(database);
        return DATABASE_ERROR;
    }
    if (PQntuples(result) == 0) {
        PQclear(result);
        return DATABASE_NOT_FOUND;
    }
    read_item(result, 0, item);
    PQclear(result);
    close_bad_connection(database);
    return DATABASE_OK;
}

int database_get_open_lost(struct Database *database, const char *id,
                           const char *owner_id, struct Item *item) {
    if (database == NULL || id == NULL || owner_id == NULL || item == NULL)
        return DATABASE_ERROR;
    return database_get_one_open_item(database, id, "lost", owner_id, item);
}

int database_get_open_found(struct Database *database, struct Item items[],
                            size_t capacity, size_t *count) {
    static const char query[] =
        "SELECT id::text, report_type, item_name, category, description, location, "
        "TO_CHAR(item_date, 'YYYY-MM-DD'), status, "
        "TO_CHAR(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS'), photo_path "
        "FROM items WHERE report_type='found' AND status='open' "
        "ORDER BY created_at DESC LIMIT 200";
    PGconn *connection;
    PGresult *result;
    size_t row_count;
    int row;

    if (count != NULL) *count = 0;
    if (database == NULL || items == NULL || count == NULL) return DATABASE_ERROR;
    connection = get_connection(database);
    if (connection == NULL) return DATABASE_ERROR;
    result = PQexec(connection, query);
    if (!query_succeeded(result)) {
        if (result != NULL) PQclear(result);
        close_bad_connection(database);
        return DATABASE_ERROR;
    }
    row_count = (size_t) PQntuples(result);
    if (row_count > capacity) row_count = capacity;
    for (row = 0; row < (int) row_count; row++) read_item(result, row, &items[row]);
    *count = row_count;
    PQclear(result);
    close_bad_connection(database);
    return DATABASE_OK;
}

int database_store_match(struct Database *database, const char *lost_id,
                         const char *found_id, int score) {
    static const char query[] =
        "INSERT INTO matches (lost_item_id, found_item_id, match_score) "
        "VALUES ($1, $2, $3::integer) "
        "ON CONFLICT (lost_item_id, found_item_id) "
        "DO UPDATE SET match_score=EXCLUDED.match_score";
    char score_text[4];
    const char *values[3] = {lost_id, found_id, score_text};
    PGconn *connection;
    PGresult *result;

    if (database == NULL || lost_id == NULL || found_id == NULL ||
        strcmp(lost_id, found_id) == 0 || score < 40 || score > 100)
        return DATABASE_ERROR;
    connection = get_connection(database);
    if (connection == NULL) return DATABASE_ERROR;
    snprintf(score_text, sizeof(score_text), "%d", score);
    result = PQexecParams(connection, query, 3, NULL, values, NULL, NULL, 0);
    if (!query_succeeded(result)) {
        if (result != NULL) PQclear(result);
        close_bad_connection(database);
        return DATABASE_ERROR;
    }
    PQclear(result);
    close_bad_connection(database);
    return DATABASE_OK;
}

int database_update_item_status(struct Database *database, const char *id,
                                const char *owner_id, const char *status) {
    static const char update_query[] =
        "UPDATE items SET status=$1 WHERE id::text=$2 AND user_id::text=$3 "
        "AND status='open' RETURNING id";
    static const char exists_query[] =
        "SELECT id FROM items WHERE id::text=$1 AND user_id::text=$2";
    const char *values[3] = {status, id, owner_id};
    PGconn *connection;
    PGresult *result;

    if (database == NULL || id == NULL || owner_id == NULL || status == NULL ||
        (strcmp(status, "claimed") != 0 && strcmp(status, "closed") != 0))
        return DATABASE_ERROR;
    connection = get_connection(database);
    if (connection == NULL) return DATABASE_ERROR;
    result = PQexecParams(connection, update_query, 3, NULL, values, NULL, NULL, 0);
    if (!query_succeeded(result)) {
        if (result != NULL) PQclear(result);
        close_bad_connection(database);
        return DATABASE_ERROR;
    }
    if (PQntuples(result) > 0) {
        PQclear(result);
        close_bad_connection(database);
        return DATABASE_OK;
    }
    PQclear(result);
    {
        const char *exists_values[2] = {id, owner_id};
        result = PQexecParams(connection, exists_query, 2, NULL, exists_values,
                              NULL, NULL, 0);
    }
    if (!query_succeeded(result)) {
        if (result != NULL) PQclear(result);
        close_bad_connection(database);
        return DATABASE_ERROR;
    }
    if (PQntuples(result) == 0) {
        PQclear(result);
        close_bad_connection(database);
        return DATABASE_NOT_FOUND;
    }
    PQclear(result);
    close_bad_connection(database);
    return DATABASE_CONFLICT;
}

int database_get_profile(struct Database *database, const char *user_id,
                         struct Profile *profile) {
    static const char query[] =
        "SELECT full_name, COALESCE(phone, ''), "
        "TO_CHAR(created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS') "
        "FROM public.profiles WHERE user_id::text=$1";
    const char *values[1] = {user_id};
    PGconn *connection;
    PGresult *result;
    if (database == NULL || user_id == NULL || profile == NULL)
        return DATABASE_ERROR;
    connection = get_connection(database);
    if (connection == NULL) return DATABASE_ERROR;
    result = PQexecParams(connection, query, 1, NULL, values, NULL, NULL, 0);
    if (!query_succeeded(result)) {
        if (result != NULL) PQclear(result);
        close_bad_connection(database);
        return DATABASE_ERROR;
    }
    if (PQntuples(result) == 0) {
        PQclear(result);
        close_bad_connection(database);
        return DATABASE_NOT_FOUND;
    }
    memset(profile, 0, sizeof(*profile));
    copy_column(result, 0, 0, profile->full_name, sizeof(profile->full_name));
    copy_column(result, 0, 1, profile->phone, sizeof(profile->phone));
    copy_column(result, 0, 2, profile->created_at, sizeof(profile->created_at));
    PQclear(result);
    close_bad_connection(database);
    return DATABASE_OK;
}

int database_save_profile(struct Database *database, const char *user_id,
                          const char *full_name, const char *verified_phone) {
    static const char query[] =
        "INSERT INTO public.profiles (user_id, full_name, phone) "
        "VALUES ($1::uuid, $2, NULLIF($3, '')) "
        "ON CONFLICT (user_id) DO UPDATE SET "
        "full_name=EXCLUDED.full_name, "
        "phone=COALESCE(EXCLUDED.phone, public.profiles.phone)";
    const char *values[3] = {user_id, full_name, verified_phone};
    PGconn *connection;
    PGresult *result;
    if (database == NULL || user_id == NULL || full_name == NULL ||
        verified_phone == NULL || full_name[0] == '\0') return DATABASE_ERROR;
    connection = get_connection(database);
    if (connection == NULL) return DATABASE_ERROR;
    result = PQexecParams(connection, query, 3, NULL, values, NULL, NULL, 0);
    if (!query_succeeded(result)) {
        if (result != NULL) PQclear(result);
        close_bad_connection(database);
        return DATABASE_ERROR;
    }
    PQclear(result);
    close_bad_connection(database);
    return DATABASE_OK;
}

int database_create_claim(struct Database *database, const char *item_id,
                          const char *requester_id, const char *message,
                          const char *requester_contact,
                          char claim_id[UNILOST_CLAIM_ID_LEN]) {
    static const char query[] =
        "INSERT INTO public.claims (item_id, requester_id, message, "
        "requester_contact, status) "
        "SELECT i.id, $2::uuid, $3, $4, 'pending' "
        "FROM public.items i WHERE i.id::text=$1 AND i.status='open' "
        "AND i.user_id IS NOT NULL AND i.user_id<>$2::uuid "
        "ON CONFLICT (item_id, requester_id) DO NOTHING RETURNING id::text";
    const char *values[4] = {item_id, requester_id, message, requester_contact};
    PGconn *connection;
    PGresult *result;
    if (database == NULL || item_id == NULL || requester_id == NULL ||
        message == NULL || requester_contact == NULL || claim_id == NULL ||
        message[0] == '\0' || requester_contact[0] == '\0') return DATABASE_ERROR;
    connection = get_connection(database);
    if (connection == NULL) return DATABASE_ERROR;
    result = PQexecParams(connection, query, 4, NULL, values, NULL, NULL, 0);
    if (!query_succeeded(result)) {
        if (result != NULL) PQclear(result);
        close_bad_connection(database);
        return DATABASE_ERROR;
    }
    if (PQntuples(result) == 0) {
        PQclear(result);
        close_bad_connection(database);
        return DATABASE_CONFLICT;
    }
    copy_column(result, 0, 0, claim_id, UNILOST_CLAIM_ID_LEN);
    PQclear(result);
    close_bad_connection(database);
    return DATABASE_OK;
}

static void read_claim(PGresult *result, int row, struct Claim *claim,
                       int received) {
    memset(claim, 0, sizeof(*claim));
    copy_column(result, row, 0, claim->id, sizeof(claim->id));
    copy_column(result, row, 1, claim->item_id, sizeof(claim->item_id));
    copy_column(result, row, 2, claim->item_name, sizeof(claim->item_name));
    copy_column(result, row, 3, claim->report_type, sizeof(claim->report_type));
    copy_column(result, row, 4, claim->category, sizeof(claim->category));
    copy_column(result, row, 5, claim->message, sizeof(claim->message));
    if (received)
        copy_column(result, row, 6, claim->requester_contact,
                    sizeof(claim->requester_contact));
    copy_column(result, row, 7, claim->status, sizeof(claim->status));
    copy_column(result, row, 8, claim->created_at, sizeof(claim->created_at));
}

int database_list_claims(struct Database *database, const char *user_id,
                         int received, struct Claim claims[], size_t capacity,
                         size_t *count) {
    static const char received_query[] =
        "SELECT c.id::text, i.id::text, i.item_name, i.report_type, i.category, "
        "c.message, c.requester_contact, c.status, "
        "TO_CHAR(c.created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS') "
        "FROM public.claims c JOIN public.items i ON i.id=c.item_id "
        "WHERE i.user_id::text=$1 ORDER BY c.created_at DESC LIMIT 100";
    static const char sent_query[] =
        "SELECT c.id::text, i.id::text, i.item_name, i.report_type, i.category, "
        "c.message, ''::text, c.status, "
        "TO_CHAR(c.created_at, 'YYYY-MM-DD\"T\"HH24:MI:SS') "
        "FROM public.claims c JOIN public.items i ON i.id=c.item_id "
        "WHERE c.requester_id::text=$1 ORDER BY c.created_at DESC LIMIT 100";
    const char *values[1] = {user_id};
    PGconn *connection;
    PGresult *result;
    size_t row_count;
    int row;
    if (count != NULL) *count = 0;
    if (database == NULL || user_id == NULL || claims == NULL || count == NULL)
        return DATABASE_ERROR;
    connection = get_connection(database);
    if (connection == NULL) return DATABASE_ERROR;
    result = PQexecParams(connection, received ? received_query : sent_query,
                          1, NULL, values, NULL, NULL, 0);
    if (!query_succeeded(result)) {
        if (result != NULL) PQclear(result);
        close_bad_connection(database);
        return DATABASE_ERROR;
    }
    row_count = (size_t) PQntuples(result);
    if (row_count > capacity) row_count = capacity;
    for (row = 0; row < (int) row_count; row++)
        read_claim(result, row, &claims[row], received);
    *count = row_count;
    PQclear(result);
    close_bad_connection(database);
    return DATABASE_OK;
}

int database_update_claim(struct Database *database, const char *claim_id,
                          const char *owner_id, const char *status) {
    static const char query[] =
        "UPDATE public.claims c SET status=$1, updated_at=now() "
        "FROM public.items i WHERE c.item_id=i.id AND c.id::text=$2 "
        "AND i.user_id::text=$3 AND c.status='pending' RETURNING c.id";
    const char *values[3] = {status, claim_id, owner_id};
    PGconn *connection;
    PGresult *result;
    if (database == NULL || claim_id == NULL || owner_id == NULL ||
        status == NULL || (strcmp(status, "accepted") != 0 &&
                           strcmp(status, "declined") != 0)) return DATABASE_ERROR;
    connection = get_connection(database);
    if (connection == NULL) return DATABASE_ERROR;
    result = PQexecParams(connection, query, 3, NULL, values, NULL, NULL, 0);
    if (!query_succeeded(result)) {
        if (result != NULL) PQclear(result);
        close_bad_connection(database);
        return DATABASE_ERROR;
    }
    if (PQntuples(result) == 0) {
        PQclear(result);
        close_bad_connection(database);
        return DATABASE_NOT_FOUND;
    }
    PQclear(result);
    close_bad_connection(database);
    return DATABASE_OK;
}
