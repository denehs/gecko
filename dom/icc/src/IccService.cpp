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
#include "nsIRadioInterfaceLayer.h"

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

NS_IMPL_ISUPPORTS1(IccService, nsIIccService)

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

