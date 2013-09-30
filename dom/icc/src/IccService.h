/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef IccService_h
#define IccService_h

#include <sys/socket.h>
#include <sys/un.h>
#include "nsIRadioInterfaceLayer.h"
#include "nsIIccService.h"
#include "nsCOMPtr.h"
#include "nsThread.h"

namespace mozilla {
namespace dom {
namespace icc {

class IccService MOZ_FINAL : public nsIIccService, public nsIRilSendWorkerMessageCallback
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIICCSERVICE

  static already_AddRefed<IccService>
  FactoryCreate();
  void DispatchRILCommand(char *aMsg, int aLen);
  NS_IMETHOD StartListen();
  void WaitForEvent();
  void Reply(char *buff, int len);
  NS_IMETHOD HandleResponse(const JS::Value & response, bool *_retval);

private:
  IccService();
  ~IccService();

  void Listen();
  void ProcessMessage(char *aMsg, int aLen);
  void Transmit(nsIRadioInterface *radioInterface, int cla, int command, int channel, int p1, int p2, int p3, const char *data, int dataLen);

  nsCOMPtr<nsIThread> mEventThread;
  int mFd;
  struct sockaddr_un mFrom;
  socklen_t mFromLen;
};

} // namespace icc
} // namespace dom
} // namespace mozilla

#endif // IccService_h
