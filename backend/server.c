#include "database.h"
#include "auth.h"
#include "matcher.h"
#include "mongoose.h"

#include <ctype.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define MAX_REQUEST_BODY 16384
#define MAX_ITEMS 200

static const char JSON_HEADERS[] =
    "Content-Type: application/json; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "X-Content-Type-Options: nosniff\r\n";

static volatile sig_atomic_t server_running = 1;

struct ServerContext {
    struct Database database;
};

static void stop_server(int signal_number) {
    (void) signal_number;
    server_running = 0;
}

static void send_error(struct mg_connection *connection, int status,
                       const char *code, const char *message) {
    mg_http_reply(connection, status, JSON_HEADERS,
                  "{\"error\":%m,\"message\":%m}\n",
                  MG_ESC(code), MG_ESC(message));
}

static int method_is(struct mg_http_message *request, const char *method) {
    return mg_match(request->method, mg_str(method), NULL);
}

static int has_non_space(const char *value) {
    size_t i;
    for (i = 0; value[i] != '\0'; i++) {
        if (!isspace((unsigned char) value[i])) return 1;
    }
    return 0;
}

static int contains_control_character(const char *value, size_t length) {
    size_t i;
    for (i = 0; i < length; i++) {
        unsigned char character = (unsigned char) value[i];
        if (character < 32 || character == 127) return 1;
    }
    return 0;
}

/* Mongoose 7.23 parses and unescapes JSON strings into bounded destinations. */
static int read_required_string(struct mg_str json, const char *path,
                                char *destination, size_t destination_size) {
    size_t length;
    if (destination_size == 0) return 0;
    destination[0] = '\0';
    length = mg_json_unescape(json, path, destination, destination_size);
    if (length == 0 || length >= destination_size ||
        memchr(destination, '\0', length) != NULL ||
        contains_control_character(destination, length) ||
        !has_non_space(destination)) {
        destination[0] = '\0';
        return 0;
    }
    destination[length] = '\0';
    return 1;
}

static int read_optional_string(struct mg_str json, const char *path,
                                char *destination, size_t destination_size) {
    int raw_length = 0;
    int offset;
    size_t length;
    if (destination_size == 0) return 0;
    destination[0] = '\0';
    offset = mg_json_get(json, path, &raw_length);
    if (offset < 0) return 1;
    if (raw_length == 2 && json.buf[offset] == '"' &&
        json.buf[offset + 1] == '"') return 1;
    length = mg_json_unescape(json, path, destination, destination_size);
    if (length == 0 || length >= destination_size ||
        memchr(destination, '\0', length) != NULL ||
        contains_control_character(destination, length)) {
        destination[0] = '\0';
        return 0;
    }
    destination[length] = '\0';
    return 1;
}

static int photo_path_is_valid_for_owner(const char *path, const char *owner_id) {
    size_t owner_length, filename_length, i;
    const char *filename;
    size_t stem_length;
    if (path == NULL || owner_id == NULL) return 0;
    owner_length = strlen(owner_id);
    if (owner_length == 0 || strncmp(path, owner_id, owner_length) != 0 ||
        path[owner_length] != '/') return 0;
    filename = path + owner_length + 1;
    filename_length = strlen(filename);
    if (strchr(filename, '/') != NULL || strchr(filename, '\\') != NULL) return 0;
    if (filename_length > 4 && strcmp(filename + filename_length - 4, ".jpg") == 0)
        stem_length = filename_length - 4;
    else if (filename_length > 4 && strcmp(filename + filename_length - 4, ".png") == 0)
        stem_length = filename_length - 4;
    else if (filename_length > 5 && strcmp(filename + filename_length - 5, ".webp") == 0)
        stem_length = filename_length - 5;
    else return 0;
    if (stem_length != 36) return 0;
    for (i = 0; i < stem_length; i++) {
        unsigned char ch = (unsigned char) filename[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (ch != '-') return 0;
        } else if (!isxdigit(ch)) return 0;
    }
    return 1;
}

static int is_json_object(struct mg_str json) {
    int object_length = 0;
    int object_offset = mg_json_get(json, "$", &object_length);
    size_t index;
    if (object_offset < 0 || object_length < 2 || json.buf[object_offset] != '{')
        return 0;
    index = (size_t) object_offset + (size_t) object_length;
    while (index < json.len && isspace((unsigned char) json.buf[index])) index++;
    return index == json.len;
}

static int category_is_allowed(const char *category) {
    static const char *categories[] = {
        "ID Card", "Wallet", "Electronics", "Bottle", "Keys", "Bag",
        "Stationery", "Clothing", "Other"
    };
    size_t i;
    for (i = 0; i < sizeof(categories) / sizeof(categories[0]); i++) {
        if (mg_strcasecmp(mg_str(category), mg_str(categories[i])) == 0) return 1;
    }
    return 0;
}

static int date_is_not_future(const char *date) {
    time_t now = time(NULL);
    struct tm local_time;
    char today[UNILOST_DATE_LEN];
    if (localtime_r(&now, &local_time) == NULL ||
        strftime(today, sizeof(today), "%Y-%m-%d", &local_time) == 0) return 0;
    return strcmp(date, today) <= 0;
}

static int parse_report(struct mg_http_message *request, struct Item *item,
                        char contact_name[101], char contact_info[181],
                        const char *owner_id) {
    struct mg_str body = request->body;
    char type[8];
    if (request->body.len > MAX_REQUEST_BODY || !is_json_object(body)) return 0;
    memset(item, 0, sizeof(*item));
    if (!read_required_string(body, "$.report_type", type, sizeof(type)) ||
        !read_required_string(body, "$.item_name", item->item_name,
                              sizeof(item->item_name)) ||
        !read_required_string(body, "$.category", item->category,
                              sizeof(item->category)) ||
        !read_required_string(body, "$.description", item->description,
                              sizeof(item->description)) ||
        !read_required_string(body, "$.location", item->location,
                              sizeof(item->location)) ||
        !read_required_string(body, "$.item_date", item->item_date,
                              sizeof(item->item_date)) ||
        !read_required_string(body, "$.contact_name", contact_name, 101) ||
        !read_required_string(body, "$.contact_info", contact_info, 181) ||
        !read_optional_string(body, "$.photo_path", item->photo_path,
                              sizeof(item->photo_path))) return 0;

    if (strcmp(type, "lost") != 0 && strcmp(type, "found") != 0) return 0;
    if (!category_is_allowed(item->category) ||
        !unilost_valid_date(item->item_date) ||
        !date_is_not_future(item->item_date)) return 0;
    if (strlen(contact_name) > 100 || strlen(contact_info) > 180) return 0;
    if (item->photo_path[0] != '\0' &&
        !photo_path_is_valid_for_owner(item->photo_path, owner_id)) return 0;
    snprintf(item->report_type, sizeof(item->report_type), "%s", type);
    return 1;
}

static void append_json(struct mg_iobuf *output, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    mg_vxprintf(mg_pfn_iobuf, output, format, &arguments);
    va_end(arguments);
}

static void append_item_json(struct mg_iobuf *output, const struct Item *item) {
    append_json(output,
        "{\"id\":%m,\"report_type\":%m,\"item_name\":%m,"
        "\"category\":%m,\"description\":%m,\"location\":%m,"
        "\"item_date\":%m,\"status\":%m,\"created_at\":%m,"
        "\"photo_path\":%m,\"is_owner\":%s,\"claimable\":%s}",
        MG_ESC(item->id), MG_ESC(item->report_type), MG_ESC(item->item_name),
        MG_ESC(item->category), MG_ESC(item->description), MG_ESC(item->location),
        MG_ESC(item->item_date), MG_ESC(item->status), MG_ESC(item->created_at),
        MG_ESC(item->photo_path),
        item->is_owner ? "true" : "false",
        item->is_claimable ? "true" : "false");
}

static int authenticate_request(struct mg_connection *connection,
                                struct mg_http_message *request,
                                struct AuthUser *user) {
    enum AuthResult result = auth_verify_request(request, user);
    if (result == AUTH_OK) return 1;
    if (result == AUTH_MISSING || result == AUTH_INVALID) {
        send_error(connection, 401, "authentication_required",
                   "Sign in with a valid UniLost account to continue.");
    } else {
        send_error(connection, 503, "authentication_unavailable",
                   "Supabase Auth could not verify this session. Check SUPABASE_URL and SUPABASE_ANON_KEY.");
    }
    return 0;
}

static void reply_with_buffer(struct mg_connection *connection, int status,
                              struct mg_iobuf *output) {
    mg_iobuf_add(output, output->len, "", 1);
    mg_http_reply(connection, status, JSON_HEADERS, "%s",
                  output->buf == NULL ? "{}" : (const char *) output->buf);
    mg_iobuf_free(output);
}

static int query_parameter(struct mg_http_message *request, const char *name,
                           char *value, size_t value_size) {
    int length;
    value[0] = '\0';
    length = mg_http_get_var(&request->query, name, value, value_size);
    if (length < 0) return 0;
    if ((size_t) length >= value_size) return -1;
    value[length] = '\0';
    return length > 0 ? 1 : -1;
}

static int category_filter_is_valid(const char *category) {
    return category[0] == '\0' || category_is_allowed(category);
}

static int status_filter_is_valid(const char *status) {
    return status[0] == '\0' || strcmp(status, "open") == 0 ||
           strcmp(status, "claimed") == 0 || strcmp(status, "closed") == 0;
}

static int integer_parameter(struct mg_http_message *request, const char *name,
                             int default_value, int maximum, int *value) {
    char text[24];
    int result = query_parameter(request, name, text, sizeof(text));
    long parsed = 0;
    size_t i;
    if (result == 0) {
        *value = default_value;
        return 1;
    }
    if (result < 0 || text[0] == '\0') return 0;
    for (i = 0; text[i] != '\0'; i++) {
        int digit;
        if (!isdigit((unsigned char) text[i])) return 0;
        digit = text[i] - '0';
        if (parsed > (maximum - digit) / 10) return 0;
        parsed = parsed * 10 + digit;
    }
    *value = (int) parsed;
    return 1;
}

static void handle_health(struct mg_connection *connection,
                          struct ServerContext *context) {
    if (database_is_ready(&context->database)) {
        mg_http_reply(connection, 200, JSON_HEADERS,
                      "{\"server\":\"ok\",\"database\":\"connected\"}\n");
    } else {
        mg_http_reply(connection, 503, JSON_HEADERS,
                      "{\"server\":\"ok\",\"database\":\"unavailable\","
                      "\"message\":\"Set DATABASE_URL and apply database/schema.sql.\"}\n");
    }
}

static void handle_public_config(struct mg_connection *connection) {
    const char *url = getenv("SUPABASE_URL");
    const char *key = getenv("SUPABASE_ANON_KEY");
    if (url == NULL) url = "";
    if (!auth_public_key_is_safe(key)) key = "";
    mg_http_reply(connection, 200, JSON_HEADERS,
                  "{\"supabase_url\":%m,\"supabase_anon_key\":%m}\n",
                  MG_ESC(url), MG_ESC(key));
}

static void handle_summary(struct mg_connection *connection,
                          struct mg_http_message *request,
                          struct ServerContext *context) {
    struct AuthUser user;
    struct DashboardSummary summary = {0};
    if (!authenticate_request(connection, request, &user)) return;
    if (database_get_dashboard_summary(&context->database, user.id, &summary) != DATABASE_OK) {
        send_error(connection, 503, "database_unavailable",
                   "Your dashboard totals are unavailable until the database connects.");
        return;
    }
    mg_http_reply(connection, 200, JSON_HEADERS,
                  "{\"my_lost\":%ld,\"my_found\":%ld,\"my_matches\":%ld}\n",
                  summary.lost_reports, summary.found_reports, summary.matches);
}

static void handle_list_items(struct mg_connection *connection,
                              struct mg_http_message *request,
                              struct ServerContext *context) {
    char report_type[16], category[UNILOST_CATEGORY_LEN];
    char search[201], location[UNILOST_LOCATION_LEN], mine[8];
    char status_filter[16], date_from[UNILOST_DATE_LEN], date_to[UNILOST_DATE_LEN];
    char sort[16];
    struct AuthUser user;
    struct Item items[MAX_ITEMS];
    struct mg_iobuf output = {0};
    size_t count = 0, page_count, total = 0, i;
    int type_result, category_result, search_result, location_result, mine_result;
    int status_result, from_result, to_result, sort_result;
    int limit = 50, offset = 0, has_more;
    int result;

    type_result = query_parameter(request, "type", report_type, sizeof(report_type));
    category_result = query_parameter(request, "category", category, sizeof(category));
    search_result = query_parameter(request, "search", search, sizeof(search));
    location_result = query_parameter(request, "location", location, sizeof(location));
    mine_result = query_parameter(request, "mine", mine, sizeof(mine));
    status_result = query_parameter(request, "status", status_filter, sizeof(status_filter));
    from_result = query_parameter(request, "date_from", date_from, sizeof(date_from));
    to_result = query_parameter(request, "date_to", date_to, sizeof(date_to));
    sort_result = query_parameter(request, "sort", sort, sizeof(sort));
    if (type_result < 0 || category_result < 0 || search_result < 0 ||
        location_result < 0 || mine_result < 0 || status_result < 0 ||
        from_result < 0 || to_result < 0 || sort_result < 0 ||
        (mine_result == 1 && strcmp(mine, "true") != 0) ||
        (report_type[0] != '\0' && strcmp(report_type, "lost") != 0 &&
         strcmp(report_type, "found") != 0) || !category_filter_is_valid(category) ||
        !status_filter_is_valid(status_filter) ||
        (date_from[0] != '\0' && !unilost_valid_date(date_from)) ||
        (date_to[0] != '\0' && !unilost_valid_date(date_to)) ||
        (date_from[0] != '\0' && date_to[0] != '\0' &&
         strcmp(date_from, date_to) > 0) ||
        (sort[0] != '\0' && strcmp(sort, "newest") != 0 &&
         strcmp(sort, "oldest") != 0 && strcmp(sort, "name") != 0 &&
         strcmp(sort, "date") != 0) ||
        !integer_parameter(request, "limit", 50, 50, &limit) || limit < 1 ||
        !integer_parameter(request, "offset", 0, 100000, &offset)) {
        send_error(connection, 400, "invalid_filter",
                   "Check the report type, status, date range, sort, limit, and offset filters.");
        return;
    }
    if (!authenticate_request(connection, request, &user)) return;
    result = database_list_items(&context->database,
        report_type[0] ? report_type : NULL,
        category[0] ? category : NULL,
        search[0] ? search : NULL,
        location[0] ? location : NULL,
        mine_result == 1 ? user.id : NULL,
        user.id,
        status_filter[0] ? status_filter : NULL,
        date_from[0] ? date_from : NULL,
        date_to[0] ? date_to : NULL,
        sort[0] ? sort : "newest",
        limit + 1, offset, items, MAX_ITEMS, &count, &total);
    if (result != DATABASE_OK) {
        send_error(connection, 503, "database_unavailable",
                   "Check the database connection and apply database/schema.sql.");
        return;
    }
    if (!mg_iobuf_init(&output, 2048, 256)) {
        send_error(connection, 500, "response_error", "Could not prepare the response.");
        return;
    }
    has_more = count > (size_t) limit;
    page_count = has_more ? (size_t) limit : count;
    append_json(&output, "{\"items\":[");
    for (i = 0; i < page_count; i++) {
        if (i > 0) append_json(&output, ",");
        append_item_json(&output, &items[i]);
    }
    append_json(&output,
        "],\"count\":%zu,\"total\":%zu,\"has_more\":%s,"
        "\"limit\":%d,\"offset\":%d}\n",
        page_count, total, has_more ? "true" : "false", limit, offset);
    reply_with_buffer(connection, 200, &output);
}

static int extract_item_id(struct mg_str uri, char id[UNILOST_ID_LEN]) {
    static const char prefix[] = "/api/items/";
    size_t prefix_length = sizeof(prefix) - 1, i, length;
    if (uri.len <= prefix_length || memcmp(uri.buf, prefix, prefix_length) != 0)
        return 0;
    length = uri.len - prefix_length;
    if (length == 0 || length >= UNILOST_ID_LEN) return 0;
    for (i = 0; i < length; i++) {
        unsigned char ch = (unsigned char) uri.buf[prefix_length + i];
        if (!isxdigit(ch) && ch != '-') return 0;
    }
    memcpy(id, uri.buf + prefix_length, length);
    id[length] = '\0';
    return 1;
}

static void handle_get_item(struct mg_connection *connection,
                            struct mg_http_message *request,
                            struct ServerContext *context) {
    char id[UNILOST_ID_LEN];
    struct AuthUser user;
    struct Item item;
    int result;
    if (!authenticate_request(connection, request, &user)) return;
    if (!extract_item_id(request->uri, id)) {
        send_error(connection, 400, "invalid_item_id", "The item ID is invalid.");
        return;
    }
    result = database_get_item(&context->database, id, user.id, &item);
    if (result == DATABASE_NOT_FOUND) {
        send_error(connection, 404, "item_not_found", "No item with that ID was found.");
    } else if (result != DATABASE_OK) {
        send_error(connection, 503, "database_unavailable", "The database could not load this report.");
    } else {
        struct mg_iobuf output = {0};
        if (!mg_iobuf_init(&output, 512, 256)) {
            send_error(connection, 500, "response_error", "Could not prepare the response.");
            return;
        }
        append_item_json(&output, &item);
        append_json(&output, "\n");
        reply_with_buffer(connection, 200, &output);
    }
}

static void handle_create_item(struct mg_connection *connection,
                               struct mg_http_message *request,
                               struct ServerContext *context) {
    struct Item item;
    char contact_name[101] = {0};
    char contact_info[181] = {0};
    char created_id[UNILOST_ID_LEN] = {0};
    struct AuthUser user;
    int result;

    if (!authenticate_request(connection, request, &user)) return;

    if (request->body.len > MAX_REQUEST_BODY) {
        send_error(connection, 413, "body_too_large", "The request body limit is 16 KB.");
        return;
    }
    if (!parse_report(request, &item, contact_name, contact_info, user.id)) {
        send_error(connection, 400, "invalid_report",
                   "Provide all fields, a supported category, and a valid YYYY-MM-DD date.");
        return;
    }
    result = database_create_item(&context->database, &item, contact_name,
                                  contact_info, user.id, created_id);
    if (result != DATABASE_OK) {
        send_error(connection, 503, "database_unavailable",
                   "Check the database connection and apply database/schema.sql.");
        return;
    }
    mg_http_reply(connection, 201, JSON_HEADERS,
                  "{\"message\":\"Report saved.\",\"id\":%m,"
                  "\"status\":\"open\"}\n", MG_ESC(created_id));
}

static int extract_claim_item_id(struct mg_str uri, char id[UNILOST_ID_LEN]) {
    static const char prefix[] = "/api/items/";
    static const char suffix[] = "/claims";
    size_t prefix_length = sizeof(prefix) - 1, suffix_length = sizeof(suffix) - 1;
    size_t length, i;
    if (uri.len <= prefix_length + suffix_length ||
        memcmp(uri.buf, prefix, prefix_length) != 0 ||
        memcmp(uri.buf + uri.len - suffix_length, suffix, suffix_length) != 0)
        return 0;
    length = uri.len - prefix_length - suffix_length;
    if (length == 0 || length >= UNILOST_ID_LEN) return 0;
    for (i = 0; i < length; i++) {
        unsigned char ch = (unsigned char) uri.buf[prefix_length + i];
        if (!isxdigit(ch) && ch != '-') return 0;
    }
    memcpy(id, uri.buf + prefix_length, length);
    id[length] = '\0';
    return 1;
}

static void handle_create_claim(struct mg_connection *connection,
                                struct mg_http_message *request,
                                struct ServerContext *context) {
    char item_id[UNILOST_ID_LEN], message[UNILOST_CLAIM_MESSAGE_LEN];
    char claim_id[UNILOST_CLAIM_ID_LEN] = {0};
    char requester_contact[AUTH_TEXT_LEN];
    struct AuthUser user;
    int result;
    if (!authenticate_request(connection, request, &user)) return;
    if (!extract_claim_item_id(request->uri, item_id)) {
        send_error(connection, 400, "invalid_item_id", "The item ID is invalid.");
        return;
    }
    if (request->body.len > MAX_REQUEST_BODY || !is_json_object(request->body) ||
        !read_required_string(request->body, "$.message", message,
                              sizeof(message)) || strlen(message) > 500) {
        send_error(connection, 400, "invalid_claim",
                   "Write a short message up to 500 characters.");
        return;
    }
    if (user.email_confirmed && user.email[0] != '\0') {
        snprintf(requester_contact, sizeof(requester_contact), "%s", user.email);
    } else if (user.phone_confirmed && user.phone[0] != '\0') {
        snprintf(requester_contact, sizeof(requester_contact), "%s", user.phone);
    } else {
        send_error(connection, 403, "confirmed_contact_required",
                   "Confirm an email address or phone number in Profile before requesting a private handoff.");
        return;
    }
    result = database_create_claim(&context->database, item_id, user.id, message,
                                   requester_contact, claim_id);
    if (result == DATABASE_CONFLICT) {
        send_error(connection, 409, "claim_not_created",
                   "This report is not open, has no account owner, belongs to you, or already has your request.");
    } else if (result != DATABASE_OK) {
        send_error(connection, 503, "claim_unavailable",
                   "The private request could not be saved. Apply the Auth and ownership migration.");
    } else {
        mg_http_reply(connection, 201, JSON_HEADERS,
                      "{\"message\":\"Private handoff request sent to the report owner.\","
                      "\"status\":\"pending\"}\n");
    }
}

static void append_claim_json(struct mg_iobuf *output, const struct Claim *claim,
                              int include_contact) {
    append_json(output,
        "{\"id\":%m,\"item_id\":%m,\"item_name\":%m,"
        "\"report_type\":%m,\"category\":%m,\"message\":%m,"
        "\"status\":%m,\"created_at\":%m",
        MG_ESC(claim->id), MG_ESC(claim->item_id), MG_ESC(claim->item_name),
        MG_ESC(claim->report_type), MG_ESC(claim->category), MG_ESC(claim->message),
        MG_ESC(claim->status), MG_ESC(claim->created_at));
    if (include_contact)
        append_json(output, ",\"requester_contact\":%m",
                    MG_ESC(claim->requester_contact));
    append_json(output, "}");
}

static void handle_list_claims(struct mg_connection *connection,
                               struct mg_http_message *request,
                               struct ServerContext *context) {
    char scope[16];
    struct AuthUser user;
    struct Claim claims[100];
    struct mg_iobuf output = {0};
    size_t count = 0, i;
    int received, result;
    if (!authenticate_request(connection, request, &user)) return;
    if (query_parameter(request, "scope", scope, sizeof(scope)) != 1 ||
        (strcmp(scope, "received") != 0 && strcmp(scope, "sent") != 0)) {
        send_error(connection, 400, "invalid_claim_scope",
                   "Use scope=received or scope=sent.");
        return;
    }
    received = strcmp(scope, "received") == 0;
    result = database_list_claims(&context->database, user.id, received,
                                  claims, 100, &count);
    if (result != DATABASE_OK) {
        send_error(connection, 503, "claims_unavailable",
                   "Private handoff requests are unavailable. Apply the Auth and ownership migration.");
        return;
    }
    if (!mg_iobuf_init(&output, 1024, 256)) {
        send_error(connection, 500, "response_error", "Could not prepare the response.");
        return;
    }
    append_json(&output, "{\"claims\":[");
    for (i = 0; i < count; i++) {
        if (i > 0) append_json(&output, ",");
        append_claim_json(&output, &claims[i], received);
    }
    append_json(&output, "],\"count\":%zu}\n", count);
    reply_with_buffer(connection, 200, &output);
}

static int extract_claim_id(struct mg_str uri, char id[UNILOST_CLAIM_ID_LEN]) {
    static const char prefix[] = "/api/claims/";
    size_t prefix_length = sizeof(prefix) - 1, length, i;
    if (uri.len <= prefix_length || memcmp(uri.buf, prefix, prefix_length) != 0)
        return 0;
    length = uri.len - prefix_length;
    if (length == 0 || length >= UNILOST_CLAIM_ID_LEN) return 0;
    for (i = 0; i < length; i++) {
        unsigned char ch = (unsigned char) uri.buf[prefix_length + i];
        if (!isxdigit(ch) && ch != '-') return 0;
    }
    memcpy(id, uri.buf + prefix_length, length);
    id[length] = '\0';
    return 1;
}

static void handle_update_claim(struct mg_connection *connection,
                                struct mg_http_message *request,
                                struct ServerContext *context) {
    char id[UNILOST_CLAIM_ID_LEN], status[16];
    struct AuthUser user;
    int result;
    if (!authenticate_request(connection, request, &user)) return;
    if (!extract_claim_id(request->uri, id)) {
        send_error(connection, 400, "invalid_claim_id", "The request ID is invalid.");
        return;
    }
    if (request->body.len > MAX_REQUEST_BODY || !is_json_object(request->body) ||
        !read_required_string(request->body, "$.status", status, sizeof(status)) ||
        (strcmp(status, "accepted") != 0 && strcmp(status, "declined") != 0)) {
        send_error(connection, 400, "invalid_claim_status",
                   "Request status must be accepted or declined.");
        return;
    }
    result = database_update_claim(&context->database, id, user.id, status);
    if (result == DATABASE_NOT_FOUND) {
        send_error(connection, 404, "claim_not_found", "No pending request for your report was found.");
    } else if (result != DATABASE_OK) {
        send_error(connection, 503, "claim_update_failed", "The request could not be updated.");
    } else {
        mg_http_reply(connection, 200, JSON_HEADERS,
                      "{\"message\":\"Private handoff request updated.\",\"status\":%m}\n",
                      MG_ESC(status));
    }
}

static void append_match_json(struct mg_iobuf *output, const struct Match *match) {
    append_json(output, "{\"found_item\":");
    append_item_json(output, &match->found);
    append_json(output,
        ",\"score\":%d,\"strength\":%m,\"factors\":{"
        "\"category\":%d,\"name\":%d,\"description\":%d,"
        "\"location\":%d,\"date\":%d}}",
        match->score.total, MG_ESC(unilost_match_strength(match->score.total)),
        match->score.category, match->score.name, match->score.description,
        match->score.location, match->score.date);
}

static void handle_matches(struct mg_connection *connection,
                           struct mg_http_message *request,
                           struct ServerContext *context) {
    char lost_id[UNILOST_ID_LEN];
    struct Item lost_item;
    struct Item found_items[MAX_ITEMS];
    struct Match matches[MAX_ITEMS];
    struct mg_iobuf output = {0};
    size_t found_count = 0, match_count = 0, i;
    struct AuthUser user;
    int result;

    if (!authenticate_request(connection, request, &user)) return;

    if (query_parameter(request, "lost_id", lost_id, sizeof(lost_id)) != 1) {
        send_error(connection, 400, "missing_lost_id",
                   "Choose an open lost report using the lost_id query parameter.");
        return;
    }
    result = database_get_open_lost(&context->database, lost_id, user.id,
                                    &lost_item);
    if (result == DATABASE_NOT_FOUND) {
        send_error(connection, 404, "lost_report_not_found",
                   "No open lost report with that ID was found.");
        return;
    }
    if (result != DATABASE_OK ||
        database_get_open_found(&context->database, found_items, MAX_ITEMS,
                                &found_count) != DATABASE_OK) {
        send_error(connection, 503, "database_unavailable",
                   "Check the database connection and apply database/schema.sql.");
        return;
    }

    for (i = 0; i < found_count; i++) {
        struct Match candidate = {0};
        if (strcmp(lost_item.id, found_items[i].id) == 0) continue;
        candidate.lost = lost_item;
        candidate.found = found_items[i];
        unilost_calculate_score(&candidate.lost, &candidate.found,
                                &candidate.score);
        if (candidate.score.total < 40 ||
            unilost_match_pair_exists(matches, match_count,
                                      candidate.lost.id, candidate.found.id)) continue;
        if (database_store_match(&context->database, candidate.lost.id,
                                 candidate.found.id, candidate.score.total) != DATABASE_OK) {
            send_error(connection, 503, "match_storage_failed",
                       "The database could not save a potential match. Apply database/schema.sql.");
            return;
        }
        matches[match_count++] = candidate;
    }
    unilost_sort_matches(matches, match_count);

    if (!mg_iobuf_init(&output, 2048, 256)) {
        send_error(connection, 500, "response_error", "Could not prepare the response.");
        return;
    }
    append_json(&output, "{\"lost_item\":");
    append_item_json(&output, &lost_item);
    append_json(&output, ",\"matches\":[");
    for (i = 0; i < match_count; i++) {
        if (i > 0) append_json(&output, ",");
        append_match_json(&output, &matches[i]);
    }
    append_json(&output, "],\"count\":%zu}\n", match_count);
    reply_with_buffer(connection, 200, &output);
}

static int extract_status_item_id(struct mg_str uri, char id[UNILOST_ID_LEN]) {
    static const char prefix[] = "/api/items/";
    static const char suffix[] = "/status";
    size_t prefix_length = sizeof(prefix) - 1;
    size_t suffix_length = sizeof(suffix) - 1;
    size_t id_length, i;

    if (uri.len <= prefix_length + suffix_length ||
        memcmp(uri.buf, prefix, prefix_length) != 0 ||
        memcmp(uri.buf + uri.len - suffix_length, suffix, suffix_length) != 0)
        return 0;
    id_length = uri.len - prefix_length - suffix_length;
    if (id_length == 0 || id_length >= UNILOST_ID_LEN) return 0;
    for (i = 0; i < id_length; i++) {
        unsigned char character = (unsigned char) uri.buf[prefix_length + i];
        if (!isalnum(character) && character != '-') return 0;
    }
    memcpy(id, uri.buf + prefix_length, id_length);
    id[id_length] = '\0';
    return 1;
}

static void handle_item_status(struct mg_connection *connection,
                               struct mg_http_message *request,
                               struct ServerContext *context) {
    char id[UNILOST_ID_LEN];
    char status[16];
    struct AuthUser user;
    int result;
    if (!authenticate_request(connection, request, &user)) return;
    if (!extract_status_item_id(request->uri, id)) {
        send_error(connection, 400, "invalid_item_id", "The item ID is invalid.");
        return;
    }
    if (request->body.len > MAX_REQUEST_BODY || !is_json_object(request->body) ||
        !read_required_string(request->body, "$.status", status, sizeof(status)) ||
        (strcmp(status, "claimed") != 0 && strcmp(status, "closed") != 0)) {
        send_error(connection, 400, "invalid_status",
                   "Status must be claimed or closed.");
        return;
    }
    result = database_update_item_status(&context->database, id, user.id, status);
    if (result == DATABASE_NOT_FOUND) {
        send_error(connection, 404, "item_not_found", "No item with that ID was found.");
    } else if (result == DATABASE_CONFLICT) {
        send_error(connection, 409, "invalid_transition",
                   "Only an open item can be marked claimed or closed.");
    } else if (result != DATABASE_OK) {
        send_error(connection, 503, "database_unavailable",
                   "Check the database connection and apply database/schema.sql.");
    } else {
        mg_http_reply(connection, 200, JSON_HEADERS,
                      "{\"message\":\"Item status updated.\",\"status\":%m}\n",
                      MG_ESC(status));
    }
}

static void handle_profile(struct mg_connection *connection,
                           struct mg_http_message *request,
                           struct ServerContext *context, int update) {
    struct AuthUser user;
    struct Profile profile;
    int result;
    if (!authenticate_request(connection, request, &user)) return;
    if (update) {
        char full_name[UNILOST_PROFILE_NAME_LEN];
        if (request->body.len > MAX_REQUEST_BODY ||
            !is_json_object(request->body) ||
            !read_required_string(request->body, "$.full_name", full_name,
                                  sizeof(full_name)) || strlen(full_name) > 100) {
            send_error(connection, 400, "invalid_profile",
                       "Provide a valid full name up to 100 characters.");
            return;
        }
        result = database_save_profile(&context->database, user.id, full_name,
            user.phone_confirmed ? user.phone : "");
        if (result != DATABASE_OK) {
            send_error(connection, 503, "profile_save_failed",
                       "The profile could not be saved. Apply the profile migration and check the database.");
            return;
        }
    }
    result = database_get_profile(&context->database, user.id, &profile);
    if (result == DATABASE_NOT_FOUND) {
        const char *name = user.full_name[0] != '\0' ? user.full_name : "UniLost member";
        if (database_save_profile(&context->database, user.id, name,
                user.phone_confirmed ? user.phone : "") != DATABASE_OK ||
            database_get_profile(&context->database, user.id, &profile) != DATABASE_OK) {
            send_error(connection, 503, "profile_unavailable",
                       "The profile could not be loaded. Apply the profile migration.");
            return;
        }
    } else if (result != DATABASE_OK) {
        send_error(connection, 503, "profile_unavailable",
                   "The profile could not be loaded. Apply the profile migration.");
        return;
    }
    mg_http_reply(connection, 200, JSON_HEADERS,
        "{\"profile\":{\"full_name\":%m,\"email\":%m,\"phone\":%m,"
        "\"contact_email\":%m,\"contact_phone\":%m,\"created_at\":%m,"
        "\"email_confirmed\":%s,\"phone_confirmed\":%s}}\n",
        MG_ESC(profile.full_name), MG_ESC(user.email), MG_ESC(user.phone),
        MG_ESC(user.contact_email), MG_ESC(user.contact_phone),
        MG_ESC(user.created_at), user.email_confirmed ? "true" : "false",
        user.phone_confirmed ? "true" : "false");
}

static void serve_frontend(struct mg_connection *connection,
                           struct mg_http_message *request) {
    static const struct mg_http_serve_opts options = {
        .root_dir = "frontend",
        .extra_headers = "X-Content-Type-Options: nosniff\r\n"
                         "Referrer-Policy: strict-origin-when-cross-origin\r\n"
                         "Content-Security-Policy: default-src 'self'; style-src 'self'; script-src 'self' https://esm.sh; connect-src 'self' https://*.supabase.co wss://*.supabase.co; img-src 'self' data: blob:; object-src 'none'; base-uri 'self'; frame-ancestors 'none'\r\n"
    };
    mg_http_serve_dir(connection, request, &options);
}

static void handle_request(struct mg_connection *connection, int event,
                           void *event_data) {
    struct ServerContext *context = connection->fn_data;
    struct mg_http_message *request = event_data;
    if (event != MG_EV_HTTP_MSG || request == NULL || context == NULL) return;

    if (mg_match(request->uri, mg_str("/api/health"), NULL)) {
        if (method_is(request, "GET")) handle_health(connection, context);
        else send_error(connection, 405, "method_not_allowed", "Use GET for this endpoint.");
    } else if (mg_match(request->uri, mg_str("/api/config"), NULL)) {
        if (method_is(request, "GET")) handle_public_config(connection);
        else send_error(connection, 405, "method_not_allowed", "Use GET for this endpoint.");
    } else if (mg_match(request->uri, mg_str("/api/summary"), NULL)) {
        if (method_is(request, "GET")) handle_summary(connection, request, context);
        else send_error(connection, 405, "method_not_allowed", "Use GET for this endpoint.");
    } else if (mg_match(request->uri, mg_str("/api/items"), NULL)) {
        if (method_is(request, "GET")) handle_list_items(connection, request, context);
        else if (method_is(request, "POST")) handle_create_item(connection, request, context);
        else send_error(connection, 405, "method_not_allowed", "Use GET or POST for this endpoint.");
    } else if (mg_match(request->uri, mg_str("/api/items/#/status"), NULL)) {
        if (method_is(request, "PATCH")) handle_item_status(connection, request, context);
        else send_error(connection, 405, "method_not_allowed", "Use PATCH for this endpoint.");
    } else if (mg_match(request->uri, mg_str("/api/items/#/claims"), NULL)) {
        if (method_is(request, "POST")) handle_create_claim(connection, request, context);
        else send_error(connection, 405, "method_not_allowed", "Use POST for this endpoint.");
    } else if (mg_match(request->uri, mg_str("/api/items/#"), NULL)) {
        if (method_is(request, "GET")) handle_get_item(connection, request, context);
        else send_error(connection, 405, "method_not_allowed", "Use GET for this endpoint.");
    } else if (mg_match(request->uri, mg_str("/api/matches"), NULL)) {
        if (method_is(request, "GET")) handle_matches(connection, request, context);
        else send_error(connection, 405, "method_not_allowed", "Use GET for this endpoint.");
    } else if (mg_match(request->uri, mg_str("/api/profile"), NULL)) {
        if (method_is(request, "GET")) handle_profile(connection, request, context, 0);
        else if (method_is(request, "PATCH")) handle_profile(connection, request, context, 1);
        else send_error(connection, 405, "method_not_allowed", "Use GET or PATCH for this endpoint.");
    } else if (mg_match(request->uri, mg_str("/api/claims"), NULL)) {
        if (method_is(request, "GET")) handle_list_claims(connection, request, context);
        else send_error(connection, 405, "method_not_allowed", "Use GET for this endpoint.");
    } else if (mg_match(request->uri, mg_str("/api/claims/#"), NULL)) {
        if (method_is(request, "PATCH")) handle_update_claim(connection, request, context);
        else send_error(connection, 405, "method_not_allowed", "Use PATCH for this endpoint.");
    } else if (method_is(request, "GET") || method_is(request, "HEAD")) {
        serve_frontend(connection, request);
    } else {
        send_error(connection, 404, "not_found", "The requested endpoint was not found.");
    }
}

int main(void) {
    struct mg_mgr manager;
    struct ServerContext context;
    const char *address = "http://127.0.0.1:8000";

    database_init(&context.database);
    mg_log_set(MG_LL_ERROR);
    signal(SIGINT, stop_server);
    signal(SIGTERM, stop_server);
    mg_mgr_init(&manager);
    if (mg_http_listen(&manager, address, handle_request, &context) == NULL) {
        fprintf(stderr, "Could not start UniLost on %s.\n", address);
        mg_mgr_free(&manager);
        database_close(&context.database);
        return 1;
    }

    printf("UniLost is running at %s\n", address);
    printf("Database credentials are read from DATABASE_URL and are never logged.\n");
    printf("Press Ctrl+C to stop the server.\n");
    while (server_running) mg_mgr_poll(&manager, 250);

    mg_mgr_free(&manager);
    database_close(&context.database);
    return 0;
}
