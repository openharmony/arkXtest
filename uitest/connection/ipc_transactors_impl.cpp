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

#include <cinttypes>
#include "common_utilities_hpp.h"
#include "ipc_transactors_impl.h"

namespace OHOS::uitest {
    using namespace std;
    using namespace OHOS::EventFwk;
    static constexpr string_view ACTION_CALL = "uitest.api.transaction.call_";
    static constexpr string_view ACTION_REPLY = "uitest.api.transaction.reply_";

    class CommonEventForwarder : public CommonEventSubscriber {
    public:
        explicit CommonEventForwarder(const CommonEventSubscribeInfo &info, function<void(const CommonEventData &)>
        processor) : CommonEventSubscriber(info), processor_(move(processor)) {}

        virtual ~CommonEventForwarder() {}

        void OnReceiveEvent(const CommonEventData &data) override
        {
            if (processor_ != nullptr) {
                processor_(data);
            }
        }

    private:
        const function<void(const CommonEventData &)> processor_;
    };

    TransactionTransceiverImpl::TransactionTransceiverImpl(string_view token, bool asServer)
        : asServer_(asServer), token_(token) {}

    bool TransactionTransceiverImpl::Initialize()
    {
        const auto event = string(asServer_ ? ACTION_CALL : ACTION_REPLY) + token_;
        MatchingSkills matchingSkills;
        matchingSkills.AddEvent(event);
        CommonEventSubscribeInfo info(matchingSkills);
        subscriber_ = make_shared<CommonEventForwarder>(info, [this](const CommonEventData &event) {
            auto &want = event.GetWant();
            auto message = TransactionMessage {};
            message.id_ = (uint32_t)want.GetLongParam("id", 0);
            message.type_ = static_cast<TransactionType>(want.GetIntParam("type", TransactionType::INVALID));
            message.dataParcel_ = want.GetStringParam("data");
            this->OnReceiveMessage(message);
        });
        if (subscriber_ == nullptr) {
            return false;
        }
        return CommonEventManager::SubscribeCommonEvent(subscriber_);
    }

    void TransactionTransceiverImpl::Finalize()
    {
        MessageTransceiver::Finalize();

        if (subscriber_ != nullptr) {
            CommonEventManager::UnSubscribeCommonEvent(this->subscriber_);
        }
    }

    void TransactionTransceiverImpl::DoEmitMessage(const TransactionMessage &message)
    {
        Want want;
        want.SetAction(string(asServer_ ? ACTION_REPLY : ACTION_CALL) + token_);
        CommonEventData event;
        want.SetParam("id", (long) (message.id_));
        want.SetParam("type", message.type_);
        want.SetParam("data", message.dataParcel_);
        event.SetWant(want);
        CommonEventManager::PublishCommonEvent(event);
    }

    TransactionServerImpl::TransactionServerImpl(string_view token) : token_(token) {};

    TransactionServerImpl::~TransactionServerImpl() {};

    bool TransactionServerImpl::Initialize()
    {
        if (!Transactor::Initialize()) {
            return false;
        }
        // schedule connection-checking on initialization
        transceiver_->ScheduleCheckConnection(false);
        return true;
    }

    unique_ptr<MessageTransceiver> TransactionServerImpl::CreateTransceiver()
    {
        return make_unique<TransactionTransceiverImpl>(token_, true);
    }

    TransactionClientImpl::TransactionClientImpl(string_view token) : token_(token) {};

    TransactionClientImpl::~TransactionClientImpl() {};

    static constexpr uint64_t WAIT_CONNECTION_TIMEOUT_MS = 5000;

    bool TransactionClientImpl::Initialize()
    {
        if (!Transactor::Initialize()) {
            return false;
        }
        // emit handshake and wait-for first interaction established
        LOG_I("Start checking CS-interaction");
        if (!transceiver_->DiscoverPeer(WAIT_CONNECTION_TIMEOUT_MS)) {
            LOG_E("Wait CS-interaction timed out in %{public}" PRIu64 "  ms", WAIT_CONNECTION_TIMEOUT_MS);
            return false;
        }
        // schedule connection-checking with auto-handshaking
        transceiver_->ScheduleCheckConnection(true);
        LOG_I("Check CS-interaction succeed");
        return true;
    }

    unique_ptr<MessageTransceiver> TransactionClientImpl::CreateTransceiver()
    {
        return make_unique<TransactionTransceiverImpl>(token_, false);
    }

    static unique_ptr<TransactionClientImpl> sClient = nullptr;
    static atomic<bool> g_sSetupCalled = false;

    /**Exported transaction-client initialization callback function.*/
    bool SetupTransactionEnv(string_view token)
    {
        if (!g_sSetupCalled.load()) {
            if (sClient == nullptr) {
                sClient = make_unique<TransactionClientImpl>(token);
            }
            if (!sClient->Initialize()) {
                LOG_E("SetupTransactionEnv failed");
            }
            g_sSetupCalled.store(true);
        }
        return true;
    }

    /**Exported transaction client api-calling function.*/
    void TransactionClientFunc(const ApiCallInfo& call, ApiReplyInfo& reply)
    {
        DCHECK(sClient != nullptr && g_sSetupCalled.load());
        sClient->InvokeApi(call, reply);
    }

    /**Exported transaction-client dispose callback function.*/
    void DisposeTransactionEnv()
    {
        if (g_sSetupCalled.load() && sClient != nullptr) {
            sClient->Finalize();
            g_sSetupCalled.store(false);
        }
    }
}
