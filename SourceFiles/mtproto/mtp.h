/*
This file is part of Telegram Desktop,
the official desktop version of Telegram messaging app, see https://telegram.org

Telegram Desktop is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

It is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

In addition, as a special exception, the copyright holders give permission
to link the code of portions of this program with the OpenSSL library.

Full license: https://github.com/telegramdesktop/tdesktop/blob/master/LICENSE
Copyright (c) 2014-2016 John Preston, https://desktop.telegram.org
*/
#pragma once

#include "mtproto/mtpSession.h"
#include "mtproto/mtpFileLoader.h"

namespace _mtp_internal {
	MTProtoSession *getSession(int32 dc); // 0 - current set dc

	bool paused();

	void registerRequest(mtpRequestId requestId, int32 dc);
	void unregisterRequest(mtpRequestId requestId);

	static const uint32 dcShift = 10000;

	mtpRequestId storeRequest(mtpRequest &request, const RPCResponseHandler &parser);
	mtpRequest getRequest(mtpRequestId req);
	void wrapInvokeAfter(mtpRequest &to, const mtpRequest &from, const mtpRequestMap &haveSent, int32 skipBeforeRequest = 0);
	void clearCallbacks(mtpRequestId requestId, int32 errorCode = RPCError::NoError); // 0 - do not toggle onError callback
	void clearCallbacksDelayed(const RPCCallbackClears &requestIds);
	void performDelayedClear();
	void execCallback(mtpRequestId requestId, const mtpPrime *from, const mtpPrime *end);
	bool hasCallbacks(mtpRequestId requestId);
	void globalCallback(const mtpPrime *from, const mtpPrime *end);
	void onStateChange(int32 dcWithShift, int32 state);
	void onSessionReset(int32 dcWithShift);
	bool rpcErrorOccured(mtpRequestId requestId, const RPCFailHandlerPtr &onFail, const RPCError &err); // return true if need to clean request data
	inline bool rpcErrorOccured(mtpRequestId requestId, const RPCResponseHandler &handler, const RPCError &err) {
		return rpcErrorOccured(requestId, handler.onFail, err);
	}

	// used for:
	// - resending requests by timer which were postponed by flood delay
	// - destroying MTProtoConnections whose thread has finished
	class GlobalSlotCarrier : public QObject {
		Q_OBJECT

	public:

		GlobalSlotCarrier();

	public slots:

		void checkDelayed();
		void connectionFinished(MTProtoConnection *connection);

	private:

		SingleTimer _timer;
	};

	GlobalSlotCarrier *globalSlotCarrier();
	void queueQuittingConnection(MTProtoConnection *connection);
};

namespace MTP {

	extern const uint32 cfg; // send(MTPhelp_GetConfig(), MTP::cfg + dc) - for dc enum
	extern const uint32 lgt; // send(MTPauth_LogOut(), MTP::lgt + dc) - for logout of guest dcs enum
	inline uint32 dld(int32 index) { // send(req, callbacks, MTP::dld(i) + dc) - for download
		t_assert(index >= 0 && index < MTPDownloadSessionsCount);
		return (0x10 + index) * _mtp_internal::dcShift;
	};
	inline uint32 upl(int32 index) { // send(req, callbacks, MTP::upl(i) + dc) - for upload
		t_assert(index >= 0 && index < MTPUploadSessionsCount);
		return (0x20 + index) * _mtp_internal::dcShift;
	};
	extern const uint32 dldStart, dldEnd; // dc >= dldStart && dc < dldEnd => dc in dld
	extern const uint32 uplStart, uplEnd; // dc >= uplStart && dc < uplEnd => dc in upl

	void start();
	bool started();
	void restart();
	void restart(int32 dcMask);

	void pause();
	void unpause();

	void configure(int32 dc, int32 user);

	void setdc(int32 dc, bool fromZeroOnly = false);
	int32 maindc();

	int32 dcstate(int32 dc = 0);
	QString dctransport(int32 dc = 0);

	template <typename TRequest>
	inline mtpRequestId send(const TRequest &request, RPCResponseHandler callbacks = RPCResponseHandler(), int32 dc = 0, uint64 msCanWait = 0, mtpRequestId after = 0) {
		if (MTProtoSession *session = _mtp_internal::getSession(dc)) {
			return session->send(request, callbacks, msCanWait, true, !dc, after);
		}
		return 0;
	}
	template <typename TRequest>
	inline mtpRequestId send(const TRequest &request, RPCDoneHandlerPtr onDone, RPCFailHandlerPtr onFail = RPCFailHandlerPtr(), int32 dc = 0, uint64 msCanWait = 0, mtpRequestId after = 0) {
		return send(request, RPCResponseHandler(onDone, onFail), dc, msCanWait, after);
	}
	inline void sendAnything(int32 dc = 0, uint64 msCanWait = 0) {
		if (MTProtoSession *session = _mtp_internal::getSession(dc)) {
			return session->sendAnything(msCanWait);
		}
	}
	void ping();
	void cancel(mtpRequestId req);
	void killSession(int32 dc);
	void stopSession(int32 dc);

	enum {
		RequestSent = 0,
		RequestConnecting = 1,
		RequestSending = 2
	};
	int32 state(mtpRequestId req); // < 0 means waiting for such count of ms

	void finish();

	void authed(int32 uid);
	int32 authedId();
	void logoutKeys(RPCDoneHandlerPtr onDone, RPCFailHandlerPtr onFail);

	void setGlobalDoneHandler(RPCDoneHandlerPtr handler);
	void setGlobalFailHandler(RPCFailHandlerPtr handler);
	void setStateChangedHandler(MTPStateChangedHandler handler);
	void setSessionResetHandler(MTPSessionResetHandler handler);
	void clearGlobalHandlers();

	void updateDcOptions(const QVector<MTPDcOption> &options);

	template <typename T>
	T nonce() {
		T result;
		memset_rand(&result, sizeof(T));
		return result;
	}

	mtpKeysMap getKeys();
	void setKey(int32 dc, mtpAuthKeyPtr key);

	QReadWriteLock *dcOptionsMutex();

};

#include "mtproto/mtpSessionImpl.h"
