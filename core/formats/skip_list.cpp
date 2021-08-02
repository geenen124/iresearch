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
////////////////////////////////////////////////////////////////////////////////

#include "shared.hpp"
#include "skip_list.hpp"

#include "store/store_utils.hpp"

#include "index/iterators.hpp"

#include "utils/math_utils.hpp"
#include "utils/std.hpp"

namespace {

// returns maximum number of skip levels needed to store specified
// count of objects for skip list with
// step skip_0 for 0 level, step skip_n for other levels
constexpr size_t max_levels(size_t skip_0, size_t skip_n, size_t count) {
  return skip_0 < count
    ? 1 + irs::math::log(count/skip_0, skip_n)
    : 0;
}

} // LOCAL

namespace iresearch {

// ----------------------------------------------------------------------------
// --SECTION--                                       skip_writer implementation
// ----------------------------------------------------------------------------

skip_writer_base::skip_writer_base(size_t skip_0, size_t skip_n) noexcept
  : skip_0_(skip_0), skip_n_(skip_n) {
  assert(skip_0_);
}

void skip_writer_base::prepare(
    size_t max_levels, 
    size_t count,
    const memory_allocator& alloc /* = memory_allocator::global() */) {
  max_levels = std::max(size_t(1), max_levels);
  max_levels = std::min(max_levels, ::max_levels(skip_0_, skip_n_, count));
  levels_.reserve(max_levels);
  max_levels = std::max(max_levels, levels_.capacity());

  // reset existing skip levels
  for (auto& level : levels_) {
    level.reset(alloc);
  }

  // add new skip levels
  for (auto size = levels_.size(); size < max_levels; ++size) {
    levels_.emplace_back(alloc);
  }
}

void skip_writer_base::flush(index_output& out) {
  const auto rend = levels_.rend();

  // find first filled level
  auto level = std::find_if(
    levels_.rbegin(), rend,
    [](const memory_output& level) {
      return level.stream.file_pointer(); });

  // write number of levels
  out.write_vint(uint32_t(std::distance(level, rend)));

  // write levels from n downto 0
  for (; level != rend; ++level) {
    auto& stream = level->stream;
    stream.flush(); // update length of each buffer

    const uint64_t length = stream.file_pointer();
    assert(length);
    out.write_vlong(length);
    stream >> out;
  }
}

// ----------------------------------------------------------------------------
// --SECTION--                                       skip_reader implementation
// ----------------------------------------------------------------------------

skip_reader_base::level::level(
    index_input::ptr&& stream,
    size_t id,
    size_t step,
    uint64_t begin,
    uint64_t end) noexcept
  : stream{std::move(stream)}, // thread-safe input
    begin{begin},
    end{end},
    id{id},
    step{step} {
}

/* static */ void skip_reader_base::seek_skip(
    level& lvl,
    uint64_t ptr,
    size_t skipped) {
  auto &stream = *lvl.stream;
  const auto absolute_ptr = lvl.begin + ptr;
  if (absolute_ptr > stream.file_pointer()) {
    stream.seek(absolute_ptr);
    lvl.skipped = skipped;
    if (lvl.child != UNDEFINED) {
      lvl.child = stream.read_vlong();
    }
  }
}

void skip_reader_base::reset() {
  for (auto& level : levels_) {
    level.stream->seek(level.begin);
    if (level.child != UNDEFINED) {
      level.child = 0;
    }
    level.skipped = 0;
    level.doc = doc_limits::invalid();
  }
}

/*static*/ void skip_reader_base::load_level(
    std::vector<level>& levels,
    index_input::ptr&& stream,
    size_t id,
    size_t step) {
  assert(stream);

  // read level length
  const auto length = stream->read_vlong();

  if (!length) {
    throw index_error("while loading level, error: zero length");
  }

  const auto begin = stream->file_pointer();
  const auto end = begin + length;

  levels.emplace_back(std::move(stream), id, step, begin, end); // load level
}

void skip_reader_base::prepare(index_input::ptr&& in) {
  assert(in);

  // read number of levels in a skip-list
  size_t max_levels = in->read_vint();

  if (max_levels) {
    std::vector<level> levels;
    levels.reserve(max_levels);

    size_t step = skip_0_ * size_t(pow(skip_n_, --max_levels)); // skip step of the level

    // load levels from n down to 1
    for (; max_levels; --max_levels) {
      load_level(levels, in->dup(), max_levels, step);

      // seek to the next level
      in->seek(levels.back().end);

      step /= skip_n_;
    }

    // load 0 level
    load_level(levels, std::move(in), 0, skip_0_);
    levels.back().child = UNDEFINED;

    levels_ = std::move(levels);
  }
}

}
