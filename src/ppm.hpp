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

inline const char* modeName(Mode m) {
  switch (m) {
    case Mode::None: return "none";
    case Mode::Reset: return "reset";
    case Mode::Prune: return "prune";
  }
  return "?";
}

inline bool parseMode(const std::string& s, Mode& m) {
  if (s == "none") { m = Mode::None; return true; }
  if (s == "reset") { m = Mode::Reset; return true; }
  if (s == "prune") { m = Mode::Prune; return true; }
  return false;
}

struct Params {
  int bits = 8;                 // 1, 4 or 8
  int kmax = 6;                 // maximum context order
  Mode mode = Mode::None;       // none | reset | prune
  uint32_t j = 50000;           // monitoring window, symbols
  uint32_t p = 20;              // tolerated degradation, percent
  uint64_t mem_cap = 8000000;   // max number of contexts kept
};

// ---------------------------------------------------------------- bit stream

class BitWriter {
 public:
  explicit BitWriter(std::vector<uint8_t>& out) : out_(out) {}
  void put(int bit) {
    cur_ = static_cast<uint8_t>((cur_ << 1) | (bit & 1));
    if (++n_ == 8) {
      out_.push_back(cur_);
      cur_ = 0;
      n_ = 0;
    }
    ++bits_;
  }
  void flush() {
    while (n_ != 0) put(0);
  }
  uint64_t bitCount() const { return bits_; }

 private:
  std::vector<uint8_t>& out_;
  uint8_t cur_ = 0;
  int n_ = 0;
  uint64_t bits_ = 0;
};

class BitReader {
 public:
  explicit BitReader(const std::vector<uint8_t>& in) : in_(in) {}
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
  const std::vector<uint8_t>& in_;
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

  explicit ArithEncoder(BitWriter& w) : w_(w) {}

  void encode(uint32_t cum, uint32_t freq, uint32_t total) {
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
        ++pending_;
        low_ -= kQuarter;
        high_ -= kQuarter;
      } else {
        break;
      }
      low_ <<= 1;
      high_ = (high_ << 1) | 1u;
    }
  }

  void finish() {
    ++pending_;
    emit(low_ < kQuarter ? 0 : 1);
  }

 private:
  void emit(int bit) {
    w_.put(bit);
    while (pending_ != 0) {
      w_.put(bit ^ 1);
      --pending_;
    }
  }

  BitWriter& w_;
  uint32_t low_ = 0;
  uint32_t high_ = 0xFFFFFFFFu;
  uint64_t pending_ = 0;
};

class ArithDecoder {
 public:
  explicit ArithDecoder(BitReader& r) : r_(r) {
    for (int i = 0; i < 32; ++i) code_ = (code_ << 1) | static_cast<uint32_t>(r_.get());
  }

  uint32_t target(uint32_t total) const {
    uint64_t range = static_cast<uint64_t>(high_) - low_ + 1;
    return static_cast<uint32_t>(
        ((static_cast<uint64_t>(code_) - low_ + 1) * total - 1) / range);
  }

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
      } else if (low_ >= ArithEncoder::kQuarter && high_ < ArithEncoder::kThreeQuarter) {
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
  BitReader& r_;
  uint32_t low_ = 0;
  uint32_t high_ = 0xFFFFFFFFu;
  uint32_t code_ = 0;
};

// ------------------------------------------------------------- cost (Q16.16)

constexpr int kLogTableSize = 70000;
constexpr uint32_t kMaxTotal = 8192;  // rescale threshold

inline const uint32_t* logTable() {
  static uint32_t table[kLogTableSize];
  static bool once = [] {
    for (int i = 1; i < kLogTableSize; ++i)
      table[i] = static_cast<uint32_t>(std::llround(std::log2(static_cast<double>(i)) * 65536.0));
    table[0] = 0;
    return true;
  }();
  (void)once;
  return table;
}

// Ideal code length of a symbol with the given frequency, in Q16 bits.
inline uint32_t symCost(uint32_t total, uint32_t freq) {
  const uint32_t* t = logTable();
  return t[total] - t[freq];
}

// ------------------------------------------------------------------- model

struct Entry {
  uint8_t sym;
  uint16_t cnt;
};

struct Node {
  std::vector<Entry> v;  // sorted by symbol
  uint32_t total = 0;

  int find(uint8_t s) const {
    int lo = 0, hi = static_cast<int>(v.size()) - 1;
    while (lo <= hi) {
      int mid = (lo + hi) >> 1;
      if (v[mid].sym == s) return mid;
      if (v[mid].sym < s) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
  }

  void add(uint8_t s) {
    int i = find(s);
    if (i >= 0) {
      if (v[i].cnt < 65535) ++v[i].cnt;
    } else {
      auto it = std::lower_bound(v.begin(), v.end(), s,
                                 [](const Entry& e, uint8_t x) { return e.sym < x; });
      v.insert(it, Entry{s, 1});
    }
    ++total;
  }

  void rescale() {
    uint32_t t = 0;
    for (Entry& e : v) {
      e.cnt = static_cast<uint16_t>((e.cnt + 1) / 2);
      t += e.cnt;
    }
    total = t;
  }
};

class Model {
 public:
  explicit Model(const Params& p)
      : P_(p),
        alphabet_(1 << p.bits),
        maps_(static_cast<size_t>(p.kmax) + 1),
        mask_(static_cast<size_t>(p.kmax) + 1) {
    for (int k = 0; k <= P_.kmax; ++k) {
      int sh = k * P_.bits;
      mask_[k] = sh == 0 ? 0ull : (sh >= 64 ? ~0ull : ((1ull << sh) - 1));
    }
    std::memset(excluded_, 0, sizeof(excluded_));
  }

  // Encodes sym; returns its ideal cost in Q16 bits (for the monitor).
  uint32_t encodeSymbol(uint8_t sym, ArithEncoder& coder) {
    clearExcluded();
    int maxk = std::min(P_.kmax, static_cast<int>(pos_));
    for (int k = maxk; k >= 0; --k) {
      auto it = maps_[k].find(history_ & mask_[k]);
      if (it == maps_[k].end()) continue;
      Node& nd = it->second;

      uint32_t neff = 0, deff = 0, cum = 0;
      int si = -1;
      for (size_t i = 0; i < nd.v.size(); ++i) {
        const Entry& e = nd.v[i];
        if (excluded_[e.sym]) continue;
        if (e.sym == sym) { si = static_cast<int>(i); cum = neff; }
        neff += e.cnt;
        ++deff;
      }
      if (deff == 0) continue;  // everything here is excluded: no code needed
      uint32_t total = neff + deff;
      if (si >= 0) {
        coder.encode(cum, nd.v[si].cnt, total);
        return symCost(total, nd.v[si].cnt);
      }
      coder.encode(neff, deff, total);  // escape
      for (const Entry& e : nd.v) addExcluded(e.sym);
    }

    uint32_t d = 0;
    for (int s = 0; s < alphabet_; ++s)
      if (excluded_[s]) ++d;
    uint32_t rank = 0;
    for (int s = 0; s < sym; ++s)
      if (!excluded_[s]) ++rank;
    coder.encode(rank, 1, alphabet_ - d);
    return symCost(alphabet_ - d, 1);
  }

  // Decodes one symbol; returns its ideal cost in Q16 bits.
  uint32_t decodeSymbol(uint8_t& sym, ArithDecoder& coder) {
    clearExcluded();
    int maxk = std::min(P_.kmax, static_cast<int>(pos_));
    for (int k = maxk; k >= 0; --k) {
      auto it = maps_[k].find(history_ & mask_[k]);
      if (it == maps_[k].end()) continue;
      Node& nd = it->second;

      uint32_t neff = 0, deff = 0;
      for (const Entry& e : nd.v) {
        if (excluded_[e.sym]) continue;
        neff += e.cnt;
        ++deff;
      }
      if (deff == 0) continue;
      uint32_t total = neff + deff;
      uint32_t t = coder.target(total);
      if (t < neff) {
        uint32_t cum = 0;
        for (const Entry& e : nd.v) {
          if (excluded_[e.sym]) continue;
          if (t < cum + e.cnt) {
            coder.update(cum, e.cnt, total);
            sym = e.sym;
            return symCost(total, e.cnt);
          }
          cum += e.cnt;
        }
      } else {
        coder.update(neff, deff, total);  // escape
        for (const Entry& e : nd.v) addExcluded(e.sym);
      }
    }

    uint32_t d = 0;
    for (int s = 0; s < alphabet_; ++s)
      if (excluded_[s]) ++d;
    uint32_t total = alphabet_ - d;
    uint32_t t = coder.target(total);
    uint32_t rank = 0;
    uint8_t picked = 0;
    for (int s = 0; s < alphabet_; ++s) {
      if (excluded_[s]) continue;
      if (rank == t) { picked = static_cast<uint8_t>(s); break; }
      ++rank;
    }
    coder.update(t, 1, total);
    sym = picked;
    return symCost(total, 1);
  }

  void update(uint8_t sym) {
    int maxk = std::min(P_.kmax, static_cast<int>(pos_));
    for (int k = 0; k <= maxk; ++k) {
      uint64_t key = history_ & mask_[k];
      auto it = maps_[k].find(key);
      if (it != maps_[k].end()) {
        Node& nd = it->second;
        nd.add(sym);
        if (nd.total + nd.v.size() > kMaxTotal) nd.rescale();
      } else if (nodes_ < P_.mem_cap) {
        Node& nd = maps_[k][key];
        nd.add(sym);
        ++nodes_;
      }
    }
    history_ = (history_ << P_.bits) | sym;
    ++pos_;
  }

  // Local rate monitor. Called once per symbol, after update().
  void account(uint64_t cost) {
    if (P_.mode == Mode::None || P_.j == 0) return;
    cur_ += cost;
    if (++win_ < P_.j) return;
    if (prevValid_ && cur_ * 100 > prev_ * (100 + P_.p)) {
      if (P_.mode == Mode::Reset) resetModel();
      else pruneModel();
      prevValid_ = false;
    } else {
      prev_ = cur_;
      prevValid_ = true;
    }
    cur_ = 0;
    win_ = 0;
  }

  void resetModel() {
    for (auto& m : maps_) m.clear();
    nodes_ = 0;
    history_ = 0;
    ++resets_;
  }

  // Drop contexts seen only once, halve the rest. Keeps memory bounded and
  // lets counts forget old statistics.
  void pruneModel() {
    uint64_t n = 0;
    for (auto& m : maps_) {
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

  uint64_t contexts() const {
    uint64_t n = 0;
    for (const auto& m : maps_) n += m.size();
    return n;
  }
  uint64_t position() const { return pos_; }
  uint64_t nodes() const { return nodes_; }
  uint64_t resets() const { return resets_; }
  uint64_t prunes() const { return prunes_; }

  const Params& params() const { return P_; }
  int alphabet() const { return alphabet_; }

 private:
  void addExcluded(uint8_t s) {
    if (!excluded_[s]) {
      excluded_[s] = true;
      excludedList_.push_back(s);
    }
  }
  void clearExcluded() {
    for (uint8_t s : excludedList_) excluded_[s] = false;
    excludedList_.clear();
  }

  Params P_;
  int alphabet_;
  std::vector<std::unordered_map<uint64_t, Node>> maps_;
  std::vector<uint64_t> mask_;
  uint64_t history_ = 0;
  uint64_t pos_ = 0;
  uint64_t nodes_ = 0;

  bool excluded_[256];
  std::vector<uint8_t> excludedList_;

  // monitor state
  uint64_t cur_ = 0, prev_ = 0;
  uint64_t win_ = 0;
  bool prevValid_ = false;
  uint64_t resets_ = 0;
  uint64_t prunes_ = 0;
};

}  // namespace ppm
