#pragma once

#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>

#include "or_throw.h"
#include "generic_stream.h"
#include "util/signal.h"
#include "util/timeout.h"
#include "util/yield.h"
#include "util/watch_dog.h"
#include "util.h"
#include "connect_to_host.h"
#include "ssl/util.h"
#include "http_util.h"

namespace ouinet {

// Send the HTTP request `req` over the connection `con`
// (which may be already an SSL tunnel)
// *as is* and return the HTTP response or just its head
// depending on the expected `ResponseType`.
template<class ResponseBodyType, class RequestType>
inline
http::response<ResponseBodyType>
fetch_http( asio::io_service& ios
          , GenericStream& con
          , RequestType req
          , Signal<void()>& abort_signal
          , Yield yield_)
{
    Yield yield = yield_.tag("fetch_http");

    http::response<ResponseBodyType> res;

    auto slot = abort_signal.connect([&con] { con.close(); });

    sys::error_code ec;

    // Send the HTTP request to the remote host
    http::async_write(con, req, yield[ec]);

    if (ec) {
        yield.log("Failed to http::async_write ", ec.message());
    }

    // Ignore end_of_stream error, there may still be data in
    // the receive buffer we can read.
    if (ec == http::error::end_of_stream) {
        ec = sys::error_code();
    }

    if (ec) return or_throw(yield, ec, move(res));

    beast::flat_buffer buffer;

    // Receive the HTTP response
    _recv_http_response(con, buffer, res, yield[ec]);

    if (ec) {
        yield.log("Failed to http::async_read ", ec.message());
    }

    return or_throw(yield, ec, move(res));
}

template<class ResponseBodyType, class Duration, class RequestType>
inline
http::response<ResponseBodyType>
fetch_http( asio::io_service& ios
          , GenericStream& con
          , RequestType req
          , Duration timeout
          , Signal<void()>& abort_signal
          , Yield yield)
{
    return util::with_timeout
        ( ios
        , abort_signal
        , timeout
        , [&] (auto& abort_signal, auto yield) {
              return fetch_http<ResponseBodyType>
                (ios, con, req, abort_signal, yield);
          }
        , yield);
}

inline
void
_recv_http_response( GenericStream& con
                   , beast::flat_buffer& buffer
                   , http::response<http::dynamic_body>& res
                   , asio::yield_context yield)
{
    http::async_read(con, buffer, res, yield);
}

inline
void
_recv_http_response( GenericStream& con
                   , beast::flat_buffer& buffer
                   , http::response<http::empty_body>& res
                   , asio::yield_context yield)
{
    http::response_parser<http::empty_body> crph;
    http::async_read_header(con, buffer, crph, yield);
    res = move(crph.get());
}

template<class RequestType>
GenericStream
maybe_perform_ssl_handshake( GenericStream&& con
                           , asio::ssl::context& ssl_ctx
                           , const util::url_match& url
                           , RequestType req
                           , Signal<void()>& abort_signal
                           , Yield yield)
{
    using namespace std;

    sys::error_code ec;

    if (url.scheme == "https") {
        auto ret = ssl::util::client_handshake( move(con)
                                              , ssl_ctx
                                              , url.host
                                              , abort_signal
                                              , yield[ec]);

        if (ec) {
            yield.log("SSL client handshake error: "
                 , url.host, ": ", ec.message());
        }

        return or_throw(yield, ec, move(ret));
    }

    return move(con);
}

// Transform request from absolute-form to origin-form
// https://tools.ietf.org/html/rfc7230#section-5.3
template<class Request>
Request req_form_from_absolute_to_origin(const Request& absolute_req)
{
    // Parse the URL to tell HTTP/HTTPS, host, port.
    util::url_match url;

    auto absolute_target = absolute_req.target();

    if (!util::match_http_url(absolute_target, url)) {
        assert(0 && "Failed to parse url");
        return absolute_req;
    }

    Request origin_req(absolute_req);

    origin_req.target(absolute_target.substr(
                absolute_target.find( url.path
                                    // Length of "http://" or "https://",
                                    // do not fail on "http(s)://FOO/FOO".
                                    , url.scheme.length() + 3)));

    return origin_req;
}

template<class RequestType>
http::response<http::dynamic_body>
fetch_http_origin( asio::io_service& ios
                 , GenericStream& con_
                 , asio::ssl::context& ssl_ctx
                 , const util::url_match& url
                 , RequestType req
                 , Signal<void()>& abort_signal
                 , Yield yield)
{
    using namespace std;
    using Response = http::response<http::dynamic_body>;

    sys::error_code ec;

    auto con = maybe_perform_ssl_handshake( move(con_)
                                          , ssl_ctx
                                          , url
                                          , req
                                          , abort_signal
                                          , yield[ec]);
    if (ec) {
        return or_throw<Response>(yield, ec);
    }

    req = req_form_from_absolute_to_origin(req);

    return fetch_http<http::dynamic_body>(ios, con, req, abort_signal, yield);
}

template<class Duration, class RequestType>
http::response<http::dynamic_body>
fetch_http_origin( asio::io_service& ios
                 , GenericStream& con
                 , asio::ssl::context& ssl_ctx
                 , const util::url_match& url
                 , RequestType req
                 , Duration timeout
                 , Signal<void()>& abort_signal
                 , Yield yield)
{
    return util::with_timeout
        ( ios
        , abort_signal
        , timeout
        , [&] (auto& abort_signal, auto yield) {
              return fetch_http_origin
                (ios, con, ssl_ctx, url, req, abort_signal, yield);
          }
        , yield);
}

} // namespace
