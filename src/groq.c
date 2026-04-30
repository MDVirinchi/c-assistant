/*
 * groq.c — HTTPS call to Groq API using WinHTTP (built into Windows)
 * ====================================================================
 * No libcurl, no OpenSSL — WinHTTP is part of Windows itself.
 */

#include "../include/app.h"

/* ── System prompts ──────────────────────────────────────────── */
static const char *GetPrompt(const char *mode) {
    if (strcmp(mode, "explain") == 0)
        return "You are a professional C programming expert explaining to a beginner. "
               "Follow this structure: (1) Restate what the code does in one sentence. "
               "(2) Name the algorithm or pattern used. "
               "(3) Explain with a real-world analogy. "
               "(4) Mention relevant C details (pointers, malloc, stack/heap). "
               "(5) State time and space complexity. No code in response.";

    if (strcmp(mode, "hint") == 0)
        return "You are a C programming mentor. Give ONE focused hint only. "
               "Identify the right algorithm and WHY it beats simpler alternatives. "
               "Give one concrete directional nudge. Never write code. Never reveal the answer.";

    if (strcmp(mode, "fix") == 0)
        return "You are a C code debugger. "
               "Find every bug (logic errors, memory leaks, undefined behaviour, off-by-one). "
               "Return the COMPLETE corrected C code — no placeholders, no ellipsis. "
               "Then add 'Bugs fixed:' listing each change and why. "
               "Code must compile cleanly under gcc -Wall -Wextra.";

    /* solution */
    return "You are an expert-level C programming and competitive programming assistant. "
           "Before writing any code, follow these steps STRICTLY: "
           "(1) APPROACHES: List ALL possible approaches (brute-force, optimized, etc.) with time and space complexity for each. "
           "(2) REJECTION: Explicitly reject every non-optimal approach with a one-line reason. "
           "(3) BEST APPROACH: State the chosen approach and justify it in one line. "
           "(4) IMPLEMENTATION: Write clean, production-level C code — no pseudo-code, correct memory management, "
               "NULL-check every malloc, guard every function entry against NULL inputs, "
               "free() ALL memory on EVERY return path including early returns and error paths. "
           "(5) DRY RUN: Trace the example step by step. Label as 'Expected output (simulated)'. "
           "(6) EDGE CASES: List 3 tricky inputs and expected outputs. "
           "Complexity rules — strictly enforce: "
           "Prefer O(n) over O(n^2). Prefer O(1) space over O(n) when feasible. "
           "Avoid redundant traversals. "
           "If an optimal O(n) solution exists, do NOT implement O(n^2). "
           "Data structure rules — strictly enforce: "
           "HASH MAP: Never index an array by key directly unless key range is explicitly small and bounded. "
           "For LRU Cache or any key-value map: size the table by MAX_KEY+1 (use 10001 for LeetCode), never by capacity. "
           "Track 'size' (current count) separately from 'capacity' (max allowed). "
           "After evicting a node always set hashTable[evicted->key] = NULL. "
           "PRIORITY QUEUE: Never simulate a heap using a sorted linked list (that is O(n) insert = O(n^2) total). "
           "Always implement a binary min-heap with bubbleUp and bubbleDown for O(log n) operations. "
           "GRAPH: Prefer adjacency list O(V+E) over adjacency matrix O(V^2) for sparse graphs. "
           "DIJKSTRA: Always include a visited[] array or stale-entry check (if dist > best_known, skip). "
           "Store (cost, node) pairs in the heap, not just node. "
           "CONSTRAINED DIJKSTRA (k stops, k moves): Use 2D state dist[node][stops]. "
           "A single dist[node] array is WRONG when a constraint limits transitions. "
           "State = (node, constraint_value). Only update state if new cost improves dist[node][stops]. "
           "NEVER claim 'tests passed' or 'output verified' unless actually executed.";
}

/* ── Escape a string for embedding in a JSON value ──────────── */
static char *JsonEscape(const char *s) {
    size_t n   = strlen(s);
    char  *out = malloc(n * 6 + 3);   /* worst case: every char needs 6 bytes */
    char  *p   = out;
    for (size_t i = 0; i < n; i++) {
        switch (s[i]) {
        case '"':  *p++ = '\\'; *p++ = '"';  break;
        case '\\': *p++ = '\\'; *p++ = '\\'; break;
        case '\n': *p++ = '\\'; *p++ = 'n';  break;
        case '\r': *p++ = '\\'; *p++ = 'r';  break;
        case '\t': *p++ = '\\'; *p++ = 't';  break;
        default:   *p++ = s[i];              break;
        }
    }
    *p = '\0';
    return out;
}

/* ── Pull a JSON string value by key — returns heap string ─────
   Finds  "key":"<value>"  and returns <value> unescaped.
   Returns NULL if not found.                                     */
static char *JsonGetString(const char *json, const char *key) {
    /* Build search pattern: "key": */
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);

    const char *p = strstr(json, pat);
    if (!p) return NULL;
    p += strlen(pat);

    /* Skip whitespace and colon */
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p != '"') return NULL;
    p++;  /* skip opening quote */

    char  *out = malloc(4096);
    char  *q   = out;
    while (*p && (size_t)(q - out) < 4090) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
            case '"':  *q++ = '"';  break;
            case '\\': *q++ = '\\'; break;
            case 'n':  *q++ = '\n'; break;
            case 'r':  *q++ = '\r'; break;
            case 't':  *q++ = '\t'; break;
            default:   *q++ = *p;   break;
            }
        } else if (*p == '"') {
            break;  /* end of string */
        } else {
            *q++ = *p;
        }
        p++;
    }
    *q = '\0';
    return out;
}

/* ── Extract the assistant's reply from the Groq JSON response ─ */
static char *ExtractContent(const char *json) {
    /* Check for error first — show the real Groq message */
    if (strstr(json, "\"error\"")) {
        char *msg = JsonGetString(json, "message");
        if (msg && msg[0] != '\0') {
            /* Prefix with label so user knows it's from Groq */
            char *full = malloc(strlen(msg) + 64);
            strcpy(full, "Groq error: ");
            strcat(full, msg);
            free(msg);
            return full;
        }
        if (msg) free(msg);
        return strdup("Groq returned an error (no message in response).");
    }

    /* Skip past "choices" so we don't accidentally match the prompt echo */
    const char *start = strstr(json, "\"choices\"");
    if (!start) start = json;

    const char *p = strstr(start, "\"content\":");
    if (!p) return strdup("Could not parse the API response.");

    p += 10;  /* skip past "content": */
    while (*p == ' ') p++;
    if (*p != '"') return strdup("Unexpected response format.");
    p++;      /* skip opening quote */

    char  *out = malloc(65536);
    char  *q   = out;
    while (*p && (size_t)(q - out) < 65000) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
            case '"':  *q++ = '"';  break;
            case '\\': *q++ = '\\'; break;
            case 'n':  *q++ = '\n'; break;
            case 'r':  *q++ = '\r'; break;
            case 't':  *q++ = '\t'; break;
            default:   *q++ = *p;   break;
            }
        } else if (*p == '"') {
            break;  /* end of JSON string */
        } else {
            *q++ = *p;
        }
        p++;
    }
    *q = '\0';
    return out;
}

/* ── Convert a narrow string to a new heap-allocated WCHAR ───── */
static WCHAR *ToWide(const char *s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    WCHAR *w = malloc((size_t)n * sizeof(WCHAR));
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

/* ── Main API function ──────────────────────────────────────────
   Sends a POST to https://api.groq.com/openai/v1/chat/completions
   and returns the assistant's reply as a heap string.
   Caller must free() the result.                                 */
char *CallGroq(const char *query, const char *mode, const char *apiKey) {
    HINTERNET hSess = NULL, hConn = NULL, hReq = NULL;
    char     *result = NULL;
    char     *eq = NULL, *ep = NULL, *body = NULL;
    WCHAR    *wAuthHdr = NULL;

    /* ── Build JSON request body using strcat (no % injection) ── */
    eq = JsonEscape(query);
    ep = JsonEscape(GetPrompt(mode));

    size_t bodyLen = strlen(eq) + strlen(ep) + 256;
    body = malloc(bodyLen);

    strcpy(body, "{\"model\":\"llama-3.3-70b-versatile\","
                 "\"messages\":["
                 "{\"role\":\"system\",\"content\":\"");
    strcat(body, ep);
    strcat(body, "\"},"
                 "{\"role\":\"user\",\"content\":\"");
    strcat(body, eq);
    strcat(body, "\"}],"
                 "\"temperature\":0.3}");

    free(eq); eq = NULL;
    free(ep); ep = NULL;

    /* ── Build Authorization header (wide string) ─────────────── */
    char authHdr[384];
    snprintf(authHdr, sizeof(authHdr), "Authorization: Bearer %s", apiKey);
    wAuthHdr = ToWide(authHdr);

    /* ── Open WinHTTP session ────────────────────────────────────
       Use WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY (= 4) on Win8.1+;
       fall back to DEFAULT_PROXY on older systems.               */
    hSess = WinHttpOpen(L"C-Assistant/1.0",
                        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) {
        result = strdup("WinHTTP: failed to open session.");
        goto cleanup;
    }

    hConn = WinHttpConnect(hSess, L"api.groq.com",
                           INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConn) {
        result = strdup("Cannot connect to api.groq.com — check your internet.");
        goto cleanup;
    }

    hReq = WinHttpOpenRequest(hConn, L"POST",
                              L"/openai/v1/chat/completions",
                              NULL, WINHTTP_NO_REFERER,
                              WINHTTP_DEFAULT_ACCEPT_TYPES,
                              WINHTTP_FLAG_SECURE);
    if (!hReq) {
        result = strdup("WinHTTP: failed to create request.");
        goto cleanup;
    }

    /* Add headers separately — more reliable than combined string */
    WinHttpAddRequestHeaders(hReq, wAuthHdr, (DWORD)-1,
                             WINHTTP_ADDREQ_FLAG_ADD);
    WinHttpAddRequestHeaders(hReq, L"Content-Type: application/json",
                             (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    /* ── Send request ────────────────────────────────────────── */
    BOOL ok = WinHttpSendRequest(hReq,
                                 WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 body, (DWORD)strlen(body),
                                 (DWORD)strlen(body), 0);
    if (!ok || !WinHttpReceiveResponse(hReq, NULL)) {
        result = strdup("Cannot reach Groq API — check your internet connection.");
        goto cleanup;
    }

    /* ── Read response in chunks ─────────────────────────────── */
    {
        size_t total = 0;
        char  *buf   = NULL;
        DWORD  avail = 0, nread = 0;

        while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
            char *chunk = malloc((size_t)avail + 1);
            WinHttpReadData(hReq, chunk, avail, &nread);
            chunk[nread] = '\0';
            buf = realloc(buf, total + nread + 1);
            memcpy(buf + total, chunk, nread);
            total += nread;
            buf[total] = '\0';
            free(chunk);
        }

        if (buf) {
            result = ExtractContent(buf);
            free(buf);
        }
    }

cleanup:
    if (hReq)    WinHttpCloseHandle(hReq);
    if (hConn)   WinHttpCloseHandle(hConn);
    if (hSess)   WinHttpCloseHandle(hSess);
    free(wAuthHdr);
    free(body);
    free(eq);
    free(ep);

    if (!result) result = strdup("Unknown error — no response received.");
    return result;
}

/* ── Worker thread — called by popup when user clicks a button ─
   Runs CallGroq(), then posts the result back to the popup via
   PostMessage so the UI thread can update safely.               */
DWORD WINAPI ApiThread(LPVOID lpParam) {
    ApiParams *p = (ApiParams *)lpParam;

    char *response = CallGroq(p->query, p->mode, p->apiKey);

    if (response)
        PostMessageA(p->hwnd, WM_API_DONE, 0, (LPARAM)response);
    else
        PostMessageA(p->hwnd, WM_API_FAIL, 0,
                     (LPARAM)strdup("Request failed unexpectedly."));

    free(p);
    return 0;
}
