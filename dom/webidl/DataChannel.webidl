/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

enum RTCDataChannelState {
  "connecting",
  "open",
  "closing",
  "closed"
};

enum RTCDataChannelType {
  "arraybuffer",
  "blob"
};

// XXX This interface is called RTCDataChannel in the spec.
interface DataChannel : EventTarget
{
  readonly attribute DOMString label;
  readonly attribute boolean reliable;
  readonly attribute RTCDataChannelState readyState;
  readonly attribute unsigned long bufferedAmount;
  [SetterThrows]
  attribute EventHandler onopen;
  [SetterThrows]
  attribute EventHandler onerror;
  [SetterThrows]
  attribute EventHandler onclose;
  void close();
  [SetterThrows]
  attribute EventHandler onmessage;
  attribute RTCDataChannelType binaryType;
  [Throws]
  void send(DOMString data);
  [Throws]
  void send(Blob data);
  [Throws]
  void send(ArrayBuffer data);
  [Throws]
  void send(ArrayBufferView data);
};

// Mozilla extensions.
partial interface DataChannel
{
  readonly attribute DOMString protocol;
  readonly attribute boolean ordered;
  readonly attribute unsigned short id;
  // this is deprecated due to renaming in the spec, but still supported for Fx22
  readonly attribute unsigned short stream; // now id
};
