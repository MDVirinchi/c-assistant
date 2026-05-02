"""
test_assistant.py — Automated test suite for C Assistant
=========================================================
Sends coding questions to Groq using the same system prompts
as the app, then uses Groq-as-judge to rate each response.

Usage:
    python test_assistant.py
    python test_assistant.py --mode fix
    python test_assistant.py --questions 10
"""

import os
import sys
import json
import time
import argparse
import urllib.request
import urllib.error

# ── API key ───────────────────────────────────────────────────────
def load_api_key():
    search = [
        os.path.join(os.path.dirname(__file__), "dist", ".env"),
        os.path.join(os.path.dirname(__file__), ".env"),
    ]
    for path in search:
        if os.path.exists(path):
            with open(path) as f:
                for line in f:
                    line = line.strip()
                    if line.startswith("GROQ_API_KEY="):
                        return line.split("=", 1)[1].strip()
    return None


# ── Same system prompts as groq.c ─────────────────────────────────
PROMPTS = {
    "explain": (
        "You are a professional C programming expert explaining to a beginner. "
        "Follow this structure: (1) Restate what the code does in one sentence. "
        "(2) Name the algorithm or pattern used. "
        "(3) Explain with a real-world analogy. "
        "(4) Mention relevant C details (pointers, malloc, stack/heap). "
        "(5) State time and space complexity. No code in response."
    ),
    "hint": (
        "You are a C programming mentor. Give ONE focused hint only. "
        "Identify the right algorithm and WHY it beats simpler alternatives. "
        "Give one concrete directional nudge. Never write code. Never reveal the answer."
    ),
    "fix": (
        "You are a C code debugger. "
        "Step 1 - ANALYSIS: Identify EVERY bug: logic errors, memory leaks, undefined behaviour, "
        "buffer overflows, off-by-one errors, null pointer dereferences, missing free() calls, wrong loop bounds. "
        "Step 2 - FIXED CODE: Return the COMPLETE corrected C code with NO placeholders or ellipsis. "
        "Mark every changed line with a comment: // FIXED: <reason>. "
        "Do NOT include a main() function unless the original code had one. "
        "Do NOT redefine structs or typedefs that LeetCode/the platform already provides (e.g. ListNode, TreeNode). "
        "Step 3 - BUGS FIXED: After the code, list EVERY change with the line number and exact reason. "
        "Format: 'Line N: <what changed> - <why>'. "
        "If you only removed main() without fixing real logic bugs, you have missed the point - look harder. "
        "Code must compile cleanly under gcc -Wall -Wextra."
    ),
    "solution": (
        "You are a professional C programming and Data Structures expert. "
        "Generate CORRECT, OPTIMAL, and COMPILABLE code for coding interview problems. "
        "PLATFORM: Always assume LeetCode unless told otherwise. "
        "On LeetCode: DO NOT include main(). DO NOT redefine structs (ListNode, TreeNode, Node). "
        "Return ONLY the required function(s). "
        "Always respond in EXACTLY this structure: "
        "[Pattern Used] - name the pattern (Hashing, Sliding Window, Two Pointers, Prefix Sum, Heap, Greedy, DP, etc.). "
        "[Time Complexity] - Big-O. "
        "[Space Complexity] - Big-O. "
        "[Code] - clean C code only. "
        "ALGORITHM RULES: Identify the correct pattern first. Choose the MOST optimal solution. "
        "Prefer O(n) over O(n^2), O(n log k) over O(n*k). "
        "NEVER fake data structures - if using a heap implement real bubbleUp/bubbleDown. "
        "DATA STRUCTURE RULES: "
        "HASH MAP: never array[key] unless key range is bounded. LRU table size=MAX_KEY+1=10001 not capacity. "
        "Track size and capacity separately. After eviction: hashTable[evicted->key]=NULL. "
        "PRIORITY QUEUE: binary min-heap only - never sorted linked list. "
        "GRAPH: adjacency list O(V+E), not matrix O(V^2). "
        "DIJKSTRA: stale-entry check. Heap stores (cost,node). "
        "CONSTRAINED DIJKSTRA: 2D dist[node][stops] - single dist[] is WRONG with stop limits. "
        "BITMASK DP: N<=20 subsets use dp[mask][node]. "
        "CODE QUALITY: Raw C only - no u003c u003e u0026. Cache strlen(). "
        "NULL-check every malloc. free() on every return path. "
        "Declare helpers before use. Compiles under gcc -Wall -Wextra. "
        "NEVER claim tests passed unless actually executed."
    ),
}

# ── Judge prompt ──────────────────────────────────────────────────
JUDGE_PROMPT = """You are a strict C programming code reviewer.
Rate the assistant's response out of 10 using these criteria:

  Algorithm (3 pts) — correct algorithm chosen, no brute-force when better exists
  Code quality (2 pts) — compiles under gcc -Wall, no memory leaks, no UB
  Structure (2 pts) — all 5 sections present: Algorithm Selection, Complexity,
                       Implementation, Dry Run, Edge Cases
  Edge cases (2 pts) — handles NULL/empty/single-element/overflow correctly
  Clarity (1 pt)  — explanation is clear and accurate

Respond EXACTLY in this format (nothing else):
SCORE: X/10
ISSUES: <comma-separated list of issues, or "None">
VERDICT: <one sentence>"""

# ── Test questions ────────────────────────────────────────────────
QUESTIONS = [
    {
        "id": 1, "title": "Reverse Linked List", "difficulty": "Easy",
        "q": "Reverse a singly linked list in-place in C. O(n) time, O(1) space.",
    },
    {
        "id": 2, "title": "Best Time to Buy & Sell Stock", "difficulty": "Easy",
        "q": (
            "Given int prices[], find the maximum profit from one buy + one sell. "
            "Must buy before selling. Return 0 if no profit is possible. "
            "O(n) time, O(1) space. Implement in C."
        ),
    },
    {
        "id": 3, "title": "Valid Parentheses", "difficulty": "Easy",
        "q": (
            "Given a string of '(', ')', '{', '}', '[', ']', return 1 if every "
            "open bracket is closed in the correct order, 0 otherwise. "
            "O(n) time. Implement in C using a stack."
        ),
    },
    {
        "id": 4, "title": "Merge Two Sorted Linked Lists", "difficulty": "Easy",
        "q": (
            "Merge two sorted singly linked lists into one sorted list. "
            "Return the new head. O(n+m) time, O(1) space. Implement in C."
        ),
    },
    {
        "id": 5, "title": "Binary Search", "difficulty": "Easy",
        "q": (
            "Given a sorted int array and a target, return its index or -1. "
            "O(log n) time, O(1) space. Implement in C."
        ),
    },
    {
        "id": 6, "title": "Two Sum", "difficulty": "Easy",
        "q": (
            "Given int nums[] and a target, return indices of two numbers that add "
            "up to target. Each input has exactly one solution. "
            "O(n) time using a hash map. Implement in C."
        ),
    },
    {
        "id": 7, "title": "Maximum Subarray (Kadane)", "difficulty": "Medium",
        "q": (
            "Given int nums[], find the contiguous subarray with the largest sum. "
            "O(n) time, O(1) space (Kadane's algorithm). Implement in C."
        ),
    },
    {
        "id": 8, "title": "Number of Islands (BFS/DFS)", "difficulty": "Medium",
        "q": (
            "Given a 2D char grid of '1' (land) and '0' (water), count islands. "
            "An island is surrounded by water and formed by connecting adjacent "
            "lands horizontally or vertically. O(m*n) time. Implement in C."
        ),
    },
    {
        "id": 9, "title": "Network Delay Time (Dijkstra)", "difficulty": "Medium",
        "q": (
            "There are n nodes (1..n) and directed weighted edges times[i]=[u,v,w]. "
            "A signal starts at node k. Return the time for all nodes to receive it, "
            "or -1 if impossible. Implement Dijkstra in C with a min-heap. "
            "O((V+E) log V) time."
        ),
    },
    {
        "id": 10, "title": "Trapping Rain Water", "difficulty": "Hard",
        "q": (
            "Given int height[] representing an elevation map, compute how much "
            "water can be trapped after raining. "
            "O(n) time, O(1) space (two-pointer approach). Implement in C."
        ),
    },
]

# ── Groq HTTP call (no external libs — stdlib only) ───────────────
def call_groq(system_prompt, user_msg, api_key, retries=3):
    payload = json.dumps({
        "model": "llama-3.3-70b-versatile",
        "messages": [
            {"role": "system", "content": system_prompt},
            {"role": "user",   "content": user_msg},
        ],
        "temperature": 0.3,
    }).encode("utf-8")

    req = urllib.request.Request(
        "https://api.groq.com/openai/v1/chat/completions",
        data=payload,
        headers={
            "Authorization":  f"Bearer {api_key}",
            "Content-Type":   "application/json",
            "Accept":         "application/json",
            "User-Agent":     "C-Assistant/1.0",
        },
        method="POST",
    )

    for attempt in range(retries):
        try:
            with urllib.request.urlopen(req, timeout=60) as resp:
                body = json.loads(resp.read().decode("utf-8"))
                return body["choices"][0]["message"]["content"]
        except urllib.error.HTTPError as e:
            err = e.read().decode("utf-8")
            if e.code == 429:           # rate limit — back off
                wait = 20 * (attempt + 1)
                print(f"    [rate limit] waiting {wait}s...")
                time.sleep(wait)
                continue
            return f"[HTTP {e.code}] {err}"
        except Exception as ex:
            if attempt < retries - 1:
                time.sleep(5)
                continue
            return f"[ERROR] {ex}"
    return "[ERROR] All retries failed"


# ── Parse "SCORE: X/10" from judge response ───────────────────────
def parse_score(text):
    for line in text.splitlines():
        if line.strip().startswith("SCORE:"):
            try:
                return float(line.split(":")[1].strip().split("/")[0])
            except Exception:
                pass
    return 0.0


def parse_field(text, field):
    for line in text.splitlines():
        if line.strip().startswith(f"{field}:"):
            return line.split(":", 1)[1].strip()
    return "-"


# ── Main runner ───────────────────────────────────────────────────
def run(mode="solution", max_q=None, verbose=False):
    api_key = load_api_key()
    if not api_key or api_key == "your_groq_api_key_here":
        print("[ERROR] No API key found. Check dist\\.env")
        return

    questions = QUESTIONS[:max_q] if max_q else QUESTIONS
    results   = []
    total     = 0.0

    print()
    print("=" * 64)
    print(f"  C Assistant - Automated Test  |  mode={mode}  |  {len(questions)} questions")
    print("=" * 64)

    for q in questions:
        tag = f"[{q['id']:02d}/{len(questions)}]"
        print(f"\n{tag} {q['title']}  ({q['difficulty']})")
        print(f"       Getting solution...", end="", flush=True)

        solution = call_groq(PROMPTS[mode], q["q"], api_key)
        print(" done.")

        if solution.startswith("["):
            print(f"       {solution}")
            results.append({**q, "solution": solution, "score": 0,
                             "issues": "API error", "verdict": solution})
            continue

        print(f"       Rating...", end="", flush=True)
        judge_input = f"QUESTION:\n{q['q']}\n\nASSISTANT RESPONSE:\n{solution}"
        rating      = call_groq(JUDGE_PROMPT, judge_input, api_key)
        score       = parse_score(rating)
        issues      = parse_field(rating, "ISSUES")
        verdict     = parse_field(rating, "VERDICT")
        total      += score
        print(" done.")

        bar = "#" * int(score) + "-" * (10 - int(score))
        print(f"       SCORE  {score:4.1f}/10  [{bar}]")
        print(f"       ISSUES {issues}")
        print(f"       VERDICT {verdict}")

        results.append({**q, "solution": solution, "score": score,
                         "issues": issues, "verdict": verdict,
                         "rating_full": rating})

        if verbose:
            print("\n--- SOLUTION ---")
            print(solution[:2000], "..." if len(solution) > 2000 else "")

        time.sleep(2)   # stay under rate limits

    avg = total / len(questions) if questions else 0

    print()
    print("=" * 64)
    print(f"  FINAL AVERAGE: {avg:.1f} / 10")
    print("=" * 64)

    # ── Save report ───────────────────────────────────────────────
    report_path = os.path.join(os.path.dirname(__file__), "test_report.txt")
    with open(report_path, "w", encoding="utf-8") as f:
        f.write(f"C Assistant — Test Report  (mode={mode})\n")
        f.write("=" * 64 + "\n\n")
        for r in results:
            f.write(f"[{r['id']:02d}] {r['title']}  ({r['difficulty']})\n")
            f.write(f"     SCORE:   {r['score']}/10\n")
            f.write(f"     ISSUES:  {r.get('issues','—')}\n")
            f.write(f"     VERDICT: {r.get('verdict','—')}\n\n")
            f.write("     SOLUTION:\n")
            f.write(r["solution"] + "\n")
            f.write("-" * 64 + "\n\n")
        f.write(f"\nAVERAGE: {avg:.1f}/10\n")

    print(f"\n  Full report saved -> test_report.txt")
    print()

    # ── Highlight weak spots ──────────────────────────────────────
    weak = [r for r in results if r["score"] < 8]
    if weak:
        print("  Questions scoring below 8 (need improvement):")
        for r in weak:
            print(f"    • [{r['id']:02d}] {r['title']}  →  {r['score']}/10  |  {r.get('issues','—')}")
        print()
    else:
        print("  All questions scored 8+ - great results!")
        print()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="C Assistant automated tester")
    parser.add_argument("--mode",      default="solution",
                        choices=["solution", "explain", "hint", "fix"],
                        help="Which assistant mode to test (default: solution)")
    parser.add_argument("--questions", type=int, default=None,
                        help="How many questions to run (default: all 10)")
    parser.add_argument("--verbose",   action="store_true",
                        help="Print full solution text for each question")
    args = parser.parse_args()
    run(mode=args.mode, max_q=args.questions, verbose=args.verbose)
