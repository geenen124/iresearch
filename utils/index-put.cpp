////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2016 by EMC Corporation, All Rights Reserved
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is EMC Corporation
///
/// @author Andrey Abramov
/// @author Vasiliy Nabatchikov
////////////////////////////////////////////////////////////////////////////////

#if defined(_MSC_VER)
#pragma warning(disable: 4101)
#pragma warning(disable: 4267)
#endif

#include <cmdline.h>

#include <frozen/unordered_set.h>

#if defined(_MSC_VER)
#pragma warning(default: 4267)
#pragma warning(default: 4101)
#endif

#include <fstream>
#include <memory>

#if defined(_MSC_VER)
#pragma warning(disable: 4229)
#endif

#include <unicode/uclean.h> // for u_cleanup

#if defined(_MSC_VER)
#pragma warning(default: 4229)
#endif

#include "common.hpp"
#include "analysis/analyzers.hpp"
#include "analysis/token_attributes.hpp"
#include "analysis/token_streams.hpp"
#include "index/index_writer.hpp"
#include "index/norm.hpp"
#include "store/store_utils.hpp"
#include "utils/directory_utils.hpp"
#include "utils/index_utils.hpp"
#include "utils/string_utils.hpp"
#include "utils/text_format.hpp"

#include "index-put.hpp"

namespace {

const std::string HELP = "help";
const std::string BATCH_SIZE = "batch-size";
const std::string CONSOLIDATE_ALL = "consolidate-all";
const std::string INDEX_DIR = "index-dir";
const std::string OUTPUT = "out";
const std::string INPUT = "in";
const std::string MAX = "max-lines";
const std::string THR = "threads";
const std::string CONS_THR = "consolidation-threads";
const std::string CPR = "commit-period";
const std::string DIR_TYPE = "dir-type";
const std::string FORMAT = "format";
const std::string ANALYZER_TYPE = "analyzer-type";
const std::string ANALYZER_OPTIONS = "analyzer-options";
const std::string SEGMENT_MEM_MAX = "segment-memory-max";
const std::string CONSOLIDATION_INTERVAL = "consolidation-interval";

const std::string DEFAULT_ANALYZER_TYPE = "segmentation";
const std::string DEFAULT_ANALYZER_OPTIONS = R"({})";

constexpr size_t DEFAULT_SEGMENT_MEM_MAX = 1 << 28; // 256M
constexpr size_t DEFAULT_CONSOLIDATION_INTERVAL_MSEC = 500;

constexpr irs::IndexFeatures TEXT_INDEX_FEATURES =
  irs::IndexFeatures::FREQ | irs::IndexFeatures::POS;


// legacy formats supportd only variable length norms, i.e. "norm" feature
constexpr frozen::unordered_set<irs::string_ref, 6> LEGACY_FORMATS{
  "1_0", "1_1", "1_2", "1_2simd", "1_3simd", "1_3" };

// norm features supported by old format
constexpr std::array<irs::type_info::type_id, 1> LEGACY_TEXT_FEATURES{ irs::type<irs::norm>::id()  };
// fixed length norm
constexpr std::array<irs::type_info::type_id, 1> TEXT_FEATURES{ irs::type<irs::norm2>::id()  };
constexpr std::array<irs::type_info::type_id, 1> NUMERIC_FEATURES{ irs::type<irs::granularity_prefix>::id() };

}

struct Doc {
  virtual ~Doc() = default;

  static std::atomic<uint64_t> next_id;

  /**
   * C++ version 0.4 char* style "itoa":
   * Written by Lukás Chmela
   * Released under GPLv3.
   */
  char* itoa(int value, char* result, int base) {
    // check that the base if valid
    if (base < 2 || base > 36) {
      *result = '\0';
      return result;
    }

    char* ptr = result, *ptr1 = result, tmp_char;
    int tmp_value;

    do {
      tmp_value = value;
      value /= base;
      *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz" [35 + (tmp_value - value * base)];
    } while (value);

    // Apply negative sign
    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    while (ptr1 < ptr) {
      tmp_char = *ptr;
      *ptr-- = *ptr1;
      *ptr1++ = tmp_char;
    }
    return result;
  }

  struct Field {
    irs::string_ref _name;
    const irs::features_t _features;
    const irs::IndexFeatures _index_features;

    Field(const irs::string_ref& n,
          irs::IndexFeatures index_features,
          const irs::features_t& flags)
      : _name(n),
        _features(flags),
        _index_features(index_features) {
    }

    irs::string_ref name() const noexcept {
      return _name;
    }

    const irs::features_t& features() const noexcept {
      return _features;
    }

    irs::IndexFeatures index_features() const noexcept {
      return _index_features;
    }

    virtual irs::token_stream& get_tokens() const = 0;

    virtual bool write(irs::data_output& out) const = 0;

    virtual ~Field() = default;
  };

  struct StringField : public Field {
    std::string f;
    mutable irs::string_token_stream _stream;

    StringField(
        const irs::string_ref& n,
        irs::IndexFeatures index_features,
        const irs::features_t& flags)
      : Field(n, index_features, flags) {
    }

    StringField(
        const irs::string_ref& n,
        irs::IndexFeatures index_features,
        const irs::features_t& flags,
        const std::string& a)
      : Field(n, index_features, flags), f(a) {
    }

    irs::token_stream& get_tokens() const override {
      _stream.reset(f);
      return _stream;
    }

    bool write(irs::data_output& out) const override {
      irs::write_string(out, f.c_str(), f.length());
      return true;
    }
  };

  struct TextField : public Field {
    std::string f;
    mutable irs::analysis::analyzer::ptr stream;

    TextField(
        const irs::string_ref& n,
        irs::IndexFeatures index_features,
        const irs::features_t& flags,
        irs::analysis::analyzer::ptr stream)
      : Field(n, index_features, flags),
        stream(std::move(stream)) {
    }

    irs::token_stream& get_tokens() const override {
      stream->reset(f);
      return *stream;
    }

    bool write(irs::data_output& out) const override {
      irs::write_string(out, f.c_str(), f.length());
      return true;
    }
  };

  struct NumericField : public Field {
    mutable irs::numeric_token_stream stream;
    int64_t value;

    NumericField(
        const irs::string_ref& n,
        irs::IndexFeatures index_features,
        const irs::features_t& flags)
      : Field(n, index_features, flags) {
    }

    NumericField(
        const irs::string_ref& n,
        irs::IndexFeatures index_features,
        const irs::features_t& flags,
        uint64_t v)
      : Field(n, index_features, flags), value(v) {
    }

    irs::token_stream& get_tokens() const override {
      stream.reset(value);
      return stream;
    }

    bool write(irs::data_output& out) const override {
      irs::write_zvlong(out, value);
      return true;
    }
  };

  std::vector<std::shared_ptr<Field>> elements;
  std::vector<std::shared_ptr<Field>> store;

  /**
   * Parse line to fields
   * @todo way too many string copies here
   * @param line
   * @return 
   */
  virtual void fill(std::string* line) = 0;
};

std::atomic<uint64_t> Doc::next_id(0);
using analyzer_factory_f = std::function<irs::analysis::analyzer::ptr()>;

struct WikiDoc : Doc {
  explicit WikiDoc(const analyzer_factory_f& analyzer_factory, const irs::features_t& text_features) {
    // id
    id = std::make_shared<StringField>("id", irs::IndexFeatures::NONE, irs::features_t{});
    elements.emplace_back(id);
    store.emplace_back(id);

    // title: string
    title = std::make_shared<StringField>("title", irs::IndexFeatures::NONE, irs::features_t{});
    elements.push_back(title);

    // date: string
    date = std::make_shared<StringField>("date", irs::IndexFeatures::NONE, irs::features_t{});
    elements.push_back(date);
    store.push_back(date);

    // date: uint64_t
    ndate = std::make_shared<NumericField>(
      "timesecnum", irs::IndexFeatures::NONE,
      irs::features_t{ NUMERIC_FEATURES.data(), NUMERIC_FEATURES.size() });
    elements.push_back(ndate);

    // body: text
    body = std::make_shared<TextField>(
      "body", TEXT_INDEX_FEATURES,
      text_features,
      analyzer_factory());
    elements.push_back(body);
  }

  virtual void fill(std::string* line) {
    std::stringstream lineStream(*line);

    // id: uint64_t to string, base 36
    uint64_t id = next_id++; // atomic fetch and get
    char str[10];
    itoa(id, str, 36);
    char str2[10];
    snprintf(str2, sizeof (str2), "%6s", str);
    auto& s = this->id->f;
    s = str2;
    std::replace(s.begin(), s.end(), ' ', '0');

    // title: string
    std::getline(lineStream, title->f, '\t');

    // date: string
    std::getline(lineStream, date->f, '\t');

    // +date: uint64_t
    uint64_t t = 0; //boost::posix_time::microsec_clock::local_time().total_milliseconds();
    ndate->value = t;

    // body: text
    std::getline(lineStream, body->f, '\t');
  }

  std::shared_ptr<StringField> id;
  std::shared_ptr<StringField> title;
  std::shared_ptr<StringField> date;
  std::shared_ptr<NumericField> ndate;
  std::shared_ptr<TextField> body;
};

int put(
    const std::string& path,
    const std::string& dir_type,
    const std::string& format,
    const std::string& analyzer_type,
    const std::string& analyzer_options,
    std::istream& stream,
    size_t lines_max,
    size_t indexer_threads,
    size_t consolidation_threads,
    size_t commit_interval_ms,
    size_t consolidation_interval_ms,
    size_t batch_size,
    size_t segment_mem_max,
    bool consolidate_all) {
  auto dir = create_directory(dir_type, path);

  if (!dir) {
    std::cerr << "Unable to create directory of type '" << dir_type << "'" << std::endl;
    return 1;
  }

  auto codec = irs::formats::get(format);

  if (!codec) {
    std::cerr << "Unable to find format of type '" << format << "'" << std::endl;
    return 1;
  }

  irs::features_t text_features{ TEXT_FEATURES.data(), TEXT_FEATURES.size() };
  if (LEGACY_FORMATS.count(codec->type().name()) > 0) {
    // legacy formats don't support pluggable features
    text_features = { LEGACY_TEXT_FEATURES.data(), LEGACY_TEXT_FEATURES.size() };
  }
  analyzer_factory_f analyzer_factory = [&analyzer_type, &analyzer_options](){
    irs::analysis::analyzer::ptr analyzer;

    const auto res = irs::analysis::analyzers::get(
      analyzer, analyzer_type,
      irs::type<irs::text_format::json>::get(),
      analyzer_options);

    if (!res) {
      std::cerr << "Unable to load an analyzer of type '" << analyzer_type
                << "', error '" << res.c_str() << "'\n";
    }

    return analyzer;
  };

  if (!analyzer_factory()) {
    return 1;
  }

  indexer_threads = (std::min)(indexer_threads, (std::numeric_limits<size_t>::max)() - 1 - consolidation_threads); // -1 for commiter thread
  indexer_threads = (std::max)(size_t(1), indexer_threads);

  irs::index_writer::init_options opts;
  opts.features[irs::type<irs::granularity_prefix>::id()] = nullptr;
  opts.features[irs::type<irs::norm>::id()] = &irs::norm::compute;
  opts.segment_pool_size = indexer_threads;
  opts.segment_memory_max = segment_mem_max;
  opts.feature_column_info = [](irs::type_info::type_id) {
    return irs::column_info{ irs::type<irs::compression::none>::get(), {}, false };
  };

  auto writer = irs::index_writer::make(*dir, codec, irs::OM_CREATE, opts);

  irs::async_utils::thread_pool thread_pool(indexer_threads + consolidation_threads + 1); // +1 for commiter thread

  SCOPED_TIMER("Total Time");
  std::cout << "Configuration:\n"
            << INDEX_DIR << "=" << path << '\n'
            << DIR_TYPE << "=" << dir_type << '\n'
            << FORMAT << "=" << format << '\n'
            << MAX << "=" << lines_max << '\n'
            << THR << "=" << indexer_threads << '\n'
            << CONS_THR << "=" << consolidation_threads << '\n'
            << CPR << "=" << commit_interval_ms << '\n'
            << CONSOLIDATION_INTERVAL << "=" << consolidation_interval_ms << '\n'
            << BATCH_SIZE << "=" << batch_size << '\n'
            << CONSOLIDATE_ALL << "=" << consolidate_all << '\n'
            << ANALYZER_TYPE << "=" << analyzer_type << '\n'
            << ANALYZER_OPTIONS << "=" << analyzer_options << '\n'
            << SEGMENT_MEM_MAX << "=" << segment_mem_max << '\n';

  struct {
    std::condition_variable cond_;
    std::atomic<bool> done_;
    bool eof_;
    std::mutex mutex_;
    std::vector<std::string> buf_;

    bool swap(std::vector<std::string>& buf) {
      auto lock = irs::make_unique_lock(mutex_);

      for (;;) {
        buf_.swap(buf);
        buf_.resize(0);
        cond_.notify_all();

        if (!buf.empty()) {
          return true;
        }

        if (eof_) {
          done_.store(true);
          return false;
        }

        if (!eof_) {
          SCOPED_TIMER("Stream read wait time");
          cond_.wait(lock);
        }
      }
    }
  } batch_provider;

  batch_provider.done_.store(false);
  batch_provider.eof_ = false;

  // stream reader thread
  thread_pool.run([&batch_provider, lines_max, batch_size, &stream]()->void {
    SCOPED_TIMER("Stream read total time");
    auto lock = irs::make_unique_lock(batch_provider.mutex_);

    for (auto i = lines_max ? lines_max : (std::numeric_limits<size_t>::max)(); i; --i) {
      batch_provider.buf_.resize(batch_provider.buf_.size() + 1);
      batch_provider.cond_.notify_all();

      auto& line = batch_provider.buf_.back();

      if (std::getline(stream, line).eof()) {
        batch_provider.buf_.pop_back();
        break;
      }

      if (batch_size && batch_provider.buf_.size() >= batch_size) {
        SCOPED_TIMER("Stream read idle time");
        batch_provider.cond_.wait(lock);
      }
    }

    batch_provider.eof_ = true;
    std::cout << "EOF" << std::flush;
  });

  std::mutex consolidation_mutex;
  std::condition_variable consolidation_cv;

  // commiter thread
  if (commit_interval_ms) {
    thread_pool.run([&consolidation_cv, &consolidation_mutex, &batch_provider, commit_interval_ms, &writer, consolidation_threads]()->void {
      while (!batch_provider.done_.load()) {
        {
          SCOPED_TIMER("Commit time");
          std::cout << "COMMIT" << std::endl; // break indexer thread output by commit
          writer->commit();
        }

        // notify consolidation threads
        if (consolidation_threads) {
          auto lock = irs::make_unique_lock(consolidation_mutex);
          consolidation_cv.notify_all();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(commit_interval_ms));
      }
    });
  }

  // consolidation threads
  const irs::index_utils::consolidate_tier consolidation_options;
  auto policy = irs::index_utils::consolidation_policy(consolidation_options);

  for (size_t i = consolidation_threads; i; --i) {
    thread_pool.run([&dir, &policy, &batch_provider, &consolidation_mutex, &consolidation_cv, consolidation_interval_ms, &writer]()->void {
      while (!batch_provider.done_.load()) {
        {
          auto lock = irs::make_unique_lock(consolidation_mutex);
          if (std::cv_status::timeout ==
              consolidation_cv.wait_for(lock, std::chrono::milliseconds(consolidation_interval_ms))) {
            continue;
          }
        }

        {
          SCOPED_TIMER("Consolidation time");
          writer->consolidate(policy);
        }

        {
          SCOPED_TIMER("Cleanup time");
          irs::directory_utils::remove_all_unreferenced(*dir);
        }
      }
    });
  }

  // indexer threads
  for (size_t i = indexer_threads; i; --i) {
    thread_pool.run([text_features, &analyzer_factory, &batch_provider, &writer](){
      std::vector<std::string> buf;
      WikiDoc doc(analyzer_factory, text_features);

      while (batch_provider.swap(buf)) {
        SCOPED_TIMER(std::string("Index batch ") + std::to_string(buf.size()));
        auto ctx = writer->documents();
        size_t i = 0;

        do {
          auto builder = ctx.insert();

          doc.fill(&(buf[i]));

          for (auto& field: doc.elements) {
            builder.insert<irs::Action::INDEX>(*field);
          }

          for (auto& field : doc.store) {
            builder.insert<irs::Action::STORE>(*field);
          }

        } while (++i < buf.size());

        std::cout << "." << std::flush; // newline in commit thread
      }
    });
  }

  thread_pool.stop();

  {
    SCOPED_TIMER("Commit time");
    std::cout << "COMMIT" << std::endl; // break indexer thread output by commit
    writer->commit();
  }

  if (consolidate_all) {
    // merge all segments into a single segment
    SCOPED_TIMER("Consolidating all time");
    std::cout << "Consolidating all segments:" << std::endl;
    writer->consolidate(irs::index_utils::consolidation_policy(irs::index_utils::consolidate_count()));
    writer->commit();
  }

  if (consolidate_all || consolidation_threads) {
    irs::directory_utils::remove_all_unreferenced(*dir);
  }

  u_cleanup();

  return 0;
}

int put(const cmdline::parser& args) {
  if (!args.exist(INDEX_DIR)) {
    return 1;
  }

  const auto& path = args.get<std::string>(INDEX_DIR);

  if (path.empty()) {
    return 1;
  }

  const auto batch_size = args.exist(BATCH_SIZE) ? args.get<size_t>(BATCH_SIZE) : size_t(0);
  const auto consolidate = args.exist(CONSOLIDATE_ALL) ? args.get<bool>(CONSOLIDATE_ALL) : false;
  const auto commit_interval_ms = args.exist(CPR) ? args.get<size_t>(CPR) : size_t(0);
  const auto indexer_threads = args.exist(THR) ? args.get<size_t>(THR) : size_t(0);
  const auto consolidation_threads = args.exist(CONS_THR) ? args.get<size_t>(CONS_THR) : size_t(0);
  const auto lines_max = args.exist(MAX) ? args.get<size_t>(MAX) : size_t(0);
  const auto dir_type = args.exist(DIR_TYPE) ? args.get<std::string>(DIR_TYPE) : std::string("mmap");
  const auto format = args.exist(FORMAT) ? args.get<std::string>(FORMAT) : std::string("1_0");
  const auto analyzer_type = args.exist(ANALYZER_TYPE)
    ? args.get<std::string>(ANALYZER_TYPE)
    : DEFAULT_ANALYZER_TYPE;
  const auto analyzer_options = args.exist(ANALYZER_TYPE)
    ? args.get<std::string>(ANALYZER_OPTIONS)
    : DEFAULT_ANALYZER_OPTIONS;
  const auto segment_mem_max = args.exist(SEGMENT_MEM_MAX)
    ? args.get<size_t>(SEGMENT_MEM_MAX)
    : DEFAULT_SEGMENT_MEM_MAX;
  const auto consolidation_internval_ms = args.exist(CONSOLIDATION_INTERVAL)
    ? args.get<size_t>(CONSOLIDATION_INTERVAL)
    : DEFAULT_CONSOLIDATION_INTERVAL_MSEC;

  std::fstream fin;
  std::istream* in;
  if (args.exist(INPUT)) {
    const auto& file = args.get<std::string>(INPUT);
    fin.open(file, std::fstream::in);

    if (!fin) {
      return 1;
    }

    in = &fin;
  } else {
    in = &std::cin;
  }

  return put(path, dir_type, format, analyzer_type, analyzer_options,
             *in, lines_max, indexer_threads, consolidation_threads,
             commit_interval_ms, consolidation_internval_ms,
             batch_size, segment_mem_max, consolidate);
}

int put(int argc, char* argv[]) {
  // mode put
  cmdline::parser cmdput;
  cmdput.add(HELP, '?', "Produce help message");
  cmdput.add(INDEX_DIR, 0, "Path to index directory", true, std::string());
  cmdput.add(DIR_TYPE, 0, "Directory type (fs|mmap)", false, std::string("mmap"));
  cmdput.add(FORMAT, 0, "Format (1_0|1_1|1_2|1_2simd)", false, std::string("1_0"));
  cmdput.add(INPUT, 0, "Input file", true, std::string());
  cmdput.add(BATCH_SIZE, 0, "Lines per batch", false, size_t(0));
  cmdput.add(CONSOLIDATE_ALL, 0, "Consolidate all segments into one", false, false);
  cmdput.add(MAX, 0, "Maximum lines", false, size_t(0));
  cmdput.add(THR, 0, "Number of insert threads", false, size_t(0));
  cmdput.add(CONS_THR, 0, "Number of consolidation threads", false, size_t(0));
  cmdput.add(CPR, 0, "Commit period in lines", false, size_t(0));
  cmdput.add(CONSOLIDATION_INTERVAL, 0, "Consolidation interval (msec)", false, DEFAULT_CONSOLIDATION_INTERVAL_MSEC);
  cmdput.add(ANALYZER_TYPE, 0, "Text analyzer type", false, DEFAULT_ANALYZER_TYPE);
  cmdput.add(ANALYZER_OPTIONS, 0, "Text analyzer options", false, DEFAULT_ANALYZER_OPTIONS);
  cmdput.add(SEGMENT_MEM_MAX, 0, "Max size of per-segment in-memory buffer", false, DEFAULT_SEGMENT_MEM_MAX);

  cmdput.parse(argc, argv);

  if (cmdput.exist(HELP)) {
    std::cout << cmdput.usage() << std::endl;
    return 0;
  }

  return put(cmdput);
}
