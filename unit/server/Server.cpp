//
// Created by Gosha Stolyarov on 28.07.2022.
//

#include <chrono>
#include <ctime>
#include <iostream>
#include <memory>
#include <utility>

#include "Server.h"

namespace beast = boost::beast;   // from <boost/beast.hpp>
namespace http = beast::http;     // from <boost/beast/http.hpp>
namespace net = boost::asio;      // from <boost/asio.hpp>
using tcp = boost::asio::ip::tcp; // from <boost/asio/ip/tcp.hpp>

namespace my_program_state {
    std::size_t request_count() {
        static std::size_t count = 0;
        return ++count;
    }
    std::time_t now() {
        return std::time(0);
    }
}

class http_connection : public std::enable_shared_from_this<http_connection> {
public:
    http_connection(tcp::socket socket) : socket_(std::move(socket)){}
    http_connection(tcp::socket socket, const std::shared_ptr<std::string>& path) : socket_(std::move(socket)), userProvider(*path){}
    // Initiate the asynchronous operations associated with the connection.
    void start(unit::list<ValidTransaction> *txDeque) {
        this->txDeque = std::shared_ptr<unit::list<ValidTransaction>>(txDeque);
        read_request();
        check_deadline();
    }

private:
    // database provider for users accounts
    dbProvider::BatchProvider userProvider;
    // pointer to tx deque
    std::shared_ptr<unit::list<ValidTransaction>> txDeque;
    // The socket for the currently connected client.
    tcp::socket socket_;
    // The buffer for performing reads.
    beast::flat_buffer buffer_{8192};
    // The request message.
    http::request<http::string_body> request_;
    // The response message.
    http::response<http::dynamic_body> response_;
    // The timer for putting a deadline on connection processing.
    net::steady_timer deadline_{socket_.get_executor(), std::chrono::seconds(60)};
    // Asynchronously receive a complete request message.
    /// const errors
    static constexpr char httpMethodError[106] = R"({"jsonrpc": "2.0", "error": {"code": -32003, "message": "HTTP request method not supported"}, "id": null})";
    static constexpr char parsingError[96] = R"({"jsonrpc": "2.0", "error": {"code": -32700, "message": "failed to parse request"}, "id": null})";
    static constexpr char emptyBalanceError[86] = R"({"jsonrpc": "2.0", "error": {"code": -32002, "message": "empty balance"}, "id": null})";
    static constexpr char defaultError[93] = R"({"jsonrpc": "2.0", "error": {"code": -32004, "message": "something went wrong"}, "id": null})";
    static constexpr char defaultAccountError[110] = R"({"jsonrpc": "2.0", "error": {"code": -32004, "message": "something happening with your account"}, "id": null})";
    static constexpr char badNonce[114] = R"({"jsonrpc": "2.0", "error": {"code": -32005, "message": "old nonce is used for current transaction"}, "id": null})";
    static constexpr char invalidSignature[90] = R"({"jsonrpc": "2.0", "error": {"code": -32006, "message": "invalid signature"}, "id": null})";
    static constexpr char invalidMethod[89] = R"({"jsonrpc": "2.0", "error": {"code": -32001, "message": "method not found"}, "id": null})";


    inline void read_request() {
        auto self = shared_from_this();
        http::async_read(
                socket_,
                buffer_,
                request_,
                [self](beast::error_code ec, std::size_t bytes_transferred){
                    boost::ignore_unused(bytes_transferred);
                    if (!ec) self->process_request();
                });
    }

    /*request*/
    inline void process_request() {
        response_.version(request_.version());
        response_.keep_alive(true);

        // featured json body
        boost::json::value body_to_json;
        boost::json::error_code ec;
        switch (request_.method()) {
            case http::verb::get:
                create_error_response(http_connection::httpMethodError);
                break;
            case http::verb::post:
                body_to_json = boost::json::parse(request_.body(), ec);
                if (ec.failed()) {
                    create_error_response(http_connection::parsingError);
                    break;
                }
                process_instruction(body_to_json);
                break;
            default:
                create_error_response(http_connection::httpMethodError);
                break;
        }
        write_response();
    }

    /* RESPONSES */
    /*-----------*/
    inline void create_error_response(const std::string& message = R"({"message":"Error"})", bool isJSON = true) {
        response_.result(http::status::bad_request);
        response_.set(http::field::content_type, (isJSON ? "application/json" : "text/plain"));
        response_.set(http::field::server, "Unit");
        beast::ostream(response_.body()) << message;
    }

    inline void create_success_response(const std::string& message = R"({"message":"Ok"})", bool isJSON = true) {
        response_.result(http::status::ok);
        response_.set(http::field::content_type, (isJSON ? "application/json" : "text/plain"));
        response_.set(http::field::server, "unit");
        beast::ostream(response_.body()) << message;
    }

    inline void write_response() {
        auto self = shared_from_this();
        response_.content_length(response_.body().size());
        http::async_write(
                socket_,
                response_,
                [self](beast::error_code ec, std::size_t){
                    self->socket_.shutdown(tcp::socket::shutdown_send, ec);
                    self->deadline_.cancel();
                });
    }
    inline void check_deadline() {
        auto self = shared_from_this();
        deadline_.async_wait(
                [self](beast::error_code ec){
                    if (!ec) {
                        self->socket_.close(ec);
                    }
                });
    }
    /* END OF RESPONSES */
    /*------------------*/

    /*INSTRUCTIONS*/
    /*------------*/
    inline void process_instruction(const boost::json::value& json) {
//        try {
//            std::string instruction;
//            instruction = boost::json::value_to<std::string>(json.at("instruction"));
//            if (instruction == "i_balance") {
//                i_balance(json);
//            } else if (instruction == "i_push_transaction") {
//                try {
//                    i_push_transaction(json);
//                } catch (std::exception &e) {
//                    create_error_response(R"({"message":"Invalid data"})");
//                    return;
//                }
//            } else if (instruction == "i_block_height") {
//                i_block_height();
//            } else if (instruction == "i_tx") {
//                i_tx(json);
//            } else if (instruction == "i_pool_size") {
//                i_pool_size();
//            } else {
//                create_error_response(R"({"message":"Instruction not found"})");
//            }
//        } catch (const boost::wrapexcept<std::out_of_range> &o) {
//            create_error_response(R"({"message":"Instruction field not found"})");
//        }
        try {
            RpcMethodHandler rpcMethodHandler;
            auto method = boost::json::value_to<std::string>(json.at("method"));
            if ("transfer" == method) {
                rpcMethodHandler = RpcMethodHandler(std::make_shared<TransferMethod>(TransferMethod()).get());
                std::shared_ptr<boost::json::value> validating = rpcMethodHandler.executeValidating(
                        std::make_shared<boost::json::value>(json.at("params")).get());
                if (validating != nullptr) {
                    create_error_response(boost::json::value_to<std::string>(*validating), true);
                } else {
                    boost::json::value parameters = json.at("params");
                    RpcFilterBuilder rpcFilterBuilder = RpcFilterBuilder();
                    if (std::get<0>(rpcFilterBuilder.setParameter(std::make_shared<boost::json::value>(parameters))
                    ->setFilter(std::make_shared<BasicTransactionFilter>(BasicTransactionFilter(&(this->userProvider),&(this->response_))))->build())) {
                        throw;
                    }
                    // need to send success request and check logic again
                    //
                    //
                    //          change to doFilterInternal in the future!!!
                    //
                    //
                    //
                    //
                }
            } else if ("unit_get_balance" == method) {
                // implement balance method
            } else if ("unit_get_tx_pool_size" == method) {
                // implement balance method
            } else if ("unit_get_address_tx_history" == method) {
                // implement balance method
            } else if ("unit_get_block_height" == method) {
                // implement balance method
            } else if ("unit_get_tx" == method) {
                // implement balance method
            } else {
                create_error_response(rpcError::invalidMethod, true);
            }
        } catch (const boost::wrapexcept<std::out_of_range> &o) {

        }
    }
};

// "Loop" forever accepting new connections.
void http_server(tcp::acceptor &acceptor, tcp::socket &socket, unit::list<ValidTransaction> *tx_deque, const std::shared_ptr<std::string>& path) {
    acceptor.async_accept(socket,
                          [&, tx_deque](beast::error_code ec){
                              if (!ec) std::make_shared<http_connection>(std::move(socket))->start(tx_deque);
                              http_server(acceptor, socket, tx_deque, path);
                          });
}

int Server::start_server(unit::list<ValidTransaction> *tx_deque, const std::shared_ptr<std::string>& path) {
    rerun_server:{};
    try {
        std::string ip_address = LOCAL_IP;
        auto const address = net::ip::make_address(ip_address);
        uint16_t port = PORT;
        net::io_context ioc{1};
        tcp::acceptor acceptor{ioc, {address, port}};
        tcp::socket socket{ioc};
        http_server(acceptor, socket, tx_deque, path);
        std::cout << "server has been started" << std::endl;
        ioc.run();
    } catch (std::exception const &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        goto rerun_server;
    }
    return 0;
}