/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <unistd.h>
#include "IccService.h"
#include "nsServiceManagerUtils.h"
#include "mozilla/ModuleUtils.h"
#include "mozilla/ClearOnShutdown.h"
#include "nsXULAppAPI.h"
#include "jsapi.h"
#include "nsCxPusher.h"

#undef LOG_TAG
#define LOG_TAG "IccService"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__);

#define SERVER_SOCK_FILE "/dev/socket/icc.server.sock"
#define MAX_RESPONSE_SIZE  264

#define ICC_IPC_COMMAND_OPEN_CHANNEL "iccOpenChannel"
#define ICC_IPC_COMMAND_CLOSE_CHANNEL "iccCloseChannel"
#define ICC_IPC_COMMAND_EXCHANGE_APDU "iccExchangeAPDU"
#define ICC_IPC_COMMAND_CARD_PRESENT "iccCardPresent"

namespace mozilla {
namespace dom {
namespace icc {
// The singleton Wifi service, to be used on the main thread.
StaticRefPtr<IccService> gIccService;

// Runnable used dispatch the RIL command on the main thread.
class RILCommandDispatcher : public nsRunnable
{
public:
  RILCommandDispatcher(char *aMsg, int aLen): mMsg(aMsg), mLen(aLen)
  {
    MOZ_ASSERT(!NS_IsMainThread());
  }

  NS_IMETHOD Run()
  {
    MOZ_ASSERT(NS_IsMainThread());
    gIccService->DispatchRILCommand(mMsg, mLen);
    return NS_OK;
  }

private:
  char * mMsg;
  int mLen;
};

// Runnable used to call WaitForEvent on the event thread.
class EventRunnable : public nsRunnable
{
public:
  EventRunnable()
  {
    MOZ_ASSERT(NS_IsMainThread());
  }

  NS_IMETHOD Run()
  {
    MOZ_ASSERT(!NS_IsMainThread());
    gIccService->WaitForEvent();
    return NS_OK;
  }
private:
};

class ReplyRunnable : public nsRunnable
{
public:
  ReplyRunnable(char *aMsg, int aLen): mMsg(aMsg), mLen(aLen)
  {
    MOZ_ASSERT(NS_IsMainThread());
  }

  NS_IMETHOD Run()
  {
    MOZ_ASSERT(!NS_IsMainThread());
    gIccService->Reply(mMsg, mLen);
    return NS_OK;
  }
private:
  char * mMsg;
  int mLen;
};

NS_IMPL_ISUPPORTS2(IccService, nsIIccService, nsIRilSendWorkerMessageCallback)

IccService::IccService()
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(!gIccService);
}

IccService::~IccService()
{
  MOZ_ASSERT(!gIccService);
}

already_AddRefed<IccService>
IccService::FactoryCreate()
{
  if (XRE_GetProcessType() != GeckoProcessType_Default) {
    return nullptr;
  }

  MOZ_ASSERT(NS_IsMainThread());

  if (!gIccService) {
    gIccService = new IccService();
    ClearOnShutdown(&gIccService);
  }

  nsRefPtr<IccService> service = gIccService.get();
  return service.forget();
}

NS_IMETHODIMP
IccService::Start()
{
  MOZ_ASSERT(NS_IsMainThread());

  nsresult rv = NS_NewThread(getter_AddRefs(mEventThread));
  if (NS_FAILED(rv)) {
    NS_WARNING("Can't create icc event thread");
    return NS_ERROR_FAILURE;
  }
  StartListen(); // TODO: check return value
  nsCOMPtr<nsIRunnable> runnable = new EventRunnable();
  mEventThread->Dispatch(runnable, nsIEventTarget::DISPATCH_NORMAL);

  return NS_OK;
}

NS_IMETHODIMP
IccService::StartListen()
{
  struct sockaddr_un addr;
  mFromLen = sizeof(mFrom);

  mFd = socket(PF_UNIX, SOCK_DGRAM, 0);
  if (mFd < 0) {
    LOGI("socket error");
    return NS_OK;
  }
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strcpy(addr.sun_path, SERVER_SOCK_FILE);
  unlink(SERVER_SOCK_FILE);

  if (bind(mFd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    LOGI("bind error");
    return NS_OK;
  }

  LOGI("start listening...");
  return NS_OK;
}

void
IccService::WaitForEvent()
{
  char buff[MAX_RESPONSE_SIZE+1];
  int len;
  if ((len = recvfrom(mFd, buff, MAX_RESPONSE_SIZE+1, 0, (struct sockaddr *)&mFrom, &mFromLen)) > 0) {
    LOGI ("recvfrom: %d", len);
    ProcessMessage(buff, len);
  }
}

void
IccService::ProcessMessage(char *aMsg, int aLen)
{
  nsCOMPtr<nsIRunnable> runnable = new RILCommandDispatcher(aMsg, aLen);
  NS_DispatchToMainThread(runnable);
}

NS_IMETHODIMP
IccService::Shutdown()
{
  MOZ_ASSERT(NS_IsMainThread());
  mEventThread->Shutdown();
  mEventThread = nullptr;
  return NS_OK;
}

void
IccService::DispatchRILCommand(char *aMsg, int aLen)
{
  MOZ_ASSERT(NS_IsMainThread());
  LOGI("DispatchRILCommand");

  nsCOMPtr<nsIRadioInterfaceLayer> ril = do_GetService("@mozilla.org/ril;1");
  nsIRadioInterface *radioInterface;
  if (ril) {
    // TODO: multi-sim
    ril->GetRadioInterface(0, &radioInterface);
    if (radioInterface) {
      char command[20];
      strcpy (command, aMsg);
      if (strcmp(command, ICC_IPC_COMMAND_CARD_PRESENT) == 0) {
        LOGI("process card present");
        uint8_t cardPresent;
        radioInterface->IsCardPresent((bool *)&cardPresent);

        nsCOMPtr<nsIRunnable> runnable = new ReplyRunnable((char *)&cardPresent, 1);
        mEventThread->Dispatch(runnable, nsIEventTarget::DISPATCH_NORMAL);
        Listen();
      }
      else if (strcmp(command, ICC_IPC_COMMAND_EXCHANGE_APDU) == 0) {
        LOGI("process exchange apdu");
        aMsg += strlen(command) + 1;
        int cla = *(aMsg++);
        int command = *(aMsg++);
        int channel = *(aMsg++);
        int p1 = *(aMsg++);
        int p2 = *(aMsg++);
        int p3 = *(aMsg++);
        int dataLen = *(aMsg++);
        char *data = aMsg;
        Transmit(radioInterface, cla, command, channel, p1, p2, p3, data, dataLen);
      }
      else {
        LOGI("unimplemented command: %s", command);
      }
    }
    else {
      LOGI("can't get nsIRadioInterface");
    }
  }
  else {
    LOGI("can't get nsIRadioInterfaceLayer");
  }

}

void
IccService::Listen()
{
  MOZ_ASSERT(NS_IsMainThread());
  nsCOMPtr<nsIRunnable> runnable = new EventRunnable();
  mEventThread->Dispatch(runnable, nsIEventTarget::DISPATCH_NORMAL);
}

void
IccService::Reply(char *buff, int len)
{
  MOZ_ASSERT(!NS_IsMainThread());
  LOGI("Reply");
  int ret = sendto(mFd, buff, len, 0, (struct sockaddr *)&mFrom, mFromLen);
  LOGI("send result: %d", ret);
}


void
IccService::Transmit(nsIRadioInterface *radioInterface, int cla, int command, int channel, int p1, int p2, int p3, const char *data, int dataLen)
{
  MOZ_ASSERT(NS_IsMainThread());
  LOGI("cla:%d ins:%d ch:%d p1:%d p2:%d p3:%d", cla, command, channel, p1, p2, p3);

  AutoSafeJSContext ctx;
  JS::Rooted<JSObject*> message(ctx,
    JS_NewObject(ctx, nullptr, nullptr, nullptr));
  JS::Rooted<JSObject*> apdu(ctx,
    JS_NewObject(ctx, nullptr, nullptr, nullptr));

  JS::Value jsCla = JS_NumberValue(cla);
  JS::Value jsCommand = JS_NumberValue(command);
  JS::Value jsChannel = JS_NumberValue(channel);
  JS::Value jsP1 = JS_NumberValue(p1);
  JS::Value jsP2 = JS_NumberValue(p2);
  JS::Value jsP3 = JS_NumberValue(p3);

  JS_DefineProperty(ctx, message, "channel", jsChannel, nullptr, nullptr, JSPROP_ENUMERATE);
  JS_DefineProperty(ctx, apdu, "cla", jsCla, nullptr, nullptr, JSPROP_ENUMERATE);
  JS_DefineProperty(ctx, apdu, "command", jsCommand, nullptr, nullptr, JSPROP_ENUMERATE);
  JS_DefineProperty(ctx, apdu, "p1", jsP1, nullptr, nullptr, JSPROP_ENUMERATE);
  JS_DefineProperty(ctx, apdu, "p2", jsP2, nullptr, nullptr, JSPROP_ENUMERATE);
  JS_DefineProperty(ctx, apdu, "p3", jsP3, nullptr, nullptr, JSPROP_ENUMERATE);
  if (dataLen > 0)
  {
    JS::Rooted<JSString*> jsData(ctx, JS_NewStringCopyN(ctx, data, dataLen));
    JS_DefineProperty(ctx, apdu, "data", STRING_TO_JSVAL(jsData), nullptr, nullptr, JSPROP_ENUMERATE);
  }
  JS_DefineProperty(ctx, message, "apdu", JS::ObjectValue(*apdu), nullptr, nullptr, JSPROP_ENUMERATE);
  LOGI("sendWorkerMessage from IccService");
  nsresult ret = radioInterface->SendWorkerMessage(NS_LITERAL_STRING("iccExchangeAPDU"), JS::ObjectValue(*message), this);
  LOGI("sendWorkerMessage ret:%x", ret);
}

NS_IMETHODIMP
IccService::HandleResponse(const JS::Value & response, bool *_retval)
{
  MOZ_ASSERT(NS_IsMainThread());
  LOGI("HandleResponse");


  AutoSafeJSContext ctx;
  JS::RootedObject obj(ctx, JSVAL_TO_OBJECT(response));
  JS::RootedValue rilMessageType(ctx);
  JS::RootedValue rilRequestError(ctx);
  JS_LookupProperty(ctx, obj, "rilMessageType", &rilMessageType);
  JS_LookupProperty(ctx, obj, "rilRequestError", &rilRequestError);

  JSString *str = JS_ValueToString(ctx, rilMessageType);
  LOGI("    rilMessageType:%s", JS_EncodeString(ctx, str));
  LOGI("    rilRequestError:%d", rilRequestError.toInt32());
  char res[] = {0x02, 0x90, 0x01};
  nsCOMPtr<nsIRunnable> runnable = new ReplyRunnable(res, 3);
  mEventThread->Dispatch(runnable, nsIEventTarget::DISPATCH_NORMAL);

  Listen();
  
  *_retval = true;
  return NS_OK;
}
/*
NS_GENERIC_FACTORY_SINGLETON_CONSTRUCTOR(IccService,
                                         IccService::FactoryCreate)

NS_DEFINE_NAMED_CID(NS_ICCSERVICE_CID);

static const mozilla::Module::CIDEntry kIccServiceCIDs[] = {
  { &kNS_ICCSERVICE_CID, false, nullptr, IccServiceConstructor },
  { nullptr }
};

static const mozilla::Module::ContractIDEntry kIccServiceContracts[] = {
  { "@mozilla.org/icc/service;1", &kNS_ICCSERVICE_CID },
  { nullptr }
};

static const mozilla::Module kIccServiceModule = {
  mozilla::Module::kVersion,
  kIccServiceCIDs,
  kIccServiceContracts,
  nullptr
};

NSMODULE_DEFN(IccServiceModule) = &kIccServiceModule;
*/
} // namespace icc
} // namespace dom
} // namespace mozilla

