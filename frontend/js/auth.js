let client = null;
let config = null;
let initPromise = null;

export async function initAuth() {
  if (initPromise) return initPromise;
  initPromise = (async () => {
    const response = await fetch('/api/config', { headers: { Accept: 'application/json' } });
    if (!response.ok) throw new Error('The local C server could not load its public configuration.');
    config = await response.json();
    if (!config.supabase_url || !config.supabase_anon_key) return null;
    const { createClient } = await import('https://esm.sh/@supabase/supabase-js@2.102.0');
    client = createClient(config.supabase_url, config.supabase_anon_key, {
      auth: { persistSession: true, autoRefreshToken: true, detectSessionInUrl: true }
    });
    return client;
  })();
  return initPromise;
}

export function getAuthClient() {
  return client;
}

export function getAuthConfig() {
  return config;
}

export async function getSession() {
  if (!client) return null;
  const { data, error } = await client.auth.getSession();
  if (error) throw error;
  return data.session;
}

export async function apiRequest(path, options = {}) {
  const { auth = 'optional', ...fetchOptions } = options;
  const headers = { Accept: 'application/json', ...(fetchOptions.headers || {}) };
  if (fetchOptions.body !== undefined) headers['Content-Type'] = 'application/json';

  let session = null;
  if (client) session = await getSession();
  if (auth === 'required' && !session?.access_token) {
    throw new Error('Please sign in to continue.');
  }
  if (session?.access_token) headers.Authorization = `Bearer ${session.access_token}`;

  let response;
  try {
    response = await fetch(path, { ...fetchOptions, headers });
  } catch {
    throw new Error('Could not reach the local C server. Check that it is running at http://127.0.0.1:8000.');
  }
  let payload;
  try {
    payload = await response.json();
  } catch {
    throw new Error(`The server returned an unreadable response (${response.status}).`);
  }
  if (!response.ok) throw new Error(payload.message || payload.error || `Request failed (${response.status}).`);
  return payload;
}
