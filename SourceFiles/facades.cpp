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
#include "stdafx.h"

#include "window.h"
#include "mainwidget.h"

#include "layerwidget.h"
#include "lang.h"

Q_DECLARE_METATYPE(TextLinkPtr);
Q_DECLARE_METATYPE(Qt::MouseButton);

namespace App {

	void sendBotCommand(const QString &cmd, MsgId replyTo) {
		if (MainWidget *m = main()) m->sendBotCommand(cmd, replyTo);
	}

	bool insertBotCommand(const QString &cmd, bool specialGif) {
		if (MainWidget *m = main()) return m->insertBotCommand(cmd, specialGif);
		return false;
	}

	void searchByHashtag(const QString &tag, PeerData *inPeer) {
		if (MainWidget *m = main()) m->searchMessages(tag + ' ', (inPeer && inPeer->isChannel()) ? inPeer : 0);
	}

	void openPeerByName(const QString &username, MsgId msgId, const QString &startToken) {
		if (MainWidget *m = main()) m->openPeerByName(username, msgId, startToken);
	}

	void joinGroupByHash(const QString &hash) {
		if (MainWidget *m = main()) m->joinGroupByHash(hash);
	}

	void stickersBox(const QString &name) {
		if (MainWidget *m = main()) m->stickersBox(MTP_inputStickerSetShortName(MTP_string(name)));
	}

	void openLocalUrl(const QString &url) {
		if (MainWidget *m = main()) m->openLocalUrl(url);
	}

	bool forward(const PeerId &peer, ForwardWhatMessages what) {
		if (MainWidget *m = main()) return m->onForward(peer, what);
		return false;
	}

	void removeDialog(History *history) {
		if (MainWidget *m = main()) {
			m->removeDialog(history);
		}
	}

	void showSettings() {
		if (Window *w = wnd()) {
			w->showSettings();
		}
	}

	void activateTextLink(TextLinkPtr link, Qt::MouseButton button) {
		if (Window *w = wnd()) {
			qRegisterMetaType<TextLinkPtr>();
			qRegisterMetaType<Qt::MouseButton>();
			QMetaObject::invokeMethod(w, "app_activateTextLink", Qt::QueuedConnection, Q_ARG(TextLinkPtr, link), Q_ARG(Qt::MouseButton, button));
		}
	}

}

namespace Ui {

	void showStickerPreview(DocumentData *sticker) {
		if (Window *w = App::wnd()) {
			w->ui_showStickerPreview(sticker);
		}
	}

	void hideStickerPreview() {
		if (Window *w = App::wnd()) {
			w->ui_hideStickerPreview();
		}
	}

	void showLayer(LayeredWidget *box, ShowLayerOptions options) {
		if (Window *w = App::wnd()) {
			w->ui_showLayer(box, options);
		} else {
			delete box;
		}
	}

	void hideLayer(bool fast) {
		if (Window *w = App::wnd()) w->ui_showLayer(0, ShowLayerOptions(CloseOtherLayers) | (fast ? ForceFastShowLayer : AnimatedShowLayer));
	}

	bool isLayerShown() {
		if (Window *w = App::wnd()) return w->ui_isLayerShown();
		return false;
	}

	bool isMediaViewShown() {
		if (Window *w = App::wnd()) return w->ui_isMediaViewShown();
		return false;
	}

	bool isInlineItemBeingChosen() {
		if (MainWidget *m = App::main()) return m->ui_isInlineItemBeingChosen();
		return false;
	}

	void repaintHistoryItem(const HistoryItem *item) {
		if (!item) return;
		if (MainWidget *m = App::main()) m->ui_repaintHistoryItem(item);
	}

	void repaintInlineItem(const LayoutInlineItem *layout) {
		if (!layout) return;
		if (MainWidget *m = App::main()) m->ui_repaintInlineItem(layout);
	}

	bool isInlineItemVisible(const LayoutInlineItem *layout) {
		if (MainWidget *m = App::main()) return m->ui_isInlineItemVisible(layout);
		return false;
	}

	void showPeerHistory(const PeerId &peer, MsgId msgId, bool back) {
		if (MainWidget *m = App::main()) m->ui_showPeerHistory(peer, msgId, back);
	}

	void showPeerHistoryAsync(const PeerId &peer, MsgId msgId) {
		if (MainWidget *m = App::main()) {
			QMetaObject::invokeMethod(m, "ui_showPeerHistoryAsync", Qt::QueuedConnection, Q_ARG(quint64, peer), Q_ARG(qint32, msgId));
		}
	}

	bool hideWindowNoQuit() {
		if (!App::quitting()) {
			if (Window *w = App::wnd()) {
				if (cWorkMode() == dbiwmTrayOnly || cWorkMode() == dbiwmWindowAndTray) {
					return w->minimizeToTray();
				} else if (cPlatform() == dbipMac || cPlatform() == dbipMacOld) {
					w->hide();
					w->updateIsActive(Global::OfflineBlurTimeout());
					w->updateGlobalMenu();
					return true;
				}
			}
		}
		return false;
	}

}

namespace Notify {

	void userIsBotChanged(UserData *user) {
		if (MainWidget *m = App::main()) m->notify_userIsBotChanged(user);
	}

	void userIsContactChanged(UserData *user, bool fromThisApp) {
		if (MainWidget *m = App::main()) m->notify_userIsContactChanged(user, fromThisApp);
	}

	void botCommandsChanged(UserData *user) {
		if (MainWidget *m = App::main()) m->notify_botCommandsChanged(user);
	}

	void inlineBotRequesting(bool requesting) {
		if (MainWidget *m = App::main()) m->notify_inlineBotRequesting(requesting);
	}

	void migrateUpdated(PeerData *peer) {
		if (MainWidget *m = App::main()) m->notify_migrateUpdated(peer);
	}

	void clipStopperHidden(ClipStopperType type) {
		if (MainWidget *m = App::main()) m->notify_clipStopperHidden(type);
	}

	void historyItemResized(const HistoryItem *item, bool scrollToIt) {
		if (MainWidget *m = App::main()) m->notify_historyItemResized(item, scrollToIt);
	}

	void historyItemLayoutChanged(const HistoryItem *item) {
		if (MainWidget *m = App::main()) m->notify_historyItemLayoutChanged(item);
	}

	void automaticLoadSettingsChangedGif() {
		if (MainWidget *m = App::main()) m->notify_automaticLoadSettingsChangedGif();
	}

}

#define DefineReadOnlyVar(Namespace, Type, Name) const Type &Name() { \
	t_assert_full(Namespace##Data != 0, #Namespace "Data is null in " #Namespace "::" #Name, __FILE__, __LINE__); \
	return Namespace##Data->Name; \
}
#define DefineRefVar(Namespace, Type, Name) DefineReadOnlyVar(Namespace, Type, Name) \
Type &Ref##Name() { \
	t_assert_full(Namespace##Data != 0, #Namespace "Data is null in Global::Ref" #Name, __FILE__, __LINE__); \
	return Namespace##Data->Name; \
}
#define DefineVar(Namespace, Type, Name) DefineRefVar(Namespace, Type, Name) \
void Set##Name(const Type &Name) { \
	t_assert_full(Namespace##Data != 0, #Namespace "Data is null in Global::Set" #Name, __FILE__, __LINE__); \
	Namespace##Data->Name = Name; \
}

struct SandboxDataStruct {
	QString LangSystemISO;
	int32 LangSystem = languageDefault;

	QByteArray LastCrashDump;
	ConnectionProxy PreLaunchProxy;
};
SandboxDataStruct *SandboxData = 0;
uint64 SandboxUserTag = 0;

namespace Sandbox {

	bool CheckBetaVersionDir() {
		QFile beta(cExeDir() + qsl("TelegramBeta_data/tdata/beta"));
		if (cBetaVersion()) {
			cForceWorkingDir(cExeDir() + qsl("TelegramBeta_data/"));
			QDir().mkpath(cWorkingDir() + qstr("tdata"));
			if (*BetaPrivateKey) {
				cSetBetaPrivateKey(QByteArray(BetaPrivateKey));
			}
			if (beta.open(QIODevice::WriteOnly)) {
				QDataStream dataStream(&beta);
				dataStream.setVersion(QDataStream::Qt_5_3);
				dataStream << quint64(cRealBetaVersion()) << cBetaPrivateKey();
			} else {
				LOG(("FATAL: Could not open '%1' for writing private key!").arg(beta.fileName()));
				return false;
			}
		} else if (beta.exists()) {
			cForceWorkingDir(cExeDir() + qsl("TelegramBeta_data/"));
			if (beta.open(QIODevice::ReadOnly)) {
				QDataStream dataStream(&beta);
				dataStream.setVersion(QDataStream::Qt_5_3);

				quint64 v;
				QByteArray k;
				dataStream >> v >> k;
				if (dataStream.status() == QDataStream::Ok) {
					cSetBetaVersion(qMax(v, AppVersion * 1000ULL));
					cSetBetaPrivateKey(k);
					cSetRealBetaVersion(v);
				} else {
					LOG(("FATAL: '%1' is corrupted, reinstall private beta!").arg(beta.fileName()));
					return false;
				}
			} else {
				LOG(("FATAL: could not open '%1' for reading private key!").arg(beta.fileName()));
				return false;
			}
		}
		return true;
	}

	void WorkingDirReady() {
		if (QFile(cWorkingDir() + qsl("tdata/withtestmode")).exists()) {
			cSetTestMode(true);
		}
		if (!cDebug() && QFile(cWorkingDir() + qsl("tdata/withdebug")).exists()) {
			cSetDebug(true);
		}
		if (cBetaVersion()) {
			cSetDevVersion(false);
		} else if (!cDevVersion() && QFile(cWorkingDir() + qsl("tdata/devversion")).exists()) {
			cSetDevVersion(true);
		} else if (DevVersion) {
			QFile f(cWorkingDir() + qsl("tdata/devversion"));
			if (!f.exists() && f.open(QIODevice::WriteOnly)) {
				f.write("1");
			}
		}

		srand((int32)time(NULL));

		SandboxUserTag = 0;
		QFile usertag(cWorkingDir() + qsl("tdata/usertag"));
		if (usertag.open(QIODevice::ReadOnly)) {
			if (usertag.read(reinterpret_cast<char*>(&SandboxUserTag), sizeof(uint64)) != sizeof(uint64)) {
				SandboxUserTag = 0;
			}
			usertag.close();
		}
		if (!SandboxUserTag) {
			do {
				memsetrnd_bad(SandboxUserTag);
			} while (!SandboxUserTag);

			if (usertag.open(QIODevice::WriteOnly)) {
				usertag.write(reinterpret_cast<char*>(&SandboxUserTag), sizeof(uint64));
				usertag.close();
			}
		}
	}

	void start() {
		SandboxData = new SandboxDataStruct();

		SandboxData->LangSystemISO = psCurrentLanguage();
		if (SandboxData->LangSystemISO.isEmpty()) SandboxData->LangSystemISO = qstr("en");
		QByteArray l = LangSystemISO().toLatin1();
		for (int32 i = 0; i < languageCount; ++i) {
			if (l.at(0) == LanguageCodes[i][0] && l.at(1) == LanguageCodes[i][1]) {
				SandboxData->LangSystem = i;
				break;
			}
		}
	}

	void finish() {
		delete SandboxData;
		SandboxData = 0;
	}

	uint64 UserTag() {
		return SandboxUserTag;
	}

	DefineReadOnlyVar(Sandbox, QString, LangSystemISO);
	DefineReadOnlyVar(Sandbox, int32, LangSystem);
	DefineVar(Sandbox, QByteArray, LastCrashDump);
	DefineVar(Sandbox, ConnectionProxy, PreLaunchProxy);

}

struct GlobalDataStruct {
	uint64 LaunchId = 0;

	Adaptive::Layout AdaptiveLayout = Adaptive::NormalLayout;
	bool AdaptiveForWide = true;

	// config
	int32 ChatSizeMax = 200;
	int32 MegagroupSizeMax = 1000;
	int32 ForwardedCountMax = 100;
	int32 OnlineUpdatePeriod = 120000;
	int32 OfflineBlurTimeout = 5000;
	int32 OfflineIdleTimeout = 30000;
	int32 OnlineFocusTimeout = 1000;
	int32 OnlineCloudTimeout = 300000;
	int32 NotifyCloudDelay = 30000;
	int32 NotifyDefaultDelay = 1500;
	int32 ChatBigSize = 10;
	int32 PushChatPeriod = 60000;
	int32 PushChatLimit = 2;
	int32 SavedGifsLimit = 200;
	int32 EditTimeLimit = 172800;

	Global::HiddenPinnedMessagesMap HiddenPinnedMessages;
};
GlobalDataStruct *GlobalData = 0;

namespace Global {

	bool started() {
		return GlobalData != 0;
	}

	void start() {
		GlobalData = new GlobalDataStruct();

		memset_rand(&GlobalData->LaunchId, sizeof(GlobalData->LaunchId));
	}

	void finish() {
		delete GlobalData;
		GlobalData = 0;
	}

	DefineReadOnlyVar(Global, uint64, LaunchId);

	DefineVar(Global, Adaptive::Layout, AdaptiveLayout);
	DefineVar(Global, bool, AdaptiveForWide);

	// config
	DefineVar(Global, int32, ChatSizeMax);
	DefineVar(Global, int32, MegagroupSizeMax);
	DefineVar(Global, int32, ForwardedCountMax);
	DefineVar(Global, int32, OnlineUpdatePeriod);
	DefineVar(Global, int32, OfflineBlurTimeout);
	DefineVar(Global, int32, OfflineIdleTimeout);
	DefineVar(Global, int32, OnlineFocusTimeout);
	DefineVar(Global, int32, OnlineCloudTimeout);
	DefineVar(Global, int32, NotifyCloudDelay);
	DefineVar(Global, int32, NotifyDefaultDelay);
	DefineVar(Global, int32, ChatBigSize);
	DefineVar(Global, int32, PushChatPeriod);
	DefineVar(Global, int32, PushChatLimit);
	DefineVar(Global, int32, SavedGifsLimit);
	DefineVar(Global, int32, EditTimeLimit);

	DefineVar(Global, HiddenPinnedMessagesMap, HiddenPinnedMessages);

};
