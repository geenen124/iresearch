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

#ifndef IRESEARCH_TL_DOC_WRITER_H
#define IRESEARCH_TL_DOC_WRITER_H

#include "field_data.hpp"
#include "doc_header.hpp"
#include "analysis/token_stream.hpp"
#include "formats/formats.hpp"
#include "utils/directory_utils.hpp"
#include "utils/noncopyable.hpp"

NS_ROOT

struct segment_meta;

class IRESEARCH_API segment_writer: util::noncopyable {
 public:
  DECLARE_PTR(segment_writer);
  DECLARE_FACTORY_DEFAULT(directory& dir, format::ptr codec);

  struct update_context {
    size_t generation;
    size_t update_id;
  };

  typedef std::unordered_map<doc_id_t, update_context> update_contexts;

  template<typename FieldIterator, typename AttributeIterator>
  bool insert(
      FieldIterator begin, FieldIterator end, 
      AttributeIterator abegin, AttributeIterator aend,
      const update_context& ctx) {
    const auto doc_id = (type_limits<type_t::doc_id_t>::min)() + num_docs_cached_++;
    bool success = true;

    for (; success && begin != end; ++begin) {
      success &= insert_field(doc_id, *begin);
    }

    for (; success && abegin != aend; ++abegin) {
      success &= insert_attribute(doc_id, *abegin);
    }

    if (!success) {
      remove(doc_id); // mark as removed since not fully inserted
    }

    finish(doc_id, ctx);

    return success;
  }

  bool flush(std::string& filename, segment_meta& meta);

  const std::string& name() const { return seg_name_; }
  format::ptr codec() const { return codec_; }

  uint32_t docs_cached() const { return num_docs_cached_; }
  const update_contexts& docs_context() const { return docs_context_; }
  const document_mask& docs_mask() { return docs_mask_; }
  bool initialized() const { return initialized_; }
  bool remove(doc_id_t doc_id);
  void reset();
  void reset(std::string seg_name);

 private:
  struct column : util::noncopyable {
    column(const string_ref& name, columnstore_writer& columnstore);

    column(column&& other)
      : name(std::move(other.name)),
        handle(std::move(other.handle)) {
    }

    std::string name;
    columnstore_writer::column_t handle;
  };

  segment_writer(directory& dir, format::ptr codec) NOEXCEPT;

  bool index_field(
    doc_id_t doc_id, 
    const string_ref& name, 
    token_stream& tokens, 
    const flags& features, 
    float_t boost
  );

  // adds document attribute
  template<typename Attribute>
  bool insert_attribute(doc_id_t doc, const Attribute& attr) {
    REGISTER_TIMER_DETAILED();

    auto& stream = this->stream(
      doc, 
      static_cast<const string_ref&>(attr.name())
    );

    if (!attr.write(stream)) {
      stream.reset();
      return false;
    }

    return true;
  }

  // adds document field
  template<typename Field>
  bool insert_field(doc_id_t doc, const Field& field) {
    REGISTER_TIMER_DETAILED();

    auto& field_tokens = static_cast<token_stream&>(field.get_tokens());

    return index_field(
      doc,
      static_cast<const string_ref&>(field.name()),
      field_tokens,
      static_cast<const flags&>(field.features()),
      static_cast<float_t>(field.boost())
    );
  }

  columnstore_writer::column_output& stream(doc_id_t doc, const string_ref& name); // returns stream for storing attributes
  void finish(doc_id_t doc_id, const update_context& ctx); // finish document

  IRESEARCH_API_PRIVATE_VARIABLES_BEGIN
  update_contexts docs_context_;
  document_mask docs_mask_; // invalid/removed doc_ids (e.g. partially indexed due to indexing failure)
  fields_data fields_;
  std::unordered_map<hashed_string_ref, column> columns_;
  std::unordered_set<field_data*> norm_fields_; // document fields for normalization
  std::string seg_name_;
  field_writer::ptr field_writer_;
  column_meta_writer::ptr col_meta_writer_;
  columnstore_writer::ptr col_writer_;
  format::ptr codec_;
  tracking_directory dir_;
  std::atomic<uint32_t> num_docs_cached_{0};
  bool initialized_;
  IRESEARCH_API_PRIVATE_VARIABLES_END
}; // segment_writer

NS_END

#endif