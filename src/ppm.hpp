// PPM-C (escape method C + exclusion) for alphabets of 1, 4 and 8 bits.
// Classic 32-bit arithmetic coder, per-order hash tables, adaptive rescaling,
// local-rate monitoring with reset or pruning.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace ppm {

enum class Mode : uint8_t { None = 0, Reset = 1, Prune = 2 };

// Returns the CLI/CSV name ("none", "reset" or "prune") for `m`.
inline const char *modeName(Mode m) {
  switch (m) {
  case Mode::None:
    return "none";
  case Mode::Reset:
    return "reset";
  case Mode::Prune:
    return "prune";
  }
  return "?";
}

// Parses `s` into `m`; returns false when `s` is not a known mode name.
inline bool parseMode(const std::string &s, Mode &m) {
  if (s == "none") {
    m = Mode::None;
    return true;
  }
  if (s == "reset") {
    m = Mode::Reset;
    return true;
  }
  if (s == "prune") {
    m = Mode::Prune;
    return true;
  }
  return false;
}

struct Params {
  int bits = 8;               // 1, 4 or 8
  int kmax = 6;               // maximum context order
  Mode mode = Mode::None;     // none | reset | prune
  uint32_t j = 50000;         // monitoring window, symbols
  uint32_t p = 20;            // tolerated degradation, percent
  uint64_t mem_cap = 8000000; // max number of contexts kept
};

// ---------------------------------------------------------------- bit stream

class BitWriter {
public:
  // Binds this writer to `out`; bits are appended as bytes when 8 are buffered.
  explicit BitWriter(std::vector<uint8_t> &out) : out_(out) {}
  // Appends the least significant bit of `bit` to the output stream.
  void put(int bit) {
    cur_ = static_cast<uint8_t>((cur_ << 1) | (bit & 1));
    if (++n_ == 8) {
      out_.push_back(cur_);
      cur_ = 0;
      n_ = 0;
    }
    ++bits_;
  }
  // Pads the current partial byte with zero bits and writes it out.
  void flush() {
    while (n_ != 0)
      put(0);
  }
  // Returns the total number of bits written so far.
  uint64_t bitCount() const { return bits_; }

private:
  std::vector<uint8_t> &out_;
  uint8_t cur_ = 0;
  int n_ = 0;
  uint64_t bits_ = 0;
};

class BitReader {
public:
  // Binds this reader to `in`; bits are consumed MSB-first.
  explicit BitReader(const std::vector<uint8_t> &in) : in_(in) {}
  // Returns the next input bit, or 0 after the end of the buffer.
  int get() {
    if (n_ == 0) {
      cur_ = pos_ < in_.size() ? in_[pos_++] : 0;
      n_ = 8;
    }
    int b = (cur_ >> 7) & 1;
    cur_ = static_cast<uint8_t>(cur_ << 1);
    --n_;
    return b;
  }

private:
  const std::vector<uint8_t> &in_;
  size_t pos_ = 0;
  uint8_t cur_ = 0;
  int n_ = 0;
};

// --------------------------------------------------------- arithmetic coder

class ArithEncoder {
public:
  static constexpr uint32_t kHalf = 0x80000000u;
  static constexpr uint32_t kQuarter = 0x40000000u;
  static constexpr uint32_t kThreeQuarter = 0xC0000000u;

  // Creates an encoder that writes its bits through `w`.
  explicit ArithEncoder(BitWriter &w) : w_(w) {}

  // Encodes the interval [cum, cum+freq) out of `total` (cumulative frequency
  // `cum`, frequency `freq`) and renormalizes the interval, emitting bits.
  void encode(uint32_t cum, uint32_t freq, uint32_t total) {
    // range * total can exceed 32 bits, so the products use 64-bit math; the
    // `- 1` keeps high_ inside the new half-open interval.
    uint64_t range = static_cast<uint64_t>(high_) - low_ + 1;
    high_ = low_ + static_cast<uint32_t>((range * (cum + freq)) / total - 1);
    low_ = low_ + static_cast<uint32_t>((range * cum) / total);
    for (;;) {
      if (high_ < kHalf) {
        emit(0);
      } else if (low_ >= kHalf) {
        emit(1);
        low_ -= kHalf;
        high_ -= kHalf;
      } else if (low_ >= kQuarter && high_ < kThreeQuarter) {
        ++pending_; // underflow: defer this bit until the next output
        low_ -= kQuarter;
        high_ -= kQuarter;
      } else {
        break;
      }
      low_ <<= 1;
      high_ = (high_ << 1) | 1u;
    }
  }

  // Flushes enough final bits to disambiguate the last interval.
  void finish() {
    ++pending_;
    emit(low_ < kQuarter ? 0 : 1);
  }

private:
  // Writes `bit` followed by the complement of all pending underflow bits.
  void emit(int bit) {
    w_.put(bit);
    while (pending_ != 0) {
      w_.put(bit ^ 1);
      --pending_;
    }
  }

  BitWriter &w_;
  uint32_t low_ = 0;
  uint32_t high_ = 0xFFFFFFFFu;
  uint64_t pending_ = 0;
};

class ArithDecoder {
public:
  // Creates a decoder that reads bits from `r` and primes the 32-bit code.
  explicit ArithDecoder(BitReader &r) : r_(r) {
    for (int i = 0; i < 32; ++i)
      code_ = (code_ << 1) | static_cast<uint32_t>(r_.get());
  }

  // Maps the current code value to a target in [0, total); the caller uses it
  // to select a symbol interval.
  uint32_t target(uint32_t total) const {
    uint64_t range = static_cast<uint64_t>(high_) - low_ + 1;
    return static_cast<uint32_t>(
        ((static_cast<uint64_t>(code_) - low_ + 1) * total - 1) / range);
  }

  // Consumes the interval [cum, cum+freq) out of `total` and renormalizes,
  // reading new bits as needed to keep the 32-bit code valid.
  void update(uint32_t cum, uint32_t freq, uint32_t total) {
    uint64_t range = static_cast<uint64_t>(high_) - low_ + 1;
    high_ = low_ + static_cast<uint32_t>((range * (cum + freq)) / total - 1);
    low_ = low_ + static_cast<uint32_t>((range * cum) / total);
    for (;;) {
      if (high_ < ArithEncoder::kHalf) {
        // nothing
      } else if (low_ >= ArithEncoder::kHalf) {
        code_ -= ArithEncoder::kHalf;
        low_ -= ArithEncoder::kHalf;
        high_ -= ArithEncoder::kHalf;
      } else if (low_ >= ArithEncoder::kQuarter &&
                 high_ < ArithEncoder::kThreeQuarter) {
        code_ -= ArithEncoder::kQuarter;
        low_ -= ArithEncoder::kQuarter;
        high_ -= ArithEncoder::kQuarter;
      } else {
        break;
      }
      low_ <<= 1;
      high_ = (high_ << 1) | 1u;
      code_ = (code_ << 1) | static_cast<uint32_t>(r_.get());
    }
  }

private:
  BitReader &r_;
  uint32_t low_ = 0;
  uint32_t high_ = 0xFFFFFFFFu;
  uint32_t code_ = 0;
};

// ------------------------------------------------------------- cost (Q16.16)

constexpr int kLogTableSize = 70000; // comfortably above kMaxTotal and 256
constexpr uint32_t kMaxTotal = 8192; // rescale threshold

// Returns a lazily initialized table with round(log2(i) * 65536) for i >= 1.
inline const uint32_t *logTable() {
  static uint32_t table[kLogTableSize];
  // Fill the table once, on the first call.
  static bool once = [] {
    for (int i = 1; i < kLogTableSize; ++i)
      table[i] = static_cast<uint32_t>(
          std::llround(std::log2(static_cast<double>(i)) * 65536.0));
    table[0] = 0;
    return true;
  }();
  (void)once;
  return table;
}

// Returns the ideal code length -log2(freq / total) in Q16 bits, where `total`
// is the sum of the frequencies and `freq` is the frequency of the symbol.
// Requires 1 <= freq <= total < kLogTableSize; the model guarantees this by
// rescaling at kMaxTotal and never storing a zero-count entry.
inline uint32_t symCost(uint32_t total, uint32_t freq) {
  const uint32_t *t = logTable();
  return t[total] - t[freq];
}

// ------------------------------------------------------------------- model

struct Entry {
  uint8_t sym;
  uint16_t cnt;
};

struct Node {
  std::vector<Entry> v; // sorted by symbol
  uint32_t total = 0;

  // Returns the index of symbol `s` in the sorted entries, or -1 if absent.
  int find(uint8_t s) const {
    int lo = 0, hi = static_cast<int>(v.size()) - 1;
    while (lo <= hi) {
      int mid = (lo + hi) >> 1;
      if (v[mid].sym == s)
        return mid;
      if (v[mid].sym < s)
        lo = mid + 1;
      else
        hi = mid - 1;
    }
    return -1;
  }

  // Increments the count of `s` (inserting it in sorted order if new) and
  // increments this node's total count.
  void add(uint8_t s) {
    int i = find(s);
    if (i >= 0) {
      // Saturation is only a safety net: update() rescales well before a
      // count can reach 65535, and `total` keeps growing regardless.
      if (v[i].cnt < 65535)
        ++v[i].cnt;
    } else {
      // Find the insertion point that keeps the entries sorted by symbol.
      auto it =
          std::lower_bound(v.begin(), v.end(), s,
                           [](const Entry &e, uint8_t x) { return e.sym < x; });
      v.insert(it, Entry{s, 1});
    }
    ++total;
  }

  // Halves every count with (c + 1) / 2 and recomputes the total, keeping the
  // counts small enough for the arithmetic coder. Counts stay >= 1, so no
  // symbol is forgotten here (pruning, not rescaling, drops contexts).
  void rescale() {
    uint32_t t = 0;
    for (Entry &e : v) {
      e.cnt = static_cast<uint16_t>((e.cnt + 1) / 2);
      t += e.cnt;
    }
    total = t;
  }
};

class Model {
public:
  // Builds a model with `p.bits`, `p.kmax`, `p.mode`, `p.j`, `p.p` and
  // `p.mem_cap`: one hash table per order 0..kmax, context masks, and cleared
  // exclusion flags. No context is created until update() is called.
  explicit Model(const Params &p)
      : P_(p), alphabet_(1 << p.bits), maps_(static_cast<size_t>(p.kmax) + 1),
        mask_(static_cast<size_t>(p.kmax) + 1) {
    for (int k = 0; k <= P_.kmax; ++k) {
      int sh = k * P_.bits;
      // History packs the newest symbol in the low bits, so the order-k mask
      // keeps only the last k symbols. Order 0 uses mask 0; a shift of 64 bits
      // or more would be undefined, hence the ~0ull fallback (the CLI and the
      // header check keep kmax * bits <= 64).
      mask_[k] = sh == 0 ? 0ull : (sh >= 64 ? ~0ull : ((1ull << sh) - 1));
    }
    std::memset(excluded_, 0, sizeof(excluded_));
  }

  // Encodes `sym` through `coder` using PPM-C exclusion, searching from the
  // highest available order down to order -1. Returns the chosen symbol's ideal
  // cost in Q16 bits (escape costs are not included) for the rate monitor.
  // Does not update the model; call update(sym) afterwards.
  uint32_t encodeSymbol(uint8_t sym, ArithEncoder &coder) {
    clearExcluded();
    // Only orders whose context fits in the history seen so far can match.
    // pos_ is uint64, but the cast is safe for any stream below 2^31 symbols.
    int maxk = std::min(P_.kmax, static_cast<int>(pos_));
    for (int k = maxk; k >= 0; --k) {
      auto it = maps_[k].find(history_ & mask_[k]);
      if (it == maps_[k].end())
        continue;
      Node &nd = it->second;

      uint32_t neff = 0, deff = 0, cum = 0;
      int si = -1;
      for (size_t i = 0; i < nd.v.size(); ++i) {
        const Entry &e = nd.v[i];
        if (excluded_[e.sym])
          continue;
        if (e.sym == sym) {
          si = static_cast<int>(i);
          cum = neff;
        }
        neff += e.cnt;
        ++deff;
      }
      if (deff == 0)
        continue; // everything here is excluded: no code needed this time
      // Escape method C: the escape gets frequency deff (the number of
      // distinct non-excluded symbols), so total = sum(counts) + distinct.
      uint32_t total = neff + deff;
      if (si >= 0) {
        coder.encode(cum, nd.v[si].cnt, total);
        return symCost(total, nd.v[si].cnt);
      }
      coder.encode(neff, deff, total); // escape
      for (const Entry &e : nd.v)
        addExcluded(e.sym);
    }

    // Order -1 fallback: uniform over all symbols not excluded by the higher
    // orders; `rank` is the position of `sym` in that reduced alphabet. This
    // point is reached only when `sym` itself is not excluded, so the
    // denominator alphabet_ - d is at least 1.
    uint32_t d = 0;
    for (int s = 0; s < alphabet_; ++s)
      if (excluded_[s])
        ++d;
    uint32_t rank = 0;
    for (int s = 0; s < sym; ++s)
      if (!excluded_[s])
        ++rank;
    coder.encode(rank, 1, alphabet_ - d);
    return symCost(alphabet_ - d, 1);
  }

  // Decodes one symbol from `coder` into `sym` using the same PPM-C search as
  // encodeSymbol(). Returns the chosen symbol's ideal cost in Q16 bits.
  // Does not update the model; call update(sym) afterwards.
  uint32_t decodeSymbol(uint8_t &sym, ArithDecoder &coder) {
    clearExcluded();
    // Mirrors the order limit and search order of encodeSymbol().
    int maxk = std::min(P_.kmax, static_cast<int>(pos_));
    for (int k = maxk; k >= 0; --k) {
      auto it = maps_[k].find(history_ & mask_[k]);
      if (it == maps_[k].end())
        continue;
      Node &nd = it->second;

      uint32_t neff = 0, deff = 0;
      for (const Entry &e : nd.v) {
        if (excluded_[e.sym])
          continue;
        neff += e.cnt;
        ++deff;
      }
      if (deff == 0)
        continue;
      uint32_t total = neff + deff;
      uint32_t t = coder.target(total);
      if (t < neff) {
        uint32_t cum = 0;
        for (const Entry &e : nd.v) {
          if (excluded_[e.sym])
            continue;
          if (t < cum + e.cnt) {
            coder.update(cum, e.cnt, total);
            sym = e.sym;
            return symCost(total, e.cnt);
          }
          cum += e.cnt;
        }
      } else {
        coder.update(neff, deff, total); // escape
        for (const Entry &e : nd.v)
          addExcluded(e.sym);
      }
    }

    // Order -1: pick the t-th symbol among those still allowed, the inverse
    // of the encoder's uniform fallback. As in encodeSymbol(), at least one
    // symbol remains because the target was never excluded.
    uint32_t d = 0;
    for (int s = 0; s < alphabet_; ++s)
      if (excluded_[s])
        ++d;
    uint32_t total = alphabet_ - d;
    uint32_t t = coder.target(total);
    uint32_t rank = 0;
    uint8_t picked = 0;
    for (int s = 0; s < alphabet_; ++s) {
      if (excluded_[s])
        continue;
      if (rank == t) {
        picked = static_cast<uint8_t>(s);
        break;
      }
      ++rank;
    }
    coder.update(t, 1, total);
    sym = picked;
    return symCost(total, 1);
  }

  // Records `sym` in every context of order 0..min(kmax, position), creating
  // new nodes while below `mem_cap` and rescaling counters above the threshold.
  // Advances the history and the symbol position.
  void update(uint8_t sym) {
    int maxk = std::min(P_.kmax, static_cast<int>(pos_));
    for (int k = 0; k <= maxk; ++k) {
      uint64_t key = history_ & mask_[k];
      auto it = maps_[k].find(key);
      if (it != maps_[k].end()) {
        Node &nd = it->second;
        nd.add(sym);
        // The coder sees sum(counts) + distinct(escape) as its total, so
        // rescale before that combined value exceeds kMaxTotal.
        if (nd.total + nd.v.size() > kMaxTotal)
          nd.rescale();
      } else if (nodes_ < P_.mem_cap) {
        // Soft cap: after it is reached no new contexts are created, but the
        // existing ones keep learning (and being rescaled).
        Node &nd = maps_[k][key];
        nd.add(sym);
        ++nodes_;
      }
    }
    // Only the low kmax*bits bits matter (see mask_); dropping older bits on
    // overflow is intentional. pos_ limits the order on very short streams.
    history_ = (history_ << P_.bits) | sym;
    ++pos_;
  }

  // Local rate monitor. Adds `cost` (Q16 bits) to the current j-symbol window
  // and, when the window closes, triggers reset/prune if it is more than p%
  // worse than the previous window. Called once per symbol, after update().
  // `cost` intentionally excludes escape bits (see encodeSymbol()); the
  // monitor only compares windows, so a constant bias cancels out.
  // The first window after a trigger becomes the new baseline.
  void account(uint64_t cost) {
    if (P_.mode == Mode::None || P_.j == 0)
      return;
    cur_ += cost;
    if (++win_ < P_.j)
      return;
    // Integer test of cur > prev * (1 + p/100); avoids floating point.
    if (prevValid_ && cur_ * 100 > prev_ * (100 + P_.p)) {
      if (P_.mode == Mode::Reset)
        resetModel();
      else
        pruneModel();
      prevValid_ = false;
    } else {
      prev_ = cur_;
      prevValid_ = true;
    }
    cur_ = 0;
    win_ = 0;
  }

  // Clears all context tables and the history, resets the tracked node count,
  // and increments the reset counter. The symbol position is kept.
  void resetModel() {
    for (auto &m : maps_)
      m.clear();
    nodes_ = 0;
    history_ = 0;
    ++resets_;
  }

  // Drops contexts whose total count is <= 1, halves the remaining counts with
  // (c + 1) / 2, refreshes the node count and increments the prune counter.
  // This keeps memory bounded and lets the model forget old statistics.
  void pruneModel() {
    uint64_t n = 0;
    for (auto &m : maps_) {
      for (auto it = m.begin(); it != m.end();) {
        if (it->second.total <= 1) {
          it = m.erase(it);
        } else {
          it->second.rescale();
          ++it;
        }
      }
      n += m.size();
    }
    nodes_ = n;
    ++prunes_;
  }

  // Returns the current total number of contexts across all orders.
  uint64_t contexts() const {
    uint64_t n = 0;
    for (const auto &m : maps_)
      n += m.size();
    return n;
  }
  // Returns the number of symbols processed so far.
  uint64_t position() const { return pos_; }
  // Returns the tracked number of nodes (as maintained by update/prune/reset).
  uint64_t nodes() const { return nodes_; }
  // Returns how many resets have been triggered by the monitor.
  uint64_t resets() const { return resets_; }
  // Returns how many prunes have been triggered by the monitor.
  uint64_t prunes() const { return prunes_; }

  // Returns the parameters this model was built with.
  const Params &params() const { return P_; }
  // Returns the alphabet size (2^bits: 2, 16 or 256).
  int alphabet() const { return alphabet_; }

private:
  // Marks symbol `s` as excluded for the current symbol and records it so the
  // flag can be cleared cheaply later.
  void addExcluded(uint8_t s) {
    if (!excluded_[s]) {
      excluded_[s] = true;
      excludedList_.push_back(s);
    }
  }
  // Clears the exclusion flags set while encoding/decoding the previous symbol.
  void clearExcluded() {
    for (uint8_t s : excludedList_)
      excluded_[s] = false;
    excludedList_.clear();
  }

  Params P_;
  int alphabet_;
  std::vector<std::unordered_map<uint64_t, Node>> maps_; // one table per order
  std::vector<uint64_t> mask_;
  uint64_t history_ = 0;
  uint64_t pos_ = 0;
  uint64_t nodes_ = 0;

  bool excluded_[256]; // indexed by symbol; bits <= 8 guarantees it fits
  std::vector<uint8_t> excludedList_;

  // monitor state
  uint64_t cur_ = 0, prev_ = 0;
  uint64_t win_ = 0;
  bool prevValid_ = false;
  uint64_t resets_ = 0;
  uint64_t prunes_ = 0;
};

} // namespace ppm
