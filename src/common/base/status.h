#pragma once
#include <google/protobuf/any.h>
#include <google/protobuf/message.h>
#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>

#include "src/common/base/logging.h"
#include "src/common/base/macros.h"
#include "src/common/base/proto/status.pb.h"

namespace pl {

class PL_MUST_USE_RESULT Status {
 public:
  // Success status.
  Status() = default;
  Status(const Status& s) noexcept;
  Status(pl::statuspb::Code code, const std::string& msg);
  Status(pl::statuspb::Code code, const std::string& msg,
         std::unique_ptr<google::protobuf::Message> ctx);
  // NOLINTNEXTLINE to make it easier to return status.
  Status(const pl::statuspb::Status& status_pb);

  void operator=(const Status& s) noexcept;

  /// Returns true if the status indicates success.
  bool ok() const { return (state_ == nullptr); }

  // Return self, this makes it compatible with StatusOr<>.
  const Status& status() const { return *this; }

  pl::statuspb::Code code() const { return ok() ? pl::statuspb::OK : state_->code; }

  const std::string& msg() const { return ok() ? empty_string() : state_->msg; }

  google::protobuf::Any* context() const { return ok() ? nullptr : state_->context.get(); }
  bool has_context() const { return ok() ? false : state_->context != nullptr; }

  bool operator==(const Status& x) const;
  bool operator!=(const Status& x) const;

  std::string ToString() const;

  static Status OK() { return Status(); }

  pl::statuspb::Status ToProto() const;
  void ToProto(pl::statuspb::Status* status_pb) const;

 private:
  struct State {
    // Needed for a call in status.cc.
    State() {}
    State(const State& state) noexcept;
    State(pl::statuspb::Code code, std::string msg, std::unique_ptr<google::protobuf::Any> context)
        : code(code), msg(msg), context(std::move(context)) {}
    State(pl::statuspb::Code code, std::string msg,
          std::unique_ptr<google::protobuf::Message> generic_pb_context)
        : code(code), msg(msg) {
      if (generic_pb_context == nullptr) {
        return;
      }
      context = std::make_unique<google::protobuf::Any>();
      context->PackFrom(*generic_pb_context);
    }
    pl::statuspb::Code code;
    std::string msg;
    std::unique_ptr<google::protobuf::Any> context;
  };

  static const std::string& empty_string() {
    static auto* empty = new std::string;
    return *empty;
  }

  // Will be null if status is OK.
  std::unique_ptr<State> state_;
};

inline Status::State::State(const State& state) noexcept {
  code = state.code;
  msg = state.msg;
  if (!state.context) {
    context = nullptr;
    return;
  }
  context = std::unique_ptr<google::protobuf::Any>(state.context->New());
  context->CopyFrom(*state.context);
}

inline Status::Status(const Status& s) noexcept
    : state_((s.state_ == nullptr) ? nullptr : new State(*s.state_)) {}

inline void Status::operator=(const Status& s) noexcept {
  // The following condition catches both aliasing (when this == &s),
  // and the common case where both s and *this are ok.
  if (state_ != s.state_) {
    if (s.state_ == nullptr) {
      state_ = nullptr;
    } else {
      state_ = std::make_unique<State>(*s.state_);
    }
  }
}

inline bool Status::operator==(const Status& x) const {
  return (this->state_ == x.state_) || (ToString() == x.ToString());
}

inline bool Status::operator!=(const Status& x) const { return !(*this == x); }

template <typename T>
inline Status StatusAdapter(const T&) noexcept {
  static_assert(sizeof(T) == 0, "Implement custom status adapter, or include correct .h file.");
  return Status(statuspb::UNIMPLEMENTED, "Should never get here");
}

template <>
inline Status StatusAdapter<Status>(const Status& s) noexcept {
  return s;
}

// Conversion of proto status message.
template <>
inline Status StatusAdapter<pl::statuspb::Status>(const pl::statuspb::Status& s) noexcept {
  return Status(s);
};

inline ::testing::AssertionResult IsOK(const Status& status) {
  if (status.ok()) {
    return ::testing::AssertionSuccess();
  }
  return ::testing::AssertionFailure() << status.ToString();
}

}  // namespace pl

#define PL_RETURN_IF_ERROR_IMPL(__status_name__, __status) \
  do {                                                     \
    const auto& __status_name__ = (__status);              \
    if (!__status_name__.ok()) {                           \
      return StatusAdapter(__status_name__);               \
    }                                                      \
  } while (false)

// Early-returns the status if it is in error; otherwise, proceeds.
// The argument expression is guaranteed to be evaluated exactly once.
#define PL_RETURN_IF_ERROR(__status) \
  PL_RETURN_IF_ERROR_IMPL(PL_CONCAT_NAME(__status__, __COUNTER__), __status)

#ifdef EXPECT_OK
// There is a conflicting name in status.h in protobuf.
#undef EXPECT_OK
#endif
// TODO(yzhao): Consider rename to PL_EXPECT_OK.
#define EXPECT_OK(value) EXPECT_TRUE(IsOK(::pl::StatusAdapter(value)))
#define EXPECT_NOT_OK(value) EXPECT_FALSE(IsOK(::pl::StatusAdapter(value)))
#define ASSERT_OK(value) ASSERT_TRUE(IsOK(::pl::StatusAdapter(value)))
#define ASSERT_NOT_OK(value) ASSERT_FALSE(IsOK(::pl::StatusAdapter(value)))

#define PL_CHECK_OK_PREPEND(to_call, msg)             \
  do {                                                \
    auto _s = (to_call);                              \
    CHECK(_s.ok()) << (msg) << ": " << _s.ToString(); \
  } while (false)

#ifdef NDEBUG
#define PL_DCHECK_OK(val) PL_UNUSED(val);
#else
#define PL_DCHECK_OK(val) PL_CHECK_OK_PREPEND(val, "Bad Status");
#endif

#define PL_CHECK_OK(val) PL_CHECK_OK_PREPEND(val, "Bad Status")
