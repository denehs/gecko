/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef IccService_h
#define IccService_h

#include "nsIIccService.h"
#include "nsCOMPtr.h"
#include "nsThread.h"

namespace mozilla {
namespace dom {
namespace icc {

class IccService MOZ_FINAL : public nsIIccService
{
public:
  NS_DECL_ISUPPORTS
  NS_DECL_NSIICCSERVICE

  static already_AddRefed<IccService>
  FactoryCreate();
  void DispatchRILCommand(const nsAString& aCommand);

private:
  IccService();
  ~IccService();

  nsCOMPtr<nsIThread> mEventThread;
};

} // namespace icc
} // namespace dom
} // namespace mozilla

#endif // IccService_h
