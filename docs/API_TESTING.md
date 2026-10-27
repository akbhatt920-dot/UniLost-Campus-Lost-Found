# UniLost API checks

Start the C server with `make run`. Health and public Supabase configuration are unauthenticated. All item, dashboard, profile, match, and claim data routes require a Supabase Auth access token, verified by C against the configured Auth server.

## Public routes

```sh
curl -i http://127.0.0.1:8000/api/health
curl -i http://127.0.0.1:8000/api/config
```

The config response contains only the Supabase project URL and a publishable/anon key. Health reports whether PostgreSQL is connected. No report or user data route is public.

## Protected routes

Sign in through UniLost in a browser. For local manual API testing, retrieve the access token from the Supabase client only in a private local session; do not paste a real token into shell history, a ticket, or a shared log. Replace `TOKEN` below with a temporary test-session token in a private terminal:

```sh
curl -i http://127.0.0.1:8000/api/profile \
  -H 'Authorization: Bearer TOKEN'

curl -i 'http://127.0.0.1:8000/api/items?type=lost&mine=true' \
  -H 'Authorization: Bearer TOKEN'

# Browse found reports with filters and page by 18 results.
curl -i 'http://127.0.0.1:8000/api/items?type=found&search=wallet&category=Wallet&location=library&status=open&date_from=2026-10-01&date_to=2026-10-31&sort=newest&limit=18&offset=0' \
  -H 'Authorization: Bearer TOKEN'

# Authenticated personal dashboard totals.
curl -i http://127.0.0.1:8000/api/summary \
  -H 'Authorization: Bearer TOKEN'

# Retrieve safe item details; response does not contain contact information.
curl -i http://127.0.0.1:8000/api/items/PASTE_ITEM_ID \
  -H 'Authorization: Bearer TOKEN'

curl -i -X POST http://127.0.0.1:8000/api/items \
  -H 'Authorization: Bearer TOKEN' \
  -H 'Content-Type: application/json' \
  --data '{
    "report_type":"lost",
    "item_name":"Black leather wallet",
    "category":"Wallet",
    "description":"Small black wallet with a student card inside",
    "location":"Central Library",
    "item_date":"2026-10-20",
    "contact_name":"Test Student",
    "contact_info":"test@example.invalid"
  }'

curl -i 'http://127.0.0.1:8000/api/matches?lost_id=PASTE_YOUR_LOST_ITEM_ID' \
  -H 'Authorization: Bearer TOKEN'

curl -i -X PATCH http://127.0.0.1:8000/api/items/PASTE_YOUR_ITEM_ID/status \
  -H 'Authorization: Bearer TOKEN' \
  -H 'Content-Type: application/json' \
  --data '{"status":"claimed"}'

curl -i -X PATCH http://127.0.0.1:8000/api/profile \
  -H 'Authorization: Bearer TOKEN' \
  -H 'Content-Type: application/json' \
  --data '{"full_name":"Test Student"}'

# Request a private handoff for an open account-owned report.
curl -i -X POST http://127.0.0.1:8000/api/items/PASTE_ITEM_ID/claims \
  -H 'Authorization: Bearer TOKEN' \
  -H 'Content-Type: application/json' \
  --data '{"message":"I found a wallet matching the red student card and can verify the sticker."}'

# Only the owner of an item receives its pending requests and the requester's
# verified contact. A requester sees their own status without owner contact.
curl -i 'http://127.0.0.1:8000/api/claims?scope=received' \
  -H 'Authorization: Bearer TOKEN'
curl -i 'http://127.0.0.1:8000/api/claims?scope=sent' \
  -H 'Authorization: Bearer TOKEN'

# Accept or decline a pending request for an item that belongs to this account.
curl -i -X PATCH http://127.0.0.1:8000/api/claims/PASTE_CLAIM_UUID \
  -H 'Authorization: Bearer TOKEN' \
  -H 'Content-Type: application/json' \
  --data '{"status":"accepted"}'
```

## Expected denial and validation checks

```sh
# Missing token: 401
curl -i -X POST http://127.0.0.1:8000/api/items -H 'Content-Type: application/json' --data '{}'

# Invalid filter: 400
curl -i 'http://127.0.0.1:8000/api/items?type=unknown'

# Invalid detail ID: 400
curl -i http://127.0.0.1:8000/api/items/not-a-uuid

# Missing match ID: authenticated caller receives 400
curl -i http://127.0.0.1:8000/api/matches -H 'Authorization: Bearer TOKEN'
```

The server returns `401` for missing/invalid sessions, `503` if it cannot reach Supabase Auth or PostgreSQL, and does not accept a browser-supplied owner ID. Claim submission also requires a verified email or phone; the requester explicitly shares that verified contact with the report owner by submitting the request. A different user attempting to update an item, request matches, read its claims, or update a claim receives a not-found response. Test ownership using two separate Supabase test accounts.

`GET /api/items` accepts `type`, `category`, `search`, `location`, `status`, `date_from`, `date_to`, `sort`, `mine=true`, `limit` (1–50), and `offset`. Sort values are `newest`, `oldest`, `name`, and `date`. The response includes the page `count`, filtered `total`, `has_more`, `limit`, and `offset`.

Supabase persistence, Auth email delivery, SMS, and account recovery must be tested against the project's actual provider settings. Never use sample data that contains real people's contact details.
