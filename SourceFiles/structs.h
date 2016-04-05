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

typedef int32 ChannelId;
static const ChannelId NoChannel = 0;

typedef uint64 PeerId;
static const uint64 PeerIdMask         = 0xFFFFFFFFULL;
static const uint64 PeerIdTypeMask     = 0x300000000ULL;
static const uint64 PeerIdUserShift    = 0x000000000ULL;
static const uint64 PeerIdChatShift    = 0x100000000ULL;
static const uint64 PeerIdChannelShift = 0x200000000ULL;
inline bool peerIsUser(const PeerId &id) {
	return (id & PeerIdTypeMask) == PeerIdUserShift;
}
inline bool peerIsChat(const PeerId &id) {
	return (id & PeerIdTypeMask) == PeerIdChatShift;
}
inline bool peerIsChannel(const PeerId &id) {
	return (id & PeerIdTypeMask) == PeerIdChannelShift;
}
inline PeerId peerFromUser(int32 user_id) {
	return PeerIdUserShift | uint64(uint32(user_id));
}
inline PeerId peerFromChat(int32 chat_id) {
	return PeerIdChatShift | uint64(uint32(chat_id));
}
inline PeerId peerFromChannel(ChannelId channel_id) {
	return PeerIdChannelShift | uint64(uint32(channel_id));
}
inline PeerId peerFromUser(const MTPint &user_id) {
	return peerFromUser(user_id.v);
}
inline PeerId peerFromChat(const MTPint &chat_id) {
	return peerFromChat(chat_id.v);
}
inline PeerId peerFromChannel(const MTPint &channel_id) {
	return peerFromChannel(channel_id.v);
}
inline int32 peerToBareInt(const PeerId &id) {
	return int32(uint32(id & PeerIdMask));
}
inline int32 peerToUser(const PeerId &id) {
	return peerIsUser(id) ? peerToBareInt(id) : 0;
}
inline int32 peerToChat(const PeerId &id) {
	return peerIsChat(id) ? peerToBareInt(id) : 0;
}
inline ChannelId peerToChannel(const PeerId &id) {
	return peerIsChannel(id) ? peerToBareInt(id) : NoChannel;
}
inline MTPint peerToBareMTPInt(const PeerId &id) {
	return MTP_int(peerToBareInt(id));
}
inline PeerId peerFromMTP(const MTPPeer &peer) {
	switch (peer.type()) {
	case mtpc_peerUser: return peerFromUser(peer.c_peerUser().vuser_id);
	case mtpc_peerChat: return peerFromChat(peer.c_peerChat().vchat_id);
	case mtpc_peerChannel: return peerFromChannel(peer.c_peerChannel().vchannel_id);
	}
	return 0;
}
inline MTPpeer peerToMTP(const PeerId &id) {
	if (peerIsUser(id)) {
		return MTP_peerUser(peerToBareMTPInt(id));
	} else if (peerIsChat(id)) {
		return MTP_peerChat(peerToBareMTPInt(id));
	} else if (peerIsChannel(id)) {
		return MTP_peerChannel(peerToBareMTPInt(id));
	}
	return MTP_peerUser(MTP_int(0));
}
inline PeerId peerFromMessage(const MTPmessage &msg) {
	PeerId from_id = 0, to_id = 0;
	switch (msg.type()) {
	case mtpc_message:
		from_id = msg.c_message().has_from_id() ? peerFromUser(msg.c_message().vfrom_id) : 0;
		to_id = peerFromMTP(msg.c_message().vto_id);
		break;
	case mtpc_messageService:
		from_id = msg.c_messageService().has_from_id() ? peerFromUser(msg.c_messageService().vfrom_id) : 0;
		to_id = peerFromMTP(msg.c_messageService().vto_id);
		break;
	}
	return (from_id && peerToUser(to_id) == MTP::authedId()) ? from_id : to_id;
}
inline int32 flagsFromMessage(const MTPmessage &msg) {
	switch (msg.type()) {
	case mtpc_message: return msg.c_message().vflags.v;
	case mtpc_messageService: return msg.c_messageService().vflags.v;
	}
	return 0;
}
inline int32 idFromMessage(const MTPmessage &msg) {
	switch (msg.type()) {
	case mtpc_messageEmpty: return msg.c_messageEmpty().vid.v;
	case mtpc_message: return msg.c_message().vid.v;
	case mtpc_messageService: return msg.c_messageService().vid.v;
	}
	return 0;
}
inline int32 dateFromMessage(const MTPmessage &msg) {
	switch (msg.type()) {
	case mtpc_message: return msg.c_message().vdate.v;
	case mtpc_messageService: return msg.c_messageService().vdate.v;
	}
	return 0;
}

typedef uint64 PhotoId;
typedef uint64 VideoId;
typedef uint64 AudioId;
typedef uint64 DocumentId;
typedef uint64 WebPageId;
static const WebPageId CancelledWebPageId = 0xFFFFFFFFFFFFFFFFULL;

typedef int32 MsgId;
struct FullMsgId {
	FullMsgId() : channel(NoChannel), msg(0) {
	}
	FullMsgId(ChannelId channel, MsgId msg) : channel(channel), msg(msg) {
	}
	ChannelId channel;
	MsgId msg;
};
inline bool operator==(const FullMsgId &a, const FullMsgId &b) {
	return (a.channel == b.channel) && (a.msg == b.msg);
}
inline bool operator<(const FullMsgId &a, const FullMsgId &b) {
	if (a.msg < b.msg) return true;
	if (a.msg > b.msg) return false;
	return a.channel < b.channel;
}

static const MsgId StartClientMsgId = -0x7FFFFFFF;
static const MsgId EndClientMsgId = -0x40000000;
inline bool isClientMsgId(MsgId id) {
	return id >= StartClientMsgId && id < EndClientMsgId;
}
static const MsgId ShowAtTheEndMsgId = -0x40000000;
static const MsgId SwitchAtTopMsgId = -0x3FFFFFFF;
static const MsgId ShowAtProfileMsgId = -0x3FFFFFFE;
static const MsgId ServerMaxMsgId = 0x3FFFFFFF;
static const MsgId ShowAtUnreadMsgId = 0;

struct NotifySettings {
	NotifySettings() : flags(MTPDinputPeerNotifySettings::flag_show_previews), mute(0), sound("default") {
	}
	int32 flags, mute;
	string sound;
	bool previews() const {
		return flags & MTPDinputPeerNotifySettings::flag_show_previews;
	}
	bool silent() const {
		return flags & MTPDinputPeerNotifySettings::flag_silent;
	}
};
typedef NotifySettings *NotifySettingsPtr;

static const NotifySettingsPtr UnknownNotifySettings = NotifySettingsPtr(0);
static const NotifySettingsPtr EmptyNotifySettings = NotifySettingsPtr(1);
extern NotifySettings globalNotifyAll, globalNotifyUsers, globalNotifyChats;
extern NotifySettingsPtr globalNotifyAllPtr, globalNotifyUsersPtr, globalNotifyChatsPtr;

inline bool isNotifyMuted(NotifySettingsPtr settings, int32 *changeIn = 0) {
	if (settings != UnknownNotifySettings && settings != EmptyNotifySettings) {
		int32 t = unixtime();
		if (settings->mute > t) {
			if (changeIn) *changeIn = settings->mute - t + 1;
			return true;
		}
	}
	if (changeIn) *changeIn = 0;
	return false;
}

static const int32 UserColorsCount = 8;

style::color peerColor(int32 index);
ImagePtr userDefPhoto(int32 index);
ImagePtr chatDefPhoto(int32 index);
ImagePtr channelDefPhoto(int32 index);

static const PhotoId UnknownPeerPhotoId = 0xFFFFFFFFFFFFFFFFULL;

inline const QString &emptyUsername() {
	static QString empty;
	return empty;
}

class UserData;
class ChatData;
class ChannelData;
class PeerData {
public:

	virtual ~PeerData() {
		if (notify != UnknownNotifySettings && notify != EmptyNotifySettings) {
			delete notify;
			notify = UnknownNotifySettings;
		}
	}

	bool isUser() const {
		return peerIsUser(id);
	}
	bool isChat() const {
		return peerIsChat(id);
	}
	bool isChannel() const {
		return peerIsChannel(id);
	}
	bool isSelf() const {
		return (input.type() == mtpc_inputPeerSelf);
	}
	bool isVerified() const;
	bool isMegagroup() const;
	bool canWrite() const;
	UserData *asUser();
	const UserData *asUser() const;
	ChatData *asChat();
	const ChatData *asChat() const;
	ChannelData *asChannel();
	const ChannelData *asChannel() const;

	ChatData *migrateFrom() const;
	ChannelData *migrateTo() const;

	void updateName(const QString &newName, const QString &newNameOrPhone, const QString &newUsername);

	void fillNames();

	const Text &dialogName() const;
	const QString &shortName() const;
	const QString &userName() const;

	const PeerId id;
	int32 bareId() const {
		return int32(uint32(id & 0xFFFFFFFFULL));
	}

	TextLinkPtr lnk;

	QString name;
	Text nameText;
	typedef QSet<QString> Names;
	Names names; // for filtering
	typedef QSet<QChar> NameFirstChars;
	NameFirstChars chars;

	bool loaded;
	MTPinputPeer input;

	int32 colorIndex;
	style::color color;
	ImagePtr photo;
	PhotoId photoId;
	StorageImageLocation photoLoc;

	int32 nameVersion;

	NotifySettingsPtr notify;

private:

	PeerData(const PeerId &id);
	friend class UserData;
	friend class ChatData;
	friend class ChannelData;
};

static const uint64 UserNoAccess = 0xFFFFFFFFFFFFFFFFULL;

class PeerLink : public ITextLink {
	TEXT_LINK_CLASS(PeerLink)

public:
	PeerLink(PeerData *peer) : _peer(peer) {
	}
	void onClick(Qt::MouseButton button) const;
	PeerData *peer() const {
		return _peer;
	}

private:
	PeerData *_peer;
};

class BotCommand {
public:
	BotCommand(const QString &command, const QString &description) : command(command), _description(description) {
	}
	QString command;

	bool setDescription(const QString &description) {
		if (_description != description) {
			_description = description;
			_descriptionText = Text();
			return true;
		}
		return false;
	}

	const Text &descriptionText() const;

private:
	QString _description;
	mutable Text _descriptionText;

};

struct BotInfo {
	BotInfo() : inited(false), readsAllHistory(false), cantJoinGroups(false), version(0), text(st::msgMinWidth) {
	}
	bool inited;
	bool readsAllHistory, cantJoinGroups;
	int32 version;
	QString description, inlinePlaceholder;
	QList<BotCommand> commands;
	Text text; // description

	QString startToken, startGroupToken;
};

enum UserBlockedStatus {
	UserBlockUnknown = 0,
	UserIsBlocked,
	UserIsNotBlocked,
};

class PhotoData;
class UserData : public PeerData {
public:

	UserData(const PeerId &id) : PeerData(id)
		, access(0)
		, flags(0)
		, onlineTill(0)
		, contact(-1)
		, blocked(UserBlockUnknown)
		, photosCount(-1)
		, botInfo(0) {
		setName(QString(), QString(), QString(), QString());
	}
	void setPhoto(const MTPUserProfilePhoto &photo);
	void setName(const QString &first, const QString &last, const QString &phoneName, const QString &username);
	void setPhone(const QString &newPhone);
	void setBotInfoVersion(int32 version);
	void setBotInfo(const MTPBotInfo &info);

	void setNameOrPhone(const QString &newNameOrPhone);

	void madeAction(); // pseudo-online

	uint64 access;

	int32 flags;
	bool isVerified() const {
		return flags & MTPDuser::flag_verified;
	}
	bool canWrite() const {
		return access != UserNoAccess;
	}

	MTPInputUser inputUser;

	QString firstName;
	QString lastName;
	QString username;
	QString phone;
	QString nameOrPhone;
	Text phoneText;
	int32 onlineTill;
	int32 contact; // -1 - not contact, cant add (self, empty, deleted, foreign), 0 - not contact, can add (request), 1 - contact
	UserBlockedStatus blocked;

	typedef QList<PhotoData*> Photos;
	Photos photos;
	int32 photosCount; // -1 not loaded, 0 all loaded

	QString about;

	BotInfo *botInfo;
};
static UserData * const InlineBotLookingUpData = SharedMemoryLocation<UserData, 0>();

class ChatData : public PeerData {
public:

	ChatData(const PeerId &id) : PeerData(id)
		, inputChat(MTP_int(bareId()))
		, migrateToPtr(0)
		, count(0)
		, date(0)
		, version(0)
		, creator(0)
		, flags(0)
		, isForbidden(false)
		, botStatus(0) {
	}
	void setPhoto(const MTPChatPhoto &photo, const PhotoId &phId = UnknownPeerPhotoId);
	void invalidateParticipants() {
		participants = ChatData::Participants();
		admins = ChatData::Admins();
		flags &= ~MTPDchat::flag_admin;
		invitedByMe = ChatData::InvitedByMe();
		botStatus = 0;
	}
	bool noParticipantInfo() const {
		return (count > 0 || amIn()) && participants.isEmpty();
	}

	MTPint inputChat;

	ChannelData *migrateToPtr;

	int32 count;
	int32 date;
	int32 version;
	int32 creator;

	int32 flags;
	bool isForbidden;
	bool amIn() const {
		return !isForbidden && !haveLeft() && !wasKicked();
	}
	bool canEdit() const {
		return !isDeactivated() && (amCreator() || (adminsEnabled() ? amAdmin() : amIn()));
	}
	bool canWrite() const {
		return !isDeactivated() && amIn();
	}
	bool haveLeft() const {
		return flags & MTPDchat::flag_left;
	}
	bool wasKicked() const {
		return flags & MTPDchat::flag_kicked;
	}
	bool adminsEnabled() const {
		return flags & MTPDchat::flag_admins_enabled;
	}
	bool amCreator() const {
		return flags & MTPDchat::flag_creator;
	}
	bool amAdmin() const {
		return flags & MTPDchat::flag_admin;
	}
	bool isDeactivated() const {
		return flags & MTPDchat::flag_deactivated;
	}
	bool isMigrated() const {
		return flags & MTPDchat::flag_migrated_to;
	}
	typedef QMap<UserData*, int32> Participants;
	Participants participants;
	typedef OrderedSet<UserData*> InvitedByMe;
	InvitedByMe invitedByMe;
	typedef OrderedSet<UserData*> Admins;
	Admins admins;
	typedef QList<UserData*> LastAuthors;
	LastAuthors lastAuthors;
	typedef OrderedSet<PeerData*> MarkupSenders;
	MarkupSenders markupSenders;
	int32 botStatus; // -1 - no bots, 0 - unknown, 1 - one bot, that sees all history, 2 - other
//	ImagePtr photoFull;
	QString invitationUrl;
};

enum PtsSkippedQueue {
	SkippedUpdate,
	SkippedUpdates,
};
class PtsWaiter {
public:

	PtsWaiter()
		: _good(0)
		, _last(0)
		, _count(0)
		, _applySkippedLevel(0)
		, _requesting(false)
		, _waitingForSkipped(false)
		, _waitingForShortPoll(false) {
	}
	void init(int32 pts) {
		_good = _last = _count = pts;
		clearSkippedUpdates();
	}
	bool inited() const {
		return _good > 0;
	}
	void setRequesting(bool isRequesting) {
		_requesting = isRequesting;
		if (_requesting) {
			clearSkippedUpdates();
		}
	}
	bool requesting() const {
		return _requesting;
	}
	bool waitingForSkipped() const {
		return _waitingForSkipped;
	}
	bool waitingForShortPoll() const {
		return _waitingForShortPoll;
	}
	void setWaitingForSkipped(ChannelData *channel, int32 ms); // < 0 - not waiting
	void setWaitingForShortPoll(ChannelData *channel, int32 ms); // < 0 - not waiting
	int32 current() const{
		return _good;
	}
	bool updated(ChannelData *channel, int32 pts, int32 count);
	bool updated(ChannelData *channel, int32 pts, int32 count, const MTPUpdates &updates);
	bool updated(ChannelData *channel, int32 pts, int32 count, const MTPUpdate &update);
	void applySkippedUpdates(ChannelData *channel);
	void clearSkippedUpdates();

private:
	bool check(ChannelData *channel, int32 pts, int32 count); // return false if need to save that update and apply later
	uint64 ptsKey(PtsSkippedQueue queue);
	void checkForWaiting(ChannelData *channel);
	QMap<uint64, PtsSkippedQueue> _queue;
	QMap<uint64, MTPUpdate> _updateQueue;
	QMap<uint64, MTPUpdates> _updatesQueue;
	int32 _good, _last, _count;
	int32 _applySkippedLevel;
	bool _requesting, _waitingForSkipped, _waitingForShortPoll;
};

struct MegagroupInfo {
	MegagroupInfo()
		: botStatus(0)
		, pinnedMsgId(0)
		, joinedMessageFound(false)
		, lastParticipantsStatus(LastParticipantsUpToDate)
		, lastParticipantsCount(0)
		, migrateFromPtr(0) {
	}
	typedef QList<UserData*> LastParticipants;
	LastParticipants lastParticipants;
	typedef OrderedSet<UserData*> LastAdmins;
	LastAdmins lastAdmins;
	typedef OrderedSet<PeerData*> MarkupSenders;
	MarkupSenders markupSenders;
	typedef OrderedSet<UserData*> Bots;
	Bots bots;
	int32 botStatus; // -1 - no bots, 0 - unknown, 1 - one bot, that sees all history, 2 - other

	MsgId pinnedMsgId;
	bool joinedMessageFound;

	enum LastParticipantsStatus {
		LastParticipantsUpToDate       = 0x00,
		LastParticipantsAdminsOutdated = 0x01,
		LastParticipantsCountOutdated  = 0x02,
	};
	mutable int32 lastParticipantsStatus;
	int32 lastParticipantsCount;

	ChatData *migrateFromPtr;
};

class ChannelData : public PeerData {
public:

	ChannelData(const PeerId &id) : PeerData(id)
		, access(0)
		, inputChannel(MTP_inputChannel(MTP_int(bareId()), MTP_long(0)))
		, count(1)
		, adminsCount(1)
		, date(0)
		, version(0)
		, flags(0)
		, flagsFull(0)
		, mgInfo(nullptr)
		, isForbidden(true)
		, inviter(0)
		, _lastFullUpdate(0) {
		setName(QString(), QString());
	}
	void setPhoto(const MTPChatPhoto &photo, const PhotoId &phId = UnknownPeerPhotoId);
	void setName(const QString &name, const QString &username);

	void updateFull(bool force = false);
	void fullUpdated();

	uint64 access;

	MTPinputChannel inputChannel;

	QString username, about;

	int32 count, adminsCount;
	int32 date;
	int32 version;
	int32 flags, flagsFull;
	MegagroupInfo *mgInfo;
	bool lastParticipantsCountOutdated() const {
		if (!mgInfo || !(mgInfo->lastParticipantsStatus & MegagroupInfo::LastParticipantsCountOutdated)) {
			return false;
		}
		if (mgInfo->lastParticipantsCount == count) {
			mgInfo->lastParticipantsStatus &= ~MegagroupInfo::LastParticipantsCountOutdated;
			return false;
		}
		return true;
	}
	void flagsUpdated();
	bool isMegagroup() const {
		return flags & MTPDchannel::flag_megagroup;
	}
	bool isBroadcast() const {
		return flags & MTPDchannel::flag_broadcast;
	}
	bool isPublic() const {
		return flags & MTPDchannel::flag_username;
	}
	bool canEditUsername() const {
		return amCreator() && (flagsFull & MTPDchannelFull::flag_can_set_username);
	}
	bool amCreator() const {
		return flags & MTPDchannel::flag_creator;
	}
	bool amEditor() const {
		return flags & MTPDchannel::flag_editor;
	}
	bool amModerator() const {
		return flags & MTPDchannel::flag_moderator;
	}
	bool haveLeft() const {
		return flags & MTPDchannel::flag_left;
	}
	bool wasKicked() const {
		return flags & MTPDchannel::flag_kicked;
	}
	bool amIn() const {
		return !isForbidden && !haveLeft() && !wasKicked();
	}
	bool canPublish() const {
		return amCreator() || amEditor();
	}
	bool canWrite() const {
		return amIn() && (canPublish() || !isBroadcast());
	}
	bool canViewParticipants() const {
		return flagsFull & MTPDchannelFull::flag_can_view_participants;
	}
	bool addsSignature() const {
		return flags & MTPDchannel::flag_signatures;
	}
	bool isForbidden;
	bool isVerified() const {
		return flags & MTPDchannel::flag_verified;
	}
	bool canAddParticipants() const {
		return amCreator() || amEditor() || (flags & MTPDchannel::flag_democracy);
	}

//	ImagePtr photoFull;
	QString invitationUrl;

	int32 inviter; // > 0 - user who invited me to channel, < 0 - not in channel
	QDateTime inviteDate;

	void ptsInit(int32 pts) {
		_ptsWaiter.init(pts);
	}
	void ptsReceived(int32 pts) {
		if (_ptsWaiter.updated(this, pts, 0)) {
			_ptsWaiter.applySkippedUpdates(this);
		}
	}
	bool ptsUpdated(int32 pts, int32 count) {
		return _ptsWaiter.updated(this, pts, count);
	}
	bool ptsUpdated(int32 pts, int32 count, const MTPUpdate &update) {
		return _ptsWaiter.updated(this, pts, count, update);
	}
	int32 pts() const {
		return _ptsWaiter.current();
	}
	bool ptsInited() const {
		return _ptsWaiter.inited();
	}
	bool ptsRequesting() const {
		return _ptsWaiter.requesting();
	}
	void ptsSetRequesting(bool isRequesting) {
		return _ptsWaiter.setRequesting(isRequesting);
	}
	void ptsApplySkippedUpdates() {
		return _ptsWaiter.applySkippedUpdates(this);
	}
	void ptsWaitingForShortPoll(int32 ms) { // < 0 - not waiting
		return _ptsWaiter.setWaitingForShortPoll(this, ms);
	}

	~ChannelData();

private:

	PtsWaiter _ptsWaiter;
	uint64 _lastFullUpdate;
};

inline UserData *PeerData::asUser() {
	return isUser() ? static_cast<UserData*>(this) : 0;
}
inline const UserData *PeerData::asUser() const {
	return isUser() ? static_cast<const UserData*>(this) : 0;
}
inline ChatData *PeerData::asChat() {
	return isChat() ? static_cast<ChatData*>(this) : 0;
}
inline const ChatData *PeerData::asChat() const {
	return isChat() ? static_cast<const ChatData*>(this) : 0;
}
inline ChannelData *PeerData::asChannel() {
	return isChannel() ? static_cast<ChannelData*>(this) : 0;
}
inline const ChannelData *PeerData::asChannel() const {
	return isChannel() ? static_cast<const ChannelData*>(this) : 0;
}
inline ChatData *PeerData::migrateFrom() const {
	return (isMegagroup() && asChannel()->amIn()) ? asChannel()->mgInfo->migrateFromPtr : 0;
}
inline ChannelData *PeerData::migrateTo() const {
	return (isChat() && asChat()->migrateToPtr && asChat()->migrateToPtr->amIn()) ? asChat()->migrateToPtr : 0;
}
inline const Text &PeerData::dialogName() const {
	return migrateTo() ? migrateTo()->dialogName() : ((isUser() && !asUser()->phoneText.isEmpty()) ? asUser()->phoneText : nameText);
}
inline const QString &PeerData::shortName() const {
	return isUser() ? asUser()->firstName : name;
}
inline const QString &PeerData::userName() const {
	return isUser() ? asUser()->username : (isChannel() ? asChannel()->username : emptyUsername());
}
inline bool PeerData::isVerified() const {
	return isUser() ? asUser()->isVerified() : (isChannel() ? asChannel()->isVerified() : false);
}
inline bool PeerData::isMegagroup() const {
	return isChannel() ? asChannel()->isMegagroup() : false;
}
inline bool PeerData::canWrite() const {
	return isChannel() ? asChannel()->canWrite() : (isChat() ? asChat()->canWrite() : (isUser() ? asUser()->canWrite() : false));
}

enum ActionOnLoad {
	ActionOnLoadNone,
	ActionOnLoadOpen,
	ActionOnLoadOpenWith,
	ActionOnLoadPlayInline
};

typedef QMap<char, QPixmap> PreparedPhotoThumbs;
class PhotoData {
public:
	PhotoData(const PhotoId &id, const uint64 &access = 0, int32 date = 0, const ImagePtr &thumb = ImagePtr(), const ImagePtr &medium = ImagePtr(), const ImagePtr &full = ImagePtr());

	void automaticLoad(const HistoryItem *item);
	void automaticLoadSettingsChanged();

	void download();
	bool loaded() const;
	bool loading() const;
	bool displayLoading() const;
	void cancel();
	float64 progress() const;
	int32 loadOffset() const;
	bool uploading() const;

	void forget();
	ImagePtr makeReplyPreview();

	~PhotoData();

	PhotoId id;
	uint64 access;
	int32 date;
	ImagePtr thumb, replyPreview;
	ImagePtr medium;
	ImagePtr full;

	PeerData *peer; // for chat and channel photos connection
	// geo, caption

	struct UploadingData {
		UploadingData(int32 size) : offset(0), size(size) {
		}
		int32 offset, size;
	};
	UploadingData *uploadingData;

private:
	void notifyLayoutChanged() const;

};

class PhotoLink : public ITextLink {
	TEXT_LINK_CLASS(PhotoLink)

public:
	PhotoLink(PhotoData *photo, PeerData *peer = 0) : _photo(photo), _peer(peer) {
	}
	void onClick(Qt::MouseButton button) const;
	PhotoData *photo() const {
		return _photo;
	}
	PeerData *peer() const {
		return _peer;
	}

private:
	PhotoData *_photo;
	PeerData *_peer;

};

class PhotoSaveLink : public PhotoLink {
	TEXT_LINK_CLASS(PhotoSaveLink)

public:
	PhotoSaveLink(PhotoData *photo, PeerData *peer = 0) : PhotoLink(photo, peer) {
	}
	void onClick(Qt::MouseButton button) const;

};

class PhotoCancelLink : public PhotoLink {
	TEXT_LINK_CLASS(PhotoCancelLink)

public:
	PhotoCancelLink(PhotoData *photo, PeerData *peer = 0) : PhotoLink(photo, peer) {
	}
	void onClick(Qt::MouseButton button) const;

};

enum FileStatus {
	FileDownloadFailed = -2,
	FileUploadFailed = -1,
	FileUploading = 0,
	FileReady = 1,
};

enum DocumentType {
	FileDocument     = 0,
	VideoDocument    = 1,
	SongDocument     = 2,
	StickerDocument  = 3,
	AnimatedDocument = 4,
	VoiceDocument    = 5,
};

struct DocumentAdditionalData {
};

struct StickerData : public DocumentAdditionalData {
	StickerData() : set(MTP_inputStickerSetEmpty()) {
	}
	ImagePtr img;
	QString alt;

	MTPInputStickerSet set;
	bool setInstalled() const;

	StorageImageLocation loc; // doc thumb location

};

struct SongData : public DocumentAdditionalData {
	SongData() : duration(0) {
	}
	int32 duration;
	QString title, performer;

};

typedef QVector<char> VoiceWaveform; // [0] == -1 -- counting, [0] == -2 -- could not count
struct VoiceData : public DocumentAdditionalData {
	VoiceData() : duration(0), wavemax(0) {
	}
	~VoiceData();
	int32 duration;
	VoiceWaveform waveform;
	char wavemax;
};

bool fileIsImage(const QString &name, const QString &mime);

class DocumentData {
public:
	DocumentData(const DocumentId &id, const uint64 &access = 0, int32 date = 0, const QVector<MTPDocumentAttribute> &attributes = QVector<MTPDocumentAttribute>(), const QString &mime = QString(), const ImagePtr &thumb = ImagePtr(), int32 dc = 0, int32 size = 0);
	void setattributes(const QVector<MTPDocumentAttribute> &attributes);

	void automaticLoad(const HistoryItem *item); // auto load sticker or video
	void automaticLoadSettingsChanged();

	bool loaded(bool check = false) const;
	bool loading() const;
	bool displayLoading() const;
	void save(const QString &toFile, ActionOnLoad action = ActionOnLoadNone, const FullMsgId &actionMsgId = FullMsgId(), LoadFromCloudSetting fromCloud = LoadFromCloudOrLocal, bool autoLoading = false);
	void cancel();
	float64 progress() const;
	int32 loadOffset() const;
	bool uploading() const;

	QString already(bool check = false) const;
	QByteArray data() const;
	const FileLocation &location(bool check = false) const;
	void setLocation(const FileLocation &loc);

	bool saveToCache() const;

	void performActionOnLoad();

	void forget();
	ImagePtr makeReplyPreview();

	StickerData *sticker() {
		return (type == StickerDocument) ? static_cast<StickerData*>(_additional) : 0;
	}
	void checkSticker() {
		StickerData *s = sticker();
		if (!s) return;

		automaticLoad(0);
		if (s->img->isNull() && loaded()) {
			if (_data.isEmpty()) {
				const FileLocation &loc(location(true));
				if (loc.accessEnable()) {
					s->img = ImagePtr(loc.name());
					loc.accessDisable();
				}
			} else {
				s->img = ImagePtr(_data);
			}
		}
	}
	SongData *song() {
		return (type == SongDocument) ? static_cast<SongData*>(_additional) : 0;
	}
	VoiceData *voice() {
		return (type == VoiceDocument) ? static_cast<VoiceData*>(_additional) : 0;
	}
	const VoiceData *voice() const {
		return (type == VoiceDocument) ? static_cast<VoiceData*>(_additional) : 0;
	}
	bool isAnimation() const {
		return (type == AnimatedDocument) || !mime.compare(qstr("image/gif"), Qt::CaseInsensitive);
	}
	bool isGifv() const {
		return (type == AnimatedDocument) && !mime.compare(qstr("video/mp4"), Qt::CaseInsensitive);
	}
	bool isMusic() const {
		return (type == SongDocument) ? !static_cast<SongData*>(_additional)->title.isEmpty() : false;
	}
	bool isVideo() const {
		return (type == VideoDocument);
	}
	int32 duration() const {
		return (isAnimation() || isVideo()) ? _duration : -1;
	}
	bool isImage() const {
		return !isAnimation() && !isVideo() && (_duration > 0);
	}
	void recountIsImage();
	void setData(const QByteArray &data) {
		_data = data;
	}

	~DocumentData();

	DocumentId id;
	DocumentType type;
	QSize dimensions;
	uint64 access;
	int32 date;
	QString name, mime;
	ImagePtr thumb, replyPreview;
	int32 dc;
	int32 size;

	FileStatus status;
	int32 uploadOffset;

	int32 md5[8];

	MediaKey mediaKey() const {
		LocationType t = isVideo() ? VideoFileLocation : (voice() ? AudioFileLocation : DocumentFileLocation);
		return ::mediaKey(t, dc, id);
	}

private:

	FileLocation _location;
	QByteArray _data;
	DocumentAdditionalData *_additional;
	int32 _duration;

	ActionOnLoad _actionOnLoad;
	FullMsgId _actionOnLoadMsgId;
	mutable mtpFileLoader *_loader;

	void notifyLayoutChanged() const;

};

VoiceWaveform documentWaveformDecode(const QByteArray &encoded5bit);
QByteArray documentWaveformEncode5bit(const VoiceWaveform &waveform);

struct SongMsgId {
	SongMsgId() : song(0) {
	}
	SongMsgId(DocumentData *song, const FullMsgId &msgId) : song(song), msgId(msgId) {
	}
	SongMsgId(DocumentData *song, ChannelId channelId, MsgId msgId) : song(song), msgId(channelId, msgId) {
	}
	operator bool() const {
		return song;
	}
	DocumentData *song;
	FullMsgId msgId;

};
inline bool operator<(const SongMsgId &a, const SongMsgId &b) {
	return quintptr(a.song) < quintptr(b.song) || (quintptr(a.song) == quintptr(b.song) && a.msgId < b.msgId);
}
inline bool operator==(const SongMsgId &a, const SongMsgId &b) {
	return a.song == b.song && a.msgId == b.msgId;
}
inline bool operator!=(const SongMsgId &a, const SongMsgId &b) {
	return !(a == b);
}

struct AudioMsgId {
	AudioMsgId() : audio(0) {
	}
	AudioMsgId(DocumentData *audio, const FullMsgId &msgId) : audio(audio), msgId(msgId) {
	}
	AudioMsgId(DocumentData *audio, ChannelId channelId, MsgId msgId) : audio(audio), msgId(channelId, msgId) {
	}
	operator bool() const {
		return audio;
	}
	DocumentData *audio;
	FullMsgId msgId;

};

inline bool operator<(const AudioMsgId &a, const AudioMsgId &b) {
	return quintptr(a.audio) < quintptr(b.audio) || (quintptr(a.audio) == quintptr(b.audio) && a.msgId < b.msgId);
}
inline bool operator==(const AudioMsgId &a, const AudioMsgId &b) {
	return a.audio == b.audio && a.msgId == b.msgId;
}
inline bool operator!=(const AudioMsgId &a, const AudioMsgId &b) {
	return !(a == b);
}

class DocumentLink : public ITextLink {
	TEXT_LINK_CLASS(DocumentLink)

public:
	DocumentLink(DocumentData *document) : _document(document) {
	}
	DocumentData *document() const {
		return _document;
	}

private:
	DocumentData *_document;

};

class DocumentSaveLink : public DocumentLink {
	TEXT_LINK_CLASS(DocumentSaveLink)

public:
	DocumentSaveLink(DocumentData *document) : DocumentLink(document) {
	}
	static void doSave(DocumentData *document, bool forceSavingAs = false);
	void onClick(Qt::MouseButton button) const;

};

class DocumentOpenLink : public DocumentLink {
	TEXT_LINK_CLASS(DocumentOpenLink)

public:
	DocumentOpenLink(DocumentData *document) : DocumentLink(document) {
	}
	static void doOpen(DocumentData *document, ActionOnLoad action = ActionOnLoadOpen);
	void onClick(Qt::MouseButton button) const;

};

class VoiceSaveLink : public DocumentOpenLink {
	TEXT_LINK_CLASS(VoiceSaveLink)

public:
	VoiceSaveLink(DocumentData *document) : DocumentOpenLink(document) {
	}
	void onClick(Qt::MouseButton button) const;

};

class GifOpenLink : public DocumentOpenLink {
	TEXT_LINK_CLASS(GifOpenLink)

public:
	GifOpenLink(DocumentData *document) : DocumentOpenLink(document) {
	}
	void onClick(Qt::MouseButton button) const;

};

class DocumentCancelLink : public DocumentLink {
	TEXT_LINK_CLASS(DocumentCancelLink)

public:
	DocumentCancelLink(DocumentData *document) : DocumentLink(document) {
	}
	void onClick(Qt::MouseButton button) const;

};

enum WebPageType {
	WebPagePhoto,
	WebPageVideo,
	WebPageProfile,
	WebPageArticle
};
inline WebPageType toWebPageType(const QString &type) {
	if (type == qstr("photo")) return WebPagePhoto;
	if (type == qstr("video")) return WebPageVideo;
	if (type == qstr("profile")) return WebPageProfile;
	return WebPageArticle;
}

struct WebPageData {
	WebPageData(const WebPageId &id, WebPageType type = WebPageArticle, const QString &url = QString(), const QString &displayUrl = QString(), const QString &siteName = QString(), const QString &title = QString(), const QString &description = QString(), PhotoData *photo = 0, DocumentData *doc = 0, int32 duration = 0, const QString &author = QString(), int32 pendingTill = -1);

	void forget() {
		if (photo) photo->forget();
	}

	WebPageId id;
	WebPageType type;
	QString url, displayUrl, siteName, title, description;
	int32 duration;
	QString author;
	PhotoData *photo;
	DocumentData *doc;
	int32 pendingTill;

};

class InlineResult {
public:
	InlineResult(uint64 queryId)
		: queryId(queryId)
		, doc(0)
		, photo(0)
		, width(0)
		, height(0)
		, duration(0)
		, noWebPage(false)
		, _loader(0) {
	}
	uint64 queryId;
	QString id, type;
	DocumentData *doc;
	PhotoData *photo;
	QString title, description, url, thumb_url;
	QString content_type, content_url;
	int32 width, height, duration;

	QString message; // botContextMessageText
	bool noWebPage;
	EntitiesInText entities;
	QString caption; // if message.isEmpty() use botContextMessageMediaAuto

	ImagePtr thumb;

	void automaticLoadGif();
	void automaticLoadSettingsChangedGif();
	void saveFile(const QString &toFile, LoadFromCloudSetting fromCloud, bool autoLoading);
	void cancelFile();

	QByteArray data() const;
	bool loading() const;
	bool loaded() const;
	bool displayLoading() const;
	void forget();
	float64 progress() const;

	~InlineResult();

private:
	QByteArray _data;
	mutable webFileLoader *_loader;

};
typedef QList<InlineResult*> InlineResults;

QString saveFileName(const QString &title, const QString &filter, const QString &prefix, QString name, bool savingAs, const QDir &dir = QDir());
MsgId clientMsgId();

struct MessageCursor {
	MessageCursor() : position(0), anchor(0), scroll(QFIXED_MAX) {
	}
	MessageCursor(int position, int anchor, int scroll) : position(position), anchor(anchor), scroll(scroll) {
	}
	MessageCursor(const QTextEdit &edit) {
		fillFrom(edit);
	}
	void fillFrom(const QTextEdit &edit) {
		QTextCursor c = edit.textCursor();
		position = c.position();
		anchor = c.anchor();
		QScrollBar *s = edit.verticalScrollBar();
		scroll = (s && (s->value() != s->maximum())) ? s->value() : QFIXED_MAX;
	}
	void applyTo(QTextEdit &edit) {
		QTextCursor c = edit.textCursor();
		c.setPosition(anchor, QTextCursor::MoveAnchor);
		c.setPosition(position, QTextCursor::KeepAnchor);
		edit.setTextCursor(c);
		QScrollBar *s = edit.verticalScrollBar();
		if (s) s->setValue(scroll);
	}
	int position, anchor, scroll;
};

inline bool operator==(const MessageCursor &a, const MessageCursor &b) {
	return (a.position == b.position) && (a.anchor == b.anchor) && (a.scroll == b.scroll);
}
