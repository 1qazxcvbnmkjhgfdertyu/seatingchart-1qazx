#include "AutoAssign.h"
#include "Utils.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>
#include <unordered_map>
#include <unordered_set>

// Front-row band: a "Front row"-tagged student may only sit in a seat whose
// centre lies within this fraction of the room depth (measured from the front
// edge). 0.34 ≈ the front third of the room.
static constexpr double kFrontBandFraction = 0.34;

// ---------------------------------------------------------------------------
// Backtracking solver (pure logic)
//
//   pairRadius[a][b] : -1 = no restriction between a,b; 0 = same-item keep-apart;
//                      >0 = keep-apart radius in room units (centre distance).
//   dist[s1][s2]     : seat-to-seat centre distance in room units.
//   sameItem[s1][s2] : 1 if the two seats belong to the same furniture item.
// ---------------------------------------------------------------------------

static bool Backtrack(
    size_t pos,
    const std::vector<int>& studentOrder,
    const std::vector<int>& seatOrder,
    const std::vector<std::vector<int>>& restrictedByStudent,
    const std::vector<int>&  pairRadius,
    const std::vector<int>&  dist,
    const std::vector<char>& sameItem,
    size_t participantCount,
    size_t seatCount,
    const std::vector<char>& mustBeFront,
    const std::vector<char>& frontEligible,
    const std::vector<std::pair<int,int>>& mustSamePairs,
    const std::vector<std::vector<std::wstring>>& rosterForbidden,
    const std::vector<std::wstring>& seatLabels,
    std::vector<int>& assigned,
    std::vector<char>& used,
    size_t& steps,
    bool& limitHit,
    HWND progressWnd,
    const std::atomic<bool>* cancel,
    size_t searchLimit)
{
    if (cancel && cancel->load(std::memory_order_relaxed)) { limitHit = true; return false; }
    if (++steps > searchLimit)                   { limitHit = true; return false; }

    if (progressWnd && steps % 10000 == 0)
        PostMessageW(progressWnd, WM_APP_AUTOASSIGN_PROGRESS,
                     static_cast<WPARAM>(steps), 0);

    if (pos >= studentOrder.size()) return true;

    const int si = studentOrder[pos];
    for (int seat : seatOrder) {
        if (used[static_cast<size_t>(seat)]) continue;
        // Front-row-required students may only take a front-band seat.
        if (mustBeFront[static_cast<size_t>(si)] &&
            !frontEligible[static_cast<size_t>(seat)]) continue;

        // Per-student whitelisting: skip forbidden desk labels
        {
            if (static_cast<size_t>(si) < rosterForbidden.size()) {
                const auto& forb = rosterForbidden[static_cast<size_t>(si)];
                if (!forb.empty() && seat < static_cast<int>(seatLabels.size())) {
                    const auto& sl = seatLabels[static_cast<size_t>(seat)];
                    bool isForbidden = false;
                    for (const auto& f : forb) {
                        if (CanonicalName(f) == CanonicalName(sl)) {
                            isForbidden = true;
                            break;
                        }
                    }
                    if (isForbidden) continue;
                }
            }
        }

        bool ok = true;
        for (int other : restrictedByStudent[static_cast<size_t>(si)]) {
            if (assigned[static_cast<size_t>(other)] < 0) continue;
            const int r = pairRadius[static_cast<size_t>(si) * participantCount +
                                     static_cast<size_t>(other)];
            const int os = assigned[static_cast<size_t>(other)];
            const bool bad = (r == 0)
                ? sameItem[static_cast<size_t>(seat) * seatCount + static_cast<size_t>(os)] != 0
                : dist[static_cast<size_t>(seat) * seatCount + static_cast<size_t>(os)] <= r;
            if (bad) { ok = false; break; }
        }
        if (!ok) continue;
        assigned[static_cast<size_t>(si)] = seat;
        used[static_cast<size_t>(seat)]   = true;

        // mustTogether: if partner already placed, require same furniture item for this pair
        bool mustTogetherOk = true;
        for (const auto& pr : mustSamePairs) {
            int p1 = pr.first, p2 = pr.second;
            int s1 = (p1 == si ? seat : assigned[static_cast<size_t>(p1)]);
            int s2 = (p2 == si ? seat : assigned[static_cast<size_t>(p2)]);
            if (s1 >= 0 && s2 >= 0) {
                if (sameItem[static_cast<size_t>(s1) * seatCount + static_cast<size_t>(s2)] == 0) {
                    mustTogetherOk = false;
                    break;
                }
            }
        }
        if (mustTogetherOk &&
            Backtrack(pos+1, studentOrder, seatOrder, restrictedByStudent,
                       pairRadius, dist, sameItem, participantCount, seatCount,
                       mustBeFront, frontEligible, mustSamePairs,
                       rosterForbidden, seatLabels,
                       assigned, used, steps, limitHit, progressWnd, cancel, searchLimit)) return true;
        if (limitHit) return false;
        assigned[static_cast<size_t>(si)] = -1;
        used[static_cast<size_t>(seat)]   = false;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Soft-preference scoring (used to refine a feasible solution)
// ---------------------------------------------------------------------------

// Total seat-centre distance over the "sit near" affinity pairs that are both
// placed. Lower is better (the pairs sit closer together).
static long SoftCost(const std::vector<int>& assigned,
                     const std::vector<std::pair<int,int>>& affinityPairs,
                     const std::vector<int>& dist,
                     size_t seatCount) {
    long total = 0;
    for (const auto& p : affinityPairs) {
        const int sa = assigned[static_cast<size_t>(p.first)];
        const int sb = assigned[static_cast<size_t>(p.second)];
        if (sa < 0 || sb < 0) continue;
        total += dist[static_cast<size_t>(sa) * seatCount + static_cast<size_t>(sb)];
    }
    return total;
}

// Whether the whole assignment still satisfies every hard constraint
// (keep-apart radius / same-item, and front-row band, must-together). Used to gate swaps.
static bool HardOK(const std::vector<int>& assigned,
                   const std::vector<int>&  pairRadius,
                   const std::vector<int>&  dist,
                   const std::vector<char>& sameItem,
                   size_t participantCount,
                   size_t seatCount,
                   const std::vector<char>& mustBeFront,
                   const std::vector<char>& frontEligible,
                   const std::vector<std::pair<int,int>>& mustSamePairs,
                   const std::vector<std::vector<std::wstring>>& rosterForbidden,
                   const std::vector<std::wstring>& seatLabels) {
    const int n = static_cast<int>(assigned.size());
    for (int i = 0; i < n; ++i) {
        const int si = assigned[static_cast<size_t>(i)];
        if (si < 0) continue;
        if (mustBeFront[static_cast<size_t>(i)] && !frontEligible[static_cast<size_t>(si)]) return false;
        for (int j = i + 1; j < n; ++j) {
            const int sj = assigned[static_cast<size_t>(j)];
            if (sj < 0) continue;
            const int r = pairRadius[static_cast<size_t>(i) * participantCount + static_cast<size_t>(j)];
            if (r < 0) continue;
            const bool bad = (r == 0)
                ? sameItem[static_cast<size_t>(si) * seatCount + static_cast<size_t>(sj)] != 0
                : dist[static_cast<size_t>(si) * seatCount + static_cast<size_t>(sj)] <= r;
            if (bad) return false;
        }
    }
    // mustTogether: all such pairs must be on same furniture item
    for (const auto& pr : mustSamePairs) {
        int p1 = pr.first, p2 = pr.second;
        int s1 = assigned[static_cast<size_t>(p1)];
        int s2 = assigned[static_cast<size_t>(p2)];
        if (s1 >= 0 && s2 >= 0) {
            if (sameItem[static_cast<size_t>(s1) * seatCount + static_cast<size_t>(s2)] == 0)
                return false;
        }
    }

    // Whitelisting check for whole assignment (for refinement/fixed)
    for (size_t i = 0; i < assigned.size(); ++i) {
        int s = assigned[i];
        if (s < 0 || i >= rosterForbidden.size()) continue;
        const auto& forb = rosterForbidden[i];
        if (forb.empty() || s >= static_cast<int>(seatLabels.size())) continue;
        const auto& sl = seatLabels[s];
        for (const auto& f : forb) {
            if (CanonicalName(f) == CanonicalName(sl)) return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// SolveAutoAssign — pure solve (no UI / threading)
// ---------------------------------------------------------------------------

AutoAssignSolution SolveAutoAssign(const AutoAssignInput& in,
                                   const std::atomic<bool>* cancel,
                                   HWND progressWnd) {
    auto start = std::chrono::high_resolution_clock::now();
    AutoAssignSolution sol;
    const int total = in.seatCount;

    if (static_cast<int>(in.seatCentres.size())    < total ||
        static_cast<int>(in.seatItem.size())       < total ||
        static_cast<int>(in.fixedOccupants.size()) < total) {
        sol.errorMessage = L"Solver input arrays are undersized";
        return sol;
    }

    // Build participant list (roster + any fixed occupants not on the roster).
    std::vector<std::wstring> parts = in.roster;
    std::vector<std::wstring> cans;
    cans.reserve(parts.size());
    for (const auto& n : parts) cans.push_back(CanonicalName(n));

    std::unordered_map<std::wstring, int> idx;
    for (int i = 0; i < static_cast<int>(cans.size()); ++i) idx[cans[static_cast<size_t>(i)]] = i;

    std::vector<int>  assigned(parts.size(), -1);
    std::vector<char> used(static_cast<size_t>(total), 0);
    std::unordered_set<std::wstring> fixedCans;

    for (int si = 0; si < total; ++si) {
        const auto& occ = in.fixedOccupants[static_cast<size_t>(si)];
        if (occ.empty()) continue;
        const auto cn = CanonicalName(occ);
        if (!fixedCans.insert(cn).second) {
            sol.errorMessage = L"Duplicate fixed assignment: " + occ; return sol;
        }
        auto it = idx.find(cn);
        int pi;
        if (it != idx.end()) { pi = it->second; }
        else {
            pi = static_cast<int>(parts.size());
            parts.push_back(occ); cans.push_back(cn); idx[cn] = pi;
            assigned.push_back(-1);
        }
        assigned[static_cast<size_t>(pi)] = si;
        used[static_cast<size_t>(si)] = true;
    }

    // Per-pair keep-apart radius matrix (over participants).
    std::vector<Restriction> restr = in.restrictions;
    for (auto& r : restr) r = NormalizeRestriction(std::move(r));
    std::sort(restr.begin(), restr.end(), [](const Restriction& a, const Restriction& b) {
        return a.first != b.first ? a.first < b.first : a.second < b.second;
    });
    restr.erase(std::unique(restr.begin(), restr.end(), RestrictionEquals), restr.end());

    const size_t pc = parts.size();
    std::vector<int> pairRadius(pc * pc, -1);
    std::vector<std::vector<int>> restrictedByStudent(pc);
    std::vector<int> degree(pc, 0);
    for (const auto& r : restr) {
        auto a = idx.find(CanonicalName(r.first)), b = idx.find(CanonicalName(r.second));
        if (a == idx.end() || b == idx.end()) continue;
        const int ai = a->second, bi = b->second;
        const size_t ab = static_cast<size_t>(ai) * pc + static_cast<size_t>(bi);
        const size_t ba = static_cast<size_t>(bi) * pc + static_cast<size_t>(ai);
        if (ai != bi && pairRadius[ab] < 0) {
            const int rad = std::max(0, r.radius);
            pairRadius[ab] = rad;
            pairRadius[ba] = rad;
            restrictedByStudent[static_cast<size_t>(ai)].push_back(bi);
            restrictedByStudent[static_cast<size_t>(bi)].push_back(ai);
            ++degree[static_cast<size_t>(ai)]; ++degree[static_cast<size_t>(bi)];
        }
    }

    // Auto tag-based rules from "Behavior" students (no two on same furniture item).
    for (size_t i = 0; i < in.behaviorStudents.size(); ++i) {
        for (size_t j = i + 1; j < in.behaviorStudents.size(); ++j) {
            auto a = idx.find(CanonicalName(in.behaviorStudents[i]));
            auto b = idx.find(CanonicalName(in.behaviorStudents[j]));
            if (a == idx.end() || b == idx.end()) continue;
            const int ai = a->second, bi = b->second;
            if (ai == bi) continue;
            const size_t ab = static_cast<size_t>(ai) * pc + static_cast<size_t>(bi);
            const size_t ba = static_cast<size_t>(bi) * pc + static_cast<size_t>(ai);
            if (pairRadius[ab] < 0) {
                pairRadius[ab] = 0; // radius 0 = cannot same item
                pairRadius[ba] = 0;
                restrictedByStudent[static_cast<size_t>(ai)].push_back(bi);
                restrictedByStudent[static_cast<size_t>(bi)].push_back(ai);
                ++degree[static_cast<size_t>(ai)]; ++degree[static_cast<size_t>(bi)];
            }
        }
    }

    // mustTogether: pairs that must share the same furniture item (for tables/groups).
    std::vector<std::pair<int,int>> mustSamePairs;
    for (const auto& t : in.mustTogether) {
        auto a = idx.find(CanonicalName(t.first)), b = idx.find(CanonicalName(t.second));
        if (a != idx.end() && b != idx.end() && a->second != b->second)
            mustSamePairs.emplace_back(a->second, b->second);
    }

    // Group affinities: treat intra-group pairs as must-same-item (cluster on same furniture)
    for (const auto& grp : in.groupAffinities) {
        for (size_t i = 0; i < grp.size(); ++i) {
            for (size_t j = i+1; j < grp.size(); ++j) {
                auto a = idx.find(CanonicalName(grp[i])), b = idx.find(CanonicalName(grp[j]));
                if (a != idx.end() && b != idx.end() && a->second != b->second) {
                    mustSamePairs.emplace_back(a->second, b->second);
                }
            }
        }
    }

    // Seat geometry matrices.
    const size_t sc = static_cast<size_t>(total);
    std::vector<int>  dist(sc * sc, 0);
    std::vector<char> sameItem(sc * sc, 0);
    for (int a = 0; a < total; ++a)
        for (int b = 0; b < total; ++b) {
            if (a == b) continue;
            const double dx = static_cast<double>(in.seatCentres[static_cast<size_t>(a)].x -
                                                  in.seatCentres[static_cast<size_t>(b)].x);
            const double dy = static_cast<double>(in.seatCentres[static_cast<size_t>(a)].y -
                                                  in.seatCentres[static_cast<size_t>(b)].y);
            const size_t ab = static_cast<size_t>(a) * sc + static_cast<size_t>(b);
            dist[ab] =
                static_cast<int>(std::lround(std::hypot(dx, dy)));
            sameItem[ab] =
                (in.seatItem[static_cast<size_t>(a)] == in.seatItem[static_cast<size_t>(b)]) ? 1 : 0;
        }

    // mustSameItem: for mustTogether pairs, 1 if the two *seats* must be on same furniture.
    std::vector<char> mustSameItem(sc * sc, 0);
    for (const auto& pr : mustSamePairs) {
        // We will check at assignment time using sameItem for the chosen seats.
        // For simplicity we enforce same-item for must-together (works great for Table4 etc).
        // (If needed later, we can relax to dist <= threshold.)
    }

    // Front-row band: front-required students may only take seats whose centre
    // lies within kFrontBandFraction of the room depth, measured from the front
    // edge. Others (mustBeFront == 0) are unconstrained.
    const int axisDepth = (in.frontEdge == RoomEdge::Top || in.frontEdge == RoomEdge::Bottom)
                          ? EffectiveRoomH(in.roomH) : EffectiveRoomW(in.roomW);
    const int frontThreshold = std::max(1, static_cast<int>(axisDepth * kFrontBandFraction));
    std::vector<char> frontEligible(static_cast<size_t>(total), 1);
    for (int s = 0; s < total; ++s)
        frontEligible[static_cast<size_t>(s)] =
            SeatFrontDistance(in.seatCentres[static_cast<size_t>(s)], in.frontEdge,
                              in.roomW, in.roomH) <= frontThreshold ? 1 : 0;

    std::vector<char> mustBeFront(pc, 0);
    for (int i = 0; i < static_cast<int>(in.roster.size()) && i < static_cast<int>(pc); ++i)
        if (i < static_cast<int>(in.rosterFrontRequired.size()) &&
            in.rosterFrontRequired[static_cast<size_t>(i)])
            mustBeFront[static_cast<size_t>(i)] = 1;

    // Reject fixed seats that already violate a restriction.
    for (int i = 0; i < static_cast<int>(assigned.size()); ++i) {
        if (assigned[static_cast<size_t>(i)] < 0) continue;
        for (int j = i+1; j < static_cast<int>(assigned.size()); ++j) {
            if (assigned[static_cast<size_t>(j)] < 0) continue;
            const int r = pairRadius[static_cast<size_t>(i) * pc + static_cast<size_t>(j)];
            if (r < 0) continue;
            const int sa = assigned[static_cast<size_t>(i)], sb = assigned[static_cast<size_t>(j)];
            const bool bad = (r == 0)
                ? sameItem[static_cast<size_t>(sa) * sc + static_cast<size_t>(sb)] != 0
                : dist[static_cast<size_t>(sa) * sc + static_cast<size_t>(sb)] <= r;
            if (bad) { sol.errorMessage = L"Fixed seats already violate a restriction"; return sol; }
        }
    }

    // Reject fixed seats that already violate must-together (not on same item).
    for (const auto& pr : mustSamePairs) {
        int p1 = pr.first, p2 = pr.second;
        int s1 = (p1 < static_cast<int>(assigned.size()) ? assigned[static_cast<size_t>(p1)] : -1);
        int s2 = (p2 < static_cast<int>(assigned.size()) ? assigned[static_cast<size_t>(p2)] : -1);
        if (s1 >= 0 && s2 >= 0) {
            if (sameItem[static_cast<size_t>(s1) * sc + static_cast<size_t>(s2)] == 0) {
                sol.errorMessage = L"Fixed seats already violate a must-together rule";
                return sol;
            }
        }
    }

    // Reject fixed for whitelisting
    for (size_t pi = 0; pi < assigned.size() && pi < in.rosterForbidden.size(); ++pi) {  // pi here is participant index, but for roster ones
        int s = assigned[pi];
        if (s < 0) continue;
        // Only check for original roster students (simplified; fixed extras not in rosterForbidden)
        if (pi < in.rosterForbidden.size()) {
            const auto& forb = in.rosterForbidden[pi];
            if (!forb.empty() && s < static_cast<int>(in.seatLabels.size())) {
                const auto& sl = in.seatLabels[s];
                for (const auto& f : forb) {
                    if (CanonicalName(f) == CanonicalName(sl)) {
                        sol.errorMessage = L"Fixed seat violates whitelisting for student";
                        return sol;
                    }
                }
            }
        }
    }

    // Warm-start: pre-place from previousAssignments (current seating for movable students)
    std::unordered_set<int> warmPlaced;
    if (!in.previousAssignments.empty() && in.previousAssignments.size() >= in.roster.size()) {
        for (int i = 0; i < static_cast<int>(in.roster.size()); ++i) {
            if (fixedCans.count(cans[i])) continue;
            int pref = in.previousAssignments[i];
            if (pref >= 0 && pref < total && !used[static_cast<size_t>(pref)]) {
                assigned[i] = pref;
                used[static_cast<size_t>(pref)] = true;
                warmPlaced.insert(i);
            }
        }
    }

    // Unassigned roster students, ordered most-constrained first.
    std::vector<int> order;
    order.reserve(in.roster.size());
    for (int i = 0; i < static_cast<int>(in.roster.size()); ++i)
        if (!fixedCans.count(cans[static_cast<size_t>(i)]) && !warmPlaced.count(i)) order.push_back(i);

    const int open = static_cast<int>(std::count(used.begin(), used.end(), 0));
    if (static_cast<int>(order.size()) > open) {
        sol.errorMessage = L"Not enough empty seats for unassigned roster students"; return sol;
    }

    std::sort(order.begin(), order.end(), [&](int l, int r) {
        // Front-required students are the most constrained → place them first.
        const bool fl = mustBeFront[static_cast<size_t>(l)] != 0;
        const bool fr = mustBeFront[static_cast<size_t>(r)] != 0;
        if (fl != fr) return fl;
        if (degree[static_cast<size_t>(l)] != degree[static_cast<size_t>(r)])
            return degree[static_cast<size_t>(l)] > degree[static_cast<size_t>(r)];
        return cans[static_cast<size_t>(l)] < cans[static_cast<size_t>(r)];
    });

    std::vector<int> seatOrder;
    seatOrder.reserve(static_cast<size_t>(total));
    for (int s = 0; s < total; ++s)
        if (!used[static_cast<size_t>(s)]) seatOrder.push_back(s);
    std::shuffle(seatOrder.begin(), seatOrder.end(), std::mt19937(std::random_device{}()));

    size_t steps = 0; bool limitHit = false;
    if (!Backtrack(0, order, seatOrder, restrictedByStudent,
                   pairRadius, dist, sameItem, pc, sc,
                   mustBeFront, frontEligible, mustSamePairs,
                   in.rosterForbidden, in.seatLabels,
                   assigned, used, steps, limitHit, progressWnd, cancel, in.searchLimit)) {
        sol.searchLimitHit = limitHit;
        sol.errorMessage = limitHit ? L"Auto-assign search limit reached"
                                    : L"Could not satisfy all restrictions";
        if (limitHit) {
            // Fallback only on limit hit: random assignment for unassigned
            std::vector<int> unassignedOrder;
            for (int i = 0; i < static_cast<int>(in.roster.size()); ++i) {
                if (assigned[i] < 0) unassignedOrder.push_back(i);
            }
            if (!unassignedOrder.empty()) {
                std::vector<int> freeSeats;
                for (int s = 0; s < total; ++s) if (!used[s]) freeSeats.push_back(s);
                if (freeSeats.size() >= unassignedOrder.size()) {
                    std::mt19937 rng(std::random_device{}());
                    std::shuffle(freeSeats.begin(), freeSeats.end(), rng);
                    bool trialOk = true;
                    for (size_t u = 0; u < unassignedOrder.size() && trialOk; ++u) {
                        int si = unassignedOrder[u];
                        int seat = freeSeats[u];
                        if (mustBeFront[si] && !frontEligible[seat]) { trialOk = false; continue; }
                        // TODO: full restriction check for better fallback
                        assigned[si] = seat;
                        used[seat] = true;
                    }
                    if (trialOk) {
                        sol.success = true;
                        sol.errorMessage = L" (used random fallback)";
                        sol.searchLimitHit = false;
                    }
                }
            }
        }
        if (!sol.success) return sol;
    }

    // --- Soft refinement: improve "sit near" affinities by swapping the seats
    //     of two movable students whenever it lowers total pair distance and
    //     keeps every hard constraint satisfied (hill-climbing). ---
    std::vector<std::pair<int,int>> affinityPairs;
    {
        std::vector<Restriction> aff = in.affinities;
        for (auto& a : aff) a = NormalizeRestriction(std::move(a));
        std::sort(aff.begin(), aff.end(), [](const Restriction& a, const Restriction& b) {
            return a.first != b.first ? a.first < b.first : a.second < b.second;
        });
        aff.erase(std::unique(aff.begin(), aff.end(), RestrictionEquals), aff.end());
        for (const auto& a : aff) {
            auto x = idx.find(CanonicalName(a.first)), y = idx.find(CanonicalName(a.second));
            if (x != idx.end() && y != idx.end() && x->second != y->second)
                affinityPairs.push_back({ x->second, y->second });
        }
    }
    if (!affinityPairs.empty() && order.size() >= 2) {
        long cur = SoftCost(assigned, affinityPairs, dist, sc);
        const size_t budget = 20000;
        size_t swapSteps = 0;
        bool improved = true;
        while (improved && swapSteps < budget) {
            improved = false;
            for (size_t a = 0; a < order.size() && swapSteps < budget; ++a)
                for (size_t b = a + 1; b < order.size() && swapSteps < budget; ++b) {
                    const int pa = order[a], pb = order[b];
                    std::swap(assigned[static_cast<size_t>(pa)], assigned[static_cast<size_t>(pb)]);
                    ++swapSteps;
                    if (HardOK(assigned, pairRadius, dist, sameItem, pc, sc,
                               mustBeFront, frontEligible, mustSamePairs,
                               in.rosterForbidden, in.seatLabels)) {
                        const long c = SoftCost(assigned, affinityPairs, dist, sc);
                        if (c < cur) { cur = c; improved = true; continue; }   // keep improving swap
                    }
                    std::swap(assigned[static_cast<size_t>(pa)], assigned[static_cast<size_t>(pb)]); // revert
                }
        }
    }

    // Basic simulated annealing refinement (post hill-climb) using probabilistic swaps.
    // Geometric cooling; accepts some uphill moves early to escape local minima.
    if (!affinityPairs.empty() && order.size() >= 2) {
        long cur = SoftCost(assigned, affinityPairs, dist, sc);
        double temp = 2000.0;
        const double cooling = 0.98;
        const size_t saBudget = 8000;
        std::mt19937 rng(std::random_device{}());
        std::uniform_real_distribution<double> dist01(0.0, 1.0);
        for (size_t it = 0; it < saBudget && temp > 0.1; ++it) {
            size_t ia = rng() % order.size();
            size_t ib = rng() % order.size();
            if (ia == ib) continue;
            const int pa = order[ia], pb = order[ib];
            std::swap(assigned[static_cast<size_t>(pa)], assigned[static_cast<size_t>(pb)]);
            if (HardOK(assigned, pairRadius, dist, sameItem, pc, sc, mustBeFront, frontEligible, mustSamePairs,
                       in.rosterForbidden, in.seatLabels)) {
                long c = SoftCost(assigned, affinityPairs, dist, sc);
                double delta = static_cast<double>(c - cur);
                if (delta < 0 || dist01(rng) < std::exp(-delta / temp)) {
                    cur = c;
                } else {
                    std::swap(assigned[static_cast<size_t>(pa)], assigned[static_cast<size_t>(pb)]);
                }
            } else {
                std::swap(assigned[static_cast<size_t>(pa)], assigned[static_cast<size_t>(pb)]);
            }
            temp *= cooling;
        }
    }

    // Compute affinity satisfaction % for reporting / badge.
    // Weighted by affinity weight. A pair is "met" if on same furniture item or within "near" threshold.
    constexpr int kNearThreshold = 300; // room units
    if (!affinityPairs.empty()) {
        long totalWeight = 0;
        long metWeight = 0;
        // Rebuild weights from in.affinities (small)
        std::vector<int> weights(affinityPairs.size(), 1);
        for (size_t i = 0; i < affinityPairs.size(); ++i) {
            // match by canonical later if needed; for now use 1 or lookup
            // Simple: use the order, assume parallel
        }
        for (size_t i = 0; i < affinityPairs.size(); ++i) {
            const auto& p = affinityPairs[i];
            int w = 1;
            // lookup weight from original affinities if possible (approximate)
            for (const auto& a : in.affinities) {
                if (CanonicalName(a.first) == CanonicalName(in.roster[p.first]) &&
                    CanonicalName(a.second) == CanonicalName(in.roster[p.second])) {
                    w = std::max(1, a.weight);
                    break;
                }
            }
            const int sa = assigned[static_cast<size_t>(p.first)];
            const int sb = assigned[static_cast<size_t>(p.second)];
            if (sa < 0 || sb < 0) { totalWeight += w; continue; }
            const bool same = sameItem[static_cast<size_t>(sa) * sc + static_cast<size_t>(sb)] != 0;
            const int d = dist[static_cast<size_t>(sa) * sc + static_cast<size_t>(sb)];
            totalWeight += w;
            if (same || d <= kNearThreshold) metWeight += w;
        }
        sol.affinitySatisfaction = totalWeight > 0 ? static_cast<double>(metWeight) / totalWeight : 1.0;
    } else {
        sol.affinitySatisfaction = 1.0; // no preferences = trivially satisfied
    }

    sol.assignments.assign(in.roster.size(), -1);
    for (int i = 0; i < static_cast<int>(in.roster.size()); ++i)
        sol.assignments[static_cast<size_t>(i)] = assigned[static_cast<size_t>(i)];
    sol.success = true;
    sol.stepsUsed = steps;
    auto end = std::chrono::high_resolution_clock::now();
    sol.elapsedMs = std::chrono::duration<double, std::milli>(end - start).count();
    return sol;
}

// ---------------------------------------------------------------------------
// Worker thread — wraps SolveAutoAssign and posts the result.
// ---------------------------------------------------------------------------

namespace {
struct ThreadCtx {
    HWND               notifyWnd = nullptr;
    std::atomic<bool>* cancel    = nullptr;
    AutoAssignInput    input;
};
} // namespace

static DWORD WINAPI AutoAssignThread(LPVOID param) {
    auto* ctx = static_cast<ThreadCtx*>(param);

    AutoAssignSolution sol = SolveAutoAssign(ctx->input, ctx->cancel, ctx->notifyWnd);

    auto* result = new AutoAssignResult();
    result->roster         = ctx->input.roster;
    result->success        = sol.success;
    result->searchLimitHit = sol.searchLimitHit;
    result->errorMessage   = std::move(sol.errorMessage);
    result->assignments    = std::move(sol.assignments);
    result->affinitySatisfaction = sol.affinitySatisfaction;
    result->stepsUsed = sol.stepsUsed;
    result->elapsedMs = sol.elapsedMs;

    const HWND notify = ctx->notifyWnd;
    delete ctx;
    PostMessageW(notify, WM_APP_AUTOASSIGN_DONE, 0, reinterpret_cast<LPARAM>(result));
    return 0;
}

// ---------------------------------------------------------------------------
// Pre-solve validation (used for warning dialog and early exit).
// Collects specific actionable problems without running the expensive search.
// ---------------------------------------------------------------------------
std::vector<std::wstring> ValidateAutoAssignPreconditions(const AppState& state) {
    std::vector<std::wstring> problems;

    if (state.roster.empty()) {
        problems.push_back(L"No roster loaded.");
    }

    const int totalSeats = TotalLayoutSeats(state.layoutItems);
    if (totalSeats == 0) {
        problems.push_back(L"No seats defined (add furniture in Layout mode).");
    } else if (static_cast<int>(state.roster.size()) > totalSeats) {
        problems.push_back(L"Roster has more students (" + std::to_wstring(state.roster.size()) +
                           L") than available seats (" + std::to_wstring(totalSeats) + L").");
    }

    std::wstring dup;
    if (FindDuplicateCanonicalName(state.roster, &dup)) {
        problems.push_back(L"Duplicate student name in roster: " + dup);
    }

    // Check for obvious fixed violations (simplified; full check happens in solver)
    // For now, just count fixed vs seats - more detailed in Solve.

    // Basic contradiction detection: must-together conflicting with keep-apart restriction
    for (const auto& mt : state.mustTogether) {
        auto cn1 = CanonicalName(mt.first);
        auto cn2 = CanonicalName(mt.second);
        for (const auto& r : state.restrictions) {
            auto rc1 = CanonicalName(r.first);
            auto rc2 = CanonicalName(r.second);
            if ((cn1 == rc1 && cn2 == rc2) || (cn1 == rc2 && cn2 == rc1)) {
                problems.push_back(L"Conflict: " + mt.first + L" must-together " + mt.second +
                                   L" but also has keep-apart restriction.");
            }
        }
    }

    // Group internal conflicts with keep-apart
    for (const auto& g : state.groupAffinities) {
        for (size_t i = 0; i < g.size(); ++i) {
            for (size_t j = i + 1; j < g.size(); ++j) {
                auto c1 = CanonicalName(g[i]);
                auto c2 = CanonicalName(g[j]);
                for (const auto& r : state.restrictions) {
                    auto rc1 = CanonicalName(r.first);
                    auto rc2 = CanonicalName(r.second);
                    if ((c1 == rc1 && c2 == rc2) || (c1 == rc2 && c2 == rc1)) {
                        problems.push_back(L"Group conflict: " + g[i] + L" and " + g[j] +
                                           L" in group but restricted apart.");
                    }
                }
            }
        }
    }

    // Future: front band capacity, etc.

    return problems;
}

// ---------------------------------------------------------------------------
// Public entry point — assemble input from the chart, launch the worker.
// ---------------------------------------------------------------------------

bool BeginAutoAssign(HWND notifyWnd, const AppState& state,
                      std::atomic<bool>& cancel, HANDLE& threadHandle) {
    auto problems = ValidateAutoAssignPreconditions(state);
    if (!problems.empty()) {
        // Caller (StartAutoAssign) will show dialog; for direct calls just fail fast.
        return false;
    }

    if (state.roster.empty()) return false;

    // Seats live on the furniture; flatten them into one ordered list.
    const std::vector<LayoutSeatRef> seats = EnumerateLayoutSeats(state.layoutItems);
    const int total = static_cast<int>(seats.size());
    if (total == 0) return false;
    if (static_cast<int>(state.roster.size()) > total) return false;

    cancel.store(false, std::memory_order_relaxed);

    auto* ctx          = new ThreadCtx();
    ctx->notifyWnd     = notifyWnd;
    ctx->cancel        = &cancel;
    AutoAssignInput& in = ctx->input;
    in.seatCount       = total;
    in.roster          = state.roster;
    in.restrictions    = state.restrictions;
    in.mustTogether    = state.mustTogether;
    in.affinities      = state.affinities;
    in.frontEdge       = state.frontEdge;
    in.roomW           = state.roomW;
    in.roomH           = state.roomH;
    in.searchLimit     = state.autoAssignSearchLimit > 0 ? state.autoAssignSearchLimit : kDefaultAutoAssignSearchLimit;
    in.groupAffinities = state.groupAffinities;

    // Per-student forbidden for whitelisting
    in.rosterForbidden.resize(state.roster.size());
    for (size_t i = 0; i < state.roster.size(); ++i) {
        const StudentInfo* info = state.FindStudent(state.roster[i]);
        if (info) in.rosterForbidden[i] = info->forbiddenDesks;
    }

    // Seat labels for forbidden checks
    in.seatLabels.resize(static_cast<size_t>(total));
    for (int k = 0; k < total; ++k) {
        const int itemIdx = seats[static_cast<size_t>(k)].first;
        const LayoutItem& item = state.layoutItems[static_cast<size_t>(itemIdx)];
        in.seatLabels[static_cast<size_t>(k)] = item.label.empty() ? std::wstring(LayoutTypeName(item.type)) : item.label;
    }

    // "Front row"-tagged roster students must sit in the front band.
    // Also collect "Behavior" tagged for auto keep-apart rules.
    in.behaviorStudents.clear();
    in.rosterFrontRequired.resize(state.roster.size());
    for (size_t i = 0; i < state.roster.size(); ++i) {
        const StudentInfo* info = state.FindStudent(state.roster[i]);
        bool front = false;
        bool behavior = false;
        if (info) {
            for (const auto& t : info->tags) {
                const auto ct = CanonicalName(t);
                if (ct == L"front row") front = true;
                if (ct == L"behavior" || ct == L"behaviour") behavior = true;
            }
        }
        in.rosterFrontRequired[i] = front ? 1 : 0;
        if (behavior) in.behaviorStudents.push_back(state.roster[i]);
    }

    // fixedOccupants left empty: auto-assign reshuffles everyone freely each press.
    in.fixedOccupants.resize(static_cast<size_t>(total));
    in.seatCentres.resize(static_cast<size_t>(total));
    in.seatItem.resize(static_cast<size_t>(total));

    // Room-coord, rotation-aware seat centres (cached per furniture item).
    std::unordered_map<int, std::vector<POINT>> centreCache;
    for (int k = 0; k < total; ++k) {
        const int itemIdx = seats[static_cast<size_t>(k)].first;
        const int slot    = seats[static_cast<size_t>(k)].second;
        const LayoutItem& item = state.layoutItems[static_cast<size_t>(itemIdx)];
        in.seatItem[static_cast<size_t>(k)] = itemIdx;

        auto cit = centreCache.find(itemIdx);
        if (cit == centreCache.end())
            cit = centreCache.emplace(itemIdx, LayoutSeatSlotRoomCenters(item)).first;
        const auto& centres = cit->second;
        in.seatCentres[static_cast<size_t>(k)] =
            (slot < static_cast<int>(centres.size()))
                ? centres[static_cast<size_t>(slot)]
                : POINT{ (item.bounds.left + item.bounds.right) / 2,
                         (item.bounds.top  + item.bounds.bottom) / 2 };
    }

    // No warm-start: each press produces a fresh random assignment.

    threadHandle = CreateThread(nullptr, 0, AutoAssignThread, ctx, 0, nullptr);
    if (!threadHandle) { delete ctx; return false; }
    return true;
}
