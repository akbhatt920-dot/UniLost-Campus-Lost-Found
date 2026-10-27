# UniLost system flow

```mermaid
flowchart TD
    V[Visitor] -->|login and registration| FE[HTML CSS JavaScript pages]
    FE -->|email or phone sign-in| SA[Supabase Auth]
    SA -->|session access token| FE
    FE -->|public requests or bearer token| C[Mongoose HTTP server in C]
    C -->|HTTPS GET Auth user with bearer token| SA
    SA -->|verified user record and user ID| C
    C -->|parameterized libpq query over TLS| PG[(Supabase PostgreSQL)]
    C -->|owner-filtered create, profile, status, match requests| PG
    PG -->|public item fields / own profile| C
    C -->|JSON without contact details| FE
    FE -->|private handoff request with bearer token| C
    C -->|verified requester contact + owner-filtered inbox| PG
    PG -->|claimant contact only to item owner| C
    C -->|authenticated private inbox| FE
    C -->|open lost versus found reports| M[C matching algorithm]
    M -->|normalize, score, explain, bubble-sort| C
    C -->|ranked potential matches| FE
    FE -->|authenticated private photo upload and download| ST[Supabase Storage]
```

For protected actions, the C server uses only the user ID returned by Supabase Auth after HTTPS token verification. The PostgreSQL connection stays in C and browser requests never include a caller-selected owner ID.
