-- Optional lost/found report photos. Existing item rows and columns are preserved.
-- Run after database/migrations/20261022_profiles_auth_ownership.sql.
-- Files are kept in a private bucket: signed-in users may view report photos;
-- uploads and deletes are limited to the caller's own folder.

ALTER TABLE public.items
    ADD COLUMN IF NOT EXISTS photo_path text;

GRANT SELECT (photo_path) ON TABLE public.items TO unilost_backend;
GRANT INSERT (photo_path) ON TABLE public.items TO unilost_backend;

DO $$
BEGIN
    IF EXISTS (
        SELECT 1 FROM storage.buckets
        WHERE id = 'unilost-found-photos' AND public
    ) THEN
        RAISE EXCEPTION 'The unilost-found-photos bucket is public. Review it before applying this private-photo migration.';
    END IF;

    INSERT INTO storage.buckets
        (id, name, public, file_size_limit, allowed_mime_types)
    VALUES
        ('unilost-found-photos', 'unilost-found-photos', false, 5242880,
         ARRAY['image/jpeg', 'image/png', 'image/webp']::text[])
    ON CONFLICT (id) DO UPDATE
       SET file_size_limit = EXCLUDED.file_size_limit,
           allowed_mime_types = EXCLUDED.allowed_mime_types;
END
$$;

DROP POLICY IF EXISTS unilost_found_photos_signed_in_read ON storage.objects;
CREATE POLICY unilost_found_photos_signed_in_read ON storage.objects
    FOR SELECT TO authenticated
    USING (
        bucket_id = 'unilost-found-photos'
        AND storage.allow_any_operation(ARRAY[
            'object.get_authenticated',
            'object.get_authenticated_info'
        ])
    );

DROP POLICY IF EXISTS unilost_found_photos_owner_upload ON storage.objects;
CREATE POLICY unilost_found_photos_owner_upload ON storage.objects
    FOR INSERT TO authenticated
    WITH CHECK (
        bucket_id = 'unilost-found-photos'
        AND (storage.foldername(name))[1] = (select auth.uid())::text
    );

DROP POLICY IF EXISTS unilost_found_photos_owner_delete ON storage.objects;
CREATE POLICY unilost_found_photos_owner_delete ON storage.objects
    FOR DELETE TO authenticated
    USING (
        bucket_id = 'unilost-found-photos'
        AND (storage.foldername(name))[1] = (select auth.uid())::text
    );
