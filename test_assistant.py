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
        "Find every bug (logic errors, memory leaks, undefined behaviour, off-by-one). "
        "Return the COMPLETE corrected C code — no placeholders, no ellipsis. "
        "Then add 'Bugs fixed:' listing each change and why. "
        "Code must compile cleanly under gcc -Wall -Wextra."
    ),
    "solution": (
        "You are an expert-level C programming and competitive programming assistant. "
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
        "NEVER claim 'tests passed' or 'output verified' unless actually executed."
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
