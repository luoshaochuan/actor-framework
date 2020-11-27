/******************************************************************************
 *                       ____    _    _____                                   *
 *                      / ___|  / \  |  ___|    C++                           *
 *                     | |     / _ \ | |_       Actor                         *
 *                     | |___ / ___ \|  _|      Framework                     *
 *                      \____/_/   \_|_|                                      *
 *                                                                            *
 * Copyright (C) 2011 - 2017                                                  *
 * Dominik Charousset <dominik.charousset (at) haw-hamburg.de>                *
 *                                                                            *
 * Distributed under the terms and conditions of the BSD 3-Clause License or  *
 * (at your option) under the terms and conditions of the Boost Software      *
 * License 1.0. See accompanying files LICENSE and LICENSE_ALTERNATIVE.       *
 *                                                                            *
 * If you did not receive a copy of the license files, see                    *
 * http://opensource.org/licenses/BSD-3-Clause and                            *
 * http://www.boost.org/LICENSE_1_0.txt.                                      *
 ******************************************************************************/

#include "caf/config_value.hpp"

#include <cctype>
#include <cmath>
#include <ostream>

#include "caf/detail/config_consumer.hpp"
#include "caf/detail/overload.hpp"
#include "caf/detail/parse.hpp"
#include "caf/detail/parser/read_config.hpp"
#include "caf/detail/type_traits.hpp"
#include "caf/expected.hpp"
#include "caf/parser_state.hpp"
#include "caf/pec.hpp"
#include "caf/settings.hpp"
#include "caf/string_view.hpp"

namespace caf {

namespace {

const char* type_names[] = {"none",   "integer",  "boolean",
                            "real",   "timespan", "uri",
                            "string", "list",     "dictionary"};

template <class To, class From>
auto no_conversion() {
  return [](const From&) {
    std::string msg = "cannot convert ";
    msg += type_names[detail::tl_index_of<config_value::types, From>::value];
    msg += " to ";
    msg += type_names[detail::tl_index_of<config_value::types, To>::value];
    auto err = make_error(sec::conversion_failed, std::move(msg));
    return expected<To>{std::move(err)};
  };
}

template <class To, class... From>
auto no_conversions() {
  return detail::make_overload(no_conversion<To, From>()...);
}

} // namespace

// -- constructors, destructors, and assignment operators ----------------------

config_value::~config_value() {
  // nop
}

// -- properties ---------------------------------------------------------------

void config_value::convert_to_list() {
  if (holds_alternative<list>(data_)) {
    ; // nop
  } else if (holds_alternative<none_t>(data_)) {
    data_ = config_value::list{};
  } else {
    using std::swap;
    config_value tmp;
    swap(*this, tmp);
    data_ = config_value::list{std::move(tmp)};
  }
}

config_value::list& config_value::as_list() {
  convert_to_list();
  return get<list>(*this);
}

config_value::dictionary& config_value::as_dictionary() {
  if (!holds_alternative<dictionary>(*this))
    *this = dictionary{};
  return get<dictionary>(*this);
}

void config_value::append(config_value x) {
  convert_to_list();
  get<list>(data_).emplace_back(std::move(x));
}

const char* config_value::type_name() const noexcept {
  return type_name_at_index(data_.index());
}

const char* config_value::type_name_at_index(size_t index) noexcept {
  return type_names[index];
}

// -- utility for get_as -------------------------------------------------------

expected<bool> config_value::to_boolean() const {
  using result_type = expected<bool>;
  auto f = detail::make_overload(
    no_conversions<bool, none_t, integer, real, timespan, uri,
                   config_value::list, config_value::dictionary>(),
    [](boolean x) { return result_type{x}; },
    [](const std::string& x) {
      if (x == "true") {
        return result_type{true};
      } else if (x == "false") {
        return result_type{false};
      } else {
        std::string msg = "cannot convert ";
        detail::print_escaped(msg, x);
        msg += " to a boolean";
        return result_type{make_error(sec::conversion_failed, std::move(msg))};
      }
    });
  return visit(f, data_);
}

expected<config_value::integer> config_value::to_integer() const {
  using result_type = expected<integer>;
  auto f = detail::make_overload(
    no_conversions<integer, none_t, bool, timespan, uri, config_value::list,
                   config_value::dictionary>(),
    [](integer x) { return result_type{x}; },
    [](real x) {
      using limits = std::numeric_limits<config_value::integer>;
      if (std::isfinite(x)            // never convert NaN & friends
          && std::fmod(x, 1.0) == 0.0 // only convert whole numbers
          && x <= static_cast<config_value::real>(limits::max())
          && x >= static_cast<config_value::real>(limits::min())) {
        return result_type{static_cast<config_value::integer>(x)};
      } else {
        auto err = make_error(
          sec::conversion_failed,
          "cannot convert decimal or out-of-bounds real number to an integer");
        return result_type{std::move(err)};
      }
    },
    [](const std::string& x) {
      auto tmp_int = config_value::integer{0};
      if (detail::parse(x, tmp_int) == none)
        return result_type{tmp_int};
      auto tmp_real = 0.0;
      if (detail::parse(x, tmp_real) == none)
        if (auto ival = config_value{tmp_real}.to_integer())
          return result_type{*ival};
      std::string msg = "cannot convert ";
      detail::print_escaped(msg, x);
      msg += " to an integer";
      return result_type{make_error(sec::conversion_failed, std::move(msg))};
    });
  return visit(f, data_);
}

expected<config_value::real> config_value::to_real() const {
  using result_type = expected<real>;
  auto f = detail::make_overload(
    no_conversions<real, none_t, bool, timespan, uri, config_value::list,
                   config_value::dictionary>(),
    [](integer x) {
      // This cast may lose precision on the value. We could try and check that,
      // but refusing to convert on loss of precision could also be unexpected
      // behavior. So we rather always convert, even if it costs precision.
      return result_type{static_cast<real>(x)};
    },
    [](real x) { return result_type{x}; },
    [](const std::string& x) {
      auto tmp = 0.0;
      if (detail::parse(x, tmp) == none)
        return result_type{tmp};
      std::string msg = "cannot convert ";
      detail::print_escaped(msg, x);
      msg += " to a floating point number";
      return result_type{make_error(sec::conversion_failed, std::move(msg))};
    });
  return visit(f, data_);
}

expected<timespan> config_value::to_timespan() const {
  using result_type = expected<timespan>;
  auto f = detail::make_overload(
    no_conversions<timespan, none_t, bool, integer, real, uri,
                   config_value::list, config_value::dictionary>(),
    [](timespan x) {
      // This cast may lose precision on the value. We could try and check that,
      // but refusing to convert on loss of precision could also be unexpected
      // behavior. So we rather always convert, even if it costs precision.
      return result_type{x};
    },
    [](const std::string& x) {
      auto tmp = timespan{};
      if (detail::parse(x, tmp) == none)
        return result_type{tmp};
      std::string msg = "cannot convert ";
      detail::print_escaped(msg, x);
      msg += " to a timespan";
      return result_type{make_error(sec::conversion_failed, std::move(msg))};
    });
  return visit(f, data_);
}

expected<config_value::list> config_value::to_list() const {
  using result_type = expected<list>;
  auto f = detail::make_overload(
    no_conversions<list, none_t, bool, integer, real, timespan, uri>(),
    [](const std::string& x) {
      // Check whether we can parse the string as a list. If that fails, try
      // whether we can parse it as a dictionary instead (and then convert that
      // to a list).
      config_value::list tmp;
      if (detail::parse(x, tmp, detail::require_opening_char) == none)
        return result_type{std::move(tmp)};
      config_value::dictionary dict;
      if (detail::parse(x, dict, detail::require_opening_char) == none) {
        tmp.clear();
        for (const auto& [key, val] : dict) {
          list kvp;
          kvp.reserve(2);
          kvp.emplace_back(key);
          kvp.emplace_back(val);
          tmp.emplace_back(std::move(kvp));
        }
        return result_type{std::move(tmp)};
      }
      std::string msg = "cannot convert ";
      detail::print_escaped(msg, x);
      msg += " to a list";
      return result_type{make_error(sec::conversion_failed, std::move(msg))};
    },
    [](const list& x) { return result_type{x}; },
    [](const dictionary& x) {
      list tmp;
      for (const auto& [key, val] : x) {
        list kvp;
        kvp.reserve(2);
        kvp.emplace_back(key);
        kvp.emplace_back(val);
        tmp.emplace_back(std::move(kvp));
      }
      return result_type{std::move(tmp)};
    });
  return visit(f, data_);
}

expected<config_value::dictionary> config_value::to_dictionary() const {
  using result_type = expected<dictionary>;
  auto f = detail::make_overload(
    no_conversions<dictionary, none_t, bool, integer, timespan, real, uri,
                   list>(),
    [](const std::string& x) {
      dictionary tmp;
      if (detail::parse(x, tmp, detail::require_opening_char) == none)
        return result_type{std::move(tmp)};
      std::string msg = "cannot convert ";
      detail::print_escaped(msg, x);
      msg += " to a dictionary";
      return result_type{make_error(sec::conversion_failed, std::move(msg))};
    },
    [](const dictionary& x) { return result_type{x}; });
  return visit(f, data_);
}

bool config_value::can_convert_to_dictionary() const {
  auto f = detail::make_overload( //
    [](const auto&) { return false; },
    [this](const std::string&) {
      // TODO: implement some dry-run mode and use it here to avoid creating an
      //       actual dictionary only to throw it away.
      auto maybe_dict = to_dictionary();
      return static_cast<bool>(maybe_dict);
    },
    [](const dictionary&) { return true; });
  return visit(f, data_);
}

// -- related free functions ---------------------------------------------------

bool operator<(const config_value& x, const config_value& y) {
  return x.get_data() < y.get_data();
}

bool operator==(const config_value& x, const config_value& y) {
  return x.get_data() == y.get_data();
}

namespace {

void to_string_impl(std::string& str, const config_value& x);

struct to_string_visitor {
  std::string& str;

  void operator()(const std::string& x) {
    detail::print_escaped(str, x);
  }

  template <class T>
  void operator()(const T& x) {
    detail::print(str, x);
  }

  void operator()(none_t) {
    str += "null";
  }

  void operator()(const uri& x) {
    auto x_str = x.str();
    str.insert(str.end(), x_str.begin(), x_str.end());
  }

  void operator()(const config_value::list& xs) {
    str += '[';
    if (!xs.empty()) {
      auto i = xs.begin();
      to_string_impl(str, *i);
      for (++i; i != xs.end(); ++i) {
        str += ", ";
        to_string_impl(str, *i);
      }
    }
    str += ']';
  }

  void append_key(const std::string& key) {
    if (std::all_of(key.begin(), key.end(), ::isalnum))
      str.append(key.begin(), key.end());
    else
      (*this)(key);
  }

  void operator()(const config_value::dictionary& xs) {
    str += '{';
    if (!xs.empty()) {
      auto i = xs.begin();
      append_key(i->first);
      str += " = ";
      to_string_impl(str, i->second);
      for (++i; i != xs.end(); ++i) {
        str += ", ";
        append_key(i->first);
        str += " = ";
        to_string_impl(str, i->second);
      }
    }
    str += '}';
  }
};

void to_string_impl(std::string& str, const config_value& x) {
  to_string_visitor f{str};
  visit(f, x.get_data());
}

} // namespace

std::string to_string(const config_value& x) {
  if (auto str = get_if<std::string>(std::addressof(x.get_data()))) {
    return *str;
  } else {
    std::string result;
    to_string_impl(result, x);
    return result;
  }
}

std::string to_string(const settings& xs) {
  std::string result;
  to_string_visitor f{result};
  f(xs);
  return result;
}

std::ostream& operator<<(std::ostream& out, const config_value& x) {
  return out << to_string(x);
}

} // namespace caf
