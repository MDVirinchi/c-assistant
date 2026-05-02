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
               "Step 1 — ANALYSIS: Identify EVERY bug in the code: "
               "logic errors, memory leaks, undefined behaviour, buffer overflows, off-by-one errors, "
               "null pointer dereferences, missing free() calls, wrong loop bounds. "
               "Step 2 — FIXED CODE: Return the COMPLETE corrected C code with NO placeholders or ellipsis. "
               "Mark every changed line with a comment: // FIXED: <reason>. "
               "Do NOT include a main() function unless the original code had one. "
               "Do NOT include struct/typedef definitions that LeetCode/the platform already provides. "
               "Step 3 — BUGS FIXED: After the code, list EVERY change made with the line number and exact reason. "
               "Format: 'Line N: <what changed> — <why>'. "
               "If you only removed main() without fixing real logic bugs, you have missed the point — look harder. "
               "Code must compile cleanly under gcc -Wall -Wextra.";

    /* solution */
    return
      /* ── Role & platform ────────────────────────────────────── */
      "You are a professional C programming and Data Structures expert. "
      "Generate CORRECT, OPTIMAL, and COMPILABLE code for coding interview problems. "

      /* ── Platform rules ─────────────────────────────────────── */
      "PLATFORM: Always assume LeetCode unless told otherwise. "
      "On LeetCode: DO NOT include main(). "
      "DO NOT redefine structs (ListNode, TreeNode, Node, etc.). "
      "Return ONLY the required function(s). "

      /* ── Output format (STRICT) ─────────────────────────────── */
      "Always respond in EXACTLY this structure — no extra sections, no skipping: "
      "[Pattern Used] — name the pattern (Hashing, Sliding Window, Two Pointers, "
      "Prefix Sum, Heap/Priority Queue, Greedy, Dynamic Programming, etc.). "
      "[Time Complexity] — Big-O. "
      "[Space Complexity] — Big-O. "
      "[Code] — clean C code only, no prose inside the code block. "

      /* ── Algorithm selection ────────────────────────────────── */
      "ALGORITHM RULES: "
      "Before writing code, identify the correct pattern. "
      "Choose the MOST optimal solution: prefer O(n) over O(n^2), O(n log k) over O(n*k). "
      "NEVER fake data structures: if using a heap, implement real heap behaviour "
      "(bubbleUp/bubbleDown) — never simulate with a linear scan. "
      "NEVER use O(n^2) when O(n log n) or O(n) exists. "
      "NEVER use O(n) space when O(1) is achievable. "

      /* ── Data structure hard rules ──────────────────────────── */
      "DATA STRUCTURE RULES: "
      "HASH MAP: never index array[key] unless key range is explicitly bounded and small. "
      "LRU Cache: hashTable size = MAX_KEY+1 = 10001, never capacity. "
      "Track int size (current entries) separately from int capacity (max allowed). "
      "After every eviction: hashTable[evicted->key] = NULL. "
      "PRIORITY QUEUE: always binary min-heap (array-based, bubbleUp/bubbleDown, O(log n)). "
      "Never a sorted linked list. "
      "GRAPH: adjacency list O(V+E), not matrix O(V^2), for sparse graphs. "
      "DIJKSTRA: stale-entry check required (if cost > dist[node] skip). "
      "Heap stores (cost, node) pairs. "
      "CONSTRAINED DIJKSTRA (k stops/moves): use 2D dist[node][stops] — single dist[] is WRONG. "
      "BITMASK DP: N<=20 subsets → dp[mask][node]. "

      /* ── Code quality rules ─────────────────────────────────── */
      "CODE QUALITY: "
      "Output raw C code — no HTML encoding (u003c u003e u0026 are forbidden). "
      "Include required headers only (stdlib.h, string.h, limits.h as needed). "
      "Declare helper functions before first use. "
      "Cache strlen() — never call it inside a loop condition. "
      "NULL-check every malloc. free() on every return path. "
      "No dangling pointers. No undefined behaviour. "
      "Compiles under gcc -Wall -Wextra without modification. "
      "Keep code minimal and readable — no overengineering. "
      "NEVER claim tests passed unless code was actually executed.";
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

/* ── Decode \uXXXX → UTF-8, write to *q, advance *pp past the 4 hex digits.
   Handles all BMP code points (U+0000 – U+FFFF).                           */
static void DecodeUnicode(const char **pp, char **qq) {
    const char *p = *pp;
    /* Read 4 hex digits */
    unsigned int cp = 0;
    for (int i = 0; i < 4; i++) {
        cp <<= 4;
        char c = *p++;
        if      (c >= '0' && c <= '9') cp |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') cp |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') cp |= (unsigned)(c - 'A' + 10);
    }
    *pp = p;

    char *q = *qq;
    if (cp < 0x80) {
        /* 1-byte UTF-8 — covers all ASCII including < > & ' " */
        *q++ = (char)cp;
    } else if (cp < 0x800) {
        /* 2-byte UTF-8 */
        *q++ = (char)(0xC0 | (cp >> 6));
        *q++ = (char)(0x80 | (cp & 0x3F));
    } else {
        /* 3-byte UTF-8 */
        *q++ = (char)(0xE0 | (cp >> 12));
        *q++ = (char)(0x80 | ((cp >> 6) & 0x3F));
        *q++ = (char)(0x80 | (cp & 0x3F));
    }
    *qq = q;
}

/* ── Shared JSON-string unescape loop ───────────────────────────
   p points to the first character AFTER the opening quote.
   Writes unescaped text into *q until closing quote or end.     */
static void UnescapeJsonString(const char **pp, char **qq, size_t limit) {
    const char *p = *pp;
    char       *q = *qq;
    while (*p && (size_t)(q - *qq) < limit) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
            case '"':  *q++ = '"';  p++; break;
            case '\\': *q++ = '\\'; p++; break;
            case '/':  *q++ = '/';  p++; break;
            case 'n':  *q++ = '\n'; p++; break;
            case 'r':  *q++ = '\r'; p++; break;
            case 't':  *q++ = '\t'; p++; break;
            case 'b':  *q++ = '\b'; p++; break;
            case 'f':  *q++ = '\f'; p++; break;
            case 'u':
                /* \uXXXX — decode to UTF-8 (fixes u003c→< u003e→> u0026→&) */
                p++;
                DecodeUnicode(&p, &q);
                break;
            default:   *q++ = *p++; break;
            }
        } else if (*p == '"') {
            p++;  /* skip closing quote */
            break;
        } else {
            *q++ = *p++;
        }
    }
    *pp = p;
    *qq = q;
}

/* ── Pull a JSON string value by key — returns heap string ─────
   Finds  "key":"<value>"  and returns <value> unescaped.
   Returns NULL if not found.                                     */
static char *JsonGetString(const char *json, const char *key) {
    char pat[128];
    snprintf(pat, sizeof(pat), "\"%s\"", key);

    const char *p = strstr(json, pat);
    if (!p) return NULL;
    p += strlen(pat);

    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    if (*p != '"') return NULL;
    p++;  /* skip opening quote */

    char  *out = malloc(4096);
    char  *q   = out;
    UnescapeJsonString(&p, &q, 4090);
    *q = '\0';
    return out;
}

/* ── Extract the assistant's reply from the Groq JSON response ─ */
static char *ExtractContent(const char *json) {
    /* Check for error first — show the real Groq message */
    if (strstr(json, "\"error\"")) {
        char *msg = JsonGetString(json, "message");
        if (msg && msg[0] != '\0') {
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

    p += 10;
    while (*p == ' ') p++;
    if (*p != '"') return strdup("Unexpected response format.");
    p++;  /* skip opening quote */

    char *out = malloc(65536);
    char *q   = out;
    UnescapeJsonString(&p, &q, 65000);
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
