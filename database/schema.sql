-- UniLost schema for Supabase PostgreSQL.
-- Review the existing Supabase columns and types before running this script.
-- This script creates missing tables and indexes; it does not drop user data.

-- The C backend uses a dedicated role with no RLS bypass. Create the role here
-- without a password; PostgreSQL defaults it to NOSUPERUSER and NOBYPASSRLS.
-- The README explains how to enable login and set its password safely with
-- psql's interactive \\password command. Do not ALTER the SUPERUSER attribute
-- here: managed Supabase SQL Editor roles may not change that attribute.
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_roles WHERE rolname = 'unilost_backend') THEN
        EXECUTE 'CREATE ROLE unilost_backend NOLOGIN NOSUPERUSER NOCREATEDB NOCREATEROLE NOINHERIT NOBYPASSRLS';
    END IF;
END
$$;

CREATE TABLE IF NOT EXISTS public.items (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    report_type text NOT NULL,
    item_name varchar(120) NOT NULL,
    category varchar(50) NOT NULL,
    description varchar(400) NOT NULL,
    location varchar(120) NOT NULL,
    item_date date NOT NULL,
    contact_name varchar(100) NOT NULL,
    contact_info varchar(180) NOT NULL,
    status text NOT NULL DEFAULT 'open',
    created_at timestamptz NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS public.matches (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    lost_item_id uuid NOT NULL REFERENCES public.items(id) ON DELETE CASCADE,
    found_item_id uuid NOT NULL REFERENCES public.items(id) ON DELETE CASCADE,
    match_score smallint NOT NULL,
    created_at timestamptz NOT NULL DEFAULT now(),
    CONSTRAINT matches_items_must_differ CHECK (lost_item_id <> found_item_id),
    CONSTRAINT matches_score_valid CHECK (match_score BETWEEN 0 AND 100)
);

ALTER TABLE public.items
    ADD COLUMN IF NOT EXISTS photo_path text;

-- Add the same validation to compatible pre-existing tables, if needed.
DO $$
BEGIN
    IF NOT EXISTS (SELECT 1 FROM pg_constraint
                   WHERE conrelid = 'public.items'::regclass
                     AND conname = 'items_report_type_valid') THEN
        ALTER TABLE public.items ADD CONSTRAINT items_report_type_valid
            CHECK (report_type IN ('lost', 'found'));
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint
                   WHERE conrelid = 'public.items'::regclass
                     AND conname = 'items_status_valid') THEN
        ALTER TABLE public.items ADD CONSTRAINT items_status_valid
            CHECK (status IN ('open', 'claimed', 'closed'));
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint
                   WHERE conrelid = 'public.matches'::regclass
                     AND conname = 'matches_items_must_differ') THEN
        ALTER TABLE public.matches ADD CONSTRAINT matches_items_must_differ
            CHECK (lost_item_id <> found_item_id);
    END IF;
    IF NOT EXISTS (SELECT 1 FROM pg_constraint
                   WHERE conrelid = 'public.matches'::regclass
                     AND conname = 'matches_score_valid') THEN
        ALTER TABLE public.matches ADD CONSTRAINT matches_score_valid
            CHECK (match_score BETWEEN 0 AND 100);
    END IF;
END
$$;

CREATE INDEX IF NOT EXISTS items_report_status_created_idx
    ON public.items (report_type, status, created_at DESC);
CREATE INDEX IF NOT EXISTS items_category_idx
    ON public.items (category);
CREATE UNIQUE INDEX IF NOT EXISTS matches_pair_unique_idx
    ON public.matches (lost_item_id, found_item_id);
CREATE INDEX IF NOT EXISTS matches_score_idx
    ON public.matches (match_score DESC);

-- Keep match rows valid even if records are inserted outside the C server.
CREATE OR REPLACE FUNCTION public.unilost_check_match_pair()
RETURNS trigger
LANGUAGE plpgsql
SECURITY INVOKER
SET search_path = pg_catalog, public
AS $$
DECLARE
    lost_type text;
    found_type text;
BEGIN
    SELECT report_type INTO lost_type
      FROM public.items WHERE id = NEW.lost_item_id;
    SELECT report_type INTO found_type
      FROM public.items WHERE id = NEW.found_item_id;
    IF lost_type IS DISTINCT FROM 'lost' OR found_type IS DISTINCT FROM 'found' THEN
        RAISE EXCEPTION 'A match must link one lost item to one found item';
    END IF;
    IF NEW.lost_item_id = NEW.found_item_id THEN
        RAISE EXCEPTION 'An item cannot match itself';
    END IF;
    RETURN NEW;
END;
$$;

DROP TRIGGER IF EXISTS unilost_match_pair_check ON public.matches;
CREATE TRIGGER unilost_match_pair_check
    BEFORE INSERT OR UPDATE OF lost_item_id, found_item_id
    ON public.matches
    FOR EACH ROW EXECUTE FUNCTION public.unilost_check_match_pair();

ALTER TABLE public.items ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.matches ENABLE ROW LEVEL SECURITY;

-- Data API roles cannot read contact details or modify these tables. The
-- localhost C backend uses only the dedicated role below.
REVOKE ALL ON TABLE public.items, public.matches FROM PUBLIC;
REVOKE ALL ON TABLE public.items, public.matches FROM anon, authenticated;

GRANT USAGE ON SCHEMA public TO unilost_backend;
GRANT SELECT, INSERT ON TABLE public.items TO unilost_backend;
GRANT UPDATE (status) ON TABLE public.items TO unilost_backend;
GRANT SELECT, INSERT ON TABLE public.matches TO unilost_backend;
GRANT UPDATE (match_score) ON TABLE public.matches TO unilost_backend;

-- Existing projects may use serial/identity IDs rather than UUID defaults.
DO $$
DECLARE
    item_sequence text;
    match_sequence text;
BEGIN
    item_sequence := pg_get_serial_sequence('public.items', 'id');
    match_sequence := pg_get_serial_sequence('public.matches', 'id');
    IF item_sequence IS NOT NULL THEN
        EXECUTE format('GRANT USAGE, SELECT ON SEQUENCE %s TO unilost_backend', item_sequence);
    END IF;
    IF match_sequence IS NOT NULL THEN
        EXECUTE format('GRANT USAGE, SELECT ON SEQUENCE %s TO unilost_backend', match_sequence);
    END IF;
END
$$;

DROP POLICY IF EXISTS unilost_backend_items_select ON public.items;
DROP POLICY IF EXISTS unilost_backend_items_insert ON public.items;
DROP POLICY IF EXISTS unilost_backend_items_update ON public.items;
DROP POLICY IF EXISTS unilost_backend_matches_select ON public.matches;
DROP POLICY IF EXISTS unilost_backend_matches_insert ON public.matches;
DROP POLICY IF EXISTS unilost_backend_matches_update ON public.matches;

CREATE POLICY unilost_backend_items_select ON public.items
    FOR SELECT TO unilost_backend USING (true);
CREATE POLICY unilost_backend_items_insert ON public.items
    FOR INSERT TO unilost_backend WITH CHECK (true);
CREATE POLICY unilost_backend_items_update ON public.items
    FOR UPDATE TO unilost_backend
    USING (status = 'open')
    WITH CHECK (status IN ('claimed', 'closed'));

CREATE POLICY unilost_backend_matches_select ON public.matches
    FOR SELECT TO unilost_backend USING (true);
CREATE POLICY unilost_backend_matches_insert ON public.matches
    FOR INSERT TO unilost_backend WITH CHECK (true);
CREATE POLICY unilost_backend_matches_update ON public.matches
    FOR UPDATE TO unilost_backend USING (true) WITH CHECK (true);

-- Optional follow-up check:
-- SELECT tablename, rowsecurity FROM pg_tables
-- WHERE schemaname = 'public' AND tablename IN ('items', 'matches');
