-- UniLost additive Auth and ownership migration.
-- Requires public.items and the unilost_backend role created by
-- database/schema.sql. Item IDs may be UUID or bigint; claims.item_id is created
-- with the same type as public.items.id. This file does not drop user data.
-- Check existing column types and reconcile any differences before running.

-- Make this migration usable even if the baseline schema's role block was not
-- applied. PostgreSQL's role defaults are NOLOGIN, NOSUPERUSER, NOBYPASSRLS.
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'unilost_backend') THEN
        EXECUTE 'CREATE ROLE unilost_backend';
    END IF;
END
$$;

CREATE TABLE IF NOT EXISTS public.profiles (
    user_id uuid PRIMARY KEY REFERENCES auth.users(id) ON DELETE CASCADE,
    full_name varchar(100) NOT NULL DEFAULT 'UniLost member',
    phone varchar(32),
    created_at timestamptz NOT NULL DEFAULT now(),
    updated_at timestamptz NOT NULL DEFAULT now()
);

DO $migration$
DECLARE
    item_id_type text;
    claims_item_id_type text;
BEGIN
    SELECT format_type(attribute.atttypid, attribute.atttypmod)
      INTO item_id_type
      FROM pg_attribute AS attribute
     WHERE attribute.attrelid = 'public.items'::regclass
       AND attribute.attname = 'id'
       AND attribute.attnum > 0
       AND NOT attribute.attisdropped;

    IF item_id_type IS NULL THEN
        RAISE EXCEPTION 'public.items.id was not found';
    END IF;

    IF to_regclass('public.claims') IS NULL THEN
        EXECUTE format($ddl$
            CREATE TABLE public.claims (
                id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
                item_id %s NOT NULL REFERENCES public.items(id) ON DELETE CASCADE,
                requester_id uuid NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
                message varchar(500) NOT NULL,
                requester_contact varchar(256) NOT NULL,
                status text NOT NULL DEFAULT 'pending'
                    CHECK (status IN ('pending', 'accepted', 'declined')),
                created_at timestamptz NOT NULL DEFAULT now(),
                updated_at timestamptz NOT NULL DEFAULT now(),
                CONSTRAINT claims_one_request_per_user UNIQUE (item_id, requester_id)
            )
        $ddl$, item_id_type);
    ELSE
        SELECT format_type(attribute.atttypid, attribute.atttypmod)
          INTO claims_item_id_type
          FROM pg_attribute AS attribute
         WHERE attribute.attrelid = 'public.claims'::regclass
           AND attribute.attname = 'item_id'
           AND attribute.attnum > 0
           AND NOT attribute.attisdropped;

        IF claims_item_id_type IS DISTINCT FROM item_id_type THEN
            RAISE EXCEPTION 'public.claims.item_id type (%) does not match public.items.id type (%)',
                claims_item_id_type, item_id_type;
        END IF;
    END IF;
END
$migration$;

ALTER TABLE public.items
    ADD COLUMN IF NOT EXISTS user_id uuid;

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conrelid = 'public.items'::regclass
          AND conname = 'items_user_id_auth_fkey'
    ) THEN
        ALTER TABLE public.items
            ADD CONSTRAINT items_user_id_auth_fkey
            FOREIGN KEY (user_id) REFERENCES auth.users(id) ON DELETE SET NULL;
    END IF;
END
$$;

CREATE INDEX IF NOT EXISTS items_user_status_created_idx
    ON public.items (user_id, status, created_at DESC);
CREATE INDEX IF NOT EXISTS claims_item_created_idx
    ON public.claims (item_id, created_at DESC);
CREATE INDEX IF NOT EXISTS claims_requester_created_idx
    ON public.claims (requester_id, created_at DESC);

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conrelid = 'public.matches'::regclass
          AND contype = 'f'
          AND pg_get_constraintdef(oid) LIKE 'FOREIGN KEY (lost_item_id) REFERENCES %items(id)%'
    ) THEN
        ALTER TABLE public.matches
            ADD CONSTRAINT matches_lost_item_id_auth_fkey
            FOREIGN KEY (lost_item_id) REFERENCES public.items(id) ON DELETE CASCADE;
    END IF;
    IF NOT EXISTS (
        SELECT 1 FROM pg_constraint
        WHERE conrelid = 'public.matches'::regclass
          AND contype = 'f'
          AND pg_get_constraintdef(oid) LIKE 'FOREIGN KEY (found_item_id) REFERENCES %items(id)%'
    ) THEN
        ALTER TABLE public.matches
            ADD CONSTRAINT matches_found_item_id_auth_fkey
            FOREIGN KEY (found_item_id) REFERENCES public.items(id) ON DELETE CASCADE;
    END IF;
END
$$;

CREATE UNIQUE INDEX IF NOT EXISTS matches_pair_unique_idx
    ON public.matches (lost_item_id, found_item_id);

ALTER TABLE public.profiles ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.claims ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.items ENABLE ROW LEVEL SECURITY;

REVOKE ALL ON TABLE public.profiles, public.claims FROM PUBLIC, anon, authenticated;
REVOKE ALL ON TABLE public.items, public.matches FROM PUBLIC, anon, authenticated;
GRANT USAGE ON SCHEMA public TO unilost_backend;
GRANT SELECT, INSERT ON TABLE public.items, public.matches TO unilost_backend;
GRANT UPDATE (status) ON TABLE public.items TO unilost_backend;
GRANT UPDATE (match_score) ON TABLE public.matches TO unilost_backend;
GRANT SELECT, INSERT, UPDATE ON TABLE public.profiles TO unilost_backend;
GRANT SELECT, INSERT ON TABLE public.claims TO unilost_backend;
GRANT UPDATE (status) ON TABLE public.claims TO unilost_backend;
GRANT SELECT (user_id) ON TABLE public.items TO unilost_backend;
GRANT INSERT (user_id) ON TABLE public.items TO unilost_backend;

-- The C server is the trusted policy boundary for these service-role policies:
-- it verifies the Supabase Auth token, derives the user ID from Auth, and adds
-- owner filters to every private query. Browser roles receive no table grants.
DROP POLICY IF EXISTS unilost_backend_profiles_all ON public.profiles;
CREATE POLICY unilost_backend_profiles_all ON public.profiles
    FOR ALL TO unilost_backend USING (true) WITH CHECK (true);
DROP POLICY IF EXISTS unilost_backend_claims_select ON public.claims;
CREATE POLICY unilost_backend_claims_select ON public.claims
    FOR SELECT TO unilost_backend USING (true);
DROP POLICY IF EXISTS unilost_backend_claims_insert ON public.claims;
CREATE POLICY unilost_backend_claims_insert ON public.claims
    FOR INSERT TO unilost_backend WITH CHECK (true);
DROP POLICY IF EXISTS unilost_backend_claims_update ON public.claims;
CREATE POLICY unilost_backend_claims_update ON public.claims
    FOR UPDATE TO unilost_backend USING (true) WITH CHECK (true);

DROP POLICY IF EXISTS unilost_backend_items_select ON public.items;
CREATE POLICY unilost_backend_items_select ON public.items
    FOR SELECT TO unilost_backend USING (true);
DROP POLICY IF EXISTS unilost_backend_items_insert ON public.items;
CREATE POLICY unilost_backend_items_insert ON public.items
    FOR INSERT TO unilost_backend WITH CHECK (true);
DROP POLICY IF EXISTS unilost_backend_matches_select ON public.matches;
CREATE POLICY unilost_backend_matches_select ON public.matches
    FOR SELECT TO unilost_backend USING (true);
DROP POLICY IF EXISTS unilost_backend_matches_insert ON public.matches;
CREATE POLICY unilost_backend_matches_insert ON public.matches
    FOR INSERT TO unilost_backend WITH CHECK (true);
DROP POLICY IF EXISTS unilost_backend_matches_update ON public.matches;
CREATE POLICY unilost_backend_matches_update ON public.matches
    FOR UPDATE TO unilost_backend USING (true) WITH CHECK (true);

-- Keep existing user_id=NULL rows intact. Backfill profile rows for existing
-- Auth users without replacing an already customized profile.
INSERT INTO public.profiles (user_id, full_name, phone)
SELECT id,
       COALESCE(NULLIF(raw_user_meta_data ->> 'full_name', ''), 'UniLost member'),
       CASE WHEN phone_confirmed_at IS NOT NULL THEN phone ELSE NULL END
FROM auth.users
ON CONFLICT (user_id) DO NOTHING;

CREATE OR REPLACE FUNCTION public.unilost_sync_auth_profile()
RETURNS trigger
LANGUAGE plpgsql
SECURITY DEFINER
SET search_path = pg_catalog, public
AS $$
BEGIN
    INSERT INTO public.profiles (user_id, full_name, phone)
    VALUES (
        NEW.id,
        COALESCE(NULLIF(NEW.raw_user_meta_data ->> 'full_name', ''), 'UniLost member'),
        CASE WHEN NEW.phone_confirmed_at IS NOT NULL THEN NEW.phone ELSE NULL END
    )
    ON CONFLICT (user_id) DO UPDATE
       SET phone = CASE
           WHEN NEW.phone_confirmed_at IS NOT NULL THEN NEW.phone
           ELSE public.profiles.phone
       END,
       updated_at = now();
    RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS unilost_auth_profile_sync ON auth.users;
CREATE TRIGGER unilost_auth_profile_sync
AFTER INSERT OR UPDATE OF phone, phone_confirmed_at ON auth.users
FOR EACH ROW EXECUTE FUNCTION public.unilost_sync_auth_profile();

DROP POLICY IF EXISTS unilost_backend_items_update ON public.items;
CREATE POLICY unilost_backend_items_update ON public.items
    FOR UPDATE TO unilost_backend
    USING (status = 'open')
    WITH CHECK (status IN ('claimed', 'closed'));

-- Verify the additive results after applying this migration.
-- SELECT column_name, data_type FROM information_schema.columns
-- WHERE table_schema='public' AND table_name='items' ORDER BY ordinal_position;
