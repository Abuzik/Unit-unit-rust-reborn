//
// Created by Kirill Zhukov on 11.11.2022.
//

#ifndef UNIT_RPCFILTERCHAIN_H
#define UNIT_RPCFILTERCHAIN_H
#include <utility>

#include "tuple"
#include "iostream"
#include "boost/json.hpp"
#include "boost/json/src.hpp"
#include "RpcExceptions.h"
#include "../libdevcore/datastructures/blockchain/transaction/ValidTransaction.h"
#include "../libdevcore/datastructures/blockchain/block/Block.h"

inline void create_success_response(const std::string &message, http::response<http::dynamic_body> *response_) {
    response_->result(http::status::ok);
    response_->set(http::field::content_type, "application/json");
    response_->set(http::field::server, "unit");
    beast::ostream(response_->body()) << message;
}

class RpcFilterChain {
public:
    RpcFilterChain() = default;
    RpcFilterChain *add(RpcFilterChain *n);
    RpcFilterChain *setNext(RpcFilterChain *n);
    virtual void filter(boost::json::value *json);
private:
    RpcFilterChain *next;
};

RpcFilterChain *RpcFilterChain::add(RpcFilterChain *n) {
    if (this->next) this->next->add(n);
    else next = n;
    return this;
}

RpcFilterChain *RpcFilterChain::setNext(RpcFilterChain *n) {
    this->next = n;
    return this;
}

void RpcFilterChain::filter(boost::json::value *json) {
    if (this->next) this->next->filter(json);
}

class BasicTransactionRpcFilter : public RpcFilterChain {
public:
    BasicTransactionRpcFilter(dbProvider::BatchProvider *userProvider, http::response<http::dynamic_body> *response, std::shared_ptr<unit::list<ValidTransaction>>  txDeque)
            : userProvider(userProvider), response(response), validTxDeque(std::move(txDeque)) {}
    void filter(boost::json::value *parameter) override;
protected:
    dbProvider::BatchProvider *userProvider;
    http::response<http::dynamic_body> *response;
    std::shared_ptr<unit::list<ValidTransaction>> validTxDeque;
};

void BasicTransactionRpcFilter::filter(boost::json::value *parameter) {
    std::shared_ptr<RawTransaction> rawTransaction = RawTransaction::parse(parameter);
    auto sender = boost::json::value_to<std::string>(parameter->at("from"));
    operationDBStatus::DBResponse<std::string> dbResponse = this->userProvider->read<std::string>(&sender);
    if (dbResponse.error && ((int) dbResponse.errorResponse) == 2) {
        create_error_response(rpcError::emptyBalanceError, this->response);
        throw RpcDefaultException();
    }
    std::optional<std::shared_ptr<WalletAccount>> opAccount = WalletAccount::parseWallet(dbResponse.value);
    if (!opAccount.has_value()) {
        create_error_response(rpcError::defaultAccountError, this->response);
        throw RpcEmptyBalanceException();
    }
    std::shared_ptr<WalletAccount> account = opAccount.value();
    if (account->nonce == parameter->at("nonce")) {
        create_error_response(rpcError::badNonce, this->response);
        throw RpcBadNonceException();
    }
    if (!ecdsa_verify_signature(boost::json::value_to<std::string>(parameter->at("r")), boost::json::value_to<std::string>(parameter->at("s")),
                                *rawTransaction->serializeWithoutSignatures(), boost::json::value_to<std::string>(parameter->at("from")))) {
        create_error_response(rpcError::invalidSignature, this->response);
        throw RpcInvalidSignatureException();
    }
    if (0 == boost::json::value_to<int>(parameter->at("type")) && (account->compareNativeTokenBalance(parameter->at("amount")) < 0)) {
        create_error_response(rpcError::lowBalance, this->response);
        throw RpcLowBalanceException();
    }
    if (1 == boost::json::value_to<int>(parameter->at("type")) && (account->compareTokenBalance(parameter->at("amount"), parameter->at("extradata").at("name")) < 0)) {
        create_error_response(rpcError::lowBalance, this->response);
        throw RpcLowBalanceException();
    }
    RpcFilterChain::filter(parameter);
    rawTransaction->generateHash();
    this->validTxDeque->push_back(ValidTransaction(rawTransaction.get()));
    create_success_response(rpcResponse::processSimpleResponse(rawTransaction->hash, boost::json::value_to<std::string>(parameter->at("id"))), this->response);
}

/// Used for process balance request
class BasicBalanceFilter : public RpcFilterChain {
public:
    BasicBalanceFilter(dbProvider::BatchProvider *userProvider, http::response<http::dynamic_body> *response) : userProvider(userProvider), response(response) {}
    void filter(boost::json::value *parameter) override;
protected:
    dbProvider::BatchProvider *userProvider;
    http::response<http::dynamic_body> *response;
};

void BasicBalanceFilter::filter(boost::json::value *parameter) {
    if (!parameter->as_object().contains("params")) throw RpcInvalidParameterException();
    auto param = boost::json::value_to<std::string>(parameter->at("params"));
    operationDBStatus::DBResponse<std::string> value = this->userProvider->read<std::string>(&param);
    if (value.error) (operationDBStatus::DBCode::cNotFound == value.errorResponse) ? throw RpcEmptyBalanceException() : throw RpcDefaultException();
    create_success_response(rpcResponse::processSimpleResponse(*value.value, boost::json::value_to<std::string>(parameter->at("id"))), this->response);
}

/// Used for process pool size request
class BasicPoolFilter : public RpcFilterChain {
public:
    BasicPoolFilter(http::response<http::dynamic_body> *response, std::shared_ptr<unit::list<ValidTransaction>>  txDeque) : response(response), validTxDeque(std::move(txDeque)) {}
    void filter(boost::json::value *parameter) override;
protected:
    http::response<http::dynamic_body> *response;
    std::shared_ptr<unit::list<ValidTransaction>> validTxDeque;
};

void BasicPoolFilter::filter(boost::json::value *parameter) {
    create_success_response(rpcResponse::processSimpleResponse(std::to_string(this->validTxDeque->size()), boost::json::value_to<std::string>(parameter->at("id"))), this->response);
}

/// Used for process balance history request
class BasicBalanceHistoryFilter : public RpcFilterChain {
public:
    BasicBalanceHistoryFilter(http::response<http::dynamic_body> *response, dbProvider::BatchProvider *historyDB) : response(response), historyDB(historyDB) {}
    void filter(boost::json::value *parameter) override;
protected:
    http::response<http::dynamic_body> *response;
    dbProvider::BatchProvider *historyDB;
};

void BasicBalanceHistoryFilter::filter(boost::json::value *parameter) {
    if (!parameter->as_object().contains("params")) throw RpcInvalidParameterException();
    auto sender = boost::json::value_to<std::string>(parameter->at("params"));
    operationDBStatus::DBResponse<std::string> dbResponse = this->historyDB->read<std::string>(&sender);
    if (dbResponse.error && ((int) dbResponse.errorResponse) == 2) {
        create_error_response(rpcError::emptyBalanceError, this->response);
        throw RpcDefaultException();
    }
    create_success_response(rpcResponse::processSimpleResponse(*dbResponse.value, boost::json::value_to<std::string>(parameter->at("id"))), this->response);
}

/// Used for process current block request
class BasicBlockHeightFilter : public RpcFilterChain {
public:
    BasicBlockHeightFilter(http::response<http::dynamic_body> *response, Block *last) : response(response), last(last) {}
    void filter(boost::json::value *parameter) override;
protected:
    http::response<http::dynamic_body> *response;
    Block *last;
};

void BasicBlockHeightFilter::filter(boost::json::value *parameter) {
    create_success_response(rpcResponse::processSimpleResponse(this->last->serializeBlock(), boost::json::value_to<std::string>(parameter->at("id"))), this->response);
}

/// Used for process transaction by hash request
class BasicTransactionByHashRpcFilter : public RpcFilterChain {
public:
    BasicTransactionByHashRpcFilter(http::response<http::dynamic_body> *response, dbProvider::BatchProvider *transactionDB) : response(response), transactionDB(transactionDB) {}
    void filter(boost::json::value *parameter) override;
protected:
    http::response<http::dynamic_body> *response;
    dbProvider::BatchProvider *transactionDB;
};

void BasicTransactionByHashRpcFilter::filter(boost::json::value *parameter) {
    if (!parameter->as_object().contains("params")) throw RpcInvalidParameterException();
    auto sender = boost::json::value_to<std::string>(parameter->at("params"));
    operationDBStatus::DBResponse<std::string> dbResponse = this->transactionDB->read<std::string>(&sender);
    if (dbResponse.error && ((int) dbResponse.errorResponse) == 2) {
        create_error_response(rpcError::emptyBalanceError, this->response);
        throw RpcDefaultException();
    }
    create_success_response(rpcResponse::processSimpleResponse(*dbResponse.value, boost::json::value_to<std::string>(parameter->at("id"))), this->response);
}

#endif //UNIT_RPCFILTERCHAIN_H