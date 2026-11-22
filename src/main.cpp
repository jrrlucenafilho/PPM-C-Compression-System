// PPM-C compressor / decompressor with local-rate reset and pruning.
//
//   ppm c <in> <out> [--bits 1|4|8] [--kmax N] [--mode none|reset|prune]
//                    [--j N] [--p N] [--mem N] [--trace file.csv] [--interval N]
//   ppm d <in> <out>
//   ppm bench --files a,b --bits 1,4,8 --kmax 0:8 --j 10000,50000
//             --p 10,20,30 --modes none,reset,prune [--limit N] [--threads N]
//             [--out results.csv] [--verify]
//   ppm selftest

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include "ppm.hpp"

using namespace ppm;

namespace {

#pragma pack(push, 1)
struct Header {
  char magic[4];
  uint8_t version;
  uint8_t bits;
  uint8_t kmax;
  uint8_t mode;
  uint32_t j;
  uint32_t p;
  uint64_t orig_bytes;
  uint64_t mem_cap;
};
#pragma pack(pop)
static_assert(sizeof(Header) == 32, "header must be 32 bytes");

constexpr uint8_t kVersion = 1;

double now_s() {
  using clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

uint64_t symbolCount(uint64_t bytes, int bits) {
  return (bytes * 8 + static_cast<uint64_t>(bits) - 1) / static_cast<uint64_t>(bits);
}

bool readAll(const std::string& path, std::vector<uint8_t>& data) {
  std::ifstream f(path, std::ios::binary);
  if (!f) return false;
  f.seekg(0, std::ios::end);
  std::streamoff n = f.tellg();
  f.seekg(0, std::ios::beg);
  if (n < 0) return false;
  data.resize(static_cast<size_t>(n));
  if (n > 0) f.read(reinterpret_cast<char*>(data.data()), n);
  return static_cast<bool>(f) || f.eof();
}

bool writeAll(const std::string& path, const std::vector<uint8_t>& data) {
  std::ofstream f(path, std::ios::binary);
  if (!f) return false;
  if (!data.empty()) f.write(reinterpret_cast<const char*>(data.data()),
                             static_cast<std::streamsize>(data.size()));
  return static_cast<bool>(f);
}

// Compresses `in` into `out` (header + arithmetic-coded payload).
// `payload_bytes` reports the coded size, excluding the header.
bool compressBuffer(const std::vector<uint8_t>& in, const Params& P,
                    std::vector<uint8_t>& out, uint64_t* payload_bytes,
                    double* seconds, const std::string& tracePath = std::string(),
                    uint32_t traceInterval = 10000, uint64_t* resets = nullptr,
                    uint64_t* prunes = nullptr) {
  Header h{};
  std::memcpy(h.magic, "PPMC", 4);
  h.version = kVersion;
  h.bits = static_cast<uint8_t>(P.bits);
  h.kmax = static_cast<uint8_t>(P.kmax);
  h.mode = static_cast<uint8_t>(P.mode);
  h.j = P.j;
  h.p = P.p;
  h.orig_bytes = in.size();
  h.mem_cap = P.mem_cap;

  std::vector<uint8_t> payload;
  payload.reserve(in.size() / 2 + 64);
  BitWriter bw(payload);
  ArithEncoder coder(bw);
  Model model(P);

  const uint64_t nsym = symbolCount(in.size(), P.bits);
  BitReader br(in);
  uint64_t totalCost = 0;

  std::ofstream trace;
  if (!tracePath.empty()) {
    trace.open(tracePath);
    trace << "n,bits,avg\n";
  }

  double t0 = now_s();
  for (uint64_t i = 0; i < nsym; ++i) {
    int s = 0;
    for (int b = 0; b < P.bits; ++b) s = (s << 1) | br.get();
    uint64_t c = model.encodeSymbol(static_cast<uint8_t>(s), coder);
    model.update(static_cast<uint8_t>(s));
    model.account(c);
    totalCost += c;
    if (trace.is_open() && traceInterval != 0 && (i + 1) % traceInterval == 0) {
      double bits = static_cast<double>(totalCost) / 65536.0;
      trace << (i + 1) << ',' << bits << ',' << bits / static_cast<double>(i + 1) << '\n';
    }
  }
  coder.finish();
  bw.flush();
  double t1 = now_s();
  if (trace.is_open()) trace.close();

  if (resets) *resets = model.resets();
  if (prunes) *prunes = model.prunes();
  if (payload_bytes) *payload_bytes = payload.size();
  if (seconds) *seconds = t1 - t0;

  out.clear();
  out.reserve(sizeof(Header) + payload.size());
  out.insert(out.end(), reinterpret_cast<const uint8_t*>(&h),
             reinterpret_cast<const uint8_t*>(&h) + sizeof(Header));
  out.insert(out.end(), payload.begin(), payload.end());
  return true;
}

bool decompressBuffer(const std::vector<uint8_t>& in, std::vector<uint8_t>& out,
                      double* seconds, std::string* error) {
  if (in.size() < sizeof(Header)) {
    if (error) *error = "file too short";
    return false;
  }
  Header h{};
  std::memcpy(&h, in.data(), sizeof(Header));
  if (std::memcmp(h.magic, "PPMC", 4) != 0 || h.version != kVersion) {
    if (error) *error = "bad magic or version";
    return false;
  }
  Params P;
  P.bits = h.bits;
  P.kmax = h.kmax;
  P.mode = static_cast<Mode>(h.mode);
  P.j = h.j;
  P.p = h.p;
  P.mem_cap = h.mem_cap ? h.mem_cap : 8000000;
  if (P.bits != 1 && P.bits != 4 && P.bits != 8) {
    if (error) *error = "invalid alphabet";
    return false;
  }
  if (P.kmax < 0 || P.kmax * P.bits > 64) {
    if (error) *error = "invalid kmax";
    return false;
  }

  std::vector<uint8_t> payload(in.begin() + sizeof(Header), in.end());
  BitReader br(payload);
  ArithDecoder coder(br);
  Model model(P);

  const uint64_t nsym = symbolCount(h.orig_bytes, P.bits);
  out.clear();
  out.reserve(static_cast<size_t>(h.orig_bytes));

  uint8_t cur = 0;
  int nbits = 0;
  double t0 = now_s();
  for (uint64_t i = 0; i < nsym; ++i) {
    uint8_t s = 0;
    uint64_t c = model.decodeSymbol(s, coder);
    model.update(s);
    model.account(c);
    for (int b = P.bits - 1; b >= 0; --b) {
      cur = static_cast<uint8_t>((cur << 1) | ((s >> b) & 1));
      if (++nbits == 8) {
        if (out.size() < h.orig_bytes) out.push_back(cur);
        cur = 0;
        nbits = 0;
      }
    }
  }
  if (nbits != 0 && out.size() < h.orig_bytes) {
    cur = static_cast<uint8_t>(cur << (8 - nbits));
    out.push_back(cur);
  }
  double t1 = now_s();
  if (seconds) *seconds = t1 - t0;
  out.resize(static_cast<size_t>(h.orig_bytes));
  return true;
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  for (char c : s) {
    if (c == sep) { out.push_back(cur); cur.clear(); }
    else cur.push_back(c);
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

std::vector<int> parseIntList(const std::string& s) {
  std::vector<int> out;
  for (const std::string& part : split(s, ',')) {
    size_t colon = part.find(':');
    if (colon != std::string::npos) {
      int a = std::atoi(part.substr(0, colon).c_str());
      int b = std::atoi(part.substr(colon + 1).c_str());
      if (b < a) std::swap(a, b);
      for (int x = a; x <= b; ++x) out.push_back(x);
    } else if (!part.empty()) {
      out.push_back(std::atoi(part.c_str()));
    }
  }
  return out;
}

void usage() {
  std::cerr <<
      "usage:\n"
      "  ppm c <in> <out> [--bits 1|4|8] [--kmax N] [--mode none|reset|prune]\n"
      "                   [--j N] [--p N] [--mem N] [--trace file.csv] [--interval N]\n"
      "  ppm d <in> <out>\n"
      "  ppm bench --files a,b --bits 1,4,8 --kmax 0:8 --j 10000,50000\n"
      "            --p 10,20,30 --modes none,reset,prune [--limit N] [--threads N]\n"
      "            [--out results.csv] [--verify] [--resume]\n"
      "            [--trace-prefix P] [--trace-interval N]\n"
      "  ppm selftest\n";
}

int cmdCompress(int argc, char** argv) {
  if (argc < 4) { usage(); return 1; }
  std::string inPath = argv[2], outPath = argv[3];
  Params P;
  std::string tracePath;
  uint32_t interval = 10000;
  for (int i = 4; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char* what) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "missing value for " << what << "\n";
        std::exit(1);
      }
      return argv[++i];
    };
    if (a == "--bits") P.bits = std::stoi(next("--bits"));
    else if (a == "--kmax") P.kmax = std::stoi(next("--kmax"));
    else if (a == "--j") P.j = static_cast<uint32_t>(std::stoul(next("--j")));
    else if (a == "--p") P.p = static_cast<uint32_t>(std::stoul(next("--p")));
    else if (a == "--mem") P.mem_cap = std::stoull(next("--mem"));
    else if (a == "--trace") tracePath = next("--trace");
    else if (a == "--interval") interval = static_cast<uint32_t>(std::stoul(next("--interval")));
    else if (a == "--mode") {
      if (!parseMode(next("--mode"), P.mode)) { std::cerr << "invalid mode\n"; return 1; }
    } else {
      std::cerr << "unknown option: " << a << "\n";
      return 1;
    }
  }
  if (P.bits != 1 && P.bits != 4 && P.bits != 8) {
    std::cerr << "--bits must be 1, 4 or 8\n";
    return 1;
  }
  if (P.kmax < 0 || P.kmax * P.bits > 64) {
    std::cerr << "--kmax too large for this alphabet (max " << 64 / P.bits << ")\n";
    return 1;
  }

  std::vector<uint8_t> data, packed;
  if (!readAll(inPath, data)) { std::cerr << "cannot read " << inPath << "\n"; return 1; }
  uint64_t payload = 0, resets = 0, prunes = 0;
  double secs = 0;
  compressBuffer(data, P, packed, &payload, &secs, tracePath, interval, &resets, &prunes);
  if (!writeAll(outPath, packed)) { std::cerr << "cannot write " << outPath << "\n"; return 1; }

  uint64_t nsym = symbolCount(data.size(), P.bits);
  double bps = nsym ? static_cast<double>(payload) * 8.0 / static_cast<double>(nsym) : 0.0;
  double bpb = data.empty() ? 0.0 : static_cast<double>(payload) * 8.0 / static_cast<double>(data.size());
  std::cerr << "bits=" << P.bits << " kmax=" << P.kmax << " mode=" << modeName(P.mode)
            << " j=" << P.j << " p=" << P.p << "\n"
            << "in=" << data.size() << "B out=" << packed.size() << "B payload=" << payload
            << "B symbols=" << nsym << "\n"
            << "bits/symbol=" << bps << " bits/byte=" << bpb
            << " resets=" << resets << " prunes=" << prunes
            << " time=" << secs << "s (" << (secs > 0 ? nsym / secs / 1e6 : 0) << " Msym/s)\n";
  return 0;
}

int cmdDecompress(int argc, char** argv) {
  if (argc < 4) { usage(); return 1; }
  std::vector<uint8_t> packed, data;
  if (!readAll(argv[2], packed)) { std::cerr << "cannot read " << argv[2] << "\n"; return 1; }
  double secs = 0;
  std::string err;
  if (!decompressBuffer(packed, data, &secs, &err)) {
    std::cerr << "error: " << err << "\n";
    return 1;
  }
  if (!writeAll(argv[3], data)) { std::cerr << "cannot write " << argv[3] << "\n"; return 1; }
  std::cerr << "out=" << data.size() << "B time=" << secs << "s\n";
  return 0;
}

struct Job {
  std::string file;
  Params P;
  size_t fileIndex = 0;
};

struct JobResult {
  std::string file;
  Params P;
  uint64_t orig = 0;
  uint64_t payload = 0;
  uint64_t symbols = 0;
  double enc = 0;
  double dec = 0;
  uint64_t resets = 0;
  uint64_t prunes = 0;
  bool verified = false;
  bool decodeOk = false;
};

int cmdBench(int argc, char** argv) {
  std::string filesArg, bitsArg = "8", kmaxArg = "6", jArg = "50000",
              pArg = "20", modesArg = "none", outPath = "results.csv";
  uint64_t limit = 0;
  uint64_t memCap = 8000000;
  int threads = 4;
  bool verify = false;
  bool concat = false;
  bool resume = false;
  std::string tracePrefix;
  uint32_t traceInterval = 10000;
  for (int i = 2; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char* what) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "missing value for " << what << "\n";
        std::exit(1);
      }
      return argv[++i];
    };
    if (a == "--files") filesArg = next("--files");
    else if (a == "--bits") bitsArg = next("--bits");
    else if (a == "--kmax") kmaxArg = next("--kmax");
    else if (a == "--j") jArg = next("--j");
    else if (a == "--p") pArg = next("--p");
    else if (a == "--modes") modesArg = next("--modes");
    else if (a == "--limit") limit = std::stoull(next("--limit"));
    else if (a == "--mem") memCap = std::stoull(next("--mem"));
    else if (a == "--threads") threads = std::stoi(next("--threads"));
    else if (a == "--out") outPath = next("--out");
    else if (a == "--verify") verify = true;
    else if (a == "--resume") resume = true;
    else if (a == "--trace-prefix") tracePrefix = next("--trace-prefix");
    else if (a == "--trace-interval") traceInterval = static_cast<uint32_t>(std::stoul(next("--trace-interval")));
    else if (a == "--concat") concat = true;
    else { std::cerr << "unknown option: " << a << "\n"; return 1; }
  }
  if (filesArg.empty()) { std::cerr << "--files is required\n"; return 1; }
  if (threads < 1) threads = 1;

  std::vector<std::string> files = split(filesArg, ',');
  std::vector<int> bitsList = parseIntList(bitsArg);
  std::vector<int> kmaxList = parseIntList(kmaxArg);
  std::vector<int> jList = parseIntList(jArg);
  std::vector<int> pList = parseIntList(pArg);
  std::vector<Mode> modes;
  for (const std::string& m : split(modesArg, ',')) {
    Mode mode;
    if (!parseMode(m, mode)) { std::cerr << "invalid mode: " << m << "\n"; return 1; }
    modes.push_back(mode);
  }

  std::map<std::string, std::vector<uint8_t>> corpus;
  for (const std::string& f : files) {
    std::vector<uint8_t> data;
    if (!readAll(f, data)) { std::cerr << "cannot read " << f << "\n"; return 1; }
    if (limit && data.size() > limit) data.resize(limit);
    corpus[f] = std::move(data);
    std::cerr << "loaded " << f << " (" << corpus[f].size() << " bytes)\n";
  }
  if (concat) {
    std::vector<uint8_t> joined;
    size_t total = 0;
    for (const std::string& f : files) total += corpus[f].size();
    joined.reserve(total);
    for (const std::string& f : files)
      joined.insert(joined.end(), corpus[f].begin(), corpus[f].end());
    corpus.clear();
    corpus["CONCAT"] = std::move(joined);
    files = {"CONCAT"};
    std::cerr << "concatenated stream: " << corpus["CONCAT"].size() << " bytes\n";
  }

  std::vector<Job> jobs;
  for (const std::string& f : files)
    for (int bits : bitsList)
      for (int kmax : kmaxList) {
        if (kmax < 0 || kmax * bits > 64) continue;
        for (Mode mode : modes)
          for (int j : jList)
            for (int p : pList) {
              if (mode == Mode::None && (j != jList[0] || p != pList[0])) continue;
              Job job;
              job.file = f;
              job.P.bits = bits;
              job.P.kmax = kmax;
              job.P.mode = mode;
              job.P.j = static_cast<uint32_t>(j);
              job.P.p = static_cast<uint32_t>(p);
              job.P.mem_cap = memCap;
              jobs.push_back(job);
            }
      }

  if (resume) {
    // Skip jobs already written to outPath. Used to continue an interrupted
    // sweep without recomputing finished combinations.
    std::unordered_set<std::string> done;
    std::ifstream fin(outPath, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(fin)),
                        std::istreambuf_iterator<char>());
    if (!content.empty() && content.back() != '\n') {
      size_t lastBreak = content.rfind('\n');
      content.erase(lastBreak == std::string::npos ? 0 : lastBreak + 1);
    }
    std::istringstream lines(content);
    std::string line;
    bool header = true;
    while (std::getline(lines, line)) {
      if (header) { header = false; if (line.rfind("file,", 0) == 0) continue; }
      if (line.empty()) continue;
      std::vector<std::string> f;
      std::string cur;
      for (char c : line) {
        if (c == ',') { f.push_back(cur); cur.clear(); }
        else cur.push_back(c);
      }
      f.push_back(cur);
      if (f.size() < 6) continue;
      done.insert(f[0] + '|' + f[1] + '|' + f[2] + '|' + f[3] + '|' + f[4] + '|' + f[5]);
    }
    size_t before = jobs.size();
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [&](const Job& job) {
      std::string key = job.file + '|' + std::to_string(job.P.bits) + '|' +
                        std::to_string(job.P.kmax) + '|' + modeName(job.P.mode) + '|' +
                        std::to_string(job.P.j) + '|' + std::to_string(job.P.p);
      return done.count(key) != 0;
    }), jobs.end());
    std::cerr << "resume: " << (before - jobs.size()) << " already done, "
              << jobs.size() << " remaining\n";
  }

  std::cerr << "jobs: " << jobs.size() << ", threads: " << threads << "\n";

  std::ofstream csv(outPath, std::ios::app);
  if (!csv) { std::cerr << "cannot open " << outPath << "\n"; return 1; }
  csv.seekp(0, std::ios::end);
  if (csv.tellp() == 0) {
    csv << "file,bits,kmax,mode,j,p,mem_cap,orig_bytes,symbols,payload_bytes,bits_per_symbol,bits_per_byte,enc_s,dec_s,resets,prunes,verify\n";
  }
  std::mutex csvMutex;
  std::atomic<size_t> next{0};
  std::atomic<uint64_t> done{0};
  double wall0 = now_s();

  auto worker = [&]() {
    for (;;) {
      size_t idx = next.fetch_add(1);
      if (idx >= jobs.size()) break;
      const Job& job = jobs[idx];
      const std::vector<uint8_t>& data = corpus[job.file];

      JobResult r;
      r.file = job.file;
      r.P = job.P;
      r.orig = data.size();
      r.symbols = symbolCount(data.size(), job.P.bits);

      std::vector<uint8_t> packed;
      std::string tracePath;
      if (!tracePrefix.empty()) {
        tracePath = tracePrefix + "_A" + std::to_string(job.P.bits) + "_" +
                    modeName(job.P.mode) + ".csv";
      }
      compressBuffer(data, job.P, packed, &r.payload, &r.enc, tracePath,
                     traceInterval, &r.resets, &r.prunes);
      r.verified = true;
      if (verify) {
        std::vector<uint8_t> back;
        std::string err;
        r.decodeOk = decompressBuffer(packed, back, &r.dec, &err) && back == data;
        r.verified = r.decodeOk;
      }

      double bps = r.symbols ? static_cast<double>(r.payload) * 8.0 / static_cast<double>(r.symbols) : 0.0;
      double bpb = r.orig ? static_cast<double>(r.payload) * 8.0 / static_cast<double>(r.orig) : 0.0;
      {
        std::lock_guard<std::mutex> lock(csvMutex);
        csv << r.file << ',' << r.P.bits << ',' << r.P.kmax << ','
            << modeName(r.P.mode) << ',' << r.P.j << ',' << r.P.p << ','
            << r.P.mem_cap << ',' << r.orig << ',' << r.symbols << ','
            << r.payload << ',' << bps << ',' << bpb << ',' << r.enc << ','
            << r.dec << ',' << r.resets << ',' << r.prunes << ','
            << (verify ? (r.decodeOk ? "ok" : "FAIL") : "-") << '\n';
        csv.flush();
      }
      uint64_t n = done.fetch_add(1) + 1;
      if (n % 10 == 0 || n == jobs.size()) {
        double elapsed = now_s() - wall0;
        std::fprintf(stderr, "\r[%llu/%zu] %.1fs elapsed", (unsigned long long)n,
                     jobs.size(), elapsed);
        std::fflush(stderr);
      }
    }
  };

  std::vector<std::thread> pool;
  for (int i = 0; i < threads; ++i) pool.emplace_back(worker);
  for (auto& t : pool) t.join();
  std::fprintf(stderr, "\ndone in %.1fs -> %s\n", now_s() - wall0, outPath.c_str());
  return 0;
}

int cmdSelftest() {
  uint64_t seed = 123456789;
  auto rnd = [&]() { seed = seed * 6364136223846793005ull + 1442695040888963407ull; return seed >> 33; };

  std::vector<size_t> sizes = {0, 1, 2, 7, 8, 9, 100, 4096, 200000};
  int failures = 0;
  for (size_t size : sizes) {
    std::vector<uint8_t> data(size);
    for (auto& b : data) b = static_cast<uint8_t>(rnd());
    for (int bits : {1, 4, 8}) {
      int maxk = 64 / bits;
      for (int kmax : {0, 1, 2, 4, maxk}) {
        if (kmax > maxk) continue;
        for (Mode mode : {Mode::None, Mode::Reset, Mode::Prune}) {
          Params P;
          P.bits = bits;
          P.kmax = kmax;
          P.mode = mode;
          P.j = 777;
          P.p = 15;
          P.mem_cap = 4000000;
          std::vector<uint8_t> packed, back;
          double s1 = 0, s2 = 0;
          compressBuffer(data, P, packed, nullptr, &s1);
          std::string err;
          bool ok = decompressBuffer(packed, back, &s2, &err) && back == data;
          if (!ok) {
            ++failures;
            std::fprintf(stderr, "FAIL size=%zu bits=%d kmax=%d mode=%s: %s\n", size,
                         bits, kmax, modeName(mode), err.c_str());
          }
        }
      }
    }
  }
  std::fprintf(stderr, failures == 0 ? "selftest: OK\n" : "selftest: %d FAILURES\n", failures);
  return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { usage(); return 1; }
  std::string cmd = argv[1];
  if (cmd == "c" || cmd == "compress") return cmdCompress(argc, argv);
  if (cmd == "d" || cmd == "decompress") return cmdDecompress(argc, argv);
  if (cmd == "bench") return cmdBench(argc, argv);
  if (cmd == "selftest") return cmdSelftest();
  usage();
  return 1;
}
