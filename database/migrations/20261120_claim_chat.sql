-- Add private chat support to handoff requests.
CREATE TABLE IF NOT EXISTS public.claim_messages (
  id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
  claim_id uuid NOT NULL REFERENCES public.claims(id) ON DELETE CASCADE,
  sender_id uuid NOT NULL REFERENCES auth.users(id) ON DELETE CASCADE,
  message text NOT NULL,
  created_at timestamptz NOT NULL DEFAULT now(),
  CONSTRAINT claim_messages_message_not_empty CHECK (btrim(message) <> '')
);

CREATE INDEX IF NOT EXISTS claim_messages_claim_created_idx
  ON public.claim_messages (claim_id, created_at ASC);

-- Preserve the original handoff message as the first chat message for claims
-- created before chat support was deployed. This is safe to rerun.
INSERT INTO public.claim_messages (claim_id, sender_id, message, created_at)
SELECT c.id, c.requester_id, c.message, c.created_at
FROM public.claims c
WHERE btrim(c.message) <> ''
  AND NOT EXISTS (
    SELECT 1 FROM public.claim_messages cm
    WHERE cm.claim_id = c.id
      AND cm.sender_id = c.requester_id
      AND cm.message = c.message
  );

ALTER TABLE public.claim_messages ENABLE ROW LEVEL SECURITY;
REVOKE ALL ON TABLE public.claim_messages FROM PUBLIC, anon, authenticated;
GRANT USAGE ON SCHEMA public TO unilost_backend;
GRANT SELECT, INSERT ON TABLE public.claim_messages TO unilost_backend;

DROP POLICY IF EXISTS unilost_backend_claim_messages_select ON public.claim_messages;
CREATE POLICY unilost_backend_claim_messages_select ON public.claim_messages
  FOR SELECT TO unilost_backend USING (true);

DROP POLICY IF EXISTS unilost_backend_claim_messages_insert ON public.claim_messages;
CREATE POLICY unilost_backend_claim_messages_insert ON public.claim_messages
  FOR INSERT TO unilost_backend WITH CHECK (true);
