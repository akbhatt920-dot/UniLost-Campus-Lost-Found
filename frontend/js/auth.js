let client = null;
let config = null;
let initPromise = null;

export async function initAuth() {
  if (initPromise) return initPromise;
  initPromise = (async () => {
    const response = await fetch('/api/config', { headers: { Accept: 'application/json' } });
    if (!response.ok) throw new Error('Account services are unavailable right now. Please try again later.');
    config = await response.json();
    if (!config.supabase_url || !config.supabase_anon_key || !config.supabase_js_url) return null;
    const { createClient } = await import(config.supabase_js_url);
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
    throw new Error('Could not reach UniLost. Check your connection and try again.');
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
