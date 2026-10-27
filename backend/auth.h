#ifndef UNILOST_AUTH_H
#define UNILOST_AUTH_H

#include "mongoose.h"

#define AUTH_USER_ID_LEN 64
#define AUTH_TEXT_LEN 256

struct AuthUser {
    char id[AUTH_USER_ID_LEN];
    char email[AUTH_TEXT_LEN];
    char phone[AUTH_TEXT_LEN];
    char full_name[AUTH_TEXT_LEN];
    char contact_email[AUTH_TEXT_LEN];
    char contact_phone[AUTH_TEXT_LEN];
    char created_at[AUTH_TEXT_LEN];
    int email_confirmed;
    int phone_confirmed;
};

enum AuthResult {
    AUTH_ERROR = -1,
    AUTH_MISSING = 0,
    AUTH_OK = 1,
    AUTH_INVALID = 2
};

/* Supabase Auth verifies the bearer token and returns its authoritative user. */
enum AuthResult auth_verify_request(struct mg_http_message *request,
                                    struct AuthUser *user);
int auth_public_key_is_safe(const char *key);
int auth_copy_bearer_token(struct mg_http_message *request, char *token,
                           size_t token_size);
int auth_fetch_storage_object(const char *bearer_token, const char *object_path,
                              unsigned char **body, size_t *body_length,
                              char content_type[128]);

#endif
