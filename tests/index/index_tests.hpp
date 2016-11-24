//
// IResearch search engine 
// 
// Copyright � 2016 by EMC Corporation, All Rights Reserved
// 
// This software contains the intellectual property of EMC Corporation or is licensed to
// EMC Corporation from third parties. Use of this software and the intellectual property
// contained therein is expressly limited to the terms and conditions of the License
// Agreement under which it is provided by or on behalf of EMC.
// 

#ifndef IRESEARCH_INDEX_TESTS_H
#define IRESEARCH_INDEX_TESTS_H

#include "tests_shared.hpp"
#include "assert_format.hpp"
#include "analysis/analyzers.hpp"
#include "analysis/token_streams.hpp"
#include "analysis/token_attributes.hpp"
#include "index/index_writer.hpp"
#include "index/index_reader.hpp"
#include "doc_generator.hpp"
#include "utils/locale_utils.hpp"
#include "utils/timer_utils.hpp"
#include "document/field.hpp"

namespace iresearch {

struct term_attribute;

} // iresearch

namespace ir = iresearch;

namespace tests {

class index_test_base : public virtual test_base {
 protected:
  virtual ir::directory* get_directory() = 0;
  virtual ir::format::ptr get_codec() = 0;

  ir::directory& dir() const { return *dir_; }
  ir::format::ptr codec() { return codec_; }
  const index_t& index() const { return index_; }

  ir::index_writer::ptr open_writer(ir::OPEN_MODE mode = ir::OPEN_MODE::OM_CREATE) {
    return ir::index_writer::make(*dir_, codec_, mode);
  }

  ir::index_reader::ptr open_reader() {
    return ir::directory_reader::open(*dir_, codec_);
  }

  void assert_index(const ir::flags& features, size_t skip = 0) const {
    tests::assert_index(dir(), codec_, index(), features, skip);
  }

  virtual void SetUp() {
    test_base::SetUp();

    /* setting directory */
    dir_.reset( get_directory() );

    /* setting codec */
    codec_ = get_codec();
    
    assert( dir_ );
    assert( codec_ );    
  }

  virtual void TearDown() {
    test_base::TearDown();
    iresearch::timer_utils::init_stats(); // disable profile state tracking
  }

  void write_segment( ir::index_writer& writer, tests::index_segment& segment, tests::doc_generator_base& gen ) {
    // add segment
    const document* doc;
    while ( doc = gen.next() ) {
      segment.add(doc->begin(), doc->end());
      writer.insert(doc->begin(), doc->end());
    }
  }

  void add_segment( ir::index_writer& writer, tests::doc_generator_base& gen ) {
    index_.emplace_back();
    write_segment( writer, index_.back(), gen );
    writer.commit();
  }

  void add_segments( ir::index_writer& writer, std::vector<doc_generator_base::ptr>& gens ) {
    for ( auto& gen : gens ) {
      index_.emplace_back();
      write_segment( writer, index_.back(), *gen );
    }
    writer.commit();
  }
  
  void add_segment(tests::doc_generator_base& gen, ir::OPEN_MODE mode = ir::OPEN_MODE::OM_CREATE) {
    auto writer = open_writer(mode);
    add_segment(*writer, gen);
  }

 private: 
  index_t index_;
  ir::directory::ptr dir_;
  ir::format::ptr codec_;
}; // index_test_base

namespace templates { 

//////////////////////////////////////////////////////////////////////////////
/// @class token_stream_payload
/// @brief token stream wrapper which sets payload equal to term value
//////////////////////////////////////////////////////////////////////////////
class token_stream_payload : public ir::token_stream {
 public:
  explicit token_stream_payload(ir::token_stream* impl);
  bool next(); 

  const ir::attributes& attributes() const {
    return impl_->attributes();
  }

 private:
  ir::term_attribute* term_;
  ir::payload* pay_;
  ir::token_stream* impl_;
}; // token_stream_payload

//////////////////////////////////////////////////////////////////////////////
/// @class text_field
/// @brief field which uses text analyzer for tokenization and stemming
//////////////////////////////////////////////////////////////////////////////
template<typename T>
class text_field : public tests::field_base {
 public:
  text_field(
      const ir::string_ref& name, 
      bool indexed,
      bool payload = false)
      : token_stream_(ir::analysis::analyzers::get("text", "{\"locale\":\"C\", \"ignored_words\":{}}")) {
    if (payload) {
      token_stream_->reset(value_);
      pay_stream_.reset(new token_stream_payload(token_stream_.get()));
    }
    this->name(name);
    this->indexed(indexed);
    this->stored(false);
  }
  
  text_field(
      const ir::string_ref& name, 
      const T& value,
      bool indexed,
      bool payload = false)
      : token_stream_(ir::analysis::analyzers::get("text", "{\"locale\":\"C\", \"ignored_words\":{}}")),
      value_(value) {
    if (payload) {
      token_stream_->reset(value_);
      pay_stream_.reset(new token_stream_payload(token_stream_.get()));
    }
    this->name(name);
    this->indexed(indexed);
    this->stored(false);
  }

  ir::string_ref value() const { return value_; }
  void value(const T& value) { value = value; }
  void value(T&& value) { value_ = std::move(value); }

  const ir::flags& features() const {
    static ir::flags features{ 
      iresearch::frequency::type(), iresearch::position::type(), 
      iresearch::offset::type(), iresearch::payload::type() 
    };
    return features;
  }

  ir::token_stream* get_tokens() const {
    token_stream_->reset(value_);

    return pay_stream_
      ? static_cast<ir::token_stream*>(pay_stream_.get())
      : token_stream_.get();;
  }

 private:
  virtual bool write(ir::data_output&) const { return false; }

  static std::locale& get_locale() {
    static auto locale = iresearch::locale_utils::locale(nullptr, true);
    return locale;
  }

  std::unique_ptr<token_stream_payload> pay_stream_;
  ir::analysis::analyzer::ptr token_stream_;
  T value_;
}; // text_field

//////////////////////////////////////////////////////////////////////////////
/// @class string field
/// @brief field which uses simple analyzer without tokenization
//////////////////////////////////////////////////////////////////////////////
class string_field : public tests::field_base {
 public:
  string_field(const ir::string_ref& name, bool indexed, bool stored);
  string_field(const ir::string_ref& name, const ir::string_ref& value, bool indexed, bool stored);

  void value(const ir::string_ref& str);
  ir::string_ref value() const { return value_; }

  virtual const ir::flags& features() const override;
  virtual ir::token_stream* get_tokens() const override;
  virtual bool write(ir::data_output& out) const override;

 private:
  mutable ir::string_token_stream stream_;
  std::string value_;
}; // string_field

} // templates

void generic_json_field_factory(
  tests::document& doc,
  const std::string& name,
  const tests::json::json_value& data);

} // tests 

#endif // IRESEARCH_INDEX_TESTS_H
