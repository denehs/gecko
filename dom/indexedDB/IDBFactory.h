/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_dom_indexeddb_idbfactory_h__
#define mozilla_dom_indexeddb_idbfactory_h__

#include "mozilla/dom/BindingDeclarations.h" // for Optional
#include "nsCOMPtr.h"
#include "nsCycleCollectionParticipant.h"
#include "nsWrapperCache.h"

class mozIStorageConnection;
class nsIAtom;
class nsIFile;
class nsIFileURL;
class nsIPrincipal;
class nsPIDOMWindow;
template<typename> class nsRefPtr;

namespace mozilla {
class ErrorResult;

namespace dom {
class ContentParent;

namespace indexedDB {

struct DatabaseInfo;
class IDBDatabase;
class IDBOpenDBRequest;
class IndexedDBChild;
class IndexedDBParent;

struct ObjectStoreInfo;

class IDBFactory MOZ_FINAL : public nsISupports,
                             public nsWrapperCache
{
  typedef mozilla::dom::ContentParent ContentParent;
  typedef nsTArray<nsRefPtr<ObjectStoreInfo> > ObjectStoreInfoArray;

public:
  NS_DECL_CYCLE_COLLECTING_ISUPPORTS
  NS_DECL_CYCLE_COLLECTION_SCRIPT_HOLDER_CLASS(IDBFactory)

  // Called when using IndexedDB from a window in a different process.
  static nsresult Create(nsPIDOMWindow* aWindow,
                         const nsACString& aASCIIOrigin,
                         ContentParent* aContentParent,
                         IDBFactory** aFactory);

  // Called when using IndexedDB from a window in the current process.
  static nsresult Create(nsPIDOMWindow* aWindow,
                         ContentParent* aContentParent,
                         IDBFactory** aFactory)
  {
    return Create(aWindow, EmptyCString(), aContentParent, aFactory);
  }

  // Called when using IndexedDB from a JS component or a JSM in the current
  // process.
  static nsresult Create(JSContext* aCx,
                         JS::Handle<JSObject*> aOwningObject,
                         ContentParent* aContentParent,
                         IDBFactory** aFactory);

  // Called when using IndexedDB from a JS component or a JSM in a different
  // process.
  static nsresult Create(ContentParent* aContentParent,
                         IDBFactory** aFactory);

  static already_AddRefed<nsIFileURL>
  GetDatabaseFileURL(nsIFile* aDatabaseFile, const nsACString& aOrigin);

  static already_AddRefed<mozIStorageConnection>
  GetConnection(const nsAString& aDatabaseFilePath,
                const nsACString& aOrigin);

  static nsresult
  SetDefaultPragmas(mozIStorageConnection* aConnection);

  static nsresult
  LoadDatabaseInformation(mozIStorageConnection* aConnection,
                          nsIAtom* aDatabaseId,
                          uint64_t* aVersion,
                          ObjectStoreInfoArray& aObjectStores);

  static nsresult
  SetDatabaseMetadata(DatabaseInfo* aDatabaseInfo,
                      uint64_t aVersion,
                      ObjectStoreInfoArray& aObjectStores);

  nsresult
  OpenInternal(const nsAString& aName,
               int64_t aVersion,
               const nsACString& aASCIIOrigin,
               bool aDeleting,
               IDBOpenDBRequest** _retval);

  nsresult
  OpenInternal(const nsAString& aName,
               int64_t aVersion,
               bool aDeleting,
               IDBOpenDBRequest** _retval)
  {
    return OpenInternal(aName, aVersion, mASCIIOrigin, aDeleting, _retval);
  }

  void
  SetActor(IndexedDBChild* aActorChild)
  {
    NS_ASSERTION(!aActorChild || !mActorChild, "Shouldn't have more than one!");
    mActorChild = aActorChild;
  }

  void
  SetActor(IndexedDBParent* aActorParent)
  {
    NS_ASSERTION(!aActorParent || !mActorParent, "Shouldn't have more than one!");
    mActorParent = aActorParent;
  }

  const nsCString&
  GetASCIIOrigin() const
  {
    return mASCIIOrigin;
  }

  // WrapperCache
  nsPIDOMWindow* GetParentObject() const
  {
    return mWindow;
  }

  virtual JSObject* WrapObject(JSContext* aCx,
                               JS::Handle<JSObject*> aScope) MOZ_OVERRIDE;

  // WebIDL
  already_AddRefed<IDBOpenDBRequest>
  Open(const nsAString& aName, const Optional<uint64_t>& aVersion,
       ErrorResult& aRv)
  {
    return Open(nullptr, aName, aVersion, false, aRv);
  }

  already_AddRefed<IDBOpenDBRequest>
  DeleteDatabase(const nsAString& aName, ErrorResult& aRv)
  {
    return Open(nullptr, aName, Optional<uint64_t>(), true, aRv);
  }

  int16_t
  Cmp(JSContext* aCx, JS::Handle<JS::Value> aFirst,
      JS::Handle<JS::Value> aSecond, ErrorResult& aRv);

  already_AddRefed<IDBOpenDBRequest>
  OpenForPrincipal(nsIPrincipal* aPrincipal, const nsAString& aName,
                   const Optional<uint64_t>& aVersion, ErrorResult& aRv);

  already_AddRefed<IDBOpenDBRequest>
  DeleteForPrincipal(nsIPrincipal* aPrincipal, const nsAString& aName,
                     ErrorResult& aRv);

private:
  IDBFactory();
  ~IDBFactory();

  already_AddRefed<IDBOpenDBRequest>
  Open(nsIPrincipal* aPrincipal, const nsAString& aName,
       const Optional<uint64_t>& aVersion, bool aDelete, ErrorResult& aRv);

  nsCString mASCIIOrigin;

  // If this factory lives on a window then mWindow must be non-null. Otherwise
  // mOwningObject must be non-null.
  nsCOMPtr<nsPIDOMWindow> mWindow;
  JS::Heap<JSObject*> mOwningObject;

  IndexedDBChild* mActorChild;
  IndexedDBParent* mActorParent;

  mozilla::dom::ContentParent* mContentParent;

  bool mRootedOwningObject;
};

} // namespace indexedDB
} // namespace dom
} // namespace mozilla

#endif // mozilla_dom_indexeddb_idbfactory_h__
