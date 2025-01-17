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

#include <utility>

#include "gtest/gtest.h"
#include "frontend_api_handler.h"
#include "widget_selector.h"

using namespace OHOS::uitest;
using namespace std;
using namespace nlohmann;

// test fixture
class FrontendApiHandlerTest : public testing::Test {
public:
    ~FrontendApiHandlerTest() override = default;
protected:
    void TearDown() override
    {
        // common-preprocessors works on all apiCall, delete after testing
        FrontendApiServer::Get().RemoveCommonPreprocessor("dummyProcessor");
    }
};

TEST_F(FrontendApiHandlerTest, basicJsonReadWrite)
{
    auto container = json::array();
    // write
    container.emplace_back("wyz");
    container.emplace_back(1);
    container.emplace_back(0.1);
    container.emplace_back(true);
    container.emplace_back(nullptr);
    // read
    ASSERT_EQ(5, container.size());
    ASSERT_EQ("wyz", container.at(INDEX_ZERO).get<string>());
    ASSERT_EQ(1, container.at(INDEX_ONE).get<uint8_t>());
    ASSERT_NEAR(0.1, container.at(INDEX_TWO).get<float>(), 1E-6);
    ASSERT_EQ(true, container.at(INDEX_THREE).get<bool>());
    ASSERT_EQ(nlohmann::detail::value_t::null, container.at(INDEX_FOUR).type());
}

static string GenerateUniqueId()
{
    return to_string(GetCurrentMicroseconds());
}

TEST_F(FrontendApiHandlerTest, noInvocationHandler)
{
    static auto apiId = GenerateUniqueId();
    auto call = ApiCallInfo {.apiId_ = "wyz"};
    auto reply = ApiReplyInfo();
    FrontendApiServer::Get().Call(call, reply);
    ASSERT_EQ(INTERNAL_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.exception_.message_.find("No handler found") != string::npos);
}

TEST_F(FrontendApiHandlerTest, addRemoveHandler)
{
    static auto apiId = GenerateUniqueId();
    auto &server = FrontendApiServer::Get();
    ASSERT_FALSE(server.HasHandlerFor(apiId));
    auto handler = [](const ApiCallInfo &in, ApiReplyInfo &out) {
        // nop
    };
    server.AddHandler(apiId, handler);
    ASSERT_TRUE(server.HasHandlerFor(apiId));

    json caller;
    auto call = ApiCallInfo {.apiId_ = apiId};
    auto reply = ApiReplyInfo();
    server.Call(call, reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);

    server.RemoveHandler(apiId);
    ASSERT_FALSE(server.HasHandlerFor(apiId));
    server.Call(call, reply);
    ASSERT_EQ(INTERNAL_ERROR, reply.exception_.code_) << "The handler should be unavailable after been removed";
}

TEST_F(FrontendApiHandlerTest, inOutDataTransfer)
{
    static auto apiId = GenerateUniqueId();
    auto &server = FrontendApiServer::Get();
    auto handler = [](const ApiCallInfo &in, ApiReplyInfo &out) {
        out.resultValue_ = in.paramList_.at(0).get<string>().length() + in.paramList_.at(1).get<uint8_t>();
    };
    server.AddHandler(apiId, handler);

    auto call = ApiCallInfo {.apiId_ = apiId};
    auto reply = ApiReplyInfo();
    call.paramList_.emplace_back("wyz");
    call.paramList_.emplace_back(10);
    server.Call(call, reply);
    ASSERT_EQ(NO_ERROR, reply.exception_.code_);
    ASSERT_EQ(string("wyz").length() + 10, reply.resultValue_.get<uint8_t>());
}

TEST_F(FrontendApiHandlerTest, jsonExceptionDefance)
{
    static auto apiId = GenerateUniqueId();
    auto &server = FrontendApiServer::Get();
    auto handler = [](const ApiCallInfo &in, ApiReplyInfo &out) {
        out.resultValue_ = in.paramList_.at(100).get<uint8_t>();
    };
    server.AddHandler(apiId, handler);

    auto call = ApiCallInfo {.apiId_ = apiId};
    auto reply = ApiReplyInfo();
    server.Call(call, reply);
    // json exception should be caught and reported properly
    ASSERT_EQ(INTERNAL_ERROR, reply.exception_.code_);
    ASSERT_TRUE(reply.exception_.message_.find("json.exception.out_of_range") != string::npos);
}

TEST_F(FrontendApiHandlerTest, apiErrorDeliver)
{
    static auto apiId = GenerateUniqueId();
    auto &server = FrontendApiServer::Get();
    auto handler = [](const ApiCallInfo &in, ApiReplyInfo &out) { out.exception_.code_ = ErrCode::USAGE_ERROR; };
    server.AddHandler(apiId, handler);

    auto call = ApiCallInfo {.apiId_ = apiId};
    auto reply = ApiReplyInfo();
    server.Call(call, reply);
    // api error should be delivered out to caller
    ASSERT_EQ(USAGE_ERROR, reply.exception_.code_);
}

TEST_F(FrontendApiHandlerTest, commonPreprocessor)
{
    static auto apiId = GenerateUniqueId();
    auto &server = FrontendApiServer::Get();
    auto handlerCalled = false;
    auto handler = [&handlerCalled](const ApiCallInfo &in, ApiReplyInfo &out) { handlerCalled = true; };
    server.AddHandler(apiId, handler);

    auto processor = [](const ApiCallInfo &in, ApiReplyInfo &out) {
        if (in.paramList_.at(0).get<string>() == "oops") {
            out.exception_.code_ = ErrCode::USAGE_ERROR;
        }
    };
    server.AddCommonPreprocessor("dummyProcessor", processor);

    auto call = ApiCallInfo {.apiId_ = apiId};
    auto reply = ApiReplyInfo();
    // handler should be called if preprocessing passed
    call.paramList_.emplace_back("nice");
    server.Call(call, reply);
    ASSERT_EQ(ErrCode::NO_ERROR, reply.exception_.code_);
    ASSERT_TRUE(handlerCalled);
    // preprocessing failed, handler should not be called
    call.paramList_.clear();
    call.paramList_.emplace_back("oops");
    handlerCalled = false;
    server.Call(call, reply);
    ASSERT_EQ(ErrCode::USAGE_ERROR, reply.exception_.code_);
    ASSERT_FALSE(handlerCalled);
}

TEST_F(FrontendApiHandlerTest, checkAllHandlersRegisted)
{
    auto &server = FrontendApiServer::Get();
    // each frontend-api should have handler
    for (const auto &classDef : FRONTEND_CLASS_DEFS) {
        for (size_t idx = 0; idx < classDef->methodCount_; idx++) {
            const auto &methodDef = classDef->methods_[idx];
            const auto message = "No handler registered for '" + string(methodDef.name_) + "'";
            ASSERT_TRUE(server.HasHandlerFor(methodDef.name_)) << message;
        }
    }
}

TEST_F(FrontendApiHandlerTest, callApiE2E)
{
    const auto& server =  FrontendApiServer::Get();
    // create by1 with seed
    auto call0 = ApiCallInfo {.apiId_ = "By.text", .callerObjRef_ = string(REF_SEED_BY)};
    call0.paramList_.emplace_back("wyz");
    auto reply0 = ApiReplyInfo();
    server.Call(call0, reply0);
    // check result
    ASSERT_EQ(ErrCode::NO_ERROR, reply0.exception_.code_);
    ASSERT_EQ(nlohmann::detail::value_t::string, reply0.resultValue_.type()); // should return backend-by-ref
    const auto ref0 = reply0.resultValue_.get<string>();
    ASSERT_TRUE(ref0.find("By#") != string::npos);
    // go on creating combine by: isAfter (after ref0)
    auto call1 = ApiCallInfo {.apiId_ = "By.isAfter", .callerObjRef_ = string(REF_SEED_BY)};
    call1.paramList_.emplace_back(ref0);
    auto reply1 = ApiReplyInfo();
    server.Call(call1, reply1);
    // check result
    ASSERT_EQ(ErrCode::NO_ERROR, reply1.exception_.code_);
    ASSERT_EQ(nlohmann::detail::value_t::string, reply1.resultValue_.type()); // should return backend-by-ref
    const auto ref1 = reply1.resultValue_.get<string>();
    ASSERT_TRUE(ref1.find("By#") != string::npos);
    // should always return a new By
    ASSERT_NE(ref0, ref1);
}

TEST_F(FrontendApiHandlerTest, parameterPreChecks)
{
    const auto& server =  FrontendApiServer::Get();
    // call with argument missing
    auto call0 = ApiCallInfo {.apiId_ = "By.type", .callerObjRef_ = string(REF_SEED_BY)};
    auto reply0 = ApiReplyInfo();
    server.Call(call0, reply0);
    ASSERT_EQ(ErrCode::USAGE_ERROR, reply0.exception_.code_);
    ASSERT_TRUE(reply0.exception_.message_.find("Illegal argument count") != string::npos);
    // call with argument redundant
    auto call1 = ApiCallInfo {.apiId_ = "By.type", .callerObjRef_ = string(REF_SEED_BY)};
    auto reply1 = ApiReplyInfo();
    call1.paramList_.emplace_back("wyz");
    call1.paramList_.emplace_back("zl");
    server.Call(call1, reply1);
    ASSERT_EQ(ErrCode::USAGE_ERROR, reply1.exception_.code_);
    ASSERT_TRUE(reply1.exception_.message_.find("Illegal argument count") != string::npos);
    // call with argument of wrong type
    auto call2 = ApiCallInfo {.apiId_ = "By.type", .callerObjRef_ = string(REF_SEED_BY)};
    auto reply2 = ApiReplyInfo();
    call2.paramList_.emplace_back(1);
    server.Call(call2, reply2);
    ASSERT_EQ(ErrCode::USAGE_ERROR, reply2.exception_.code_);
    ASSERT_TRUE(reply2.exception_.message_.find("Expect string") != string::npos);
    // call with argument defaulted (bool=true)
    auto call3 = ApiCallInfo {.apiId_ = "By.enabled", .callerObjRef_ = string(REF_SEED_BY)};
    auto reply3 = ApiReplyInfo();
    call3.paramList_.emplace_back(true); // no defaulted
    server.Call(call3, reply3);
    ASSERT_EQ(ErrCode::NO_ERROR, reply3.exception_.code_);

    auto call4 = ApiCallInfo {.apiId_ = "By.enabled", .callerObjRef_ = string(REF_SEED_BY)};
    auto reply4 = ApiReplyInfo(); // defaulted
    server.Call(call4, reply4);
    ASSERT_EQ(ErrCode::NO_ERROR, reply4.exception_.code_);
    // call with bad object ref
    auto call5 = ApiCallInfo {.apiId_ = "By.isAfter", .callerObjRef_ = string(REF_SEED_BY)};
    call5.paramList_.emplace_back("By#100");
    auto reply5 = ApiReplyInfo();
    server.Call(call5, reply5);
    ASSERT_EQ(ErrCode::INTERNAL_ERROR, reply5.exception_.code_); // bad-object is internal_error
    ASSERT_TRUE(reply5.exception_.message_.find("Bad object ref") != string::npos);
    // call with json param with wrong property
    auto call6 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply6 = ApiReplyInfo();
    server.Call(call6, reply6);
    auto call7 = ApiCallInfo {.apiId_ = "UiDriver.findWindow", .callerObjRef_ = reply6.resultValue_.get<string>()};
    auto arg = json();
    arg["badProp"] = "wyz";
    call7.paramList_.emplace_back(arg);
    auto reply7 = ApiReplyInfo();
    server.Call(call7, reply7);
    ASSERT_EQ(ErrCode::USAGE_ERROR, reply7.exception_.code_);
    ASSERT_TRUE(reply7.exception_.message_.find("Illegal property") != string::npos);
}

TEST_F(FrontendApiHandlerTest, pointerMatrixparameterPreChecks)
{
    const auto& server =  FrontendApiServer::Get();
    // call with argument llegal fingers
    auto call0 = ApiCallInfo {.apiId_ = "PointerMatrix.create"};
    auto reply0 = ApiReplyInfo();
    call0.paramList_.emplace_back(11);
    call0.paramList_.emplace_back(3);
    server.Call(call0, reply0);
    ASSERT_EQ(ErrCode::USAGE_ERROR, reply0.exception_.code_);
    ASSERT_TRUE(reply0.exception_.message_.find("Number of illegal fingers") != string::npos);
    // call with argument illegal steps
    auto call2 = ApiCallInfo {.apiId_ = "PointerMatrix.create"};
    auto reply2 = ApiReplyInfo();
    call2.paramList_.emplace_back(2);
    call2.paramList_.emplace_back(1001);
    server.Call(call2, reply2);
    ASSERT_EQ(ErrCode::USAGE_ERROR, reply2.exception_.code_);
    ASSERT_TRUE(reply2.exception_.message_.find("Number of illegal steps") != string::npos);
    // call with argument illegal steps
    auto call4 = ApiCallInfo {.apiId_ = "PointerMatrix.create"};
    auto reply4 = ApiReplyInfo();
    call4.paramList_.emplace_back(5);
    call4.paramList_.emplace_back(0);
    server.Call(call4, reply4);
    ASSERT_EQ(ErrCode::USAGE_ERROR, reply4.exception_.code_);
    ASSERT_TRUE(reply4.exception_.message_.find("Number of illegal steps") != string::npos);
    // call with argument illegal fingers
    auto call5 = ApiCallInfo {.apiId_ = "PointerMatrix.create"};
    auto reply5 = ApiReplyInfo();
    call5.paramList_.emplace_back(-1);
    call5.paramList_.emplace_back(5);
    server.Call(call5, reply5);
    ASSERT_EQ(ErrCode::USAGE_ERROR, reply5.exception_.code_);
    ASSERT_TRUE(reply5.exception_.message_.find("Number of illegal fingers") != string::npos);
    // call with argument illegal fingers
    auto call6 = ApiCallInfo {.apiId_ = "PointerMatrix.create"};
    auto reply6 = ApiReplyInfo();
    call6.paramList_.emplace_back(0);
    call6.paramList_.emplace_back(5);
    server.Call(call6, reply6);
    ASSERT_EQ(ErrCode::USAGE_ERROR, reply6.exception_.code_);
    ASSERT_TRUE(reply6.exception_.message_.find("Number of illegal fingers") != string::npos);
}

TEST_F(FrontendApiHandlerTest, pointerMatrixparameterPreChecksOne)
{
    const auto& server =  FrontendApiServer::Get();
    // call with argument illegal steps
    auto call7 = ApiCallInfo {.apiId_ = "PointerMatrix.create"};
    auto reply7 = ApiReplyInfo();
    call7.paramList_.emplace_back(5);
    call7.paramList_.emplace_back(-5);
    server.Call(call7, reply7);
    ASSERT_EQ(ErrCode::USAGE_ERROR, reply7.exception_.code_);
    ASSERT_TRUE(reply7.exception_.message_.find("Number of illegal steps") != string::npos);
    // call with argument illegal fingers
    auto call10 = ApiCallInfo {.apiId_ = "PointerMatrix.create"};
    auto reply10 = ApiReplyInfo();
    call10.paramList_.emplace_back(6);
    call10.paramList_.emplace_back(10);
    server.Call(call10, reply10);
    ASSERT_EQ(ErrCode::NO_ERROR, reply10.exception_.code_);
    auto call11 = ApiCallInfo {.apiId_ = "PointerMatrix.setPoint", .callerObjRef_ = reply10.resultValue_.get<string>()};
    call11.paramList_.emplace_back(6);
    call11.paramList_.emplace_back(1);
    auto arg1 = json();
    arg1["X"] = 9;
    arg1["Y"] = 10;
    call11.paramList_.emplace_back(arg1);
    auto reply11 = ApiReplyInfo();
    server.Call(call11, reply11);
    ASSERT_EQ(ErrCode::USAGE_ERROR, reply11.exception_.code_);
    ASSERT_TRUE(reply11.exception_.message_.find("Number of illegal fingers") != string::npos);
    // call with argument illegal steps
    auto call12 = ApiCallInfo {.apiId_ = "PointerMatrix.create"};
    auto reply12 = ApiReplyInfo();
    call12.paramList_.emplace_back(6);
    call12.paramList_.emplace_back(10);
    server.Call(call12, reply12);
    ASSERT_EQ(ErrCode::NO_ERROR, reply12.exception_.code_);
    auto call13 = ApiCallInfo {.apiId_ = "PointerMatrix.setPoint", .callerObjRef_ = reply12.resultValue_.get<string>()};
    call13.paramList_.emplace_back(5);
    call13.paramList_.emplace_back(11);
    auto arg2 = json();
    arg2["X"] = 9;
    arg2["Y"] = 10;
    call13.paramList_.emplace_back(arg2);
    auto reply13 = ApiReplyInfo();
    server.Call(call13, reply13);
    ASSERT_EQ(ErrCode::USAGE_ERROR, reply13.exception_.code_);
    ASSERT_TRUE(reply13.exception_.message_.find("Number of illegal steps") != string::npos);
}

TEST_F(FrontendApiHandlerTest, injectMultiPointerActionparameterPreChecks)
{
    const auto& server =  FrontendApiServer::Get();
    auto call2 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply2 = ApiReplyInfo();
    server.Call(call2, reply2);
    auto call3 = ApiCallInfo {.apiId_ = "UiDriver.fling", .callerObjRef_ = reply2.resultValue_.get<string>()};
    auto from = json();
    from["X"] = 30;
    from["Y"] = 40;
    auto to = json();
    to["X"] = 300;
    to["Y"] = 400;
    call3.paramList_.emplace_back(from);
    call3.paramList_.emplace_back(to);
    call3.paramList_.emplace_back(0);
    call3.paramList_.emplace_back(4000);
    auto reply3 = ApiReplyInfo();
    server.Call(call3, reply3);
    ASSERT_EQ(ErrCode::USAGE_ERROR, reply3.exception_.code_);
    ASSERT_TRUE(reply3.exception_.message_.find("The stepLen is out of range") != string::npos);

    auto call4 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply4 = ApiReplyInfo();
    server.Call(call4, reply4);
    auto call5 = ApiCallInfo {.apiId_ = "UiDriver.fling", .callerObjRef_ = reply4.resultValue_.get<string>()};
    call5.paramList_.emplace_back(from);
    call5.paramList_.emplace_back(to);
    call5.paramList_.emplace_back(451);
    call5.paramList_.emplace_back(4000);
    auto reply5 = ApiReplyInfo();
    server.Call(call5, reply5);
    ASSERT_EQ(ErrCode::USAGE_ERROR, reply5.exception_.code_);
    ASSERT_TRUE(reply5.exception_.message_.find("The stepLen is out of range") != string::npos);

    auto call6 = ApiCallInfo {.apiId_ = "UiDriver.create"};
    auto reply6 = ApiReplyInfo();
    server.Call(call6, reply6);
    auto call7 = ApiCallInfo {.apiId_ = "UiDriver.fling", .callerObjRef_ = reply6.resultValue_.get<string>()};
    auto from3 = json();
    from3["X"] = "";
    from3["Y"] = "";
    call7.paramList_.emplace_back(from3);
    call7.paramList_.emplace_back(to);
    call7.paramList_.emplace_back(500);
    call7.paramList_.emplace_back(4000);
    auto reply7 = ApiReplyInfo();
    server.Call(call7, reply7);
    ASSERT_EQ(ErrCode::USAGE_ERROR, reply7.exception_.code_);
}