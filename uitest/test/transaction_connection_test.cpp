/*
 * Copyright (c) 2021-2022 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "gtest/gtest.h"
#include "common_utilities_hpp.h"
#include "ipc_transactors.h"

using namespace OHOS::uitest;
using namespace std;

static constexpr uint64_t TIME_DIFF_TOLERANCE_MS = 2;

class DummyTransceiver : public MessageTransceiver {
public:
    bool Initialize() override
    {
        return true;
    }

    /** Set the function which performs emitting messages.*/
    void SetEmitter(function<void(const TransactionMessage &)> emitterFunc)
    {
        emitterFunc_ = emitterFunc;
    }

    void DoEmitMessage(const TransactionMessage &message) override
    {
        if (emitterFunc_ != nullptr) {
            emitterFunc_(message);
        }
    }

private:
    function<void(const TransactionMessage &)> emitterFunc_;
};

class MessageTransceiverTest : public testing::Test {
public:
protected:
    void TearDown() override
    {
        transceiver_.Finalize();
        if (asyncWork_.valid()) {
            asyncWork_.get();
        }
    }

    future<void> asyncWork_;
    DummyTransceiver transceiver_;
    static constexpr uint64_t pollTimeoutMs_ = 20;
};

TEST_F(MessageTransceiverTest, checkMessageContent)
{
    auto emitted = TransactionMessage {};
    transceiver_.SetEmitter([&emitted](const TransactionMessage &msg) {
        emitted.id_ = msg.id_; // Sniff the emitted message
        emitted.type_ = msg.type_;
        emitted.dataParcel_ = msg.dataParcel_;
    });

    transceiver_.EmitCall("call");
    ASSERT_EQ(TransactionType::CALL, emitted.type_);
    ASSERT_EQ("call", emitted.dataParcel_);

    emitted.id_ = 1234;
    transceiver_.EmitReply(emitted, "reply");
    ASSERT_EQ(TransactionType::REPLY, emitted.type_);
    ASSERT_EQ(1234, emitted.id_); // calling message_id should be kept in the reply
    ASSERT_EQ("reply", emitted.dataParcel_);

    transceiver_.EmitHandshake();
    ASSERT_EQ(TransactionType::HANDSHAKE, emitted.type_);

    emitted.id_ = 5678;
    transceiver_.EmitAck(emitted);
    ASSERT_EQ(TransactionType::ACK, emitted.type_);
    ASSERT_EQ(5678, emitted.id_); // handshake message_id should be kept in the ack
}

TEST_F(MessageTransceiverTest, enqueueDequeueMessage)
{
    auto message = TransactionMessage {};
    // case1: no message in queue, polling timeout, check status and delayed time
    uint64_t startMs = GetCurrentMillisecond();
    auto status = transceiver_.PollCallReply(message, pollTimeoutMs_);
    uint64_t endMs = GetCurrentMillisecond();
    ASSERT_EQ(MessageTransceiver::PollStatus::ABORT_WAIT_TIMEOUT, status);
    ASSERT_NEAR(pollTimeoutMs_, endMs - startMs, TIME_DIFF_TOLERANCE_MS) << "Incorrect polling time";
    // case2: message in queue, should return immediately
    auto tempMessage = TransactionMessage {.id_ = 1234, .type_ = CALL};
    transceiver_.OnReceiveMessage(tempMessage);
    startMs = GetCurrentMillisecond();
    status = transceiver_.PollCallReply(message, pollTimeoutMs_);
    endMs = GetCurrentMillisecond();
    ASSERT_EQ(MessageTransceiver::PollStatus::SUCCESS, status);
    ASSERT_NEAR(endMs, startMs, TIME_DIFF_TOLERANCE_MS) << "Should return immediately";
    ASSERT_EQ(1234, message.id_) << "Incorrect message content";
    // case3. message comes at sometime before timeout, should end polling and return it
    constexpr uint64_t delayMs = 10;
    asyncWork_ = async(launch::async, [this, delayMs, tempMessage]() -> void {
        this_thread::sleep_for(chrono::milliseconds(delayMs));
        this->transceiver_.OnReceiveMessage(tempMessage);
    });
    startMs = GetCurrentMillisecond();
    status = transceiver_.PollCallReply(message, pollTimeoutMs_);
    endMs = GetCurrentMillisecond();
    ASSERT_EQ(MessageTransceiver::PollStatus::SUCCESS, status);
    ASSERT_NEAR(endMs - startMs, delayMs, TIME_DIFF_TOLERANCE_MS) << "Should return soon after message enqueue";
}

TEST_F(MessageTransceiverTest, checkMessageFilter)
{
    auto message = TransactionMessage {.type_ = CALL};
    // without filter, message should be accepted
    transceiver_.OnReceiveMessage(message);
    auto status = transceiver_.PollCallReply(message, pollTimeoutMs_);
    ASSERT_EQ(MessageTransceiver::PollStatus::SUCCESS, status);
    auto filter = [](TransactionType type) -> bool { return type != TransactionType::CALL; };
    // message should be filtered out, poll will be timeout
    transceiver_.SetMessageFilter(filter);
    transceiver_.OnReceiveMessage(message);
    status = transceiver_.PollCallReply(message, pollTimeoutMs_);
    ASSERT_EQ(MessageTransceiver::PollStatus::ABORT_WAIT_TIMEOUT, status);
}

TEST_F(MessageTransceiverTest, checkAnwserHandshakeAtomatically)
{
    auto emitted = TransactionMessage {};
    transceiver_.SetEmitter([&emitted](const TransactionMessage &msg) {
        emitted.id_ = msg.id_; // Sniff the emitted message
        emitted.type_ = msg.type_;
        emitted.dataParcel_ = msg.dataParcel_;
    });
    auto handshake = TransactionMessage {.id_ = 1234, .type_ = HANDSHAKE};
    transceiver_.OnReceiveMessage(handshake);
    // should emit ack automatically on receiving handshake
    ASSERT_EQ(TransactionType::ACK, emitted.type_);
    ASSERT_EQ(handshake.id_, emitted.id_);
}

TEST_F(MessageTransceiverTest, immediateExitHandling)
{
    auto message = TransactionMessage {.type_ = TransactionType::EXIT};
    // EXIT-message comes at sometime before timeout, should end polling and return it
    constexpr uint64_t delayMs = 10;
    asyncWork_ = async(launch::async, [this, delayMs, message]() {
        this_thread::sleep_for(chrono::milliseconds(delayMs));
        this->transceiver_.OnReceiveMessage(message);
    });
    const uint64_t startMs = GetCurrentMillisecond();
    const auto status = transceiver_.PollCallReply(message, pollTimeoutMs_);
    const uint64_t endMs = GetCurrentMillisecond();
    ASSERT_EQ(MessageTransceiver::PollStatus::ABORT_REQUEST_EXIT, status);
    ASSERT_NEAR(endMs - startMs, delayMs, TIME_DIFF_TOLERANCE_MS) << "Should return soon after exit-request";
}

TEST_F(MessageTransceiverTest, immediateConnectionDiedHandling)
{
    transceiver_.ScheduleCheckConnection(false);
    // connection died before timeout, should end polling and return it
    auto message = TransactionMessage {};
    const uint64_t startMs = GetCurrentMillisecond();
    auto status = transceiver_.PollCallReply(message, WATCH_DOG_TIMEOUT_MS * 2);
    const uint64_t endMs = GetCurrentMillisecond();
    constexpr uint64_t toleranceMs = WATCH_DOG_TIMEOUT_MS / 100;
    ASSERT_EQ(MessageTransceiver::PollStatus::ABORT_CONNECTION_DIED, status);
    ASSERT_NEAR(endMs - startMs, WATCH_DOG_TIMEOUT_MS, toleranceMs) << "Should return soon after connection died";
}

TEST_F(MessageTransceiverTest, checkScheduleHandshake)
{
    transceiver_.ScheduleCheckConnection(false);
    // connection died before timeout, should end polling and return it
    auto message = TransactionMessage {};
    constexpr uint64_t handshakeDelayMs = 1000;
    asyncWork_ = async(launch::async, [this, handshakeDelayMs, message]() {
        this_thread::sleep_for(chrono::milliseconds(handshakeDelayMs));
        auto handshake = TransactionMessage {.type_ = TransactionType::HANDSHAKE};
        this->transceiver_.OnReceiveMessage(handshake);
    });
    const uint64_t startMs = GetCurrentMillisecond();
    const auto status = transceiver_.PollCallReply(message, WATCH_DOG_TIMEOUT_MS * 2);
    const uint64_t endMs = GetCurrentMillisecond();
    // since handshake comes at 1000th ms, connection should die at (1000+WATCH_DOG_TIMEOUT_MS)th ms
    constexpr uint64_t expectedConnDieMs = handshakeDelayMs + WATCH_DOG_TIMEOUT_MS;
    constexpr uint64_t toleranceMs = WATCH_DOG_TIMEOUT_MS / 100;
    ASSERT_EQ(MessageTransceiver::PollStatus::ABORT_CONNECTION_DIED, status);
    ASSERT_NEAR(endMs - startMs, expectedConnDieMs, toleranceMs) << "Incorrect time elapse";
}

TEST_F(MessageTransceiverTest, ensureConnected)
{
    constexpr uint64_t timeoutMs = 100;
    // give no incoming message, should be timed-out
    ASSERT_FALSE(transceiver_.DiscoverPeer(timeoutMs));
    // inject incoming message before timeout, should return true immediately
    static constexpr uint64_t incomingDelayMs = 60;
    asyncWork_ = async(launch::async, [this]() {
        this_thread::sleep_for(chrono::milliseconds(incomingDelayMs));
        auto message = TransactionMessage {.type_ = TransactionType::ACK};
        this->transceiver_.OnReceiveMessage(message);
    });
    const uint64_t startMs = GetCurrentMillisecond();
    ASSERT_TRUE(transceiver_.DiscoverPeer(timeoutMs));
    const uint64_t endMs = GetCurrentMillisecond();
    ASSERT_NEAR(startMs, endMs, incomingDelayMs + TIME_DIFF_TOLERANCE_MS); // check return immediately
}

class DummyServer : public TransactionServer {
public:
    MessageTransceiver *GetTransceiver()
    {
        return transceiver_.get();
    }

protected:
    unique_ptr<MessageTransceiver> CreateTransceiver() override
    {
        return make_unique<DummyTransceiver>();
    }
};

class DummyClient : public TransactionClient {
public:
    MessageTransceiver *GetTransceiver()
    {
        return transceiver_.get();
    }

protected:
    unique_ptr<MessageTransceiver> CreateTransceiver() override
    {
        return make_unique<DummyTransceiver>();
    }
};

class TransactionTest : public testing::Test {
protected:
    void SetUp() override
    {
        server_.SetCallFunction([](const ApiCallInfo& call, ApiReplyInfo& reply) {
            // nop
        });
        server_.Initialize();
        client_.Initialize();
    }

    void TearDown() override
    {
        server_.Finalize();
        if (serverAsyncWork_.valid()) {
            serverAsyncWork_.get();
        }
        if (clientAsyncWork_.valid()) {
            // do this to ensure asyncWork_ terminates normally
            auto terminate = TransactionMessage {.type_ = TransactionType::EXIT};
            client_.GetTransceiver()->OnReceiveMessage(terminate);
            clientAsyncWork_.get();
        }
        client_.Finalize();
    }

    future<uint32_t> serverAsyncWork_;
    future<ApiReplyInfo> clientAsyncWork_;
    DummyServer server_;
    DummyClient client_;
};

TEST_F(TransactionTest, checkApiTransaction)
{
    serverAsyncWork_ = async(launch::async, [this]() { return this->server_.RunLoop(); });
    server_.SetCallFunction([](const ApiCallInfo &call, ApiReplyInfo &reply) {
        reply.resultValue_ = call.apiId_ + "_ok";
    }); // set api-call processing method
    static constexpr size_t testSetSize = 3;
    const array<string, testSetSize> apis = {"yz", "zl", "lj"};
    const array<string, testSetSize> expectedResults = {"yz_ok", "zl_ok", "lj_ok"};
    // bridge this-end's emitted message to that-end's received message between client and server
    auto serverTrans = reinterpret_cast<DummyTransceiver *>(server_.GetTransceiver());
    auto clientTrans = reinterpret_cast<DummyTransceiver *>(client_.GetTransceiver());
    clientTrans->SetEmitter([serverTrans](const TransactionMessage &msg) { serverTrans->OnReceiveMessage(msg); });
    serverTrans->SetEmitter([clientTrans](const TransactionMessage &msg) { clientTrans->OnReceiveMessage(msg); });

    for (size_t idx = 0; idx < testSetSize; idx++) {
        // no wait reply, will just trigger emitting a call-message with serialized ApiCallInfo
        auto call = ApiCallInfo {.apiId_ = apis.at(idx)};
        auto reply = ApiReplyInfo();
        client_.InvokeApi(call, reply);
        // check the result, after a short interval (because the server runs async)
        this_thread::sleep_for(chrono::milliseconds(TIME_DIFF_TOLERANCE_MS));
        auto resultStr = reply.resultValue_.get<string>();
        ASSERT_EQ(expectedResults.at(idx), resultStr);
        ASSERT_EQ(ErrCode::NO_ERROR, reply.exception_.code_);
    }
    // request exit from client, should end loop immediately with success code at server end
    const uint64_t startMs = GetCurrentMillisecond();
    client_.Finalize();
    auto exitCode = serverAsyncWork_.get();
    const uint64_t endMs = GetCurrentMillisecond();
    ASSERT_EQ(0, exitCode);
    ASSERT_NEAR(startMs, endMs, TIME_DIFF_TOLERANCE_MS); // check exit immediately
}

TEST_F(TransactionTest, checkServerExitLoopWhenConnDied)
{
    // enable connection check and enter loop
    server_.GetTransceiver()->ScheduleCheckConnection(false);
    serverAsyncWork_ = async(launch::async, [this]() { return this->server_.RunLoop(); });
    // given no handshake, should end loop immediately with failure code after timeout
    const uint64_t startMs = GetCurrentMillisecond();
    auto exitCode = serverAsyncWork_.get();
    const uint64_t endMs = GetCurrentMillisecond();
    ASSERT_NE(0, exitCode);
    // check exit immediately after timeout
    ASSERT_NEAR(startMs, endMs, WATCH_DOG_TIMEOUT_MS * 1.02f);
}

TEST_F(TransactionTest, checkResultWhenConnectionDied)
{
    // enable connection check
    client_.GetTransceiver()->ScheduleCheckConnection(false);
    clientAsyncWork_ = async(launch::async, [this]() {
        auto call = ApiCallInfo {.apiId_ = "wyz"};
        auto reply = ApiReplyInfo();
        this->client_.InvokeApi(call, reply);
        return reply;
    });
    // trigger connection timeout by giving no incoming message, should return error result
    uint64_t startMs = GetCurrentMillisecond();
    auto reply = clientAsyncWork_.get();
    uint64_t endMs = GetCurrentMillisecond();
    ASSERT_EQ(ErrCode::INTERNAL_ERROR, reply.exception_.code_);
    ASSERT_NE(string::npos, reply.exception_.message_.find("connection with uitest_daemon is dead"));
    // check return immediately after timeout
    ASSERT_NEAR(startMs, endMs, WATCH_DOG_TIMEOUT_MS * 1.02f);
    // connection is already dead, should return immediately in later invocations
    clientAsyncWork_ = async(launch::async, [this]() {
        auto call = ApiCallInfo {.apiId_ = "zl"};
        auto reply = ApiReplyInfo();
        this->client_.InvokeApi(call, reply);
        return reply;
    });
    startMs = GetCurrentMillisecond();
    reply = clientAsyncWork_.get();
    endMs = GetCurrentMillisecond();
    ASSERT_EQ(ErrCode::INTERNAL_ERROR, reply.exception_.code_);
    ASSERT_NE(string::npos, reply.exception_.message_.find("connection with uitest_daemon is dead"));
    // check return immediately due-to dead connection
    ASSERT_NEAR(startMs, endMs, TIME_DIFF_TOLERANCE_MS);
}

TEST_F(TransactionTest, checkRejectConcurrentInvoke)
{
    clientAsyncWork_ = async(launch::async, [this]() {
        auto call = ApiCallInfo {.apiId_ = "zl"};
        auto reply = ApiReplyInfo();
        this->client_.InvokeApi(call, reply);
        return reply;
    });
    // give a short delay to ensure concurrence
    this_thread::sleep_for(chrono::milliseconds(TIME_DIFF_TOLERANCE_MS));
    uint64_t startMs = GetCurrentMillisecond();
    auto call1 = ApiCallInfo {.apiId_ = "zl"};
    auto reply1 = ApiReplyInfo();
    client_.InvokeApi(call1, reply1);
    uint64_t endMs = GetCurrentMillisecond();
    // the second call should return immediately and reject the concurrent invoke
    ASSERT_EQ(reply1.exception_.code_, ErrCode::USAGE_ERROR);
    ASSERT_NE(string::npos, reply1.exception_.message_.find("uitest-api dose not allow calling concurrently"));
    ASSERT_NEAR(startMs, endMs, TIME_DIFF_TOLERANCE_MS);
}

TEST_F(TransactionTest, checkResultAfterFinalized)
{
    client_.Finalize();
    auto call = ApiCallInfo {.apiId_ = "zl"};
    auto reply = ApiReplyInfo();
    uint64_t startMs = GetCurrentMillisecond();
    client_.InvokeApi(call, reply);
    uint64_t endMs = GetCurrentMillisecond();
    // the second call should return immediately and reject the invoke
    ASSERT_EQ(ErrCode::INTERNAL_ERROR, reply.exception_.code_);
    ASSERT_NE(string::npos, reply.exception_.message_.find("connection with uitest_daemon is dead"));
    ASSERT_NEAR(startMs, endMs, TIME_DIFF_TOLERANCE_MS);
}
