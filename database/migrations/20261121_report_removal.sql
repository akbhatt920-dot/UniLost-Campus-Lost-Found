-- Allow the verified C backend to remove an owner's open report.
-- Existing ON DELETE CASCADE constraints remove related matches, handoff
-- requests, and messages.
GRANT DELETE ON TABLE public.items TO unilost_backend;
REVOKE UPDATE (status) ON TABLE public.items FROM unilost_backend;

DROP POLICY IF EXISTS unilost_backend_items_update ON public.items;
DROP POLICY IF EXISTS unilost_backend_items_delete ON public.items;
CREATE POLICY unilost_backend_items_delete ON public.items
    FOR DELETE TO unilost_backend
    USING (status = 'open');
