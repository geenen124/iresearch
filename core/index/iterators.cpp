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

#include "iterators.hpp"
#include "field_meta.hpp"
#include "formats/formats.hpp"
#include "search/cost.hpp"
#include "utils/type_limits.hpp"
#include "utils/singleton.hpp"

NS_ROOT
NS_LOCAL

iresearch::attributes empty_doc_iterator_attributes() {
  iresearch::attributes attrs(1); // cost
  attrs.add<cost>()->value(0);
  return attrs;
}

//////////////////////////////////////////////////////////////////////////////
/// @class empty_doc_iterator
/// @brief represents an iterator with no documents 
//////////////////////////////////////////////////////////////////////////////
struct empty_doc_iterator : score_doc_iterator {
  virtual doc_id_t value() const override { return type_limits<type_t::doc_id_t>::eof(); }
  virtual bool next() override { return false; }
  virtual doc_id_t seek(doc_id_t) override { return type_limits<type_t::doc_id_t>::eof(); }
  virtual void score() override { }
  virtual const iresearch::attributes& attributes() const NOEXCEPT override {
    static iresearch::attributes empty = empty_doc_iterator_attributes();
    return empty;
  }
};

//////////////////////////////////////////////////////////////////////////////
/// @class empty_term_iterator
/// @brief represents an iterator without terms
//////////////////////////////////////////////////////////////////////////////
struct empty_term_iterator : term_iterator {
  virtual const bytes_ref& value() const override { return bytes_ref::nil; }
  virtual doc_iterator::ptr postings(const flags&) const {
    return score_doc_iterator::empty();
  }
  virtual void read() { }
  virtual bool next() override { return false; }
  virtual const iresearch::attributes& attributes() const NOEXCEPT override {
    return iresearch::attributes::empty_instance();
  }
};

struct empty_term_reader : singleton<empty_term_reader>, term_reader {
  virtual iresearch::seek_term_iterator::ptr iterator() const { return nullptr; }
  virtual const iresearch::field_meta& meta() const { 
    static iresearch::field_meta EMPTY;
    return EMPTY;
  }

  virtual const iresearch::attributes& attributes() const NOEXCEPT {
    return iresearch::attributes::empty_instance();
  }

  // total number of terms
  virtual size_t size() const { return 0; }

  // total number of documents
  virtual uint64_t docs_count() const { return 0; }

  // less significant term
  virtual const iresearch::bytes_ref& (min)() const { 
    return iresearch::bytes_ref::nil; 
  }

  // most significant term
  virtual const iresearch::bytes_ref& (max)() const { 
    return iresearch::bytes_ref::nil; 
  }
}; // empty_term_reader

struct empty_field_iterator : field_iterator {
  virtual const term_reader& value() const {
    return empty_term_reader::instance();
  }

  virtual bool next() override {
    return false;
  }
};

struct empty_column_iterator : column_iterator {
  virtual const column_meta& value() const {
    static column_meta empty;
    return empty;
  }

  virtual bool next() override {
    return false;
  }
};
 
NS_END // LOCAL

// ----------------------------------------------------------------------------
// --SECTION--                                              basic_term_iterator
// ----------------------------------------------------------------------------

term_iterator::ptr term_iterator::empty() {
  //TODO: make singletone
  return term_iterator::make<empty_term_iterator>();
}

// ----------------------------------------------------------------------------
// --SECTION--                                                seek_doc_iterator 
// ----------------------------------------------------------------------------

score_doc_iterator::ptr score_doc_iterator::empty() {
  //TODO: make singletone
  return score_doc_iterator::make<empty_doc_iterator>();
}

// ----------------------------------------------------------------------------
// --SECTION--                                                   field_iterator 
// ----------------------------------------------------------------------------

field_iterator::ptr field_iterator::empty() {
  return field_iterator::make<empty_field_iterator>();
}

// ----------------------------------------------------------------------------
// --SECTION--                                                  column_iterator 
// ----------------------------------------------------------------------------

column_iterator::ptr column_iterator::empty() {
  return column_iterator::make<empty_column_iterator>();
}

NS_END // ROOT 
