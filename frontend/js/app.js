import { apiRequest, getAuthClient, getSession, initAuth } from './auth.js';

const byId = (id) => document.getElementById(id);
const page = document.body.dataset.page;
const PHOTO_BUCKET = 'unilost-found-photos';
let reports = [];
let browseTimer = null;
let pendingPhone = '';
let currentSession = null;
let activeClaimButton = null;
let browseOffset = 0;
let browseTotal = 0;
let browseRequestId = 0;
const PAGE_SIZE = 18;

function showMessage(element, message, isError = false) {
  if (!element) return;
  element.textContent = message;
  element.classList.toggle('error', isError);
  element.classList.toggle('success-text', !isError && message.length > 0);
}

function toast(message) {
  const element = byId('toast');
  if (!element) return;
  element.textContent = message;
  element.classList.add('show');
  window.setTimeout(() => element.classList.remove('show'), 3600);
}

function safeNextPath() {
  const candidate = new URLSearchParams(window.location.search).get('next');
  if (!candidate || !candidate.startsWith('/') || candidate.startsWith('//')) return '/';
  try {
    const url = new URL(candidate, window.location.origin);
    if (url.origin !== window.location.origin || /\/(login|register)\.html$/.test(url.pathname)) return '/';
    return `${url.pathname}${url.search}${url.hash}`;
  } catch {
    return '/';
  }
}

function normalizePhone(value) {
  const phone = String(value || '').replace(/[\s().-]/g, '');
  return /^\+[1-9]\d{7,14}$/.test(phone) ? phone : '';
}

function isEmail(value) {
  return /^[^\s@]+@[^\s@]+\.[^\s@]+$/.test(value);
}

function renderHeader(session) {
  const host = byId('site-header');
  if (!host) return;
  const current = window.location.pathname;
  const requestedType = new URLSearchParams(window.location.search).get('type');
  const links = session ? [
    ['/index.html', 'Home', 'home', 'home'],
    ['/report-item.html?type=lost', 'Report Lost', 'report-lost', 'lost'],
    ['/report-item.html?type=found', 'Report Found', 'report-found', 'found'],
    ['/matches.html', 'Matches', 'matches', 'matches'],
    ['/reports.html?type=found', 'Browse Found Items', 'items', 'browse'],
    ['/profile.html', 'Profile', 'profile', 'profile']
  ] : [];
  host.innerHTML = '';
  const header = document.createElement('div');
  header.className = 'site-header';
  const brand = document.createElement('a');
  brand.className = 'brand'; brand.href = '/';
  const mark = document.createElement('span'); mark.className = 'brand-mark'; mark.textContent = 'U';
  const name = document.createElement('span'); name.textContent = 'UniLost';
  brand.append(mark, name);
  const toggle = document.createElement('button');
  toggle.className = 'menu-toggle'; toggle.type = 'button'; toggle.textContent = 'Menu';
  toggle.setAttribute('aria-label', 'Open navigation'); toggle.setAttribute('aria-expanded', 'false');
  const nav = document.createElement('nav'); nav.className = 'main-nav'; nav.id = 'main-nav';
  nav.setAttribute('aria-label', 'Main navigation');
  toggle.setAttribute('aria-controls', nav.id);
  const active = session ? page === 'home' ? 'home'
    : page === 'report' ? (requestedType === 'found' ? 'report-found' : 'report-lost')
      : page === 'matches' ? 'matches'
        : page === 'items' ? 'browse'
          : page === 'profile' ? 'profile' : '' : '';
  const icons = {
    home: ['M3 10.5 12 3l9 7.5', 'M5.5 9.5v11h13v-11', 'M9 20.5v-7h6v7'],
    lost: ['M10.5 18.5a8 8 0 1 1 5.7-2.4', 'm16.5 16.5 4 4', 'M8 10.5h5'],
    found: ['M4 7.5h16v13H4z', 'M4 11h16', 'M12 11v9.5', 'M12 3v5', 'M9.5 5.5h5'],
    matches: ['M8.5 15.5 6 18a4 4 0 0 1-5.5-5.8l4-4a4 4 0 0 1 5.7 0', 'm15.5 8.5 2.5-2.5a4 4 0 0 1 5.5 5.8l-4 4a4 4 0 0 1-5.7 0', 'M8 12h8'],
    browse: ['M3 5.5h11v14H3z', 'M6 9h5', 'M6 12h4', 'M17 15.5l4 4', 'M16 11.5a4 4 0 1 0 0 .1'],
    profile: ['M20 21a8 8 0 0 0-16 0', 'M12 12a4.5 4.5 0 1 0 0-9 4.5 4.5 0 0 0 0 9z']
  };
  links.forEach(([href, label, key, icon]) => {
    const link = document.createElement('a'); link.href = href;
    const svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
    svg.setAttribute('viewBox', '0 0 24 24'); svg.setAttribute('aria-hidden', 'true');
    svg.classList.add('nav-icon');
    icons[icon].forEach((d) => {
      const path = document.createElementNS('http://www.w3.org/2000/svg', 'path');
      path.setAttribute('d', d); svg.append(path);
    });
    const text = makeElement('span', '', label);
    link.append(svg, text);
    if (active === key) link.setAttribute('aria-current', 'page');
    nav.append(link);
  });
  const actions = document.createElement('div'); actions.className = 'nav-actions';
  if (!session) {
    const login = document.createElement('a'); login.className = 'button secondary small'; login.href = `/login.html?next=${encodeURIComponent(window.location.pathname + window.location.search)}`; login.textContent = 'Log in';
    const register = document.createElement('a'); register.className = 'button primary small'; register.href = '/register.html'; register.textContent = 'Create account';
    actions.append(login, register);
  }
  toggle.addEventListener('click', () => {
    const open = nav.classList.toggle('open');
    toggle.setAttribute('aria-expanded', String(open));
    toggle.setAttribute('aria-label', open ? 'Close navigation' : 'Open navigation');
  });
  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape' && nav.classList.contains('open')) {
      nav.classList.remove('open'); toggle.setAttribute('aria-expanded', 'false');
      toggle.setAttribute('aria-label', 'Open navigation'); toggle.focus();
    }
  });
  header.append(brand, toggle, nav, actions);
  host.append(header);
}

function renderFooter() {
  const host = byId('site-footer');
  if (!host) return;
  host.className = 'site-footer';
  const inner = document.createElement('div'); inner.className = 'footer-inner';
  const brand = document.createElement('a'); brand.className = 'brand footer-brand'; brand.href = '/index.html';
  const mark = document.createElement('span'); mark.className = 'brand-mark'; mark.textContent = 'U';
  const title = document.createElement('span'); title.textContent = 'UniLost'; brand.append(mark, title);
  const text = document.createElement('p'); text.textContent = 'A campus community service · C-powered potential matching';
  const top = document.createElement('a'); top.href = '#main'; top.textContent = 'Back to top ↑';
  inner.append(brand, text, top); host.replaceChildren(inner);
}

async function logoutUser() {
  const auth = getAuthClient();
  if (!auth) return;
  const { error } = await auth.auth.signOut();
  if (error) { toast(error.message); return; }
  sessionStorage.removeItem('unilost-password-recovery');
  window.location.assign('/login.html');
}

async function routeGuard(session, authReady) {
  const publicPages = ['login', 'register', 'forgot', 'reset'];
  if (publicPages.includes(page)) return true;
  const main = byId('main');
  if (!session) {
    const next = window.location.pathname + window.location.search;
    window.location.replace(`/login.html?next=${encodeURIComponent(next)}`);
    return false;
  }
  if (!authReady) {
    const note = makeElement('p', 'form-message error',
      'Authentication is not configured. Add SUPABASE_URL and SUPABASE_ANON_KEY to .env, then restart the C server.');
    main?.replaceChildren(note);
    document.body.removeAttribute('data-auth-required');
    return false;
  }
  document.body.removeAttribute('data-auth-required');
  return true;
}

function makeElement(tag, className, text) {
  const element = document.createElement(tag);
  if (className) element.className = className;
  if (text !== undefined) element.textContent = text;
  return element;
}

function formatDate(value) {
  if (!value) return 'Date unavailable';
  const date = new Date(`${String(value).slice(0, 10)}T00:00:00`);
  return Number.isNaN(date.getTime()) ? String(value) : new Intl.DateTimeFormat(undefined, { dateStyle: 'medium' }).format(date);
}

function localDate() {
  const now = new Date();
  return `${now.getFullYear()}-${String(now.getMonth() + 1).padStart(2, '0')}-${String(now.getDate()).padStart(2, '0')}`;
}

function attachItemPhoto(container, item, variant = 'card') {
  const frame = makeElement('div', `item-photo-frame ${variant}`);
  const fallback = makeElement('div', 'photo-fallback', 'No photo provided');
  frame.append(fallback);
  container.append(frame);
  const photoPath = typeof item?.photo_path === 'string' ? item.photo_path.trim() : '';
  if (!photoPath) return frame;
  if (!currentSession) {
    fallback.textContent = 'Sign in to view this photo';
    return frame;
  }
  const client = getAuthClient();
  if (!client) {
    fallback.textContent = 'Photo service unavailable';
    return frame;
  }
  fallback.textContent = 'Loading photo…';
  const photo = makeElement('img', 'item-photo');
  photo.alt = `Photo of ${item.item_name || 'reported item'}`;
  photo.loading = variant === 'card' ? 'lazy' : 'eager';
  photo.addEventListener('load', () => { fallback.hidden = true; }, { once: true });
  let imageUrl = '';
  const photoUnavailable = (error) => {
    photo.remove();
    if (imageUrl) URL.revokeObjectURL(imageUrl);
    fallback.hidden = false;
    fallback.textContent = 'Photo unavailable';
    if (error) console.warn('UniLost could not load a report photo:', error.message || error);
  };
  photo.addEventListener('error', () => photoUnavailable(), { once: true });
  frame.append(photo);
  client.storage.from(PHOTO_BUCKET).download(photoPath).then(({ data, error }) => {
    if (error || !data) { photoUnavailable(error || new Error('Storage returned no image data.')); return; }
    imageUrl = URL.createObjectURL(data);
    photo.addEventListener('load', () => URL.revokeObjectURL(imageUrl), { once: true });
    photo.addEventListener('error', () => URL.revokeObjectURL(imageUrl), { once: true });
    photo.src = imageUrl;
  }).catch(photoUnavailable);
  return frame;
}

function ensureItemDetailsDialog() {
  let dialog = byId('item-details-dialog');
  if (dialog) return dialog;
  dialog = makeElement('dialog', 'item-details-dialog');
  dialog.id = 'item-details-dialog';
  dialog.setAttribute('aria-labelledby', 'item-details-title');
  const close = makeElement('button', 'dialog-close', '×');
  close.type = 'button'; close.setAttribute('aria-label', 'Close item details');
  close.addEventListener('click', () => dialog.close());
  const content = makeElement('div', 'details-content');
  dialog.append(close, content);
  dialog.addEventListener('click', (event) => {
    if (event.target === dialog) dialog.close();
  });
  document.body.append(dialog);
  return dialog;
}

async function openItemDetails(itemId) {
  const dialog = ensureItemDetailsDialog();
  if (dialog.open && dialog.dataset.itemId === itemId && dialog.dataset.loaded === 'true') return;
  if (dialog.open && dialog.dataset.itemId === itemId && dialog.dataset.loading === 'true') return;
  dialog.dataset.itemId = itemId;
  dialog.dataset.loading = 'true';
  dialog.dataset.loaded = 'false';
  const content = dialog.querySelector('.details-content');
  content.replaceChildren(makeElement('p', 'loading', 'Loading item details…'));
  if (!dialog.open) dialog.showModal();
  try {
    const item = await apiRequest(`/api/items/${encodeURIComponent(itemId)}`, { auth: 'required' });
    if (dialog.dataset.itemId !== itemId) return;
    dialog.dataset.loading = 'false';
    dialog.dataset.loaded = 'true';
    content.replaceChildren();
    attachItemPhoto(content, item, 'details');
    const heading = makeElement('h2', '', item.item_name || 'Item details');
    heading.id = 'item-details-title';
    const badges = makeElement('div', 'item-card-top');
    badges.append(makeElement('span', `item-tag ${item.report_type}`, item.report_type === 'lost' ? 'Lost report' : 'Found report'),
      makeElement('span', `status-tag ${item.status}`, item.status));
    const meta = makeElement('dl', 'details-meta');
    for (const [label, value] of [
      ['Category', item.category], ['Location', item.location],
      [item.report_type === 'lost' ? 'Date lost' : 'Date found', formatDate(item.item_date)],
      ['Reported', formatDate(item.created_at)]
    ]) {
      const row = makeElement('div', 'details-meta-row');
      row.append(makeElement('dt', '', label), makeElement('dd', '', value || 'Not provided'));
      meta.append(row);
    }
    content.append(badges, heading, makeElement('p', 'details-description', item.description), meta);
    const actions = makeElement('div', 'details-actions');
    if (item.is_owner && item.report_type === 'lost' && item.status === 'open') {
      const matches = makeElement('a', 'button primary', 'Find matches');
      matches.href = `/matches.html?lost_id=${encodeURIComponent(item.id)}`;
      actions.append(matches);
    } else if (item.is_owner && item.status === 'open') {
      actions.append(makeElement('p', 'field-hint', 'This is your report. You can update its status from your Profile.'));
    } else if (item.claimable && item.status === 'open') {
      const request = makeElement('button', 'button primary', 'Request a private handoff');
      request.type = 'button';
      request.addEventListener('click', () => {
        const claimDialog = byId('claim-dialog');
        const form = byId('claim-form');
        if (!claimDialog || !form) { toast('Open Browse Found Items to send a private handoff request.'); return; }
        activeClaimButton = request;
        byId('claim-dialog-title').textContent = `Contact the owner of “${item.item_name}”`;
        form.elements.item_id.value = item.id;
        form.elements.message.value = '';
        showMessage(byId('claim-message'), '');
        if (dialog.open) dialog.close();
        claimDialog.showModal();
      });
      actions.append(request);
    } else {
      actions.append(makeElement('p', 'field-hint', 'This report is not currently available for a handoff request.'));
    }
    content.append(actions);
  } catch (error) {
    if (dialog.dataset.itemId !== itemId) return;
    dialog.dataset.loading = 'false';
    dialog.dataset.loaded = 'false';
    const message = makeElement('p', 'form-message error', error.message);
    const retry = makeElement('button', 'button secondary', 'Try again');
    retry.type = 'button'; retry.addEventListener('click', () => openItemDetails(itemId));
    content.replaceChildren(message, retry);
  }
}

function fillItemCard(container, item, compact = false) {
  const card = makeElement('article', `item-card${compact ? ' compact-card' : ''}`);
  attachItemPhoto(card, item, 'card');
  const top = makeElement('div', 'item-card-top');
  top.append(
    makeElement('span', `item-tag ${item.report_type}`, item.report_type === 'lost' ? 'Lost report' : 'Found report'),
    makeElement('span', `status-tag ${item.status}`, item.status)
  );
  card.append(top, makeElement('h3', '', item.item_name), makeElement('p', 'item-description', item.description));
  const meta = makeElement('div', 'item-meta');
  meta.append(
    makeElement('span', '', item.category),
    makeElement('span', '', item.location),
    makeElement('span', '', `${item.report_type === 'lost' ? 'Date lost' : 'Date found'}: ${formatDate(item.item_date)}`),
    makeElement('span', '', `Reported: ${formatDate(item.created_at)}`)
  );
  card.append(meta);
  const actions = makeElement('div', 'item-actions');
  const details = makeElement('button', 'item-action details-action', 'View Details');
  details.type = 'button';
  details.addEventListener('click', () => openItemDetails(item.id));
  actions.append(details);
  if (item.is_owner && item.status === 'open') {
    for (const [status, label] of [['claimed', 'Mark claimed'], ['closed', 'Close report']]) {
      const button = makeElement('button', 'item-action', label); button.type = 'button';
      button.addEventListener('click', async () => {
        if (!window.confirm(`Mark your report as ${status}?`)) return;
        button.disabled = true;
        try {
          await apiRequest(`/api/items/${encodeURIComponent(item.id)}/status`, {
            method: 'PATCH', auth: 'required', body: JSON.stringify({ status })
          });
          toast('Your report status was updated.');
          if (page === 'items') await loadBrowseItems(true);
          if (page === 'home') await loadHome();
          if (page === 'profile') await loadMyItems();
        } catch (error) { toast(error.message); button.disabled = false; }
      });
      actions.append(button);
    }
  } else if (!compact && item.claimable && item.status === 'open') {
    const request = makeElement('button', 'item-action', 'Request private handoff');
    request.type = 'button';
    request.addEventListener('click', () => {
      if (!currentSession) {
        const next = window.location.pathname + window.location.search;
        window.location.assign(`/login.html?next=${encodeURIComponent(next)}`);
        return;
      }
      const dialog = byId('claim-dialog');
      const form = byId('claim-form');
      activeClaimButton = request;
      byId('claim-dialog-title').textContent = `Contact the owner of “${item.item_name}”`;
      form.elements.item_id.value = item.id;
      form.elements.message.value = '';
      showMessage(byId('claim-message'), '');
      dialog.showModal();
    });
    actions.append(request);
  }
  card.append(actions);
  container.append(card);
}

function renderEmpty(container, message) {
  container.replaceChildren(makeElement('p', 'empty-state', message));
}

async function loadHome() {
  const connection = byId('connection-state');
  const [health, summary, lost, found, recentFound] = await Promise.allSettled([
    apiRequest('/api/health'), apiRequest('/api/summary', { auth: 'required' }),
    apiRequest('/api/items?type=lost&mine=true&limit=3', { auth: 'required' }),
    apiRequest('/api/items?type=found&mine=true&limit=3', { auth: 'required' }),
    apiRequest('/api/items?type=found&status=open&limit=3', { auth: 'required' })
  ]);
  if (connection && health.status === 'fulfilled') {
    connection.textContent = health.value.database === 'connected'
      ? 'C server and PostgreSQL are connected.'
      : 'C server is running; PostgreSQL is not configured.';
    connection.classList.toggle('connected', health.value.database === 'connected');
    connection.classList.toggle('disconnected', health.value.database !== 'connected');
  } else if (connection) {
    connection.textContent = health.reason.message; connection.classList.add('disconnected');
  }
  if (summary.status === 'fulfilled') {
    byId('my-lost-count').textContent = String(summary.value.my_lost);
    byId('my-found-count').textContent = String(summary.value.my_found);
    byId('my-match-count').textContent = String(summary.value.my_matches);
    showMessage(byId('dashboard-status'), 'Your account totals are up to date.');
  } else {
    byId('my-lost-count').textContent = '—';
    byId('my-found-count').textContent = '—';
    byId('my-match-count').textContent = '—';
    showMessage(byId('dashboard-status'), summary.reason.message, true);
  }
  for (const [id, result, label, emptyText] of [
    ['my-recent-lost', lost, 'lost', 'You have not reported a lost item yet.'],
    ['my-recent-found', found, 'found', 'You have not reported a found item yet.'],
    ['recent-campus-found', recentFound, 'found', 'No open found items have been reported yet.']
  ]) {
    const container = byId(id);
    if (!container) continue;
    container.replaceChildren();
    if (result.status === 'rejected') { renderEmpty(container, `Your ${label} reports could not be loaded.`); continue; }
    if (!result.value.items.length) { renderEmpty(container, emptyText); continue; }
    result.value.items.forEach((item) => fillItemCard(container, item, true));
  }
}

async function loadBrowseItems(reset = false) {
  const container = byId('item-list');
  const status = byId('browse-status');
  if (!container || !status) return;
  if (reset) browseOffset = 0;
  const requestId = ++browseRequestId;
  status.textContent = 'Loading found items…'; container.replaceChildren();
  const type = new URLSearchParams(window.location.search).get('type') || document.body.dataset.type || 'found';
  const mineOnly = new URLSearchParams(window.location.search).get('mine') === 'true';
  const query = new URLSearchParams();
  if (type !== 'all') query.set('type', type);
  if (mineOnly) query.set('mine', 'true');
  const search = byId('search-filter')?.value.trim();
  const category = byId('category-filter')?.value;
  const location = byId('location-filter')?.value.trim();
  const itemStatus = byId('status-filter')?.value;
  const dateFrom = byId('date-from-filter')?.value;
  const dateTo = byId('date-to-filter')?.value;
  const sort = byId('sort-filter')?.value || 'newest';
  if (search) query.set('search', search);
  if (category) query.set('category', category);
  if (location) query.set('location', location);
  if (itemStatus) query.set('status', itemStatus);
  if (dateFrom) query.set('date_from', dateFrom);
  if (dateTo) query.set('date_to', dateTo);
  query.set('sort', sort);
  query.set('limit', String(PAGE_SIZE));
  query.set('offset', String(browseOffset));
  const activeFilters = [search, category, location, itemStatus, dateFrom, dateTo, mineOnly ? 'mine' : ''].filter(Boolean).length;
  const filterNote = byId('active-filter-count');
  if (filterNote) filterNote.textContent = activeFilters ? `${activeFilters} filter${activeFilters === 1 ? '' : 's'} active` : 'No filters applied';
  try {
    const payload = await apiRequest(`/api/items?${query}`, { auth: 'required' });
    if (requestId !== browseRequestId) return;
    reports = payload.items;
    browseTotal = Number(payload.total) || 0;
    status.textContent = browseTotal
      ? `Showing ${browseOffset + 1}–${browseOffset + reports.length} of ${browseTotal} ${mineOnly ? 'of your ' : ''}${type === 'all' ? 'reports' : `${type} items`}.`
      : `No ${type === 'all' ? 'reports' : `${type} items`} match these filters.`;
    if (!reports.length) {
      renderEmpty(container, `No ${type === 'all' ? 'reports' : `${type} items`} match these filters. Adjust or clear the filters to continue.`);
    } else {
      reports.forEach((item) => fillItemCard(container, item));
    }
    const pageCount = Math.max(1, Math.ceil(browseTotal / PAGE_SIZE));
    const pageNumber = Math.floor(browseOffset / PAGE_SIZE) + 1;
    byId('page-count').textContent = `Page ${pageNumber} of ${pageCount}`;
    byId('browse-prev').disabled = browseOffset === 0;
    byId('browse-next').disabled = !payload.has_more;
  } catch (error) {
    if (requestId !== browseRequestId) return;
    status.textContent = error.message;
    renderEmpty(container, 'Found items could not be loaded. Check the connection and retry.');
    const retry = makeElement('button', 'button secondary', 'Retry');
    retry.type = 'button'; retry.addEventListener('click', () => loadBrowseItems(false));
    container.append(retry);
  }
}

function initBrowse() {
  const initialType = new URLSearchParams(window.location.search).get('type');
  const mineOnly = new URLSearchParams(window.location.search).get('mine') === 'true';
  if (['lost', 'found', 'all'].includes(initialType)) document.body.dataset.type = initialType;
  const type = document.body.dataset.type || 'found';
  if (mineOnly) {
    const mineTitle = type === 'lost' ? 'My Lost Items' : type === 'found' ? 'My Found Items' : 'My Item Reports';
    byId('browse-title').textContent = mineTitle;
    document.title = `${mineTitle} — UniLost`;
    document.querySelector('.page-intro .lede').textContent = 'Manage the status of your reports and open item details.';
  } else if (type === 'lost') {
    byId('browse-title').textContent = 'Lost Item Reports';
    document.title = 'Lost Item Reports — UniLost';
    document.querySelector('.page-intro .lede').textContent = 'Search lost-item reports shared with signed-in UniLost members.';
    byId('date-from-filter').closest('label').querySelector('span').textContent = 'Lost on or after';
    byId('date-to-filter').closest('label').querySelector('span').textContent = 'Lost on or before';
  } else if (type === 'all') {
    byId('browse-title').textContent = 'Campus Item Reports';
    document.title = 'Campus Item Reports — UniLost';
  }
  for (const id of ['search-filter', 'category-filter', 'location-filter', 'status-filter', 'date-from-filter', 'date-to-filter', 'sort-filter']) {
    byId(id)?.addEventListener('input', () => {
      window.clearTimeout(browseTimer); browseTimer = window.setTimeout(() => loadBrowseItems(true), 220);
    });
    byId(id)?.addEventListener('change', () => loadBrowseItems(true));
  }
  byId('clear-filters')?.addEventListener('click', () => {
    for (const id of ['search-filter', 'category-filter', 'location-filter', 'status-filter', 'date-from-filter', 'date-to-filter']) {
      const control = byId(id); if (control) control.value = '';
    }
    if (byId('sort-filter')) byId('sort-filter').value = 'newest';
    loadBrowseItems(true);
  });
  byId('refresh-items')?.addEventListener('click', () => loadBrowseItems(false));
  byId('browse-prev')?.addEventListener('click', () => {
    browseOffset = Math.max(0, browseOffset - PAGE_SIZE); loadBrowseItems(false);
  });
  byId('browse-next')?.addEventListener('click', () => {
    if (browseOffset + PAGE_SIZE < browseTotal) { browseOffset += PAGE_SIZE; loadBrowseItems(false); }
  });
  initClaimDialog();
  ensureItemDetailsDialog();
  loadBrowseItems(true);
}

function initClaimDialog() {
  const dialog = ensureClaimDialog();
  const form = byId('claim-form');
  if (dialog.dataset.initialized === 'true') return;
  dialog.dataset.initialized = 'true';
  byId('claim-dialog-close')?.addEventListener('click', () => dialog.close());
  form.addEventListener('submit', async (event) => {
    event.preventDefault();
    if (!form.reportValidity()) return;
    const button = form.querySelector('button[type=submit]');
    button.disabled = true;
    try {
      const itemId = form.elements.item_id.value;
      const result = await apiRequest(`/api/items/${encodeURIComponent(itemId)}/claims`, {
        method: 'POST', auth: 'required',
        body: JSON.stringify({ message: form.elements.message.value.trim() })
      });
      dialog.close();
      toast(result.message || 'Private handoff request sent.');
      if (activeClaimButton) {
        activeClaimButton.disabled = true;
        activeClaimButton.textContent = 'Request sent';
      }
    } catch (error) { showMessage(byId('claim-message'), error.message, true); }
    finally { button.disabled = false; }
  });
}

function ensureClaimDialog() {
  let dialog = byId('claim-dialog');
  if (dialog) return dialog;
  dialog = makeElement('dialog', 'claim-dialog'); dialog.id = 'claim-dialog';
  dialog.setAttribute('aria-labelledby', 'claim-dialog-title');
  const close = makeElement('button', 'dialog-close', '×');
  close.type = 'button'; close.id = 'claim-dialog-close';
  close.setAttribute('aria-label', 'Close request form');
  close.addEventListener('click', () => dialog.close());
  dialog.append(close, makeElement('p', 'eyebrow', 'Private handoff request'));
  const title = makeElement('h2', '', 'Contact the report owner'); title.id = 'claim-dialog-title';
  dialog.append(title, makeElement('p', 'field-hint', 'Your verified contact is shared only with the owner to arrange a handoff.'));
  const form = makeElement('form', 'stack-form'); form.id = 'claim-form';
  const itemId = document.createElement('input'); itemId.type = 'hidden'; itemId.name = 'item_id'; form.append(itemId);
  const label = makeElement('label', 'field'); label.append(makeElement('span', '', 'Message to the owner'));
  const textarea = document.createElement('textarea'); textarea.name = 'message'; textarea.rows = 4;
  textarea.maxLength = 500; textarea.required = true; textarea.placeholder = 'Share a detail that helps identify the item.';
  label.append(textarea); form.append(label);
  const submit = makeElement('button', 'button primary', 'Send private request'); submit.type = 'submit';
  form.append(submit, makeElement('p', 'form-message', ''));
  const message = form.lastElementChild; message.id = 'claim-message'; message.setAttribute('role', 'status');
  message.setAttribute('aria-live', 'polite');
  dialog.append(form); document.body.append(dialog);
  return dialog;
}

async function loadMyItems() {
  for (const [type, id] of [['lost', 'profile-lost-items'], ['found', 'profile-found-items']]) {
    const container = byId(id);
    if (!container) continue;
    try {
      const payload = await apiRequest(`/api/items?type=${type}&mine=true&limit=50`, { auth: 'required' });
      container.replaceChildren();
      if (!payload.items.length) {
        renderEmpty(container, `You have not submitted any ${type} reports yet.`);
        continue;
      }
      payload.items.forEach((item) => fillItemCard(container, item));
    } catch (error) {
      renderEmpty(container, error.message);
      const retry = makeElement('button', 'button secondary', 'Retry');
      retry.type = 'button'; retry.addEventListener('click', loadMyItems);
      container.append(retry);
    }
  }
}

async function fillReportContact() {
  const form = byId('report-form');
  if (!form) return;
  form.elements.item_date.max = localDate();
  try {
    const { profile } = await apiRequest('/api/profile', { auth: 'required' });
    form.elements.contact_name.value = profile.full_name || '';
    form.elements.contact_info.value = profile.email || profile.phone || profile.contact_email || profile.contact_phone || '';
  } catch { /* Keep required inputs available for manual entry. */ }
}

function initReport() {
  const form = byId('report-form');
  if (!form) return;
  const typeChoices = form.querySelectorAll('input[name="report_type"]');
  const typeFromUrl = new URLSearchParams(window.location.search).get('type');
  const fixedType = ['lost', 'found'].includes(typeFromUrl) ? typeFromUrl : '';
  if (fixedType) {
    typeChoices.forEach((choice) => { choice.checked = choice.value === fixedType; });
    byId('report-type-choice').hidden = true;
  }
  const reportType = () => fixedType || form.querySelector('input[name="report_type"]:checked').value;
  const dateLabel = byId('report-date-label');
  const locationLabel = byId('report-location-label');
  const photoField = byId('photo-field');
  const photoInput = form.elements.photo;
  const preview = byId('photo-preview');
  const previewImage = byId('photo-preview-image');
  let previewUrl = '';
  const clearPhoto = () => {
    if (previewUrl) URL.revokeObjectURL(previewUrl);
    previewUrl = '';
    if (photoInput) photoInput.value = '';
    if (previewImage) previewImage.removeAttribute('src');
    if (preview) preview.hidden = true;
  };
  const updateReportType = () => {
    const type = reportType();
    byId('report-eyebrow').textContent = type === 'lost' ? 'Report a lost item' : 'Report a found item';
    byId('report-title').textContent = type === 'lost' ? 'Tell us what went missing.' : 'Help its owner find it.';
    byId('report-intro').textContent = type === 'lost'
      ? 'Add clear details to help someone recognize it. Your contact details stay private.'
      : 'Share where and when you found it. Add a photo if it helps identify the item.';
    dateLabel.textContent = type === 'lost' ? 'Date lost' : 'Date found';
    locationLabel.textContent = type === 'lost' ? 'Where did you last see it?' : 'Where did you find it?';
    photoField.hidden = false;
    byId('submit-report').textContent = type === 'lost' ? 'Submit lost-item report →' : 'Submit found-item report →';
  };
  typeChoices.forEach((choice) => choice.addEventListener('change', updateReportType));
  updateReportType();
  fillReportContact();
  photoInput?.addEventListener('change', () => {
    if (previewUrl) URL.revokeObjectURL(previewUrl);
    previewUrl = '';
    const file = photoInput.files?.[0];
    if (!file) { if (preview) preview.hidden = true; return; }
    if (!['image/jpeg', 'image/png', 'image/webp'].includes(file.type) || file.size > 5 * 1024 * 1024) {
      clearPhoto();
      showMessage(byId('form-message'), 'Choose a JPEG, PNG, or WebP image no larger than 5 MB.', true);
      return;
    }
    previewUrl = URL.createObjectURL(file);
    previewImage.src = previewUrl;
    preview.hidden = false;
    showMessage(byId('form-message'), '');
  });
  byId('photo-clear')?.addEventListener('click', clearPhoto);
  form.addEventListener('submit', async (event) => {
    event.preventDefault();
    const message = byId('form-message');
    byId('report-success').hidden = true;
    if (!form.reportValidity()) return;
    const data = Object.fromEntries(new FormData(form).entries());
    Object.keys(data).forEach((key) => { if (typeof data[key] === 'string') data[key] = data[key].trim(); });
    const submittedType = reportType();
    data.report_type = submittedType;
    const photoFile = photoInput?.files?.[0] || null;
    delete data.photo;
    if (data.item_date > localDate()) { showMessage(message, 'Choose a date that is today or earlier.', true); return; }
    if (photoFile && (!['image/jpeg', 'image/png', 'image/webp'].includes(photoFile.type) || photoFile.size > 5 * 1024 * 1024)) {
      showMessage(message, 'Choose a JPEG, PNG, or WebP image no larger than 5 MB.', true); return;
    }
    const button = form.querySelector('button[type=submit]'); button.disabled = true; button.textContent = 'Saving report…';
    let uploadedPath = '';
    try {
      if (photoFile) {
        const client = getAuthClient();
        const session = await getSession();
        if (!client || !session?.user?.id) throw new Error('Please sign in again before uploading a photo.');
        const extension = { 'image/jpeg': 'jpg', 'image/png': 'png', 'image/webp': 'webp' }[photoFile.type];
        uploadedPath = `${session.user.id}/${crypto.randomUUID()}.${extension}`;
        const { error: uploadError } = await client.storage.from(PHOTO_BUCKET).upload(uploadedPath, photoFile, {
          cacheControl: '3600', contentType: photoFile.type, upsert: false
        });
        if (uploadError) throw uploadError;
        data.photo_path = uploadedPath;
      }
      const result = await apiRequest('/api/items', { method: 'POST', auth: 'required', body: JSON.stringify(data) });
      showMessage(message, `${result.message} Your report is now open.`);
      const success = byId('report-success');
      success.hidden = false;
      const findMatches = byId('report-find-matches');
      findMatches.hidden = submittedType !== 'lost';
      findMatches.href = `/matches.html?lost_id=${encodeURIComponent(result.id)}`;
      byId('report-view-item').onclick = () => openItemDetails(result.id);
      form.reset();
      if (fixedType) typeChoices.forEach((choice) => { choice.checked = choice.value === fixedType; });
      if (!fixedType) typeChoices.forEach((choice) => { choice.checked = choice.value === submittedType; });
      clearPhoto(); form.elements.item_date.max = localDate(); updateReportType();
    } catch (error) {
      if (uploadedPath) {
        try { await getAuthClient()?.storage.from(PHOTO_BUCKET).remove([uploadedPath]); } catch { /* Leave an inaccessible orphan if cleanup fails. */ }
      }
      showMessage(message, error.message, true);
    }
    finally { button.disabled = false; updateReportType(); }
  });
}

function appendMatch(container, match, lostItem) {
  if (!match?.found_item || !lostItem) return false;
  const card = makeElement('article', 'match-card');
  for (const [label, item] of [['Your lost report', lostItem], ['Potential found report', match.found_item]]) {
    const info = makeElement('div', 'match-item');
    info.append(makeElement('div', 'match-item-label', label));
    attachItemPhoto(info, item, 'match');
    info.append(makeElement('h3', '', item.item_name),
      makeElement('p', '', `${item.category} · ${formatDate(item.item_date)}`),
      makeElement('p', '', item.description), makeElement('p', '', item.location));
    const details = makeElement('button', 'item-action details-action', 'View details');
    details.type = 'button'; details.addEventListener('click', () => openItemDetails(item.id));
    info.append(details);
    card.append(info);
  }
  const score = makeElement('div', 'match-score');
  const number = makeElement('div', 'score-number'); number.append(document.createTextNode(String(match.score)), makeElement('small', '', '/100'));
  score.append(number, makeElement('div', 'score-strength', match.strength));
  const factors = makeElement('div', 'factor-list');
  for (const [key, label] of [['category', 'Category'], ['name', 'Name'], ['description', 'Description'], ['location', 'Location'], ['date', 'Date']]) {
    factors.append(makeElement('span', 'factor-chip', `${label} ${Number(match.factors?.[key]) || 0}`));
  }
  score.append(factors); card.append(score);
  card.append(makeElement('p', 'match-next-step', 'Compare distinctive details and arrange a safe campus handoff. Contact details are kept private.'));
  container.append(card);
  return true;
}

async function initMatches() {
  const select = byId('lost-report-select'); const status = byId('match-status');
  const results = byId('match-results');
  try {
    const payload = await apiRequest('/api/items?type=lost&mine=true', { auth: 'required' });
    const open = payload.items.filter((item) => item.status === 'open');
    select.replaceChildren(new Option(open.length ? 'Choose an open lost report' : 'You have no open lost reports', ''));
    open.forEach((item) => select.add(new Option(`${item.item_name} · ${item.category} · ${formatDate(item.item_date)}`, item.id)));
    const requestedId = new URLSearchParams(window.location.search).get('lost_id');
    if (requestedId && open.some((item) => item.id === requestedId)) select.value = requestedId;
  } catch (error) { select.replaceChildren(new Option(error.message, '')); status.textContent = error.message; }
  byId('find-matches')?.addEventListener('click', async () => {
    results.replaceChildren();
    const id = select.value;
    if (!id) { status.textContent = 'Choose one of your open lost reports first.'; return; }
    status.textContent = 'C is comparing your report with open found reports…';
    try {
      const payload = await apiRequest(`/api/matches?lost_id=${encodeURIComponent(id)}`, { auth: 'required' });
      if (!Array.isArray(payload.matches)) {
        status.textContent = 'The matches service returned an invalid response. Try again in a moment.';
        return;
      }
      const matches = payload.matches;
      if (matches.length && (!payload.lost_item || matches.some((match) => !match?.found_item))) {
        status.textContent = 'Matches were returned in an unexpected format. Refresh the page and try again.';
        return;
      }
      status.textContent = matches.length
        ? `${matches.length} potential match${matches.length === 1 ? '' : 'es'} found, ranked by score.`
        : 'No potential matches scored 40 or higher yet.';
      matches.forEach((match) => appendMatch(results, match, payload.lost_item));
    } catch (error) { status.textContent = error.message; }
  });
  if (select.value && new URLSearchParams(window.location.search).has('lost_id')) {
    byId('find-matches')?.click();
  }
}

function togglePasswordButtons() {
  document.querySelectorAll('.password-toggle').forEach((button) => button.addEventListener('click', () => {
    const input = button.parentElement.querySelector('input');
    const visible = input.type === 'password'; input.type = visible ? 'text' : 'password';
    button.textContent = visible ? 'Hide' : 'Show'; button.setAttribute('aria-label', visible ? 'Hide password' : 'Show password');
  }));
}

function initLogin(auth) {
  const form = byId('login-form'); if (!form) return;
  togglePasswordButtons();
  form.addEventListener('submit', async (event) => {
    event.preventDefault(); const message = byId('form-message');
    const identifier = form.elements.identifier.value.trim(); const password = form.elements.password.value;
    let credentials;
    if (isEmail(identifier)) credentials = { email: identifier, password };
    else {
      const phone = normalizePhone(identifier);
      if (!phone) { showMessage(message, 'Enter a valid email or phone in international format, such as +91 98765 43210.', true); return; }
      credentials = { phone, password };
    }
    const button = form.querySelector('button[type=submit]'); button.disabled = true; button.textContent = 'Signing in…';
    const { error } = await auth.auth.signInWithPassword(credentials);
    button.disabled = false; button.textContent = 'Sign in →';
    if (error) { showMessage(message, error.message, true); return; }
    sessionStorage.removeItem('unilost-password-recovery');
    window.location.assign(safeNextPath());
  });
}

function initRegister(auth) {
  const form = byId('register-form'); if (!form) return;
  const phoneVerifyForm = byId('signup-verify-form');
  let pendingPhoneNumber = '';
  form.addEventListener('submit', async (event) => {
    event.preventDefault(); const message = byId('form-message');
    if (!form.reportValidity()) return;
    const fullName = form.elements.full_name.value.trim();
    const email = form.elements.email.value.trim();
    const phone = normalizePhone(form.elements.phone.value);
    const password = form.elements.password.value;
    const confirm = form.elements.confirm_password.value;
    const method = form.elements.login_method.value;
    if (!phone) { showMessage(message, 'Enter a phone number in international format, such as +91 98765 43210.', true); return; }
    if (password.length < 10) { showMessage(message, 'Choose a password with at least 10 characters.', true); return; }
    if (password !== confirm) { showMessage(message, 'The passwords do not match.', true); return; }
    const credentials = {
      password,
      options: { data: { full_name: fullName, contact_email: email, contact_phone: phone } }
    };
    if (method === 'email') credentials.email = email;
    else credentials.phone = phone;
    const button = form.querySelector('button[type=submit]'); button.disabled = true; button.textContent = 'Creating account…';
    const { data, error } = await auth.auth.signUp(credentials);
    button.disabled = false; button.textContent = 'Create account →';
    if (error) { showMessage(message, error.message, true); return; }
    if (data.session) {
      showMessage(message, 'Account created and signed in.');
      window.location.assign(safeNextPath());
    } else {
      if (method === 'phone') {
        pendingPhoneNumber = phone;
        phoneVerifyForm.classList.remove('hidden');
        showMessage(message, 'Supabase accepted the phone registration. Enter the SMS code if it arrives; the other contact stays unverified until you confirm it in Profile.');
      } else {
        showMessage(message, 'Supabase accepted registration. Confirm your email before signing in. The other contact stays unverified until you confirm it in Profile.');
      }
    }
  });
  phoneVerifyForm?.addEventListener('submit', async (event) => {
    event.preventDefault(); const message = byId('signup-verify-message');
    const token = phoneVerifyForm.elements.token.value.trim();
    const { data, error } = await auth.auth.verifyOtp({ phone: pendingPhoneNumber, token, type: 'sms' });
    if (error) { showMessage(message, error.message, true); return; }
    if (!data.session) { showMessage(message, 'Supabase did not create a verified session. Try signing in after confirming the number.', true); return; }
    showMessage(message, 'Phone verified. Your account is ready.');
    window.location.assign(safeNextPath());
  });
}

function initForgot(auth) {
  const form = byId('forgot-form'); if (!form) return;
  const phoneForm = byId('phone-recovery-form');
  let pendingPhoneNumber = '';
  form.addEventListener('submit', async (event) => {
    event.preventDefault(); const message = byId('form-message');
    const identifier = form.elements.identifier.value.trim();
    if (isEmail(identifier)) {
      const { error } = await auth.auth.resetPasswordForEmail(identifier, {
        redirectTo: `${window.location.origin}/reset-password.html`
      });
      if (error) showMessage(message, error.message, true);
      else showMessage(message, 'If an account matches that email, Supabase will send recovery instructions. Check your inbox and spam folder.');
      return;
    }
    pendingPhoneNumber = normalizePhone(identifier);
    if (!pendingPhoneNumber) { showMessage(message, 'Enter a valid email or phone in international format, such as +91 98765 43210.', true); return; }
    const { error } = await auth.auth.signInWithOtp({ phone: pendingPhoneNumber, options: { shouldCreateUser: false } });
    if (error) { showMessage(message, error.message, true); return; }
    showMessage(message, 'Supabase accepted the SMS request. Enter the code if it arrives.');
    phoneForm.classList.remove('hidden'); phoneForm.dataset.phone = pendingPhoneNumber;
  });
  phoneForm?.addEventListener('submit', async (event) => {
    event.preventDefault(); const message = byId('phone-message');
    const token = phoneForm.elements.token.value.trim();
    const { data, error } = await auth.auth.verifyOtp({ phone: phoneForm.dataset.phone || pendingPhoneNumber, token, type: 'sms' });
    if (error) { showMessage(message, error.message, true); return; }
    if (!data.session) { showMessage(message, 'Supabase did not create a verified session. Try again or use email recovery.', true); return; }
    sessionStorage.setItem('unilost-password-recovery', 'true');
    window.location.assign('/reset-password.html');
  });
}

function initReset(auth, session) {
  const form = byId('reset-form'); if (!form) return;
  const fromRecoveryLink = new URLSearchParams(window.location.hash.slice(1)).get('type') === 'recovery';
  const fromPhoneOtp = sessionStorage.getItem('unilost-password-recovery') === 'true';
  if (session && (fromRecoveryLink || fromPhoneOtp)) {
    form.classList.remove('hidden'); byId('reset-intro').textContent = 'Set a new password for the account that completed recovery.';
  } else {
    byId('reset-intro').textContent = 'This page needs a valid Supabase recovery link or verified phone recovery session. Request a new link or code if this one expired.';
  }
  form.addEventListener('submit', async (event) => {
    event.preventDefault(); const message = byId('form-message');
    const password = form.elements.password.value;
    if (password.length < 10 || password !== form.elements.confirm_password.value) {
      showMessage(message, password.length < 10 ? 'Use at least 10 characters.' : 'The passwords do not match.', true); return;
    }
    const { error } = await auth.auth.updateUser({ password });
    if (error) { showMessage(message, error.message, true); return; }
    sessionStorage.removeItem('unilost-password-recovery');
    showMessage(message, 'Password updated.');
    window.setTimeout(() => window.location.assign('/profile.html'), 900);
  });
}

function renderClaimList(container, claims, received) {
  container.replaceChildren();
  if (!claims.length) {
    renderEmpty(container, received ? 'No one has requested a private handoff for your reports yet.' : 'You have not sent any private handoff requests yet.');
    return;
  }
  claims.forEach((claim) => {
    const card = makeElement('article', 'claim-card');
    const head = makeElement('div', 'claim-card-head');
    head.append(makeElement('h4', '', `${claim.item_name} · ${claim.category}`),
      makeElement('span', `status-tag ${claim.status}`, claim.status));
    card.append(head, makeElement('p', '', `${claim.report_type === 'lost' ? 'Lost report' : 'Found report'} · ${formatDate(claim.created_at)}`),
      makeElement('p', '', claim.message));
    if (received && claim.requester_contact) {
      card.append(makeElement('p', 'claim-contact', `Verified requester contact: ${claim.requester_contact}`));
    }
    if (received && claim.status === 'pending') {
      const actions = makeElement('div', 'claim-actions');
      for (const [status, label, className] of [['accepted', 'Accept request', 'accept'], ['declined', 'Decline', 'decline']]) {
        const button = makeElement('button', `item-action ${className}`, label); button.type = 'button';
        button.addEventListener('click', async () => {
          if (!window.confirm(`${label}? The requester will see this status in their profile.`)) return;
          button.disabled = true;
          try {
            await apiRequest(`/api/claims/${encodeURIComponent(claim.id)}`, {
              method: 'PATCH', auth: 'required', body: JSON.stringify({ status })
            });
            await loadClaimInbox();
          } catch (error) { toast(error.message); button.disabled = false; }
        });
        actions.append(button);
      }
      card.append(actions);
    }
    container.append(card);
  });
}

async function loadClaimInbox() {
  try {
    const [received, sent] = await Promise.all([
      apiRequest('/api/claims?scope=received', { auth: 'required' }),
      apiRequest('/api/claims?scope=sent', { auth: 'required' })
    ]);
    renderClaimList(byId('received-claims'), received.claims, true);
    renderClaimList(byId('sent-claims'), sent.claims, false);
  } catch (error) {
    renderEmpty(byId('received-claims'), error.message);
    renderEmpty(byId('sent-claims'), 'Your sent requests are unavailable.');
  }
}

async function initProfile() {
  const profileForm = byId('profile-form');
  if (!profileForm) return;
  const auth = getAuthClient();
  let pendingPhoneNumber = '';
  let savedName = '';
  try {
    const { profile } = await apiRequest('/api/profile', { auth: 'required' });
    savedName = profile.full_name || '';
    profileForm.elements.full_name.value = savedName;
    byId('profile-avatar').textContent = savedName.trim().charAt(0).toLocaleUpperCase() || 'U';
    byId('profile-email').value = profile.email || profile.contact_email || '';
    byId('profile-phone').value = profile.phone || profile.contact_phone || '';
    byId('account-created').textContent = profile.created_at
      ? `Account created ${formatDate(profile.created_at)}` : '';
    if (profile.email && !profile.email_confirmed) {
      showMessage(byId('email-message'), 'This email is awaiting Supabase confirmation.');
    } else if (profile.contact_email && !profile.email) {
      showMessage(byId('email-message'), 'This contact email is unverified. Request confirmation before using it to sign in.');
    }
    if (profile.phone && !profile.phone_confirmed) {
      showMessage(byId('phone-message'), 'This phone is awaiting Supabase verification.');
    }
  } catch (error) {
    showMessage(byId('profile-message'), error.message, true);
  }
  await loadClaimInbox();
  await loadMyItems();

  const nameInput = profileForm.elements.full_name;
  const editButton = byId('profile-edit');
  const saveButton = byId('profile-save');
  const cancelButton = byId('profile-cancel');
  editButton?.addEventListener('click', () => {
    nameInput.disabled = false;
    editButton.hidden = true; saveButton.hidden = false; cancelButton.hidden = false;
    nameInput.focus();
  });
  cancelButton?.addEventListener('click', () => {
    nameInput.value = savedName; nameInput.disabled = true;
    editButton.hidden = false; saveButton.hidden = true; cancelButton.hidden = true;
    showMessage(byId('profile-message'), '');
  });

  profileForm.addEventListener('submit', async (event) => {
    event.preventDefault();
    const message = byId('profile-message');
    if (!profileForm.reportValidity()) return;
    saveButton.disabled = true;
    try {
      await apiRequest('/api/profile', {
        method: 'PATCH', auth: 'required',
        body: JSON.stringify({ full_name: nameInput.value.trim() })
      });
      savedName = nameInput.value.trim(); nameInput.value = savedName; nameInput.disabled = true;
      byId('profile-avatar').textContent = savedName.charAt(0).toLocaleUpperCase() || 'U';
      editButton.hidden = false; saveButton.hidden = true; cancelButton.hidden = true;
      showMessage(message, 'Your profile was saved.');
      const navName = document.querySelector('.nav-user');
      if (navName) navName.textContent = savedName;
    } catch (error) { showMessage(message, error.message, true); }
    finally { saveButton.disabled = false; }
  });

  byId('request-email-verification')?.addEventListener('click', async () => {
    const input = byId('profile-email');
    const message = byId('email-message');
    if (!input.value || !input.checkValidity()) {
      showMessage(message, 'Enter a valid email address first.', true); return;
    }
    const session = await getSession();
    if (session?.user?.email === input.value && session.user.email_confirmed_at) {
      showMessage(message, 'This email is already confirmed.'); return;
    }
    const { error } = await auth.auth.updateUser({ email: input.value.trim() });
    if (error) showMessage(message, error.message, true);
    else showMessage(message, 'Supabase accepted the email change. Follow its confirmation link; your email changes only after verification.');
  });

  byId('request-phone-verification')?.addEventListener('click', async () => {
    const phone = normalizePhone(byId('profile-phone').value);
    const message = byId('phone-message');
    if (!phone) {
      showMessage(message, 'Enter a phone number in international format, such as +91 98765 43210.', true); return;
    }
    const session = await getSession();
    if (session?.user?.phone === phone && session.user.phone_confirmed_at) {
      showMessage(message, 'This phone is already verified.'); return;
    }
    const { error } = await auth.auth.updateUser({ phone });
    if (error) { showMessage(message, error.message, true); return; }
    pendingPhoneNumber = phone;
    byId('phone-change-form').classList.remove('hidden');
    showMessage(message, 'Supabase accepted the phone change. Enter the code if it arrives.');
  });

  byId('phone-change-form')?.addEventListener('submit', async (event) => {
    event.preventDefault();
    const form = event.currentTarget;
    const { error } = await auth.auth.verifyOtp({
      phone: pendingPhoneNumber, token: form.elements.token.value.trim(), type: 'phone_change'
    });
    if (error) { showMessage(byId('phone-message'), error.message, true); return; }
    showMessage(byId('phone-message'), 'Phone verified through Supabase Auth.');
    byId('phone-change-form').classList.add('hidden');
    pendingPhoneNumber = '';
  });

  byId('password-form')?.addEventListener('submit', async (event) => {
    event.preventDefault();
    const form = event.currentTarget;
    const message = byId('password-message');
    const password = form.elements.password.value;
    if (password.length < 10 || password !== form.elements.confirm_password.value) {
      showMessage(message, password.length < 10 ? 'Use at least 10 characters.' : 'The passwords do not match.', true); return;
    }
    const { error } = await auth.auth.updateUser({
      password, current_password: form.elements.current_password.value
    });
    if (error) showMessage(message, error.message, true);
    else { form.reset(); showMessage(message, 'Password updated through Supabase Auth.'); }
  });
  byId('profile-logout')?.addEventListener('click', logoutUser);
}

async function initialize() {
  renderFooter();
  let auth = null, session = null, authReady = false;
  try {
    auth = await initAuth(); authReady = Boolean(auth);
    if (auth) {
      const { data: listener } = auth.auth.onAuthStateChange((event) => {
        if (event === 'PASSWORD_RECOVERY') sessionStorage.setItem('unilost-password-recovery', 'true');
        if (event === 'SIGNED_OUT') sessionStorage.removeItem('unilost-password-recovery');
      });
      void listener;
      session = await getSession();
      await new Promise((resolve) => window.setTimeout(resolve, 0));
      session = await getSession();
    }
  } catch (error) {
    const main = byId('main');
    const note = makeElement('p', 'form-message error', `Supabase client could not initialize: ${error.message}`);
    main?.prepend(note);
  }
  currentSession = session;
  renderHeader(session);
  if (!authReady && ['login', 'register', 'forgot', 'reset'].includes(page)) {
    const note = makeElement('p', 'form-message error', 'Authentication is not configured. Add SUPABASE_URL and SUPABASE_ANON_KEY to .env, then restart the C server.');
    byId('main')?.prepend(note);
    document.querySelectorAll('form button[type=submit]').forEach((button) => { button.disabled = true; });
  }
  if (auth && session && ['login', 'register'].includes(page)) {
    window.location.replace(safeNextPath()); return;
  }
  if (!(await routeGuard(session, authReady))) return;

  if (page === 'legacy') {
    window.location.replace(document.body.dataset.destination || '/reports.html');
    return;
  }
  ensureItemDetailsDialog();
  initClaimDialog();
  if (page === 'home') {
    byId('dashboard-retry')?.addEventListener('click', loadHome);
    await loadHome();
  }
  if (page === 'items') initBrowse();
  if (page === 'report') initReport();
  if (page === 'matches') { ensureItemDetailsDialog(); await initMatches(); }
  if (page === 'login' && auth) initLogin(auth);
  if (page === 'register' && auth) initRegister(auth);
  if (page === 'forgot' && auth) initForgot(auth);
  if (page === 'reset' && auth) initReset(auth, session);
  if (page === 'profile') await initProfile();
}

initialize();
