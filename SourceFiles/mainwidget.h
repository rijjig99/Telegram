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

#include "dialogswidget.h"
#include "historywidget.h"
#include "profilewidget.h"
#include "overviewwidget.h"
#include "playerwidget.h"
#include "apiwrap.h"

class Window;
struct DialogRow;
class MainWidget;
class ConfirmBox;

class TopBarWidget : public TWidget {
	Q_OBJECT

public:

	TopBarWidget(MainWidget *w);

	void enterEvent(QEvent *e) override;
	void enterFromChildEvent(QEvent *e) override;
	void leaveEvent(QEvent *e) override;
	void leaveToChildEvent(QEvent *e) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;

	void step_appearance(float64 ms, bool timer);
	void enableShadow(bool enable = true);

	void startAnim();
    void stopAnim();
	void showAll();
	void showSelected(uint32 selCount, bool canDelete = false);

	void updateAdaptiveLayout();

	FlatButton *mediaTypeButton();

	void grabStart() override {
		_sideShadow.hide();
	}
	void grabFinish() override {
		_sideShadow.setVisible(!Adaptive::OneColumn());
	}

public slots:

	void onForwardSelection();
	void onDeleteSelection();
	void onClearSelection();
	void onInfoClicked();
	void onAddContact();
	void onEdit();
	void onDeleteContact();
	void onDeleteContactSure();
	void onDeleteAndExit();
	void onDeleteAndExitSure();

signals:

	void clicked();

private:

	MainWidget *main();
	anim::fvalue a_over;
	Animation _a_appearance;

	PeerData *_selPeer;
	uint32 _selCount;
	bool _canDelete;
	QString _selStr;
	int32 _selStrLeft, _selStrWidth;

    bool _animating;

	FlatButton _clearSelection;
	FlatButton _forward, _delete;
	int32 _selectionButtonsWidth, _forwardDeleteWidth;

	FlatButton _info;
	FlatButton _edit, _leaveGroup, _addContact, _deleteContact;
	FlatButton _mediaType;

	PlainShadow _sideShadow;

};

enum StackItemType {
	HistoryStackItem,
	ProfileStackItem,
	OverviewStackItem,
};

class StackItem {
public:
	StackItem(PeerData *peer) : peer(peer) {
	}
	virtual StackItemType type() const = 0;
	virtual ~StackItem() {
	}
	PeerData *peer;
};

class StackItemHistory : public StackItem {
public:
	StackItemHistory(PeerData *peer, MsgId msgId, QList<MsgId> replyReturns) : StackItem(peer),
msgId(msgId), replyReturns(replyReturns) {
	}
	StackItemType type() const {
		return HistoryStackItem;
	}
	MsgId msgId;
	QList<MsgId> replyReturns;
};

class StackItemProfile : public StackItem {
public:
	StackItemProfile(PeerData *peer, int32 lastScrollTop) : StackItem(peer), lastScrollTop(lastScrollTop) {
	}
	StackItemType type() const {
		return ProfileStackItem;
	}
	int32 lastScrollTop;
};

class StackItemOverview : public StackItem {
public:
	StackItemOverview(PeerData *peer, MediaOverviewType mediaType, int32 lastWidth, int32 lastScrollTop) : StackItem(peer), mediaType(mediaType), lastWidth(lastWidth), lastScrollTop(lastScrollTop) {
	}
	StackItemType type() const {
		return OverviewStackItem;
	}
	MediaOverviewType mediaType;
	int32 lastWidth, lastScrollTop;
};

class StackItems : public QVector<StackItem*> {
public:
	bool contains(PeerData *peer) const {
		for (int32 i = 0, l = size(); i < l; ++i) {
			if (at(i)->peer == peer) {
				return true;
			}
		}
		return false;
	}
	void clear() {
		for (int32 i = 0, l = size(); i < l; ++i) {
			delete at(i);
		}
		QVector<StackItem*>::clear();
	}
	~StackItems() {
		clear();
	}
};

inline int chatsListWidth(int windowWidth) {
	return snap<int>((windowWidth * 5) / 14, st::dlgMinWidth, st::dlgMaxWidth);
}

enum SilentNotifiesStatus {
	SilentNotifiesDontChange,
	SilentNotifiesSetSilent,
	SilentNotifiesSetNotify,
};
enum NotifySettingStatus {
	NotifySettingDontChange,
	NotifySettingSetMuted,
	NotifySettingSetNotify,
};

class MainWidget : public TWidget, public RPCSender {
	Q_OBJECT

public:

	MainWidget(Window *window);

	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

	void updateAdaptiveLayout();
	bool needBackButton();

	void paintTopBar(QPainter &p, float64 over, int32 decreaseWidth);
	TopBarWidget *topBar();

	PlayerWidget *player();
	int32 contentScrollAddToY() const;

	void animShow(const QPixmap &bgAnimCache, bool back = false);
	void step_show(float64 ms, bool timer);
	void animStop_show();

	void start(const MTPUser &user);

	void openLocalUrl(const QString &str);
	void openPeerByName(const QString &name, MsgId msgId = ShowAtUnreadMsgId, const QString &startToken = QString());
	void joinGroupByHash(const QString &hash);
	void stickersBox(const MTPInputStickerSet &set);

	void startFull(const MTPVector<MTPUser> &users);
	bool started();
	void applyNotifySetting(const MTPNotifyPeer &peer, const MTPPeerNotifySettings &settings, History *history = 0);
	void gotNotifySetting(MTPInputNotifyPeer peer, const MTPPeerNotifySettings &settings);
	bool failNotifySetting(MTPInputNotifyPeer peer, const RPCError &error);

	void updateNotifySetting(PeerData *peer, NotifySettingStatus notify, SilentNotifiesStatus silent = SilentNotifiesDontChange);

	void incrementSticker(DocumentData *sticker);

	void activate();

	void createDialog(History *history);
	void removeDialog(History *history);
	void dlgUpdated(DialogRow *row = 0);
	void dlgUpdated(History *row, MsgId msgId);

	void windowShown();

	void sentUpdatesReceived(uint64 randomId, const MTPUpdates &updates);
	void sentUpdatesReceived(const MTPUpdates &updates) {
		return sentUpdatesReceived(0, updates);
	}
	void inviteToChannelDone(ChannelData *channel, const MTPUpdates &updates);
	void historyToDown(History *hist);
	void dialogsToUp();
	void newUnreadMsg(History *history, HistoryItem *item);
	void historyWasRead();
	void historyCleared(History *history);

	void peerBefore(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg);
	void peerAfter(const PeerData *inPeer, MsgId inMsg, PeerData *&outPeer, MsgId &outMsg);
	PeerData *historyPeer();
	PeerData *peer();

	PeerData *activePeer();
	MsgId activeMsgId();

	PeerData *profilePeer();
	PeerData *overviewPeer();
	bool mediaTypeSwitch();
	void showPeerProfile(PeerData *peer, bool back = false, int32 lastScrollTop = -1);
	void showMediaOverview(PeerData *peer, MediaOverviewType type, bool back = false, int32 lastScrollTop = -1);
	void showBackFromStack();
	void orderWidgets();
	QRect historyRect() const;

	void onSendFileConfirm(const FileLoadResultPtr &file, bool ctrlShiftEnter);
	void onSendFileCancel(const FileLoadResultPtr &file);
	void onShareContactConfirm(const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, bool ctrlShiftEnter);
	void onShareContactCancel();

	void destroyData();
	void updateOnlineDisplayIn(int32 msecs);

	bool isActive() const;
	bool historyIsActive() const;
	bool lastWasOnline() const;
	uint64 lastSetOnline() const;

	int32 dlgsWidth() const;

	void forwardLayer(int32 forwardSelected = 0); // -1 - send paths
	void deleteLayer(int32 selectedCount = -1); // -1 - context item, else selected, -2 - cancel upload
	void shareContactLayer(UserData *contact);
	void shareUrlLayer(const QString &url, const QString &text);
	void hiderLayer(HistoryHider *h);
	void noHider(HistoryHider *destroyed);
	bool onForward(const PeerId &peer, ForwardWhatMessages what);
	bool onShareUrl(const PeerId &peer, const QString &url, const QString &text);
	void onShareContact(const PeerId &peer, UserData *contact);
	void onSendPaths(const PeerId &peer);
	void onFilesOrForwardDrop(const PeerId &peer, const QMimeData *data);
	bool selectingPeer(bool withConfirm = false);
	void offerPeer(PeerId peer);
	void dialogsActivate();

	DragState getDragState(const QMimeData *mime);

	bool leaveChatFailed(PeerData *peer, const RPCError &e);
	void deleteHistoryAfterLeave(PeerData *peer, const MTPUpdates &updates);
	void deleteMessages(PeerData *peer, const QVector<MTPint> &ids);
	void deletedContact(UserData *user, const MTPcontacts_Link &result);
	void deleteConversation(PeerData *peer, bool deleteHistory = true);
	void clearHistory(PeerData *peer);
	void deleteAllFromUser(ChannelData *channel, UserData *from);

	void addParticipants(PeerData *chatOrChannel, const QVector<UserData*> &users);
	bool addParticipantFail(UserData *user, const RPCError &e);
	bool addParticipantsFail(ChannelData *channel, const RPCError &e); // for multi invite in channels

	void kickParticipant(ChatData *chat, UserData *user);
	bool kickParticipantFail(ChatData *chat, const RPCError &e);

	void checkPeerHistory(PeerData *peer);
	void checkedHistory(PeerData *peer, const MTPmessages_Messages &result);

	bool sendMessageFail(const RPCError &error);

	void forwardSelectedItems();
	void deleteSelectedItems();
	void clearSelectedItems();

	DialogsIndexed &contactsList();
	DialogsIndexed &dialogsList();

	void sendMessage(History *hist, const QString &text, MsgId replyTo, bool broadcast, bool silent, WebPageId webPageId = 0);
	void saveRecentHashtags(const QString &text);

    void readServerHistory(History *history, bool force = true);

	uint64 animActiveTimeStart(const HistoryItem *msg) const;
	void stopAnimActive();

	void sendBotCommand(const QString &cmd, MsgId msgId);
	bool insertBotCommand(const QString &cmd, bool specialGif);

	void searchMessages(const QString &query, PeerData *inPeer);
	bool preloadOverview(PeerData *peer, MediaOverviewType type);
	void preloadOverviews(PeerData *peer);
	void mediaOverviewUpdated(PeerData *peer, MediaOverviewType type);
	void changingMsgId(HistoryItem *row, MsgId newId);
	void itemRemoved(HistoryItem *item);
	void itemEdited(HistoryItem *item);

	void loadMediaBack(PeerData *peer, MediaOverviewType type, bool many = false);
	void peerUsernameChanged(PeerData *peer);

	void checkLastUpdate(bool afterSleep);
	void showAddContact();
	void showNewGroup();

	void serviceNotification(const QString &msg, const MTPMessageMedia &media);
	void serviceHistoryDone(const MTPmessages_Messages &msgs);
	bool serviceHistoryFail(const RPCError &error);

	bool isIdle() const;

	void clearCachedBackground();
	QPixmap cachedBackground(const QRect &forRect, int &x, int &y);
	void backgroundParams(const QRect &forRect, QRect &to, QRect &from) const;
	void updateScrollColors();

	void setChatBackground(const App::WallPaper &wp);
	bool chatBackgroundLoading();
	void checkChatBackground();
	ImagePtr newBackgroundThumb();

	ApiWrap *api();
	void messageDataReceived(ChannelData *channel, MsgId msgId);
	void updateBotKeyboard(History *h);

	void pushReplyReturn(HistoryItem *item);

	bool hasForwardingItems();
	void fillForwardingInfo(Text *&from, Text *&text, bool &serviceColor, ImagePtr &preview);
	void updateForwardingTexts();
	void cancelForwarding();
	void finishForwarding(History *hist, bool broadcast, bool silent); // send them

	void mediaMarkRead(DocumentData *data);
	void mediaMarkRead(const HistoryItemsMap &items);

	void webPageUpdated(WebPageData *page);
	void updateMutedIn(int32 seconds);

	void updateStickers();

	void choosePeer(PeerId peerId, MsgId showAtMsgId); // does offerPeer or showPeerHistory
	void clearBotStartToken(PeerData *peer);

	void contactsReceived();

	void ptsWaiterStartTimerFor(ChannelData *channel, int32 ms); // ms <= 0 - stop timer
	void feedUpdates(const MTPUpdates &updates, uint64 randomId = 0);
	void feedUpdate(const MTPUpdate &update);
	void updateAfterDrag();

	void ctrlEnterSubmitUpdated();
	void setInnerFocus();

	void scheduleViewIncrement(HistoryItem *item);

	HistoryItem *atTopImportantMsg(int32 &bottomUnderScrollTop) const;

	void gotRangeDifference(ChannelData *channel, const MTPupdates_ChannelDifference &diff);
	void onSelfParticipantUpdated(ChannelData *channel);

	bool contentOverlapped(const QRect &globalRect);

	QPixmap grabTopBar();
	QPixmap grabInner();

	void rpcClear() override {
		history.rpcClear();
		dialogs.rpcClear();
		if (profile) profile->rpcClear();
		if (overview) overview->rpcClear();
		if (_api) _api->rpcClear();
		RPCSender::rpcClear();
	}

	bool isItemVisible(HistoryItem *item);

	void ui_repaintHistoryItem(const HistoryItem *item);
	void ui_repaintInlineItem(const LayoutInlineItem *layout);
	bool ui_isInlineItemVisible(const LayoutInlineItem *layout);
	bool ui_isInlineItemBeingChosen();
	void ui_showPeerHistory(quint64 peer, qint32 msgId, bool back);

	void notify_botCommandsChanged(UserData *bot);
	void notify_inlineBotRequesting(bool requesting);
	void notify_userIsBotChanged(UserData *bot);
	void notify_userIsContactChanged(UserData *user, bool fromThisApp);
	void notify_migrateUpdated(PeerData *peer);
	void notify_clipStopperHidden(ClipStopperType type);
	void notify_historyItemResized(const HistoryItem *row, bool scrollToIt);
	void notify_historyItemLayoutChanged(const HistoryItem *item);
	void notify_automaticLoadSettingsChangedGif();

	void cmd_search();
	void cmd_next_chat();
	void cmd_previous_chat();

	~MainWidget();

signals:

	void peerUpdated(PeerData *peer);
	void peerNameChanged(PeerData *peer, const PeerData::Names &oldNames, const PeerData::NameFirstChars &oldChars);
	void peerPhotoChanged(PeerData *peer);
	void dialogRowReplaced(DialogRow *oldRow, DialogRow *newRow);
	void dialogsUpdated();
	void stickersUpdated();
	void savedGifsUpdated();

public slots:

	void webPagesUpdate();

	void audioPlayProgress(const AudioMsgId &audioId);
	void documentLoadProgress(FileLoader *loader);
	void documentLoadFailed(FileLoader *loader, bool started);
	void documentLoadRetry();
	void documentPlayProgress(const SongMsgId &songId);
	void inlineResultLoadProgress(FileLoader *loader);
	void inlineResultLoadFailed(FileLoader *loader, bool started);
	void hidePlayer();

	void dialogsCancelled();

	void onParentResize(const QSize &newSize);
	void getDifference();
	void onGetDifferenceTimeByPts();
	void onGetDifferenceTimeAfterFail();
	void mtpPing();

	void updateOnline(bool gotOtherOffline = false);
	void checkIdleFinish();
	void updateOnlineDisplay();

	void onTopBarClick();
	void onHistoryShown(History *history, MsgId atMsgId);

	void searchInPeer(PeerData *peer);

	void onUpdateNotifySettings();

	void onPhotosSelect();
	void onVideosSelect();
	void onSongsSelect();
	void onDocumentsSelect();
	void onAudiosSelect();
	void onLinksSelect();

	void onForwardCancel(QObject *obj = 0);

	void onCacheBackground();

	void onInviteImport();

	void onUpdateMuted();

	void onStickersInstalled(uint64 setId);
	void onFullPeerUpdated(PeerData *peer);

	void onViewsIncrement();
	void onActiveChannelUpdateFull();

	void onDownloadPathSettings();

	void ui_showPeerHistoryAsync(quint64 peerId, qint32 showAtMsgId);

private:

	void sendReadRequest(PeerData *peer, MsgId upTo);
	void channelWasRead(PeerData *peer, const MTPBool &result);
    void historyWasRead(PeerData *peer, const MTPmessages_AffectedMessages &result);
	bool readRequestFail(PeerData *peer, const RPCError &error);
	void readRequestDone(PeerData *peer);

	void messagesAffected(PeerData *peer, const MTPmessages_AffectedMessages &result);
	void overviewLoaded(History *history, const MTPmessages_Messages &result, mtpRequestId req);

	bool _started;

	uint64 failedObjId;
	QString failedFileName;
	void loadFailed(mtpFileLoader *loader, bool started, const char *retrySlot);

	SelectedItemSet _toForward;
	Text _toForwardFrom, _toForwardText;
	int32 _toForwardNameVersion;

	QMap<WebPageId, bool> _webPagesUpdated;
	QTimer _webPageUpdater;

	SingleTimer _updateMutedTimer;

	enum GetChannelDifferenceFrom {
		GetChannelDifferenceFromUnknown,
		GetChannelDifferenceFromPtsGap,
		GetChannelDifferenceFromFail,
	};
	void getChannelDifference(ChannelData *channel, GetChannelDifferenceFrom from = GetChannelDifferenceFromUnknown);
	void gotDifference(const MTPupdates_Difference &diff);
	bool failDifference(const RPCError &e);
	void feedDifference(const MTPVector<MTPUser> &users, const MTPVector<MTPChat> &chats, const MTPVector<MTPMessage> &msgs, const MTPVector<MTPUpdate> &other);
	void gotState(const MTPupdates_State &state);
	void updSetState(int32 pts, int32 date, int32 qts, int32 seq);
	void gotChannelDifference(ChannelData *channel, const MTPupdates_ChannelDifference &diff);
	bool failChannelDifference(ChannelData *channel, const RPCError &err);
	void failDifferenceStartTimerFor(ChannelData *channel);

	void feedUpdateVector(const MTPVector<MTPUpdate> &updates, bool skipMessageIds = false);
	void feedMessageIds(const MTPVector<MTPUpdate> &updates);

	void deleteHistoryPart(PeerData *peer, const MTPmessages_AffectedHistory &result);
	struct DeleteAllFromUserParams {
		ChannelData *channel;
		UserData *from;
	};
	void deleteAllFromUserPart(DeleteAllFromUserParams params, const MTPmessages_AffectedHistory &result);

	void updateReceived(const mtpPrime *from, const mtpPrime *end);
	bool updateFail(const RPCError &e);

	void usernameResolveDone(QPair<MsgId, QString> msgIdAndStartToken, const MTPcontacts_ResolvedPeer &result);
	bool usernameResolveFail(QString name, const RPCError &error);

	void inviteCheckDone(QString hash, const MTPChatInvite &invite);
	bool inviteCheckFail(const RPCError &error);
	QString _inviteHash;
	void inviteImportDone(const MTPUpdates &result);
	bool inviteImportFail(const RPCError &error);

	void hideAll();
	void showAll();

	void overviewPreloaded(PeerData *data, const MTPmessages_Messages &result, mtpRequestId req);
	bool overviewFailed(PeerData *data, const RPCError &error, mtpRequestId req);

	Animation _a_show;
	QPixmap _cacheUnder, _cacheOver;
	anim::ivalue a_coordUnder, a_coordOver;
	anim::fvalue a_shadow;

	int32 _dialogsWidth;

	DialogsWidget dialogs;
	HistoryWidget history;
	ProfileWidget *profile;
	OverviewWidget *overview;
	PlayerWidget _player;
	TopBarWidget _topBar;
	ConfirmBox *_forwardConfirm; // for narrow mode
	HistoryHider *_hider;
	StackItems _stack;
	PeerData *_peerInStack;
	MsgId _msgIdInStack;

	int32 _playerHeight;
	int32 _contentScrollAddToY;

	Dropdown _mediaType;
	int32 _mediaTypeMask;

	int32 updDate, updQts, updSeq;
	SingleTimer noUpdatesTimer;

	bool ptsUpdated(int32 pts, int32 ptsCount);
	bool ptsUpdated(int32 pts, int32 ptsCount, const MTPUpdates &updates);
	bool ptsUpdated(int32 pts, int32 ptsCount, const MTPUpdate &update);
	void ptsApplySkippedUpdates();
	PtsWaiter _ptsWaiter;

	typedef QMap<ChannelData*, uint64> ChannelGetDifferenceTime;
	ChannelGetDifferenceTime _channelGetDifferenceTimeByPts, _channelGetDifferenceTimeAfterFail;
	uint64 _getDifferenceTimeByPts, _getDifferenceTimeAfterFail;

	bool getDifferenceTimeChanged(ChannelData *channel, int32 ms, ChannelGetDifferenceTime &channelCurTime, uint64 &curTime);

	SingleTimer _byPtsTimer;

	QMap<int32, MTPUpdates> _bySeqUpdates;
	SingleTimer _bySeqTimer;

	SingleTimer _byMinChannelTimer;

	mtpRequestId _onlineRequest;
	SingleTimer _onlineTimer, _onlineUpdater, _idleFinishTimer;
	bool _lastWasOnline;
	uint64 _lastSetOnline;
	bool _isIdle;

	QSet<PeerData*> updateNotifySettingPeers;
	SingleTimer updateNotifySettingTimer;

    typedef QMap<PeerData*, QPair<mtpRequestId, MsgId> > ReadRequests;
    ReadRequests _readRequests;
	typedef QMap<PeerData*, MsgId> ReadRequestsPending;
	ReadRequestsPending _readRequestsPending;

	typedef QMap<PeerData*, mtpRequestId> OverviewsPreload;
	OverviewsPreload _overviewPreload[OverviewCount], _overviewLoad[OverviewCount];

	int32 _failDifferenceTimeout; // growing timeout for getDifference calls, if it fails
	typedef QMap<ChannelData*, int32> ChannelFailDifferenceTimeout;
	ChannelFailDifferenceTimeout _channelFailDifferenceTimeout; // growing timeout for getChannelDifference calls, if it fails
	SingleTimer _failDifferenceTimer;

	uint64 _lastUpdateTime;
	bool _handlingChannelDifference;

	QPixmap _cachedBackground;
	QRect _cachedFor, _willCacheFor;
	int _cachedX, _cachedY;
	SingleTimer _cacheBackgroundTimer;

	typedef QMap<ChannelData*, bool> UpdatedChannels;
	UpdatedChannels _updatedChannels;

	typedef QMap<MsgId, bool> ViewsIncrementMap;
	typedef QMap<PeerData*, ViewsIncrementMap> ViewsIncrement;
	ViewsIncrement _viewsIncremented, _viewsToIncrement;
	typedef QMap<PeerData*, mtpRequestId> ViewsIncrementRequests;
	ViewsIncrementRequests _viewsIncrementRequests;
	typedef QMap<mtpRequestId, PeerData*> ViewsIncrementByRequest;
	ViewsIncrementByRequest _viewsIncrementByRequest;
	SingleTimer _viewsIncrementTimer;
	void viewsIncrementDone(QVector<MTPint> ids, const MTPVector<MTPint> &result, mtpRequestId req);
	bool viewsIncrementFail(const RPCError &error, mtpRequestId req);

	App::WallPaper *_background;

	ApiWrap *_api;

};
