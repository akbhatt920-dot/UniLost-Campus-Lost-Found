#include "auth.h"

#include <CoreFoundation/CoreFoundation.h>
#include <CFNetwork/CFNetwork.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define AUTH_TOKEN_MAX 8192
#define AUTH_BODY_MAX 16384
#define AUTH_OBJECT_MAX (5 * 1024 * 1024)

static int copy_json_string(struct mg_str json, const char *path,
                            char *destination, size_t capacity);

static int base64url_value(unsigned char ch) {
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
    if (ch >= '0' && ch <= '9') return ch - '0' + 52;
    if (ch == '-' || ch == '+') return 62;
    if (ch == '_' || ch == '/') return 63;
    return -1;
}

/* Prevent an accidentally pasted service-role key from being returned to JS. */
int auth_public_key_is_safe(const char *key) {
    const char *first, *second;
    char payload[4096], decoded[3072], role[32];
    size_t input_length, output_length = 0, i;
    unsigned accumulator = 0;
    int bits = 0;
    if (key == NULL) return 0;
    if (strncmp(key, "sb_publishable_", 15) == 0) return 1;
    if (strncmp(key, "sb_secret_", 10) == 0) return 0;
    first = strchr(key, '.');
    if (first == NULL || (second = strchr(first + 1, '.')) == NULL) return 0;
    input_length = (size_t) (second - first - 1);
    if (input_length == 0 || input_length >= sizeof(payload)) return 0;
    memcpy(payload, first + 1, input_length); payload[input_length] = '\0';
    for (i = 0; i < input_length; i++) {
        int value = base64url_value((unsigned char) payload[i]);
        if (value < 0) return 0;
        accumulator = (accumulator << 6) | (unsigned) value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (output_length + 1 >= sizeof(decoded)) return 0;
            decoded[output_length++] = (char) ((accumulator >> bits) & 0xffu);
        }
    }
    decoded[output_length] = '\0';
    if (!copy_json_string(mg_str_n(decoded, output_length), "$.role", role,
                          sizeof(role))) return 0;
    return strcmp(role, "anon") == 0;
}

static int copy_json_string(struct mg_str json, const char *path,
                            char *destination, size_t capacity) {
    size_t length;
    if (destination == NULL || capacity == 0) return 0;
    destination[0] = '\0';
    length = mg_json_unescape(json, path, destination, capacity);
    if (length == 0 || length >= capacity ||
        memchr(destination, '\0', length) != NULL) {
        destination[0] = '\0';
        return 0;
    }
    destination[length] = '\0';
    return 1;
}

static int valid_user_id(const char *id) {
    size_t i, length = strlen(id);
    if (length < 32 || length >= AUTH_USER_ID_LEN) return 0;
    for (i = 0; i < length; i++) {
        unsigned char ch = (unsigned char) id[i];
        if (!isxdigit(ch) && ch != '-') return 0;
    }
    return 1;
}

static CFStringRef cf_string(const char *value) {
    if (value == NULL) return NULL;
    return CFStringCreateWithCString(kCFAllocatorDefault, value,
                                     kCFStringEncodingUTF8);
}

/*
 * This isolated CFNetwork C stream API uses macOS system trust and keeps the
 * backend in C. Apple deprecated its synchronous interface in favor of
 * NSURLSession; replacing it with that API would require Objective-C glue.
 */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
static int verify_with_auth_server(const char *token, struct AuthUser *user) {
    const char *base_url = getenv("SUPABASE_URL");
    const char *public_key = getenv("SUPABASE_ANON_KEY");
    char endpoint[1024], authorization[AUTH_TOKEN_MAX + 8];
    char response_body[AUTH_BODY_MAX + 1];
    size_t response_length = 0, base_length;
    CFStringRef endpoint_string = NULL, authorization_string = NULL;
    CFStringRef public_key_string = NULL;
    CFURLRef url = NULL;
    CFHTTPMessageRef http_request = NULL, http_response = NULL;
    CFReadStreamRef stream = NULL;
    CFIndex status_code = 0;
    int outcome = 0;

    if (base_url == NULL || public_key == NULL || token == NULL || user == NULL ||
        !auth_public_key_is_safe(public_key))
        return 0;
    if (strncmp(base_url, "https://", 8) != 0 ||
        strchr(base_url, '?') != NULL || strchr(base_url, '#') != NULL ||
        strchr(base_url + 8, '@') != NULL) return 0;
    base_length = strlen(base_url);
    while (base_length > 8 && base_url[base_length - 1] == '/') base_length--;
    if (base_length + sizeof("/auth/v1/user") > sizeof(endpoint) ||
        strlen(public_key) >= 512 || strlen(token) >= AUTH_TOKEN_MAX) return 0;
    memcpy(endpoint, base_url, base_length);
    endpoint[base_length] = '\0';
    strncat(endpoint, "/auth/v1/user", sizeof(endpoint) - strlen(endpoint) - 1);
    if (snprintf(authorization, sizeof(authorization), "Bearer %s", token) < 0)
        return 0;

    endpoint_string = cf_string(endpoint);
    authorization_string = cf_string(authorization);
    public_key_string = cf_string(public_key);
    if (endpoint_string == NULL || authorization_string == NULL ||
        public_key_string == NULL) goto cleanup;
    url = CFURLCreateWithString(kCFAllocatorDefault, endpoint_string, NULL);
    if (url == NULL) goto cleanup;
    http_request = CFHTTPMessageCreateRequest(kCFAllocatorDefault, CFSTR("GET"),
                                               url, kCFHTTPVersion1_1);
    if (http_request == NULL) goto cleanup;
    CFHTTPMessageSetHeaderFieldValue(http_request, CFSTR("Authorization"),
                                     authorization_string);
    CFHTTPMessageSetHeaderFieldValue(http_request, CFSTR("apikey"),
                                     public_key_string);
    CFHTTPMessageSetHeaderFieldValue(http_request, CFSTR("Accept"),
                                     CFSTR("application/json"));
    stream = CFReadStreamCreateForHTTPRequest(kCFAllocatorDefault, http_request);
    if (stream == NULL) goto cleanup;
    CFReadStreamSetProperty(stream, kCFStreamPropertyHTTPShouldAutoredirect,
                            kCFBooleanFalse);
    if (!CFReadStreamOpen(stream)) goto cleanup;

    for (;;) {
        UInt8 chunk[2048];
        CFIndex received = CFReadStreamRead(stream, chunk, sizeof(chunk));
        if (received < 0) goto cleanup;
        if (received == 0) break;
        if ((size_t) received > AUTH_BODY_MAX - response_length) goto cleanup;
        memcpy(response_body + response_length, chunk, (size_t) received);
        response_length += (size_t) received;
    }
    response_body[response_length] = '\0';
    http_response = (CFHTTPMessageRef) CFReadStreamCopyProperty(
        stream, kCFStreamPropertyHTTPResponseHeader);
    if (http_response == NULL) goto cleanup;
    status_code = CFHTTPMessageGetResponseStatusCode(http_response);
    if (status_code == 401 || status_code == 403 || status_code == 400) {
        outcome = 2;
        goto cleanup;
    }
    if (status_code != 200) goto cleanup;

    memset(user, 0, sizeof(*user));
    {
        struct mg_str json = mg_str_n(response_body, response_length);
        if (!copy_json_string(json, "$.id", user->id, sizeof(user->id)) ||
            !valid_user_id(user->id)) goto cleanup;
        copy_json_string(json, "$.email", user->email, sizeof(user->email));
        copy_json_string(json, "$.phone", user->phone, sizeof(user->phone));
        copy_json_string(json, "$.user_metadata.full_name", user->full_name,
                         sizeof(user->full_name));
        copy_json_string(json, "$.user_metadata.contact_email", user->contact_email,
                         sizeof(user->contact_email));
        copy_json_string(json, "$.user_metadata.contact_phone", user->contact_phone,
                         sizeof(user->contact_phone));
        copy_json_string(json, "$.created_at", user->created_at,
                         sizeof(user->created_at));
        {
            char confirmed[64];
            user->email_confirmed = copy_json_string(
                json, "$.email_confirmed_at", confirmed, sizeof(confirmed));
            user->phone_confirmed = copy_json_string(
                json, "$.phone_confirmed_at", confirmed, sizeof(confirmed));
        }
    }
    outcome = 1;

cleanup:
    if (stream != NULL) {
        CFReadStreamClose(stream);
        CFRelease(stream);
    }
    if (http_response != NULL) CFRelease(http_response);
    if (http_request != NULL) CFRelease(http_request);
    if (url != NULL) CFRelease(url);
    if (endpoint_string != NULL) CFRelease(endpoint_string);
    if (authorization_string != NULL) CFRelease(authorization_string);
    if (public_key_string != NULL) CFRelease(public_key_string);
    memset(authorization, 0, sizeof(authorization));
    memset(response_body, 0, sizeof(response_body));
    return outcome;
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

enum AuthResult auth_verify_request(struct mg_http_message *request,
                                    struct AuthUser *user) {
    struct mg_str *header;
    char token[AUTH_TOKEN_MAX];
    size_t token_length;
    int verified;
    if (request == NULL || user == NULL) return AUTH_ERROR;
    header = mg_http_get_header(request, "Authorization");
    if (header == NULL) return AUTH_MISSING;
    if (header->len < 8 || header->len >= sizeof(token) ||
        strncasecmp(header->buf, "Bearer ", 7) != 0) return AUTH_INVALID;
    token_length = header->len - 7;
    if (token_length == 0 || token_length >= sizeof(token)) return AUTH_INVALID;
    memcpy(token, header->buf + 7, token_length);
    token[token_length] = '\0';
    if (strchr(token, ' ') != NULL || strchr(token, '\r') != NULL ||
        strchr(token, '\n') != NULL) {
        memset(token, 0, sizeof(token));
        return AUTH_INVALID;
    }
    verified = verify_with_auth_server(token, user);
    memset(token, 0, sizeof(token));
    return verified == 1 ? AUTH_OK : verified == 2 ? AUTH_INVALID : AUTH_ERROR;
}
