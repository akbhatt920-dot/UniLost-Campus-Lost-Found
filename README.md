# UniLost — Campus Lost-and-Found

UniLost is a campus lost-and-found service. Supabase Auth handles accounts, the Mongoose HTTP server and matching algorithm remain in C, and Supabase PostgreSQL stores reports, profiles, and private handoff requests.

## Pages

| URL | Purpose |
|---|---|
| `/` or `/index.html` | Authenticated dashboard with the signed-in user's report and match totals, their recent reports, and recently found items; logged-out visitors are sent to Login |
| `/login.html` | Sign in with email or a verified phone number and password |
| `/register.html` | Create an account; choose whether email or phone is the initial sign-in identifier |
| `/forgot-password.html` | Request an email recovery link or, when configured, phone OTP recovery |
| `/reset-password.html` | Set a new password after a valid recovery session |
| `/report-item.html?type=lost` | Protected Report Lost form with an optional photo |
| `/report-item.html?type=found` | Protected Report Found form with an optional photo |
| `/matches.html` | Protected C-ranked matches for the signed-in user's open lost reports |
| `/reports.html?type=found` | Protected Browse Found Items board with search, category, location, status, date, sort, and page controls |
| `/profile.html` | Protected profile editing, My Lost Items, My Found Items, verified contacts, handoff inbox, password change, and Sign Out |

The authenticated navigation has exactly six entries: **Home, Report Lost, Report Found, Matches, Browse Found Items, Profile**. The older `/lost-items.html`, `/found-items.html`, `/report-lost.html`, and `/report-found.html` addresses remain protected redirects for compatibility; they are not extra navigation items.

Only sign-in, account creation, and password recovery are available without an account session. Protected pages and data APIs require a signed-in account. A protected deep link returns to its original page after login. Sign Out is available inside Profile, not in the shared navigation.

## Architecture

```text
Browser (HTML/CSS/JS + Supabase Auth client)
       │ all application data requests carry the Auth access token
       ▼
Mongoose HTTP server in C ── HTTPS ──► Supabase Auth /user token verification
       │                                  │ verified Auth user ID
       ▼                                  ▼
libpq over TLS ──► Supabase PostgreSQL ◄── C ownership filters
       │
       └── C matcher normalizes text, scores, explains, and ranks matches
```

The frontend never connects directly to the database. It sends item and profile API requests to the C server. For protected requests, C sends the bearer token to the configured Supabase Auth `/auth/v1/user` endpoint over macOS CFNetwork HTTPS. Supabase Auth validates the token and returns the authoritative user record; C uses that returned ID and never treats decoded, unverified JWT claims as identity. This uses Supabase's supported Auth-server verification path described in its [JWT verification guidance](https://supabase.com/docs/guides/auth/jwts). The C server performs owner-scoped queries and only returns public item fields. The configured publishable/anon key is public by design; service-role and secret keys are rejected by the config endpoint and must never be placed in `.env` for this app.

The Auth verification request is synchronous in this localhost classroom server, so a slow Auth response temporarily blocks other requests. Do not expose this prototype to the public internet without rate limiting, asynchronous upstream calls, deployment hardening, and a security review.

## Requirements

- macOS with Apple Clang and `make`
- Homebrew `libpq` (`brew install libpq` if needed)
- A Supabase project with PostgreSQL and Auth enabled
- Network access from the C server to Supabase Auth and PostgreSQL

The build uses Homebrew's detected `libpq` prefix and macOS's built-in CoreFoundation/CFNetwork frameworks. No Node.js server or Node build step is used. The browser imports the official `@supabase/supabase-js` package as an ES module from esm.sh.

## Set up Supabase

### 1. Check the existing tables

For an existing project, inspect the `items` and `matches` columns in Supabase **Table Editor** before applying any SQL. The Auth migration adapts `claims.item_id` to the existing `items.id` type, which may be UUID or bigint. Existing item rows are preserved.

### 2. Apply SQL

- For a new, empty UniLost database, review and run `database/schema.sql`, then run `database/migrations/20261022_profiles_auth_ownership.sql` and `database/migrations/20261022_found_item_photos.sql`.
- For an existing project, preserve `items` and `matches`, inspect their columns in Table Editor, then run `database/migrations/20261022_profiles_auth_ownership.sql` followed by `database/migrations/20261022_found_item_photos.sql`. These migrations add Auth ownership, claims, and private photo storage without deleting existing item data.
- The Auth and ownership migration creates `unilost_backend` if it is missing and grants the app's database access. The role still needs a login and password before the C server can connect.

The migration does not update Supabase's internal Auth tables directly. Its trigger only reads `auth.users` insert/phone-confirmation events and mirrors verified profile fields into `public.profiles`. Email and phone changes still use Supabase Auth APIs.

### 3. Set the backend database password

The baseline schema creates `unilost_backend` as a non-login role. After applying the SQL above, enable its login and set a unique password from Terminal. Use the host shown in the Supabase **Connect** dialog; `psql` will prompt for the project owner's password and then the new backend password:

```sh
export PATH="$(brew --prefix libpq)/bin:$PATH"
psql "host=DB_HOST_FROM_SUPABASE dbname=postgres user=postgres sslmode=require"
```

At the `psql` prompt, run:

```sql
ALTER ROLE unilost_backend LOGIN;
```

Then use `\password unilost_backend` and `\q`. The password is entered interactively, not stored in SQL history. Copy the backend role's connection string or parameters from Supabase **Connect**, use the new password, and URL-encode reserved password characters. Use the direct connection when it works on your network, or the session pooler settings if the network is IPv4-only.

### 4. Enable email and phone providers

In **Supabase → Authentication → Sign In / Providers**, keep Email enabled. Configure email confirmations and an SMTP sender appropriate for your project; Supabase's default sender may be rate-limited or restricted. In **URL Configuration**, set the local Site URL to:

```text
http://127.0.0.1:8000
```

Add this exact allowed redirect URL for password recovery:

```text
http://127.0.0.1:8000/reset-password.html
```

If you open the app using `http://localhost:8000`, add its reset URL separately and use the same hostname consistently. Supabase requires redirect URLs to be on the allow list. See [Supabase redirect URL setup](https://supabase.com/docs/guides/auth/redirect-urls) and [password-based Auth](https://supabase.com/docs/guides/auth/passwords).

Phone/password registration and phone/password login work only when the phone provider is enabled and the account's phone number is verified. UniLost requires E.164 input such as `+919876543210`. To send verification and recovery SMS, configure a supported SMS provider (for example Twilio, MessageBird, or Vonage), its credentials, allowed destinations, and rate limits in Supabase. The app displays the actual Supabase error if phone auth or SMS delivery is unavailable. Phone-number recycling is a security risk; phone-based accounts should use MFA where appropriate.

### Found-item photos

Run `database/migrations/20261022_found_item_photos.sql` in the Supabase SQL Editor after the Auth and ownership migration. It creates a private `unilost-found-photos` bucket for JPEG, PNG, and WebP images up to 5 MB. Signed-in users can view photos; each user can upload and delete files only in their own folder. Report Lost and Report Found both accept one optional photo and preview it before submission. Photos are loaded through authenticated Storage downloads, then displayed from a temporary browser object URL. Avoid uploading faces, student IDs, or other private details.

The registration page requires both email and phone for contact, then asks which one is the initial login identity. Supabase's signup call creates the selected identity; the other contact remains unverified metadata until the user adds and confirms it in Profile. Supabase provider confirmation settings control whether an identity can sign in; UniLost displays confirmed state only when Supabase Auth reports it.

Email recovery uses `resetPasswordForEmail()` and displays a neutral response to avoid revealing whether an account exists. Phone recovery requests an SMS OTP, verifies it with Supabase, then allows the authenticated user to set a new password. Email delivery, SMS delivery, and phone-provider behavior depend on your Supabase project configuration and must be verified in that project.

### 5. Create local configuration

Create a file named `.env` in the project root and add your local values:

```sh
DATABASE_URL='YOUR_POSTGRES_CONNECTION_STRING?sslmode=require'
SUPABASE_URL='https://YOUR_PROJECT_REF.supabase.co'
SUPABASE_ANON_KEY='YOUR_SUPABASE_PUBLISHABLE_OR_ANON_KEY'
```

- `DATABASE_URL`: PostgreSQL connection string for the dedicated `unilost_backend` login, with TLS (`sslmode=require`). Use the Supabase direct connection if your network supports it, or session pooler on IPv4-only networks. Percent-encode reserved characters in the password.
- `SUPABASE_URL`: project URL from Supabase API settings, such as `https://your-project-ref.supabase.co`.
- `SUPABASE_ANON_KEY`: the publishable key (`sb_publishable_…`) or legacy anon JWT key from Supabase API settings. **Never use a secret or `service_role` key.** The C config endpoint will not return those key types to the browser.

Keep `.env` private. It is ignored by Git. The Makefile loads `.env` only for `make run`; do not paste a database password or secret key into frontend files or commit them. The publishable/anon key is intentionally public and is used only for Supabase Auth requests.

If the earlier database setup did not create a login for `unilost_backend`, review the role instructions in `database/schema.sql` and the prior setup notes. Use a unique password and do not use the Supabase `postgres` or service-role credential as the app's `DATABASE_URL`.

## Build and run

From the project root:

```sh
make build
make test
make run
```

Open [http://127.0.0.1:8000](http://127.0.0.1:8000). Press **Ctrl+C** in the server terminal to stop it. `make run` builds the C server if needed and loads `.env`. Sign-in, registration, and password recovery are available without a session; the home dashboard and all item, match, claim, and profile features require a valid Supabase session. If Supabase Auth is not configured, feature pages show a setup message and protected APIs deny requests.

Useful endpoints:

| Endpoint | Access |
|---|---|
| `GET /api/health` | Public server/database readiness |
| `GET /api/config` | Public Supabase URL and publishable/anon key only; needed to initialize Auth |
| `GET /api/summary` | Authenticated; counts the signed-in user's lost reports, found reports, and stored matches |
| `GET /api/items?type=found&category=Wallet&search=black&location=library&status=open&date_from=2026-10-01&date_to=2026-10-31&sort=newest&limit=18&offset=0` | Authenticated; parameterized filters and bounded pages, with `total` and `has_more` |
| `GET /api/items?type=lost&mine=true` | Authenticated; only the signed-in user's own reports |
| `GET /api/items/:id` | Authenticated; safe item fields and viewer ownership flags, never contact details |
| `POST /api/items` | Authenticated; C validates and saves owner ID from Supabase Auth |
| `GET /api/matches?lost_id=UUID` | Authenticated; only the owner of the lost report can request it |
| `PATCH /api/items/:id/status` | Authenticated; only the item owner can update an open item |
| `GET /api/profile` | Authenticated; only the verified account's own profile |
| `PATCH /api/profile` | Authenticated; updates the verified account's display name |
| `POST /api/items/:id/claims` | Authenticated; shares a verified contact only with the report owner |
| `GET /api/claims?scope=received` | Authenticated; incoming requests and requester contact for the signed-in owner's items |
| `GET /api/claims?scope=sent` | Authenticated; the signed-in user's own outgoing request statuses, without owner contact |
| `PATCH /api/claims/:id` | Authenticated; only the report owner can accept or decline a pending request |

Email and phone changes use Supabase Auth in the browser and require verification. The C API never accepts a browser-supplied user ID as proof of ownership.

The C API validates status, dates, sort, page size, and offset before issuing parameterized SQL. Page size is at most 50. The optional `mine=true` filter always uses the verified Auth user ID returned by Supabase Auth.

On item cards, a signed-in user can send a private handoff request. C requires at least one confirmed Auth email or phone, saves that verified contact in `claims`, and returns it only in the report owner's authenticated inbox. The requester sees only their own request status. The owner can accept or decline from Profile; there is no public contact field or automatic email/SMS notification. The owner must check the inbox and arrange a safe handoff.

## C matching algorithm

For one signed-in user's open lost report, C compares up to 200 recent open found reports. It normalizes text to lowercase words and calculates unique-word Jaccard overlap. The parts add to 100:

| Factor | Maximum |
|---|---:|
| Exact normalized category | 30 |
| Item name overlap | 20 |
| Description overlap | 20 |
| Location overlap | 15 |
| Date proximity | 15 |

Results scoring 40 or more are stored once per lost/found pair and sorted highest first using the existing beginner-friendly bubble sort. Scores 70–100 are labeled Strong potential match; 40–69 Possible match. The explanation displays each factor. A score is only a lead, not proof of ownership.

## Tests and verification

Run `make build` to compile the C server and `make test` for standalone matcher and public-key safety tests; the tests do not need Supabase. Browser interaction, email-link delivery, password recovery, SMS, photo Storage policies, profile persistence, and owner-bound PostgreSQL actions require the configured Supabase project and must be tested there. A passing C build or unit test does not establish that hosted flows are working.

See [`docs/API_TESTING.md`](docs/API_TESTING.md) for curl examples. Use a fresh test account and non-sensitive fictional reports. Do not place an access token in shell history or share it.

## Security model and limits

- Supabase Auth stores passwords and issues sessions. UniLost does not store password hashes.
- C asks the configured Supabase Auth `/user` endpoint to verify each protected bearer token over TLS. The returned Auth user ID is the only ID used for owner checks. Missing or invalid tokens receive `401`; an Auth outage receives `503`.
- Public item JSON omits contact names and contact channels. Profile routes are protected. Browser code receives only a publishable/anon key; service-role keys are rejected from public config.
- A claimant's verified email or phone is stored in the private `claims` table only after they choose to request a handoff. C returns it only to the owner of that specific item. Sent-request responses omit owner contact details.
- The dedicated PostgreSQL role has no `BYPASSRLS`. It is a trusted server role with table access; database ownership isolation is enforced by C's verified-user filters and must not be bypassed by adding a browser database connection.
- Existing unowned items remain visible to signed-in users with no account owner. They cannot be changed by a newly registered user.
- This classroom prototype has no CAPTCHA, rate limiting, campus SSO, account-based moderation, or automated claim notifications. A report owner must check Profile for requests. C Auth verification is synchronous, and DB/Auth calls can block this single-threaded local server. Keep the server on localhost and do not deploy it publicly as-is.
- Contact details are stored in PostgreSQL for follow-up. They are omitted from public API output but are not separately encrypted at the application layer.

## Project structure

```text
backend/       C HTTP server, Supabase Auth verification, libpq queries, C matcher, Mongoose 7.23
frontend/      multi-page HTML app, shared responsive stylesheet, browser Auth/API code
database/      baseline schema and additive Auth/ownership and private-photo migrations
tests/         matcher and public-key safety tests
docs/          API checks and system flow
Makefile       Apple Clang build, run, test, clean
```

See [`docs/system-flow.md`](docs/system-flow.md) for the request and matching flow.

## Viva explanation

“The frontend has separate pages for account access, reporting, browsing, matches, and profile management. Supabase Auth handles passwords and verification. The C server uses Mongoose to receive HTTP requests and libpq parameterized queries to store reports in PostgreSQL. For private actions, C asks Supabase Auth to validate the access token, then uses the verified account ID in owner-filtered SQL. The C matcher normalizes report text, scores category, name, description, location, and date, then bubble-sorts possible matches from highest score to lowest. A signed-in person can send a private handoff request; only the report owner can view the requester's verified contact in Profile.”
