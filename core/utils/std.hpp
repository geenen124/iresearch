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

#ifndef IRESEARCH_STD_H
#define IRESEARCH_STD_H

#include "shared.hpp"
#include <iterator>

NS_ROOT

NS_BEGIN(irstd)

// helper for prior C++17 compilers
template<typename Container, typename... Args>
FORCE_INLINE std::pair<typename Container::iterator, bool> try_emplace(
    Container& cont,
    const typename Container::key_type& key,
    Args&&... args) {
#if (defined(_MSC_VER) && _MSC_VER >= 1900) || defined(__GNUC__)

  return cont.try_emplace(key, std::forward<Args>(args)...);

#else

  const auto it = cont.find(key);
  if (it != cont.end()) {
    return{ it, false };
  }

  return cont.emplace(
    std::piecewise_construct,
    std::forward_as_tuple(key),
    std::forward_as_tuple(std::forward<Args>(args)...)
  );

#endif
}

// converts reverse iterator to corresponding forward iterator
// the function does not accept containers "end"
template<typename ReverseIterator>
typename ReverseIterator::iterator_type to_forward(ReverseIterator it) {
  return --(it.base());
}

template<typename Iterator>
CONSTEXPR std::reverse_iterator<Iterator> make_reverse_iterator(Iterator it) {
  return std::reverse_iterator<Iterator>(it);
}

NS_BEGIN( heap )
NS_BEGIN( detail )

template<typename RandomAccessIterator, typename Diff, typename Func >
inline void _for_each_top(
    RandomAccessIterator begin,
    Diff idx, Diff bottom,
    Func func) {
  if (idx < bottom) {
    const RandomAccessIterator target = begin + idx;
    if (*begin == *target) {
      func(*target);
      _for_each_top(begin, 2 * idx + 1, bottom, func);
      _for_each_top(begin, 2 * idx + 2, bottom, func);
    }
  }
}

template<
  typename RandomAccessIterator, typename Diff,
  typename Pred, typename Func >
inline void _for_each_if(
    RandomAccessIterator begin,
    Diff idx, Diff bottom,
    Pred pred, Func func) {
  if (idx < bottom) {
    const RandomAccessIterator target = begin + idx;
    if (pred(*target)) {
      func(*target);
      _for_each_if(begin, 2 * idx + 1, bottom, pred, func);
      _for_each_if(begin, 2 * idx + 2, bottom, pred, func);
    }
  }
}

NS_END // detail

/////////////////////////////////////////////////////////////////////////////
/// @brief calls func for each element in a heap equals to top
////////////////////////////////////////////////////////////////////////////
template<typename RandomAccessIterator, typename Pred, typename Func>
inline void for_each_if(
    RandomAccessIterator begin,
    RandomAccessIterator end,
    Pred pred,
    Func func ) {
  typedef typename std::iterator_traits<
      RandomAccessIterator
  >::difference_type difference_type;

  detail::_for_each_if(
      begin,
      difference_type(0),
      difference_type(end - begin),
      pred, func);
}

/////////////////////////////////////////////////////////////////////////////
/// @brief calls func for each element in a heap equals to top
////////////////////////////////////////////////////////////////////////////
template< typename RandomAccessIterator, typename Func >
inline void for_each_top( 
    RandomAccessIterator begin, 
    RandomAccessIterator end, 
    Func func ) {
  typedef typename std::iterator_traits<
    RandomAccessIterator
  >::difference_type difference_type;
  
  detail::_for_each_top( 
    begin, 
    difference_type(0),
    difference_type(end - begin),
    func);
}

NS_END // heap

/////////////////////////////////////////////////////////////////////////////
/// @brief checks that all values in the specified range are equals 
////////////////////////////////////////////////////////////////////////////
template< typename InputIterator >
bool all_equals( InputIterator begin, InputIterator end ) {
  InputIterator cur = (begin == end ? end : begin+1);
  for ( ; cur != end; ++cur ) {
    if ( *cur != *begin ) {
      return false;
    }
  }

  return true;
}

//////////////////////////////////////////////////////////////////////////////
/// @class back_emplace_iterator 
/// @brief provide in place construction capabilities for stl algorithms 
////////////////////////////////////////////////////////////////////////////
template<typename Container>
class back_emplace_iterator : public std::iterator < std::output_iterator_tag, void, void, void, void > {
public:
  typedef Container container_type;

  explicit back_emplace_iterator(Container& cont) : cont_(std::addressof(cont)) { }

  template<class T>
  back_emplace_iterator<Container>& operator=( T&& t ) {
    cont_->emplace_back( std::forward<T>( t ) );
    return *this;
  }

  back_emplace_iterator& operator*( ) { return *this; }
  back_emplace_iterator& operator++( ) { return *this; }
  back_emplace_iterator& operator++( int ) { return *this; }

private:
  Container* cont_;
};

template<typename Container>
inline back_emplace_iterator<Container> back_emplacer( Container& cont ) {
  return back_emplace_iterator<Container>( cont );
}

NS_END
NS_END

#endif
