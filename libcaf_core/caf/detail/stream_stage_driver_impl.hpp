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

#ifndef CAF_STREAM_STAGE_DRIVER_IMPL_HPP
#define CAF_STREAM_STAGE_DRIVER_IMPL_HPP

#include "caf/stream_slot.hpp"
#include "caf/stream_stage_driver.hpp"
#include "caf/stream_stage_trait.hpp"

namespace caf {
namespace detail {

/// Default implementation for a `stream_stage_driver` that hardwires `message`
/// as result type and implements `process` and `finalize` using user-provided
/// function objects (usually lambdas).
template <class Input, class Scatterer, class Process, class Finalize>
class stream_stage_driver_impl final
    : public stream_stage_driver<Input, Scatterer> {
public:
  // -- member types -----------------------------------------------------------

  using super = stream_stage_driver<Input, Scatterer>;

  using typename super::input_type;

  using typename super::output_type;

  using typename super::stream_type;

  using trait = stream_stage_trait_t<Process>;

  using state_type = typename trait::state;

  template <class Init>
  stream_stage_driver_impl(Init init, Process f, Finalize fin)
      : process_(std::move(f)),
        fin_(std::move(fin)) {
    init(state_);
  }

  void process(downstream<output_type>& out,
               std::vector<input_type>& batch) override {
    trait::process::invoke(process_, state_, out, batch);
  }

  void finalize(const error& err) override {
    return fin_(state_, err);
  }

private:
  state_type state_;
  Process process_;
  Finalize fin_;
  message result_;
};

} // namespace detail
} // namespace caf

#endif // CAF_STREAM_STAGE_DRIVER_IMPL_HPP
