#include "likegit/diff.hpp"
#include <algorithm>
#include <functional>

std::vector<Edit> compute_diff(const std::vector<std::string_view>& a,
                               const std::vector<std::string_view>& b) {
    int n = static_cast<int>(a.size());
    int m = static_cast<int>(b.size());

    if (n == 0 && m == 0) return {};

    // ── 1. Hash every line ───────────────────────────────────────────────────
    std::hash<std::string_view> hasher;
    std::vector<size_t> ha(n), hb(m);
    for (int i = 0; i < n; i++) ha[i] = hasher(a[i]);
    for (int j = 0; j < m; j++) hb[j] = hasher(b[j]);

    // ── 2. Strip common prefix ───────────────────────────────────────────────
    int prefix = 0;
    while (prefix < n && prefix < m && ha[prefix] == hb[prefix] && a[prefix] == b[prefix])
        prefix++;

    // ── 3. Strip common suffix ───────────────────────────────────────────────
    int suffix = 0;
    while (suffix < n - prefix && suffix < m - prefix
           && ha[n-1-suffix] == hb[m-1-suffix]
           && a[n-1-suffix] == b[m-1-suffix])
        suffix++;

    // Work on the sliced portion only
    int as = prefix, ae = n - suffix;
    int bs = prefix, be = m - suffix;
    int na = ae - as;
    int mb = be - bs;

    // ── 4. Myers forward search ──────────────────────────────────────────────
    int max_d = na + mb;
    int v0 = max_d + 1;
    std::vector<int> v(2 * max_d + 3, -1);
    v[v0 + 1] = 0;

    // all_v[d] = snapshot of v BEFORE processing step d
    std::vector<std::vector<int>> all_v;
    bool found = (na == 0 && mb == 0);

    for (int d = 0; d <= max_d && !found; d++) {
        all_v.push_back(v);
        for (int k = -d; k <= d; k += 2) {
            int x;
            if (k == -d || (k != d && v[v0+k-1] < v[v0+k+1]))
                x = v[v0+k+1];
            else
                x = v[v0+k-1] + 1;

            int y = x - k;
            while (x < na && y < mb && ha[as+x] == hb[bs+y] && a[as+x] == b[bs+y])
                x++, y++;

            v[v0+k] = x;
            if (x >= na && y >= mb) {
                all_v.push_back(v); // final snapshot
                found = true;
                break;
            }
        }
    }

    // ── 5. Backtrack: recover the edit sequence ──────────────────────────────
    std::vector<Edit> middle;
    int x = na, y = mb;

    for (int d = static_cast<int>(all_v.size()) - 2; d >= 1; d--) {
        const auto& vd = all_v[d];
        int k = x - y;

        int prev_k;
        if (k == -d || (k != d && vd[v0+k-1] < vd[v0+k+1]))
            prev_k = k + 1; // came down → insert
        else
            prev_k = k - 1; // came right → delete

        int prev_x = vd[v0 + prev_k];
        int prev_y = prev_x - prev_k;

        if (prev_k == k - 1) {
            // came right: deleted a[prev_x], then slid to (x, y)
            for (int i = x - 1; i > prev_x; i--)
                middle.push_back({EditType::EQUAL, as+i, bs+(i-k), a[as+i]});
            middle.push_back({EditType::DELETE, as+prev_x, -1, a[as+prev_x]});
        } else {
            // came down: inserted b[prev_y], then slid to (x, y)
            for (int i = x - 1; i >= prev_x; i--)
                middle.push_back({EditType::EQUAL, as+i, bs+(i-k), a[as+i]});
            middle.push_back({EditType::INSERT, -1, bs+prev_y, b[bs+prev_y]});
        }

        x = prev_x;
        y = prev_y;
    }

    // Any remaining diagonal slides at (x, y) back to (0, 0)
    for (int i = x - 1; i >= 0; i--)
        middle.push_back({EditType::EQUAL, as+i, bs+i, a[as+i]});

    std::reverse(middle.begin(), middle.end());

    // ── 6. Assemble: prefix + middle + suffix ────────────────────────────────
    std::vector<Edit> result;
    result.reserve(n + m);

    for (int i = 0; i < prefix; i++)
        result.push_back({EditType::EQUAL, i, i, a[i]});

    for (auto& e : middle)
        result.push_back(e);

    for (int i = 0; i < suffix; i++)
        result.push_back({EditType::EQUAL, ae+i, be+i, a[ae+i]});

    return result;
}
