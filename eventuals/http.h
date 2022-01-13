#pragma once

#include <string>
#include <vector>

#include "curl/curl.h"
#include "eventuals/event-loop.h"
#include "eventuals/pem.h"
#include "eventuals/scheduler.h"
#include "eventuals/x509.h"

////////////////////////////////////////////////////////////////////////

namespace eventuals {
namespace http {

////////////////////////////////////////////////////////////////////////

enum Method {
  GET,
  POST,
};

////////////////////////////////////////////////////////////////////////

// Used for application/x-www-form-urlencoded case.
// First string is key, second is value.
// TODO(folming): switch to RapidJSON.
using PostFields = std::vector<std::pair<std::string, std::string>>;

////////////////////////////////////////////////////////////////////////

// Forward declarations.
template <bool, bool>
class RequestBuilder;

////////////////////////////////////////////////////////////////////////

class Request {
 public:
  // Constructs a new http::Request "builder" with the default
  // undefined values.
  static RequestBuilder<false, false> Builder();

  const auto& uri() {
    return uri_;
  }

  const auto& method() {
    return method_;
  }

  const auto& headers() {
    return headers_;
  }

  const auto& body() {
    return body_;
  }

  const auto& timeout() {
    return timeout_;
  }

  const auto& fields() {
    return fields_;
  }

  const auto& verify_peer() {
    return verify_peer_;
  }

  const auto& certificate() {
    return certificate_;
  }

 private:
  template <bool, bool>
  friend class RequestBuilder;

  friend class Client;

  std::string uri_;
  Method method_;
  std::vector<std::pair<std::string, std::string>> headers_;
  std::string body_;
  std::chrono::nanoseconds timeout_;
  PostFields fields_;
  bool verify_peer_ = true;
  std::optional<x509::Certificate> certificate_;
};

////////////////////////////////////////////////////////////////////////

template <bool HasUri_, bool HasMethod_>
class RequestBuilder {
 public:
  Request Build() && {
    static_assert(HasUri_, "Missing 'uri(...)' for http::Request");
    static_assert(HasMethod_, "Missing 'method(...)' for http::Request");
    return std::move(request_);
  }

  RequestBuilder<true, HasMethod_> uri(std::string&& uri) && {
    static_assert(!HasUri_, "Duplicate 'uri(...)' for http::Request");
    request_.uri_ = std::move(uri);
    return RequestBuilder<true, HasMethod_>(std::move(request_));
  }

  RequestBuilder<HasUri_, true> method(Method method) && {
    static_assert(!HasMethod_, "Duplicate 'method(...)' for http::Request");
    request_.method_ = method;
    return RequestBuilder<HasUri_, true>(std::move(request_));
  }

  RequestBuilder<HasUri_, HasMethod_> timeout(
      std::chrono::nanoseconds&& timeout) && {
    request_.timeout_ = std::move(timeout);
    return RequestBuilder<HasUri_, HasMethod_>(std::move(request_));
  }

  RequestBuilder<HasUri_, HasMethod_> fields(PostFields&& fields) && {
    request_.fields_ = std::move(fields);
    return RequestBuilder<HasUri_, HasMethod_>(std::move(request_));
  }

  RequestBuilder<HasUri_, HasMethod_> verify_peer(bool value) && {
    // TODO(benh): consider checking that the scheme is 'https'.
    request_.verify_peer_ = value;
    return RequestBuilder<HasUri_, HasMethod_>(std::move(request_));
  }

  RequestBuilder<HasUri_, HasMethod_> certificate(
      x509::Certificate&& certificate) && {
    // TODO(benh): consider checking that the scheme is 'https'.
    request_.certificate_ = std::move(certificate);
    return RequestBuilder<HasUri_, HasMethod_>(std::move(request_));
  }

 private:
  template <bool, bool>
  friend class RequestBuilder;

  friend class Request;

  RequestBuilder() {}

  RequestBuilder(Request&& request)
    : request_(std::move(request)) {}

  Request request_;
};

////////////////////////////////////////////////////////////////////////

RequestBuilder<false, false> Request::Builder() {
  return RequestBuilder<false, false>();
}

////////////////////////////////////////////////////////////////////////

struct Response {
  long code;
  std::string body;
};

////////////////////////////////////////////////////////////////////////

// Forward declarations.
class ClientBuilder;

////////////////////////////////////////////////////////////////////////

class Client {
 public:
  // Constructs a new http::Client "builder" with the default
  // undefined values.
  static ClientBuilder Builder();

  Client() {}

  auto Get(
      std::string&& uri,
      std::chrono::nanoseconds&& timeout = std::chrono::nanoseconds(0));

  auto Post(
      std::string&& uri,
      PostFields&& fields,
      std::chrono::nanoseconds&& timeout = std::chrono::nanoseconds(0));

  auto Do(Request&& request);

 private:
  friend class ClientBuilder;

  std::optional<bool> verify_peer_;
  std::optional<x509::Certificate> certificate_;
};

////////////////////////////////////////////////////////////////////////

class ClientBuilder {
 public:
  Client Build() && {
    return std::move(client_);
  }

  ClientBuilder verify_peer(bool value) && {
    client_.verify_peer_ = value;
    return ClientBuilder(std::move(client_));
  }

  // Specify the certificate to use when doing verification. Same
  // semantics as the following:
  //
  // $ curl --cacert /path/to/certificate ...
  //
  // TODO(benh): provide support for a "bundle" of certificates.
  ClientBuilder certificate(x509::Certificate&& certificate) && {
    client_.certificate_ = std::move(certificate);
    return ClientBuilder(std::move(client_));
  }

 private:
  friend class Client;

  ClientBuilder() {}

  ClientBuilder(Client&& client)
    : client_(std::move(client)) {}

  Client client_;
};

////////////////////////////////////////////////////////////////////////

ClientBuilder Client::Builder() {
  return ClientBuilder();
}

////////////////////////////////////////////////////////////////////////

// Our own eventual for using libcurl with the EventLoop.
//
// The general algorithm:
// 1. Create easy and multi handles. Set options for them.
//    Add easy handle to multi handle with curl_multi_add_handle.
//    TIMERFUNCTION is called to set a timer which will tell us when
//    to perform checks on libcurl handles.
//    SOCKETFUNCTION is called by using curl_multi_socket_action.
//    We can pass specific socket descriptor to work with that particular
//    socket or we can pass CURL_SOCKET_TIMEOUT to let libcurl call a function
//    for each socket that is currently in use.
// 2. Whenever SOCKETFUNCTION is called we check for events and set a poll
//    handle for the particular socket. This poll handle is created on heap
//    and that's why we put its pointer inside vector so that we can stop it
//    when we have to interrupt the transfer.
// 3. Whenever curl_multi_socket_action is called we can get an amount of
//    remaining running easy handles. If this value is 0 then we read info
//    from multi handle using check_multi_info lambda and clean everything up.
struct _HTTP {
  template <typename K_>
  struct Continuation {
    Continuation(K_&& k, EventLoop& loop, Request&& request)
      : k_(std::move(k)),
        loop_(loop),
        request_(std::move(request)),
        start_(&loop, "HTTP (start)"),
        interrupt_(&loop_, "HTTP (interrupt)") {}

    Continuation(Continuation&& that)
      : k_(std::move(that.k_)),
        loop_(that.loop_),
        request_(std::move(that.request_)),
        start_(&that.loop_, "HTTP (start)"),
        interrupt_(&that.loop_, "HTTP (interrupt)") {
      CHECK(!that.started_ || !that.completed_) << "moving after starting";
      CHECK(!handler_);
    }

    ~Continuation() {
      CHECK(!started_ || closed_);
    }

    void Start() {
      CHECK(!started_ && !completed_);

      loop_.Submit(
          [this]() {
            if (!completed_) {
              started_ = true;

              CHECK(!error_);

              CHECK_NOTNULL(easy_ = curl_easy_init());
              CHECK_NOTNULL(multi_ = curl_multi_init());

              // If applicable, PEM encode any certificate now before we start
              // anything and can easily propagate an error.
              auto certificate = request_.certificate();
              if (certificate) {
                auto pem_certificate = pem::Encode(
                    x509::Certificate(*certificate));

                if (!pem_certificate) {
                  completed_ = true;

                  k_.Fail("Failed to PEM encode certificate");

                  curl_easy_cleanup(easy_);
                  curl_multi_cleanup(multi_);

                  return; // Don't do anything else!
                } else {
                  curl_blob blob;
                  blob.data = pem_certificate->data();
                  blob.len = pem_certificate->size();
                  blob.flags = CURL_BLOB_COPY;
                  CHECK_EQ(
                      curl_easy_setopt(
                          easy_,
                          CURLOPT_CAINFO_BLOB,
                          &blob),
                      CURLE_OK);
                }
              }

              CHECK_EQ(0, uv_timer_init(loop_, &timer_));
              uv_handle_set_data((uv_handle_t*) &timer_, this);

              // Called only one time to finish the transfer
              // and clean everything up.
              static auto check_multi_info = [](Continuation& continuation) {
                continuation.completed_ = true;

                // Stores the amount of remaining messages in multi handle.
                // Unused.
                int msgq = 0;
                CURLMsg* message = curl_multi_info_read(
                    continuation.multi_,
                    &msgq);

                // Getting the response code and body.
                switch (message->msg) {
                  case CURLMSG_DONE:
                    if (message->data.result == CURLE_OK) {
                      curl_easy_getinfo(
                          continuation.easy_,
                          CURLINFO_RESPONSE_CODE,
                          &continuation.code_);
                    } else {
                      continuation.error_ = message->data.result;
                    }
                    break;
                  default:
                    continuation.error_ = CURLE_ABORTED_BY_CALLBACK;
                    break;
                }

                // Stop transfer completely.
                CHECK_EQ(
                    curl_multi_remove_handle(
                        continuation.multi_,
                        message->easy_handle),
                    CURLM_OK);

                // Memory cleanup.
                for (auto& poll : continuation.polls_) {
                  if (uv_is_active((uv_handle_t*) poll)) {
                    uv_poll_stop(poll);
                  }
                  uv_close(
                      (uv_handle_t*) poll,
                      [](uv_handle_t* handle) {
                        delete handle;
                      });
                }

                continuation.polls_.clear();

                // We don't have to check uv_is_active for timer since
                // libuv checks it by itself.
                // Return value is always 0.
                uv_timer_stop(&continuation.timer_);
                uv_close(
                    (uv_handle_t*) &continuation.timer_,
                    [](uv_handle_t* handle) {
                      auto& continuation = *(Continuation*) handle->data;
                      continuation.closed_ = true;

                      if (!continuation.error_) {
                        continuation.k_.Start(Response{
                            continuation.code_,
                            continuation.buffer_.Extract()});
                      } else {
                        continuation.k_.Fail(
                            curl_easy_strerror(
                                (CURLcode) continuation.error_));
                      }
                    });

                if (continuation.fields_string_ != nullptr) {
                  curl_free(continuation.fields_string_);
                  continuation.fields_string_ = nullptr;
                }
                curl_easy_cleanup(continuation.easy_);
                curl_multi_cleanup(continuation.multi_);
              };

              static auto poll_callback = [](uv_poll_t* handle,
                                             int status,
                                             int events) {
                auto& continuation = *(Continuation*) handle->data;

                int flags = 0;
                if (status < 0) {
                  flags = CURL_CSELECT_ERR;
                }
                if (status == 0 && (events & UV_READABLE)) {
                  flags |= CURL_CSELECT_IN;
                }
                if (status == 0 && (events & UV_WRITABLE)) {
                  flags |= CURL_CSELECT_OUT;
                }

                // Getting underlying socket desriptor from poll handle.
                uv_os_fd_t socket_descriptor;
                uv_fileno(
                    (uv_handle_t*) handle,
                    &socket_descriptor);

                // Stores the amount of running easy handles.
                // Set by curl_multi_socket_action.
                int running_handles = 0;

                // Perform an action for the particular socket
                // which is the one we are currently working with.
                // We don't want to perform an action on every socket inside
                // libcurl - only that one.
                curl_multi_socket_action(
                    continuation.multi_,
                    (curl_socket_t) socket_descriptor,
                    flags,
                    &running_handles);

                // If 0 - finalize the transfer.
                if (running_handles == 0) {
                  check_multi_info(continuation);
                }
              };

              static auto timer_callback = [](uv_timer_t* handle) {
                auto& continuation = *(Continuation*) handle->data;

                // Stores the amount of running easy handles.
                // Set by curl_multi_socket_action.
                int running_handles = 0;

                // Called with CURL_SOCKET_TIMEOUT to
                // perform an action with each and every socket
                // currently in use by libcurl.
                curl_multi_socket_action(
                    continuation.multi_,
                    CURL_SOCKET_TIMEOUT,
                    0,
                    &running_handles);

                // If 0 - finalize the transfer.
                if (running_handles == 0) {
                  check_multi_info(continuation);
                }
              };

              static auto socket_function = +[](CURL* easy,
                                                curl_socket_t sockfd,
                                                int what,
                                                Continuation* continuation,
                                                void* socket_poller) {
                int events = 0;

                switch (what) {
                  case CURL_POLL_IN:
                  case CURL_POLL_OUT:
                  case CURL_POLL_INOUT:
                    // Add poll handle for this particular socket.
                    if (what & CURL_POLL_IN) {
                      events |= UV_READABLE;
                    }
                    if (what & CURL_POLL_OUT) {
                      events |= UV_WRITABLE;
                    }

                    // If no poll handle is assigned to this socket.
                    if (socket_poller == nullptr) {
                      socket_poller = new uv_poll_t();
                      continuation->polls_.push_back(
                          (uv_poll_t*) socket_poller);

                      CHECK_EQ(
                          uv_poll_init_socket(
                              continuation->loop_,
                              (uv_poll_t*) socket_poller,
                              sockfd),
                          0);

                      uv_handle_set_data(
                          (uv_handle_t*) socket_poller,
                          continuation);

                      // Assign created poll handle so that in the future
                      // we can get it through socket_poller argument.
                      // Useful to check if we already have a poll handle
                      // for the socket currently in use.
                      CHECK_EQ(
                          curl_multi_assign(
                              continuation->multi_,
                              sockfd,
                              socket_poller),
                          CURLM_OK);
                    }

                    // Stops poll handle if it was started.
                    if (uv_is_active((uv_handle_t*) socket_poller)) {
                      CHECK_EQ(
                          uv_poll_stop(
                              (uv_poll_t*) socket_poller),
                          0);
                    }

                    CHECK_EQ(
                        uv_poll_start(
                            (uv_poll_t*) socket_poller,
                            events,
                            poll_callback),
                        0);

                    break;
                  case CURL_POLL_REMOVE:
                    // Remove poll handle for this particular socket.
                    uv_poll_stop((uv_poll_t*) socket_poller);
                    uv_close(
                        (uv_handle_t*) socket_poller,
                        [](uv_handle_t* handle) {
                          delete handle;
                        });

                    // Remove this poll handle from vector.
                    for (auto it = continuation->polls_.begin();
                         it != continuation->polls_.end();
                         it++) {
                      if (*it == (uv_poll_t*) socket_poller) {
                        continuation->polls_.erase(it);
                        break;
                      }
                    }

                    // Remove assignment of poll handle to this socket.
                    CHECK_EQ(
                        curl_multi_assign(
                            continuation->multi_,
                            sockfd,
                            nullptr),
                        CURLM_OK);
                    break;
                }
              };

              // Used by libcurl to set a timer after
              // which we should start checking handles inside libcurl.
              static auto timer_function = +[](CURLM* multi,
                                               long timeout_ms,
                                               Continuation* continuation) {
                if (timeout_ms < 0) {
                  timeout_ms = 0;
                }

                uv_timer_start(
                    &continuation->timer_,
                    timer_callback,
                    timeout_ms,
                    0);
              };

              // https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html
              static auto write_function = +[](char* data,
                                               size_t size,
                                               size_t nmemb,
                                               Continuation* continuation) {
                continuation->buffer_ += std::string(data, size * nmemb);

                return nmemb * size;
              };

              using std::chrono::duration_cast;
              using std::chrono::milliseconds;

              // CURL multi options.
              CHECK_EQ(
                  curl_multi_setopt(
                      multi_,
                      CURLMOPT_SOCKETDATA,
                      this),
                  CURLM_OK);
              CHECK_EQ(
                  curl_multi_setopt(
                      multi_,
                      CURLMOPT_SOCKETFUNCTION,
                      socket_function),
                  CURLM_OK);
              CHECK_EQ(
                  curl_multi_setopt(
                      multi_,
                      CURLMOPT_TIMERDATA,
                      this),
                  CURLM_OK);
              CHECK_EQ(
                  curl_multi_setopt(
                      multi_,
                      CURLMOPT_TIMERFUNCTION,
                      timer_function),
                  CURLM_OK);

              // CURL easy options.
              if (!request_.verify_peer()) {
                CHECK_EQ(
                    curl_easy_setopt(
                        easy_,
                        CURLOPT_SSL_VERIFYPEER,
                        0),
                    CURLE_OK);
              }

              switch (request_.method()) {
                case Method::GET:
                  CHECK_EQ(
                      curl_easy_setopt(
                          easy_,
                          CURLOPT_HTTPGET,
                          1),
                      CURLE_OK);
                  break;
                case Method::POST:
                  // Converting PostFields.
                  CURLU* curl_url_handle = curl_url();
                  CHECK_EQ(
                      curl_url_set(
                          curl_url_handle,
                          CURLUPART_URL,
                          request_.uri().c_str(),
                          0),
                      CURLUE_OK);
                  for (const auto& field : request_.fields()) {
                    std::string combined =
                        field.first
                        + '='
                        + field.second;
                    CHECK_EQ(
                        curl_url_set(
                            curl_url_handle,
                            CURLUPART_QUERY,
                            combined.c_str(),
                            CURLU_APPENDQUERY | CURLU_URLENCODE),
                        CURLUE_OK);
                  }
                  CHECK_EQ(
                      curl_url_get(
                          curl_url_handle,
                          CURLUPART_QUERY,
                          &fields_string_,
                          0),
                      CURLUE_OK);
                  curl_url_cleanup(curl_url_handle);
                  // End of conversion.

                  CHECK_EQ(
                      curl_easy_setopt(
                          easy_,
                          CURLOPT_HTTPPOST,
                          1),
                      CURLE_OK);
                  CHECK_EQ(
                      curl_easy_setopt(
                          easy_,
                          CURLOPT_POSTFIELDS,
                          fields_string_),
                      CURLE_OK);

                  break;
              }

              CHECK_EQ(
                  curl_easy_setopt(
                      easy_,
                      CURLOPT_URL,
                      request_.uri().c_str()),
                  CURLE_OK);
              CHECK_EQ(
                  curl_easy_setopt(
                      easy_,
                      CURLOPT_WRITEDATA,
                      this),
                  CURLE_OK);
              CHECK_EQ(
                  curl_easy_setopt(
                      easy_,
                      CURLOPT_WRITEFUNCTION,
                      write_function),
                  CURLE_OK);
              // Option to follow redirects.
              CHECK_EQ(
                  curl_easy_setopt(
                      easy_,
                      CURLOPT_FOLLOWLOCATION,
                      1),
                  CURLE_OK);
              // The internal mechanism of libcurl to provide timeout support.
              // Not accurate at very low values.
              // 0 means that transfer can run indefinitely.
              CHECK_EQ(
                  curl_easy_setopt(
                      easy_,
                      CURLOPT_TIMEOUT_MS,
                      duration_cast<milliseconds>(request_.timeout())),
                  CURLE_OK);
              // If onoff is 1, libcurl will not use any functions that install
              // signal handlers or any functions that cause signals to be sent
              // to the process. This option is here to allow multi-threaded
              // unix applications to still set/use all timeout options etc,
              // without risking getting signals.
              // More here: https://curl.se/libcurl/c/CURLOPT_NOSIGNAL.html
              CHECK_EQ(
                  curl_easy_setopt(
                      easy_,
                      CURLOPT_NOSIGNAL,
                      1),
                  CURLE_OK);

              // Start handling connection.
              CHECK_EQ(
                  curl_multi_add_handle(
                      multi_,
                      easy_),
                  CURLM_OK);
            }
          },
          &start_);
    }

    template <typename... Args>
    void Fail(Args&&... args) {
      k_.Fail(std::forward<Args>(args)...);
    }

    void Stop() {
      k_.Stop();
    }

    void Register(Interrupt& interrupt) {
      k_.Register(interrupt);

      handler_.emplace(&interrupt, [this]() {
        loop_.Submit(
            [this]() {
              if (!started_) {
                CHECK(!completed_ && !error_);
                completed_ = true;
                k_.Stop();
              } else if (!completed_) {
                CHECK(started_ && !error_);
                completed_ = true;

                for (auto& poll : polls_) {
                  if (uv_is_active((uv_handle_t*) poll)) {
                    uv_poll_stop(poll);
                  }
                  uv_close(
                      (uv_handle_t*) poll,
                      [](uv_handle_t* handle) {
                        delete handle;
                      });
                }
                polls_.clear();

                // We don't have to check uv_is_active for timer since
                // libuv checks it by itself.
                // Return value is always 0.
                uv_timer_stop(&timer_);
                uv_close(
                    (uv_handle_t*) &timer_,
                    [](uv_handle_t* handle) {
                      auto& continuation = *(Continuation*) handle->data;
                      continuation.closed_ = true;

                      continuation.k_.Stop();
                    });

                CHECK_EQ(
                    curl_multi_remove_handle(
                        multi_,
                        easy_),
                    CURLM_OK);

                if (fields_string_ != nullptr) {
                  curl_free(fields_string_);
                  fields_string_ = nullptr;
                }
                curl_easy_cleanup(easy_);
                curl_multi_cleanup(multi_);
              }
            },
            &interrupt_);
      });

      // NOTE: we always install the handler in case 'Start()'
      // never gets called.
      handler_->Install();
    }

   private:
    K_ k_;
    EventLoop& loop_;

    Request request_;

    // Stores converted PostFields as a C string.
    char* fields_string_ = nullptr;

    CURL* easy_ = nullptr;
    CURLM* multi_ = nullptr;

    uv_timer_t timer_;
    std::vector<uv_poll_t*> polls_;

    // Response variables.
    long code_ = 0;
    EventLoop::Buffer buffer_;

    bool started_ = false;
    bool completed_ = false;
    bool closed_ = false;

    int error_ = 0;

    EventLoop::Waiter start_;
    EventLoop::Waiter interrupt_;

    std::optional<Interrupt::Handler> handler_;
  };

  struct Composable {
    template <typename Arg>
    using ValueFrom = Response;

    template <typename Arg, typename K>
    auto k(K k) && {
      return Continuation<K>{std::move(k), loop_, std::move(request_)};
    }

    EventLoop& loop_;
    Request request_;
  };
};

////////////////////////////////////////////////////////////////////////

inline auto Client::Do(Request&& request) {
  // TODO(benh): need 'Client::Default()'.
  auto& loop = EventLoop::Default();

  if (verify_peer_.has_value()) {
    request.verify_peer_ = verify_peer_.value();
  }

  if (certificate_ && !request.certificate()) {
    request.certificate_ = certificate_;
  }

  // NOTE: we use a 'RescheduleAfter()' to ensure we use current
  // scheduling context to invoke the continuation after the transfer has
  // completed (or was interrupted).
  return RescheduleAfter(
      // TODO(benh): borrow '&loop' so http call can't outlive a loop.
      _HTTP::Composable{loop, std::move(request)});
}

////////////////////////////////////////////////////////////////////////

inline auto Client::Get(
    std::string&& uri,
    std::chrono::nanoseconds&& timeout) {
  return Do(
      Request::Builder()
          .uri(std::move(uri))
          .method(GET)
          .timeout(std::move(timeout))
          .Build());
}

////////////////////////////////////////////////////////////////////////

inline auto Client::Post(
    std::string&& uri,
    PostFields&& fields,
    std::chrono::nanoseconds&& timeout) {
  return Do(
      Request::Builder()
          .uri(std::move(uri))
          .method(POST)
          .timeout(std::move(timeout))
          .fields(std::move(fields))
          .Build());
}

////////////////////////////////////////////////////////////////////////

inline auto Get(
    std::string&& url,
    std::chrono::nanoseconds&& timeout = std::chrono::nanoseconds(0)) {
  return Client().Get(std::move(url), std::move(timeout));
}

////////////////////////////////////////////////////////////////////////

inline auto Post(
    std::string&& url,
    PostFields&& fields,
    std::chrono::nanoseconds&& timeout = std::chrono::nanoseconds(0)) {
  return Client().Post(std::move(url), std::move(fields), std::move(timeout));
}

////////////////////////////////////////////////////////////////////////

} // namespace http
} // namespace eventuals

////////////////////////////////////////////////////////////////////////
