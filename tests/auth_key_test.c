#include "auth.h"

#include <stdio.h>

int main(void) {
    static const char legacy_anon[] = "x.eyJyb2xlIjoiYW5vbiJ9.x";
    static const char legacy_service[] = "x.eyJyb2xlIjoic2VydmljZV9yb2xlIn0.x";
    static const char publishable[] = "sb_publishable_test-key";
    static const char secret[] = "sb_secret_test-key";
    int failures = 0;

    if (!auth_public_key_is_safe(legacy_anon)) {
        fprintf(stderr, "Legacy anon key should be safe to publish.\n");
        failures++;
    }
    if (!auth_public_key_is_safe(publishable)) {
        fprintf(stderr, "Publishable key should be safe to publish.\n");
        failures++;
    }
    if (auth_public_key_is_safe(legacy_service)) {
        fprintf(stderr, "Legacy service-role key must not be published.\n");
        failures++;
    }
    if (auth_public_key_is_safe(secret)) {
        fprintf(stderr, "Secret key must not be published.\n");
        failures++;
    }
    if (auth_public_key_is_safe("not-a-supabase-key")) {
        fprintf(stderr, "Malformed key must not be published.\n");
        failures++;
    }
    if (failures != 0) return 1;
    puts("All public-key safety tests passed.");
    return 0;
}
