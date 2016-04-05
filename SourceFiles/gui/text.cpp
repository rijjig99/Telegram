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
#include "text.h"

#include "lang.h"

#include "pspecific.h"
#include "boxes/confirmbox.h"
#include "window.h"

#include <private/qharfbuzz_p.h>

namespace {

	const QRegularExpression _reDomain(QString::fromUtf8("(?<![\\w\\$\\-\\_%=\\.])(?:([a-zA-Z]+)://)?((?:[A-Za-zА-яА-ЯёЁ0-9\\-\\_]+\\.){1,10}([A-Za-zрф\\-\\d]{2,22})(\\:\\d+)?)"), QRegularExpression::UseUnicodePropertiesOption);
	const QRegularExpression _reExplicitDomain(QString::fromUtf8("(?<![\\w\\$\\-\\_%=\\.])(?:([a-zA-Z]+)://)((?:[A-Za-zА-яА-ЯёЁ0-9\\-\\_]+\\.){0,5}([A-Za-zрф\\-\\d]{2,22})(\\:\\d+)?)"), QRegularExpression::UseUnicodePropertiesOption);
	const QRegularExpression _reMailName(qsl("[a-zA-Z\\-_\\.0-9]{1,256}$"));
	const QRegularExpression _reMailStart(qsl("^[a-zA-Z\\-_\\.0-9]{1,256}\\@"));
	const QRegularExpression _reHashtag(qsl("(^|[\\s\\.,:;<>|'\"\\[\\]\\{\\}`\\~\\!\\%\\^\\*\\(\\)\\-\\+=\\x10])#[\\w]{2,64}([\\W]|$)"), QRegularExpression::UseUnicodePropertiesOption);
	const QRegularExpression _reMention(qsl("(^|[\\s\\.,:;<>|'\"\\[\\]\\{\\}`\\~\\!\\%\\^\\*\\(\\)\\-\\+=\\x10])@[A-Za-z_0-9]{1,32}([\\W]|$)"), QRegularExpression::UseUnicodePropertiesOption);
	const QRegularExpression _reBotCommand(qsl("(^|[\\s\\.,:;<>|'\"\\[\\]\\{\\}`\\~\\!\\%\\^\\*\\(\\)\\-\\+=\\x10])/[A-Za-z_0-9]{1,64}(@[A-Za-z_0-9]{5,32})?([\\W]|$)"));
	const QRegularExpression _rePre(qsl("(^|[\\s\\.,:;<>|'\"\\[\\]\\{\\}`\\~\\!\\?\\%\\^\\*\\(\\)\\-\\+=\\x10])(````?)[\\s\\S]+?(````?)([\\s\\.,:;<>|'\"\\[\\]\\{\\}`\\~\\!\\?\\%\\^\\*\\(\\)\\-\\+=\\x10]|$)"), QRegularExpression::UseUnicodePropertiesOption);
	const QRegularExpression _reCode(qsl("(^|[\\s\\.,:;<>|'\"\\[\\]\\{\\}`\\~\\!\\?\\%\\^\\*\\(\\)\\-\\+=\\x10])(`)[^\\n]+?(`)([\\s\\.,:;<>|'\"\\[\\]\\{\\}`\\~\\!\\?\\%\\^\\*\\(\\)\\-\\+=\\x10]|$)"), QRegularExpression::UseUnicodePropertiesOption);
	QSet<int32> _validProtocols, _validTopDomains;

	const style::textStyle *_textStyle = 0;

	TextLinkPtr _overLnk, _downLnk, _zeroLnk;

	void _initDefault() {
		_textStyle = &st::defaultTextStyle;
	}

	inline int32 _blockHeight(const ITextBlock *b, const style::font &font) {
		return (b->type() == TextBlockTSkip) ? static_cast<const SkipBlock*>(b)->height() : (_textStyle->lineHeight > font->height) ? _textStyle->lineHeight : font->height;
	}

	inline QFixed _blockRBearing(const ITextBlock *b) {
		return (b->type() == TextBlockTText) ? static_cast<const TextBlock*>(b)->f_rbearing() : 0;
	}
}

const QRegularExpression &reDomain() {
	return _reDomain;
}

const QRegularExpression &reMailName() {
	return _reMailName;
}

const QRegularExpression &reMailStart() {
	return _reMailStart;
}

const QRegularExpression &reHashtag() {
	return _reHashtag;
}

const QRegularExpression &reBotCommand() {
	return _reBotCommand;
}

const style::textStyle *textstyleCurrent() {
	return _textStyle;
}

void textstyleSet(const style::textStyle *style) {
	_textStyle = style ? style : &st::defaultTextStyle;
}

void textlnkOver(const TextLinkPtr &lnk) {
	_overLnk = lnk;
}

const TextLinkPtr &textlnkOver() {
	return _overLnk;
}

void textlnkDown(const TextLinkPtr &lnk) {
	_downLnk = lnk;
}

const TextLinkPtr &textlnkDown() {
	return _downLnk;
}

bool textlnkDrawOver(const TextLinkPtr &lnk) {
	return (_overLnk == lnk) && (!_downLnk || _downLnk == lnk);
}

QString textOneLine(const QString &text, bool trim, bool rich) {
	QString result(text);
	const QChar *s = text.unicode(), *ch = s, *e = text.unicode() + text.size();
	if (trim) {
		while (s < e && chIsTrimmed(*s)) {
			++s;
		}
		while (s < e && chIsTrimmed(*(e - 1))) {
			--e;
		}
		if (e - s != text.size()) {
			result = text.mid(s - text.unicode(), e - s);
		}
	}
	for (const QChar *ch = s; ch != e; ++ch) {
		if (chIsNewline(*ch)) {
            result[int(ch - s)] = QChar::Space;
		}
	}
	return result;
}

QString textClean(const QString &text) {
	QString result(text);
	for (const QChar *s = text.unicode(), *ch = s, *e = text.unicode() + text.size(); ch != e; ++ch) {
		if (*ch == TextCommand) {
            result[int(ch - s)] = QChar::Space;
		}
	}
	return result;
}

QString textRichPrepare(const QString &text) {
	QString result;
	result.reserve(text.size());
	const QChar *s = text.constData(), *ch = s;
	for (const QChar *e = s + text.size(); ch != e; ++ch) {
		if (*ch == TextCommand) {
			if (ch > s) result.append(s, ch - s);
			result.append(QChar::Space);
			s = ch + 1;
			continue;
		}
		if (ch->unicode() == '\\' || ch->unicode() == '[') {
			if (ch > s) result.append(s, ch - s);
			result.append('\\');
			s = ch;
			continue;
		}
	}
	if (ch > s) result.append(s, ch - s);
	return result;
}

QString textcmdSkipBlock(ushort w, ushort h) {
	static QString cmd(5, TextCommand);
	cmd[1] = QChar(TextCommandSkipBlock);
	cmd[2] = QChar(w);
	cmd[3] = QChar(h);
	return cmd;
}

QString textcmdStartLink(ushort lnkIndex) {
	static QString cmd(4, TextCommand);
	cmd[1] = QChar(TextCommandLinkIndex);
	cmd[2] = QChar(lnkIndex);
	return cmd;
}

QString textcmdStartLink(const QString &url) {
	if (url.size() >= 4096) return QString();

	QString result;
	result.reserve(url.size() + 4);
	return result.append(TextCommand).append(QChar(TextCommandLinkText)).append(QChar(url.size())).append(url).append(TextCommand);
}

QString textcmdStopLink() {
	return textcmdStartLink(0);
}

QString textcmdLink(ushort lnkIndex, const QString &text) {
	QString result;
	result.reserve(4 + text.size() + 4);
	return result.append(textcmdStartLink(lnkIndex)).append(text).append(textcmdStopLink());
}

QString textcmdLink(const QString &url, const QString &text) {
	QString result;
	result.reserve(4 + url.size() + text.size() + 4);
	return result.append(textcmdStartLink(url)).append(text).append(textcmdStopLink());
}

QString textcmdStartColor(const style::color &color) {
	QString result;
	result.reserve(7);
	return result.append(TextCommand).append(QChar(TextCommandColor)).append(QChar(color->c.red())).append(QChar(color->c.green())).append(QChar(color->c.blue())).append(QChar(color->c.alpha())).append(TextCommand);
}

QString textcmdStopColor() {
	QString result;
	result.reserve(3);
	return result.append(TextCommand).append(QChar(TextCommandNoColor)).append(TextCommand);
}

QString textcmdStartSemibold() {
	QString result;
	result.reserve(3);
	return result.append(TextCommand).append(QChar(TextCommandSemibold)).append(TextCommand);
}

QString textcmdStopSemibold() {
	QString result;
	result.reserve(3);
	return result.append(TextCommand).append(QChar(TextCommandNoSemibold)).append(TextCommand);
}

const QChar *textSkipCommand(const QChar *from, const QChar *end, bool canLink) {
	const QChar *result = from + 1;
	if (*from != TextCommand || result >= end) return from;

	ushort cmd = result->unicode();
	++result;
	if (result >= end) return from;

	switch (cmd) {
	case TextCommandBold:
	case TextCommandNoBold:
	case TextCommandSemibold:
	case TextCommandNoSemibold:
	case TextCommandItalic:
	case TextCommandNoItalic:
	case TextCommandUnderline:
	case TextCommandNoUnderline:
	case TextCommandNoColor:
		break;

	case TextCommandLinkIndex:
		if (result->unicode() > 0x7FFF) return from;
		++result;
		break;

	case TextCommandLinkText: {
		ushort len = result->unicode();
		if (len >= 4096 || !canLink) return from;
		result += len + 1;
	} break;

	case TextCommandColor: {
		const QChar *e = result + 4;
		if (e >= end) return from;

		for (; result < e; ++result) {
			if (result->unicode() >= 256) return from;
		}
	} break;

	case TextCommandSkipBlock:
		result += 2;
		break;

	case TextCommandLangTag:
		result += 1;
		break;
	}
	return (result < end && *result == TextCommand) ? (result + 1) : from;
}

class TextParser {
public:

	static Qt::LayoutDirection stringDirection(const QString &str, int32 from, int32 to) {
		const ushort *p = reinterpret_cast<const ushort*>(str.unicode()) + from;
		const ushort *end = p + (to - from);
		while (p < end) {
			uint ucs4 = *p;
			if (QChar::isHighSurrogate(ucs4) && p < end - 1) {
				ushort low = p[1];
				if (QChar::isLowSurrogate(low)) {
					ucs4 = QChar::surrogateToUcs4(ucs4, low);
					++p;
				}
			}
			switch (QChar::direction(ucs4)) {
			case QChar::DirL:
				return Qt::LeftToRight;
			case QChar::DirR:
			case QChar::DirAL:
				return Qt::RightToLeft;
			default:
				break;
			}
			++p;
		}
		return Qt::LayoutDirectionAuto;
	}

	void blockCreated() {
		sumWidth += _t->_blocks.back()->f_width();
		if (sumWidth.floor().toInt() > stopAfterWidth) {
			sumFinished = true;
		}
	}

	void createBlock(int32 skipBack = 0) {
		if (lnkIndex < 0x8000 && lnkIndex > maxLnkIndex) maxLnkIndex = lnkIndex;
		int32 len = int32(_t->_text.size()) + skipBack - blockStart;
		if (len > 0) {
			bool newline = !emoji && (len == 1 && _t->_text.at(blockStart) == QChar::LineFeed);
			if (newlineAwaited) {
				newlineAwaited = false;
				if (!newline) {
					_t->_text.insert(blockStart, QChar::LineFeed);
					createBlock(skipBack - len);
				}
			}
			lastSkipped = false;
			if (emoji) {
				_t->_blocks.push_back(new EmojiBlock(_t->_font, _t->_text, blockStart, len, flags, color, lnkIndex, emoji));
				emoji = 0;
				lastSkipped = true;
			} else if (newline) {
				_t->_blocks.push_back(new NewlineBlock(_t->_font, _t->_text, blockStart, len));
			} else {
				_t->_blocks.push_back(new TextBlock(_t->_font, _t->_text, _t->_minResizeWidth, blockStart, len, flags, color, lnkIndex));
			}
			blockStart += len;
			blockCreated();
		}
	}

	void createSkipBlock(int32 w, int32 h) {
		createBlock();
		_t->_text.push_back('_');
		_t->_blocks.push_back(new SkipBlock(_t->_font, _t->_text, blockStart++, w, h, lnkIndex));
		blockCreated();
	}

	void createNewlineBlock() {
		createBlock();
		_t->_text.push_back(QChar::LineFeed);
		createBlock();
	}

	void getLinkData(const QString &original, QString &result, int32 &fullDisplayed) {
		if (!original.isEmpty() && original.at(0) == '/') {
			result = original;
			fullDisplayed = -4; // bot command
		} else if (!original.isEmpty() && original.at(0) == '@') {
			result = original;
			fullDisplayed = -3; // mention
		} else if (!original.isEmpty() && original.at(0) == '#') {
			result = original;
			fullDisplayed = -2; // hashtag
		} else if (_reMailStart.match(original).hasMatch()) {
			result = original;
			fullDisplayed = -1; // email
		} else {
			QUrl url(original), good(url.isValid() ? url.toEncoded() : "");
			QString readable = good.isValid() ? good.toDisplayString() : original;
			result = _t->_font->elided(readable, st::linkCropLimit);
			fullDisplayed = (result == readable) ? 1 : 0;
		}
	}

	bool checkCommand() {
		bool result = false;
		for (QChar c = ((ptr < end) ? *ptr : 0); c == TextCommand; c = ((ptr < end) ? *ptr : 0)) {
			if (!readCommand()) {
				break;
			}
			result = true;
		}
		return result;
	}

	void checkEntities() {
		while (!removeFlags.isEmpty() && (ptr >= removeFlags.firstKey() || ptr >= end)) {
			const QList<int32> &removing(removeFlags.first());
			for (int32 i = removing.size(); i > 0;) {
				int32 flag = removing.at(--i);
				if (flags & flag) {
					createBlock();
					flags &= ~flag;
					if (flag == TextBlockFPre) {
						newlineAwaited = true;
					}
				}
			}
			removeFlags.erase(removeFlags.begin());
		}
		while (waitingEntity != entitiesEnd && start + waitingEntity->offset + waitingEntity->length <= ptr) {
			++waitingEntity;
		}
		if (waitingEntity == entitiesEnd || ptr < start + waitingEntity->offset) {
			return;
		}

		bool lnk = false;
		int32 startFlags = 0;
		int32 fullDisplayed;
		QString lnkUrl, lnkText;
		if (waitingEntity->type == EntityInTextCustomUrl) {
			lnk = true;
			lnkUrl = waitingEntity->text;
			lnkText = QString(start + waitingEntity->offset, waitingEntity->length);
			fullDisplayed = -5;
		} else if (waitingEntity->type == EntityInTextBold) {
			startFlags = TextBlockFSemibold;
		} else if (waitingEntity->type == EntityInTextItalic) {
			startFlags = TextBlockFItalic;
		} else if (waitingEntity->type == EntityInTextCode) {
			startFlags = TextBlockFCode;
		} else if (waitingEntity->type == EntityInTextPre) {
			startFlags = TextBlockFPre;
			createBlock();
			if (!_t->_blocks.isEmpty() && _t->_blocks.back()->type() != TextBlockTNewline) {
				createNewlineBlock();
			}
		} else {
			lnk = true;
			lnkUrl = QString(start + waitingEntity->offset, waitingEntity->length);
			getLinkData(lnkUrl, lnkText, fullDisplayed);
		}

		if (lnk) {
			createBlock();

			links.push_back(TextLinkData(lnkUrl, fullDisplayed));
			lnkIndex = 0x8000 + links.size();

			_t->_text += lnkText;
			ptr = start + waitingEntity->offset + waitingEntity->length;

			createBlock();

			lnkIndex = 0;
		} else if (startFlags) {
			if (!(flags & startFlags)) {
				createBlock();
				flags |= startFlags;
				removeFlags[start + waitingEntity->offset + waitingEntity->length].push_front(startFlags);
			}
		}

		++waitingEntity;
		if (links.size() >= 0x7FFF) {
			while (waitingEntity != entitiesEnd && (
				waitingEntity->type == EntityInTextUrl ||
				waitingEntity->type == EntityInTextCustomUrl ||
				waitingEntity->type == EntityInTextEmail ||
				waitingEntity->type == EntityInTextHashtag ||
				waitingEntity->type == EntityInTextMention ||
				waitingEntity->type == EntityInTextBotCommand ||
				waitingEntity->length <= 0)) {
				++waitingEntity;
			}
		} else {
			while (waitingEntity != entitiesEnd && waitingEntity->length <= 0) ++waitingEntity;
		}
	}

	bool readSkipBlockCommand() {
		const QChar *afterCmd = textSkipCommand(ptr, end, links.size() < 0x7FFF);
		if (afterCmd == ptr) {
			return false;
		}

		ushort cmd = (++ptr)->unicode();
		++ptr;

		switch (cmd) {
		case TextCommandSkipBlock:
			createSkipBlock(ptr->unicode(), (ptr + 1)->unicode());
		break;
		}

		ptr = afterCmd;
		return true;
	}

	bool readCommand() {
		const QChar *afterCmd = textSkipCommand(ptr, end, links.size() < 0x7FFF);
		if (afterCmd == ptr) {
			return false;
		}

		ushort cmd = (++ptr)->unicode();
		++ptr;

		switch (cmd) {
		case TextCommandBold:
			if (!(flags & TextBlockFBold)) {
				createBlock();
				flags |= TextBlockFBold;
			}
		break;

		case TextCommandNoBold:
			if (flags & TextBlockFBold) {
				createBlock();
				flags &= ~TextBlockFBold;
			}
		break;

		case TextCommandSemibold:
		if (!(flags & TextBlockFSemibold)) {
			createBlock();
			flags |= TextBlockFSemibold;
		}
		break;

		case TextCommandNoSemibold:
		if (flags & TextBlockFSemibold) {
			createBlock();
			flags &= ~TextBlockFSemibold;
		}
		break;

		case TextCommandItalic:
			if (!(flags & TextBlockFItalic)) {
				createBlock();
				flags |= TextBlockFItalic;
			}
		break;

		case TextCommandNoItalic:
			if (flags & TextBlockFItalic) {
				createBlock();
				flags &= ~TextBlockFItalic;
			}
		break;

		case TextCommandUnderline:
			if (!(flags & TextBlockFUnderline)) {
				createBlock();
				flags |= TextBlockFUnderline;
			}
		break;

		case TextCommandNoUnderline:
			if (flags & TextBlockFUnderline) {
				createBlock();
				flags &= ~TextBlockFUnderline;
			}
		break;

		case TextCommandLinkIndex:
			if (ptr->unicode() != lnkIndex) {
				createBlock();
				lnkIndex = ptr->unicode();
			}
		break;

		case TextCommandLinkText: {
			createBlock();
			int32 len = ptr->unicode();
			links.push_back(TextLinkData(QString(++ptr, len), false));
			lnkIndex = 0x8000 + links.size();
		} break;

		case TextCommandColor: {
			style::color c(ptr->unicode(), (ptr + 1)->unicode(), (ptr + 2)->unicode(), (ptr + 3)->unicode());
			if (color != c) {
				createBlock();
				color = c;
			}
		} break;

		case TextCommandSkipBlock:
			createSkipBlock(ptr->unicode(), (ptr + 1)->unicode());
		break;

		case TextCommandNoColor:
			if (color) {
				createBlock();
				color = style::color();
			}
		break;
		}

		ptr = afterCmd;
		return true;
	}

	void parseCurrentChar() {
		int skipBack = 0;
		ch = ((ptr < end) ? *ptr : 0);
		emojiLookback = 0;
		bool skip = false, isNewLine = multiline && chIsNewline(ch), isSpace = chIsSpace(ch), isDiac = chIsDiac(ch), isTilde = checkTilde && (ch == '~');
		if (chIsBad(ch) || ch.isLowSurrogate()) {
			skip = true;
		} else if (isDiac) {
			if (lastSkipped || emoji || ++diacs > chMaxDiacAfterSymbol()) {
				skip = true;
			}
		} else if (ch.isHighSurrogate()) {
			if (ptr + 1 >= end || !(ptr + 1)->isLowSurrogate()) {
				skip = true;
			} else {
				_t->_text.push_back(ch);
				skipBack = -1;
				++ptr;
				ch = *ptr;
				emojiLookback = 1;
			}
		}

		lastSkipped = skip;
		if (skip) {
			ch = 0;
		} else {
			if (isTilde) { // tilde fix in OpenSans
				if (!(flags & TextBlockFTilde)) {
					createBlock(skipBack);
					flags |= TextBlockFTilde;
				}
			} else {
				if (flags & TextBlockFTilde) {
					createBlock(skipBack);
					flags &= ~TextBlockFTilde;
				}
			}
			if (isNewLine) {
				createNewlineBlock();
			} else if (isSpace) {
				_t->_text.push_back(QChar::Space);
			} else {
				if (emoji) createBlock(skipBack);
				_t->_text.push_back(ch);
			}
			if (!isDiac) diacs = 0;
		}
	}

	void parseEmojiFromCurrent() {
		int len = 0;
		EmojiPtr e = emojiFromText(ptr - emojiLookback, end, &len);
		if (!e) return;

		for (int l = len - emojiLookback - 1; l > 0; --l) {
			_t->_text.push_back(*++ptr);
		}
		if (e->postfix && _t->_text.at(_t->_text.size() - 1).unicode() != e->postfix) {
			_t->_text.push_back(e->postfix);
			++len;
		}

		createBlock(-len);
		emoji = e;
	}

	TextParser(Text *t, const QString &text, const TextParseOptions &options) : _t(t),
		src(text),
		rich(options.flags & TextParseRichText),
		multiline(options.flags & TextParseMultiline),
		maxLnkIndex(0),
		flags(0),
		lnkIndex(0),
		stopAfterWidth(QFIXED_MAX) {
		if (options.flags & TextParseLinks) {
			entities = textParseEntities(src, options.flags, rich);
		}
		parse(options);
	}
	TextParser(Text *t, const QString &text, const EntitiesInText &preparsed, const TextParseOptions &options) : _t(t),
		src(text),
		rich(options.flags & TextParseRichText),
		multiline(options.flags & TextParseMultiline),
		maxLnkIndex(0),
		flags(0),
		lnkIndex(0),
		stopAfterWidth(QFIXED_MAX) {
		if ((options.flags & TextParseLinks) && !preparsed.isEmpty()) {
			bool parseMentions = (options.flags & TextParseMentions);
			bool parseHashtags = (options.flags & TextParseHashtags);
			bool parseBotCommands = (options.flags & TextParseBotCommands);
			bool parseMono = (options.flags & TextParseMono);
			if (parseMentions && parseHashtags && parseBotCommands && parseMono) {
				entities = preparsed;
			} else {
				int32 i = 0, l = preparsed.size();
				entities.reserve(l);
				const QChar *p = text.constData(), s = text.size();
				for (; i < l; ++i) {
					EntityInTextType t = preparsed.at(i).type;
					if ((t == EntityInTextMention && !parseMentions) ||
						(t == EntityInTextHashtag && !parseHashtags) ||
						(t == EntityInTextBotCommand && !parseBotCommands) ||
						((t == EntityInTextBold || t == EntityInTextItalic || t == EntityInTextCode || t == EntityInTextPre) && !parseMono)) {
						continue;
					}
					entities.push_back(preparsed.at(i));
				}
			}
		}
		parse(options);
	}
	void parse(const TextParseOptions &options) {
		if (options.maxw > 0 && options.maxh > 0) {
			stopAfterWidth = ((options.maxh / _t->_font->height) + 1) * options.maxw;
		}

		start = src.constData();
		end = start + src.size();

		ptr = start;
		while (ptr != end && chIsTrimmed(*ptr, rich)) {
			++ptr;
		}
		while (ptr != end && chIsTrimmed(*(end - 1), rich)) {
			--end;
		}

		_t->_text.resize(0);
		_t->_text.reserve(end - ptr);

		diacs = 0;
		sumWidth = 0;
		sumFinished = newlineAwaited = false;
		blockStart = 0;
		emoji = 0;

		ch = emojiLookback = 0;
		lastSkipped = false;
		checkTilde = !cRetina() && _t->_font->size() == 13 && _t->_font->flags() == 0; // tilde Open Sans fix
		entitiesEnd = entities.cend();
		waitingEntity = entities.cbegin();
		while (waitingEntity != entitiesEnd && waitingEntity->length <= 0) ++waitingEntity;
		for (; ptr <= end; ++ptr) {
			checkEntities();
			if (rich) {
				if (checkCommand()) {
					checkEntities();
				}
			}
			parseCurrentChar();
			parseEmojiFromCurrent();

			if (sumFinished || _t->_text.size() >= 0x8000) break; // 32k max
		}
		createBlock();
		if (sumFinished && rich) { // we could've skipped the final skip block command
			for (; ptr < end; ++ptr) {
				if (*ptr == TextCommand && readSkipBlockCommand()) {
					break;
				}
			}
		}
		removeFlags.clear();

		_t->_links.resize(maxLnkIndex);
		for (Text::TextBlocks::const_iterator i = _t->_blocks.cbegin(), e = _t->_blocks.cend(); i != e; ++i) {
			ITextBlock *b = *i;
			if (b->lnkIndex() > 0x8000) {
				lnkIndex = maxLnkIndex + (b->lnkIndex() - 0x8000);
				if (_t->_links.size() < lnkIndex) {
					_t->_links.resize(lnkIndex);
					const TextLinkData &data(links[lnkIndex - maxLnkIndex - 1]);
					TextLinkPtr lnk;
					if (data.fullDisplayed < -4) { // hidden link
						lnk = TextLinkPtr(new CustomTextLink(data.url));
					} else if (data.fullDisplayed < -3) { // bot command
						lnk = TextLinkPtr(new BotCommandLink(data.url));
					} else if (data.fullDisplayed < -2) { // mention
						if (options.flags & TextTwitterMentions) {
							lnk = TextLinkPtr(new TextLink(qsl("https://twitter.com/") + data.url.mid(1), true));
						} else if (options.flags & TextInstagramMentions) {
							lnk = TextLinkPtr(new TextLink(qsl("https://instagram.com/") + data.url.mid(1) + '/', true));
						} else {
							lnk = TextLinkPtr(new MentionLink(data.url));
						}
					} else if (data.fullDisplayed < -1) { // hashtag
						if (options.flags & TextTwitterMentions) {
							lnk = TextLinkPtr(new TextLink(qsl("https://twitter.com/hashtag/") + data.url.mid(1) + qsl("?src=hash"), true));
						} else if (options.flags & TextInstagramMentions) {
							lnk = TextLinkPtr(new TextLink(qsl("https://instagram.com/explore/tags/") + data.url.mid(1) + '/', true));
						} else {
							lnk = TextLinkPtr(new HashtagLink(data.url));
						}
					} else if (data.fullDisplayed < 0) { // email
						lnk = TextLinkPtr(new EmailLink(data.url));
					} else {
						lnk = TextLinkPtr(new TextLink(data.url, data.fullDisplayed > 0));
					}
					_t->setLink(lnkIndex, lnk);
				}
				b->setLnkIndex(lnkIndex);
			}
		}
		_t->_links.squeeze();
		_t->_blocks.squeeze();
		_t->_text.squeeze();
	}

private:

	Text *_t;
	QString src;
	const QChar *start, *end, *ptr;
	bool rich, multiline;

	EntitiesInText entities;
	EntitiesInText::const_iterator waitingEntity, entitiesEnd;

	struct TextLinkData {
		TextLinkData(const QString &url = QString(), int32 fullDisplayed = 1) : url(url), fullDisplayed(fullDisplayed) {
		}
		QString url;
		int32 fullDisplayed; // -5 - custom text link, -4 - bot command, -3 - mention, -2 - hashtag, -1 - email
	};
	typedef QVector<TextLinkData> TextLinks;
	TextLinks links;

	typedef QMap<const QChar*, QList<int32> > RemoveFlagsMap;
	RemoveFlagsMap removeFlags;

	uint16 maxLnkIndex;

	// current state
	int32 flags;
	uint16 lnkIndex;
	const EmojiData *emoji; // current emoji, if current word is an emoji, or zero
	int32 blockStart; // offset in result, from which current parsed block is started
	int32 diacs; // diac chars skipped without good char
	QFixed sumWidth, stopAfterWidth; // summary width of all added words
	bool sumFinished, newlineAwaited;
	style::color color; // current color, could be invalid

	// current char data
	QChar ch; // current char (low surrogate, if current char is surrogate pair)
	int32 emojiLookback; // how far behind the current ptr to look for current emoji
	bool lastSkipped; // did we skip current char
	bool checkTilde; // do we need a special text block for tilde symbol
};

namespace {
	// COPIED FROM qtextengine.cpp AND MODIFIED

	struct BidiStatus {
		BidiStatus() {
			eor = QChar::DirON;
			lastStrong = QChar::DirON;
			last = QChar:: DirON;
			dir = QChar::DirON;
		}
		QChar::Direction eor;
		QChar::Direction lastStrong;
		QChar::Direction last;
		QChar::Direction dir;
	};

	enum { _MaxBidiLevel = 61 };
	enum { _MaxItemLength = 4096 };

	struct BidiControl {
		inline BidiControl(bool rtl)
			: cCtx(0), base(rtl ? 1 : 0), level(rtl ? 1 : 0), override(false) {}

		inline void embed(bool rtl, bool o = false) {
			unsigned int toAdd = 1;
			if((level%2 != 0) == rtl ) {
				++toAdd;
			}
			if (level + toAdd <= _MaxBidiLevel) {
				ctx[cCtx].level = level;
				ctx[cCtx].override = override;
				cCtx++;
				override = o;
				level += toAdd;
			}
		}
		inline bool canPop() const { return cCtx != 0; }
		inline void pdf() {
			Q_ASSERT(cCtx);
			--cCtx;
			level = ctx[cCtx].level;
			override = ctx[cCtx].override;
		}

		inline QChar::Direction basicDirection() const {
			return (base ? QChar::DirR : QChar:: DirL);
		}
		inline unsigned int baseLevel() const {
			return base;
		}
		inline QChar::Direction direction() const {
			return ((level%2) ? QChar::DirR : QChar:: DirL);
		}

		struct {
			unsigned int level;
			bool override;
		} ctx[_MaxBidiLevel];
		unsigned int cCtx;
		const unsigned int base;
		unsigned int level;
		bool override;
	};

	static void eAppendItems(QScriptAnalysis *analysis, int &start, int &stop, const BidiControl &control, QChar::Direction dir) {
		if (start > stop)
			return;

		int level = control.level;

		if(dir != QChar::DirON && !control.override) {
			// add level of run (cases I1 & I2)
			if(level % 2) {
				if(dir == QChar::DirL || dir == QChar::DirAN || dir == QChar::DirEN)
					level++;
			} else {
				if(dir == QChar::DirR)
					level++;
				else if(dir == QChar::DirAN || dir == QChar::DirEN)
					level += 2;
			}
		}

		QScriptAnalysis *s = analysis + start;
		const QScriptAnalysis *e = analysis + stop;
		while (s <= e) {
			s->bidiLevel = level;
			++s;
		}
		++stop;
		start = stop;
	}
}

void TextLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		PopupTooltip::Hide();

		QString url = TextLink::encoded();
		QRegularExpressionMatch telegramMeUser = QRegularExpression(qsl("^https?://telegram\\.me/([a-zA-Z0-9\\.\\_]+)(/?\\?|/?$|/(\\d+)/?(?:\\?|$))"), QRegularExpression::CaseInsensitiveOption).match(url);
		QRegularExpressionMatch telegramMeGroup = QRegularExpression(qsl("^https?://telegram\\.me/joinchat/([a-zA-Z0-9\\.\\_\\-]+)(\\?|$)"), QRegularExpression::CaseInsensitiveOption).match(url);
		QRegularExpressionMatch telegramMeStickers = QRegularExpression(qsl("^https?://telegram\\.me/addstickers/([a-zA-Z0-9\\.\\_]+)(\\?|$)"), QRegularExpression::CaseInsensitiveOption).match(url);
		QRegularExpressionMatch telegramMeShareUrl = QRegularExpression(qsl("^https?://telegram\\.me/share/url\\?(.+)$"), QRegularExpression::CaseInsensitiveOption).match(url);
		if (telegramMeGroup.hasMatch()) {
			url = qsl("tg://join?invite=") + myUrlEncode(telegramMeGroup.captured(1));
		} else if (telegramMeStickers.hasMatch()) {
			url = qsl("tg://addstickers?set=") + myUrlEncode(telegramMeStickers.captured(1));
		} else if (telegramMeShareUrl.hasMatch()) {
			url = qsl("tg://msg_url?") + telegramMeShareUrl.captured(1);
		} else if (telegramMeUser.hasMatch()) {
			QString params = url.mid(telegramMeUser.captured(0).size()), postParam;
			if (QRegularExpression(qsl("^/\\d+/?(?:\\?|$)")).match(telegramMeUser.captured(2)).hasMatch()) {
				postParam = qsl("&post=") + telegramMeUser.captured(3);
			}
			url = qsl("tg://resolve/?domain=") + myUrlEncode(telegramMeUser.captured(1)) + postParam + (params.isEmpty() ? QString() : '&' + params);
		}

		if (QRegularExpression(qsl("^tg://[a-zA-Z0-9]+"), QRegularExpression::CaseInsensitiveOption).match(url).hasMatch()) {
			App::openLocalUrl(url);
		} else {
			QDesktopServices::openUrl(url);
		}
	}
}

void EmailLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		PopupTooltip::Hide();
		QUrl url(qstr("mailto:") + _email);
		if (!QDesktopServices::openUrl(url)) {
			psOpenFile(url.toString(QUrl::FullyEncoded), true);
		}
	}
}

void CustomTextLink::onClick(Qt::MouseButton button) const {
	Ui::showLayer(new ConfirmLinkBox(text()));
}

void LocationLink::onClick(Qt::MouseButton button) const {
	if (!psLaunchMaps(_coords)) {
		QDesktopServices::openUrl(_text);
	}
}

void LocationLink::setup() {
	QString latlon(qsl("%1,%2").arg(_coords.lat).arg(_coords.lon));
	_text = qsl("https://maps.google.com/maps?q=") + latlon + qsl("&ll=") + latlon + qsl("&z=16");
}

void MentionLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		App::openPeerByName(_tag.mid(1), ShowAtProfileMsgId);
	}
}

void HashtagLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
		App::searchByHashtag(_tag, App::mousedItem() ? App::mousedItem()->history()->peer : 0);
	}
}

void BotCommandLink::onClick(Qt::MouseButton button) const {
	if (button == Qt::LeftButton || button == Qt::MiddleButton) {
//		App::insertBotCommand(_cmd);
		App::sendBotCommand(_cmd);
	}
}

class TextPainter {
public:

	static inline uint16 _blockEnd(const Text *t, const Text::TextBlocks::const_iterator &i, const Text::TextBlocks::const_iterator &e) {
		return (i + 1 == e) ? t->_text.size() : (*(i + 1))->from();
	}
	static inline uint16 _blockLength(const Text *t, const Text::TextBlocks::const_iterator &i, const Text::TextBlocks::const_iterator &e) {
		return _blockEnd(t, i, e) - (*i)->from();
	}

	TextPainter(QPainter *p, const Text *t) : _p(p), _t(t), _elideLast(false), _breakEverywhere(false), _elideRemoveFromEnd(0), _str(0), _elideSavedBlock(0), _lnkResult(0), _inTextFlag(0), _getSymbol(0), _getSymbolAfter(0), _getSymbolUpon(0) {
	}

	void initNextParagraph(Text::TextBlocks::const_iterator i) {
		_parStartBlock = i;
		Text::TextBlocks::const_iterator e = _t->_blocks.cend();
		if (i == e) {
			_parStart = _t->_text.size();
			_parLength = 0;
		} else {
			_parStart = (*i)->from();
			for (; i != e; ++i) {
				if ((*i)->type() == TextBlockTNewline) {
					break;
				}
			}
			_parLength = ((i == e) ? _t->_text.size() : (*i)->from()) - _parStart;
		}
		_parAnalysis.resize(0);
	}

	void initParagraphBidi() {
		if (!_parLength || !_parAnalysis.isEmpty()) return;

		Text::TextBlocks::const_iterator i = _parStartBlock, e = _t->_blocks.cend(), n = i + 1;

		bool ignore = false;
		bool rtl = (_parDirection == Qt::RightToLeft);
		if (!ignore && !rtl) {
			ignore = true;
			const ushort *start = reinterpret_cast<const ushort*>(_str) + _parStart;
			const ushort *curr = start;
			const ushort *end = start + _parLength;
			while (curr < end) {
				while (n != e && (*n)->from() <= _parStart + (curr - start)) {
					i = n;
					++n;
				}
				if ((*i)->type() != TextBlockTEmoji && *curr >= 0x590) {
					ignore = false;
					break;
				}
				++curr;
			}
		}

		_parAnalysis.resize(_parLength);
		QScriptAnalysis *analysis = _parAnalysis.data();

		BidiControl control(rtl);

		_parHasBidi = false;
		if (ignore) {
			memset(analysis, 0, _parLength * sizeof(QScriptAnalysis));
			if (rtl) {
				for (int i = 0; i < _parLength; ++i)
					analysis[i].bidiLevel = 1;
				_parHasBidi = true;
			}
		} else {
			_parHasBidi = eBidiItemize(analysis, control);
		}
	}

	void draw(int32 left, int32 top, int32 w, style::align align, int32 yFrom, int32 yTo, uint16 selectedFrom = 0, uint16 selectedTo = 0) {
		if (_t->isEmpty()) return;

		_blocksSize = _t->_blocks.size();
		if (!_textStyle) _initDefault();

		if (_p) {
			_p->setFont(_t->_font->f);
			_originalPen = _p->pen();
		}

		_x = left;
		_y = top;
		_yFrom = yFrom + top;
		_yTo = (yTo < 0) ? -1 : (yTo + top);
		if (_elideLast) {
			_yToElide = _yTo;
		}
		_selectedFrom = selectedFrom;
		_selectedTo = selectedTo;
		_wLeft = _w = w;
		_str = _t->_text.unicode();

		if (_p) {
			QRectF clip = _p->clipBoundingRect();
			if (clip.width() > 0 || clip.height() > 0) {
				if (_yFrom < clip.y()) _yFrom = clip.y();
				if (_yTo < 0 || _yTo > clip.y() + clip.height()) _yTo = clip.y() + clip.height();
			}
		}

		_align = align;

		_parDirection = _t->_startDir;
		if (_parDirection == Qt::LayoutDirectionAuto) _parDirection = cLangDir();
		if ((*_t->_blocks.cbegin())->type() != TextBlockTNewline) {
			initNextParagraph(_t->_blocks.cbegin());
		}

		_lineStart = 0;
		_lineStartBlock = 0;

		_lineHeight = 0;
		_fontHeight = _t->_font->height;
		QFixed last_rBearing = 0, last_rPadding = 0;

		int32 blockIndex = 0;
		bool longWordLine = true;
		Text::TextBlocks::const_iterator e = _t->_blocks.cend();
		for (Text::TextBlocks::const_iterator i = _t->_blocks.cbegin(); i != e; ++i, ++blockIndex) {
			ITextBlock *b = *i;
			TextBlockType _btype = b->type();
			int32 blockHeight = _blockHeight(b, _t->_font);
			QFixed _rb = _blockRBearing(b);

			if (_btype == TextBlockTNewline) {
				if (!_lineHeight) _lineHeight = blockHeight;
				ushort nextStart = _blockEnd(_t, i, e);
				if (!drawLine(nextStart, i + 1, e)) return;

				_y += _lineHeight;
				_lineHeight = 0;
				_lineStart = nextStart;
				_lineStartBlock = blockIndex + 1;

				last_rBearing = _rb;
				last_rPadding = b->f_rpadding();
				_wLeft = _w - (b->f_width() - last_rBearing);
				if (_elideLast && _elideRemoveFromEnd > 0 && (_y + blockHeight >= _yToElide)) {
					_wLeft -= _elideRemoveFromEnd;
				}

				_parDirection = static_cast<NewlineBlock*>(b)->nextDirection();
				if (_parDirection == Qt::LayoutDirectionAuto) _parDirection = cLangDir();
				initNextParagraph(i + 1);

				longWordLine = true;
				continue;
			}

			QFixed lpadding = b->f_lpadding();
			QFixed newWidthLeft = _wLeft - lpadding - last_rBearing - (last_rPadding + b->f_width() - _rb);
			if (newWidthLeft >= 0) {
				last_rBearing = _rb;
				last_rPadding = b->f_rpadding();
				_wLeft = newWidthLeft;

				_lineHeight = qMax(_lineHeight, blockHeight);

				longWordLine = false;
				continue;
			}

			if (_btype == TextBlockTText) {
				TextBlock *t = static_cast<TextBlock*>(b);
				if (t->_words.isEmpty()) { // no words in this block, spaces only => layout this block in the same line
					last_rPadding += lpadding;

					_lineHeight = qMax(_lineHeight, blockHeight);

					longWordLine = false;
					continue;
				}

				QFixed f_wLeft = _wLeft; // vars for saving state of the last word start
				int32 f_lineHeight = _lineHeight; // f points to the last word-start element of t->_words
				for (TextBlock::TextWords::const_iterator j = t->_words.cbegin(), en = t->_words.cend(), f = j; j != en; ++j) {
					bool wordEndsHere = (j->width >= 0);
					QFixed j_width = wordEndsHere ? j->width : -j->width;

					QFixed newWidthLeft = _wLeft - lpadding - last_rBearing - (last_rPadding + j_width - j->f_rbearing());
					lpadding = 0;
					if (newWidthLeft >= 0) {
						last_rBearing = j->f_rbearing();
						last_rPadding = j->rpadding;
						_wLeft = newWidthLeft;

						_lineHeight = qMax(_lineHeight, blockHeight);

						if (wordEndsHere) {
							longWordLine = false;
						}
						if (wordEndsHere || longWordLine) {
							f = j + 1;
							f_wLeft = _wLeft;
							f_lineHeight = _lineHeight;
						}
						continue;
					}

					int32 elidedLineHeight = qMax(_lineHeight, blockHeight);
					bool elidedLine = _elideLast && (_y + elidedLineHeight >= _yToElide);
					if (elidedLine) {
						_lineHeight = elidedLineHeight;
					} else if (f != j && !_breakEverywhere) {
						// word did not fit completely, so we roll back the state to the beginning of this long word
						j = f;
						_wLeft = f_wLeft;
						_lineHeight = f_lineHeight;
						j_width = (j->width >= 0) ? j->width : -j->width;
					}
					if (!drawLine(elidedLine ? ((j + 1 == en) ? _blockEnd(_t, i, e) : (j + 1)->from) : j->from, i, e)) return;
					_y += _lineHeight;
					_lineHeight = qMax(0, blockHeight);
					_lineStart = j->from;
					_lineStartBlock = blockIndex;

					last_rBearing = j->f_rbearing();
					last_rPadding = j->rpadding;
					_wLeft = _w - (j_width - last_rBearing);
					if (_elideLast && _elideRemoveFromEnd > 0 && (_y + blockHeight >= _yToElide)) {
						_wLeft -= _elideRemoveFromEnd;
					}

					longWordLine = true;
					f = j + 1;
					f_wLeft = _wLeft;
					f_lineHeight = _lineHeight;
				}
				continue;
			}

			int32 elidedLineHeight = qMax(_lineHeight, blockHeight);
			bool elidedLine = _elideLast && (_y + elidedLineHeight >= _yToElide);
			if (elidedLine) {
				_lineHeight = elidedLineHeight;
			}
			if (!drawLine(elidedLine ? _blockEnd(_t, i, e) : b->from(), i, e)) return;
			_y += _lineHeight;
			_lineHeight = qMax(0, blockHeight);
			_lineStart = b->from();
			_lineStartBlock = blockIndex;

			last_rBearing = _rb;
			last_rPadding = b->f_rpadding();
			_wLeft = _w - (b->f_width() - last_rBearing);
			if (_elideLast && _elideRemoveFromEnd > 0 && (_y + blockHeight >= _yToElide)) {
				_wLeft -= _elideRemoveFromEnd;
			}

			longWordLine = true;
			continue;
		}
		if (_lineStart < _t->_text.size()) {
			if (!drawLine(_t->_text.size(), e, e)) return;
		}
		if (_getSymbol) {
			*_getSymbol = _t->_text.size();
			*_getSymbolAfter = false;
			*_getSymbolUpon = false;
		}
	}

	void drawElided(int32 left, int32 top, int32 w, style::align align, int32 lines, int32 yFrom, int32 yTo, int32 removeFromEnd, bool breakEverywhere) {
		if (lines <= 0 || _t->isNull()) return;

		if (yTo < 0 || (lines - 1) * _t->_font->height < yTo) {
			yTo = lines * _t->_font->height;
			_elideLast = true;
			_elideRemoveFromEnd = removeFromEnd;
		}
		_breakEverywhere = breakEverywhere;
		draw(left, top, w, align, yFrom, yTo);
	}

	const TextLinkPtr &link(int32 x, int32 y, int32 w, style::align align) {
		_lnkX = x;
		_lnkY = y;
		_lnkResult = &_zeroLnk;
		if (!_t->isNull() && _lnkX >= 0 && _lnkX < w && _lnkY >= 0) {
			draw(0, 0, w, align, _lnkY, _lnkY + 1);
		}
		return *_lnkResult;
	}

	void getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y, int32 w, style::align align, bool breakEverywhere) {
		lnk = TextLinkPtr();
		inText = false;

		if (!_t->isNull() && x >= 0 && x < w && y >= 0) {
			_lnkX = x;
			_lnkY = y;
			_lnkResult = &lnk;
			_inTextFlag = &inText;
			_breakEverywhere = breakEverywhere;
			draw(0, 0, w, align, _lnkY, _lnkY + 1);
			lnk = *_lnkResult;
		}
	}

	void getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y, int32 w, style::align align) {
		symbol = 0;
		after = false;
		upon = false;
		if (!_t->isNull() && y >= 0) {
			_lnkX = x;
			_lnkY = y;
			_getSymbol = &symbol;
			_getSymbolAfter = &after;
			_getSymbolUpon = &upon;
			draw(0, 0, w, align, _lnkY, _lnkY + 1);
		}
	}

	const QPen &blockPen(ITextBlock *block) {
		if (block->color()) {
			return block->color()->p;
		}
		if (block->lnkIndex()) {
			const TextLinkPtr &l(_t->_links.at(block->lnkIndex() - 1));
			if (l == _overLnk) {
				if (l == _downLnk) {
					return _textStyle->linkFgDown->p;
				}
			}
			return _textStyle->linkFg->p;
		}
		if ((block->flags() & TextBlockFCode) || (block->flags() & TextBlockFPre)) {
			return _textStyle->monoFg->p;
		}
		return _originalPen;
	}

	bool drawLine(uint16 _lineEnd, const Text::TextBlocks::const_iterator &_endBlockIter, const Text::TextBlocks::const_iterator &_end) {
		_yDelta = (_lineHeight - _fontHeight) / 2;
		if (_yTo >= 0 && _y + _yDelta >= _yTo) return false;
		if (_y + _yDelta + _fontHeight <= _yFrom) return true;

		uint16 trimmedLineEnd = _lineEnd;
		for (; trimmedLineEnd > _lineStart; --trimmedLineEnd) {
			QChar ch = _t->_text.at(trimmedLineEnd - 1);
			if ((ch != QChar::Space || trimmedLineEnd == _lineStart + 1) && ch != QChar::LineFeed) {
				break;
			}
		}

		ITextBlock *_endBlock = (_endBlockIter == _end) ? 0 : (*_endBlockIter);
		bool elidedLine = _elideLast && _endBlock && (_y + _lineHeight >= _yToElide);

		int blockIndex = _lineStartBlock;
		ITextBlock *currentBlock = _t->_blocks[blockIndex];
		ITextBlock *nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;

		int32 delta = (currentBlock->from() < _lineStart ? qMin(_lineStart - currentBlock->from(), 2) : 0);
		_localFrom = _lineStart - delta;
		int32 lineEnd = (_endBlock && _endBlock->from() < trimmedLineEnd && !elidedLine) ? qMin(uint16(trimmedLineEnd + 2), _blockEnd(_t, _endBlockIter, _end)) : trimmedLineEnd;

		QString lineText = _t->_text.mid(_localFrom, lineEnd - _localFrom);
		int32 lineStart = delta, lineLength = trimmedLineEnd - _lineStart;

		if (elidedLine) {
			initParagraphBidi();
			prepareElidedLine(lineText, lineStart, lineLength, _endBlock);
		}

		QFixed x = _x;
		if (_align & Qt::AlignHCenter) {
			x += (_wLeft / 2).toInt();
		} else if (((_align & Qt::AlignLeft) && _parDirection == Qt::RightToLeft) || ((_align & Qt::AlignRight) && _parDirection == Qt::LeftToRight)) {
			x += _wLeft;
		}

		if (_getSymbol) {
			if (_lnkX < x) {
				if (_parDirection == Qt::RightToLeft) {
					*_getSymbol = (_lineEnd > _lineStart) ? (_lineEnd - 1) : _lineStart;
					*_getSymbolAfter = (_lineEnd > _lineStart) ? true : false;
					*_getSymbolUpon = ((_lnkX >= _x) && (_lineEnd < _t->_text.size()) && (!_endBlock || _endBlock->type() != TextBlockTSkip)) ? true : false;
				} else {
					*_getSymbol = _lineStart;
					*_getSymbolAfter = false;
					*_getSymbolUpon = ((_lnkX >= _x) && (_lineStart > 0)) ? true : false;
				}
				return false;
			} else if (_lnkX >= x + (_w - _wLeft)) {
				if (_parDirection == Qt::RightToLeft) {
					*_getSymbol = _lineStart;
					*_getSymbolAfter = false;
					*_getSymbolUpon = ((_lnkX < _x + _w) && (_lineStart > 0)) ? true : false;
				} else {
					*_getSymbol = (_lineEnd > _lineStart) ? (_lineEnd - 1) : _lineStart;
					*_getSymbolAfter = (_lineEnd > _lineStart) ? true : false;
					*_getSymbolUpon = ((_lnkX < _x + _w) && (_lineEnd < _t->_text.size()) && (!_endBlock || _endBlock->type() != TextBlockTSkip)) ? true : false;
				}
				return false;
			}
		}

		bool selectFromStart = (_selectedTo > _lineStart) && (_lineStart > 0) && (_selectedFrom <= _lineStart);
		bool selectTillEnd = (_selectedTo >= _lineEnd) && (_lineEnd < _t->_text.size()) && (_selectedFrom < _lineEnd) && (!_endBlock || _endBlock->type() != TextBlockTSkip);

		if ((selectFromStart && _parDirection == Qt::LeftToRight) || (selectTillEnd && _parDirection == Qt::RightToLeft)) {
			if (x > _x) {
				_p->fillRect(QRectF(_x.toReal(), _y + _yDelta, (x - _x).toReal(), _fontHeight), _textStyle->selectBg->b);
			}
		}
		if ((selectTillEnd && _parDirection == Qt::LeftToRight) || (selectFromStart && _parDirection == Qt::RightToLeft)) {
			if (x < _x + _wLeft) {
				_p->fillRect(QRectF((x + _w - _wLeft).toReal(), _y + _yDelta, (_x + _wLeft - x).toReal(), _fontHeight), _textStyle->selectBg->b);
			}
		}

		if (trimmedLineEnd == _lineStart && !elidedLine) return true;

		if (!elidedLine) initParagraphBidi(); // if was not inited

		_f = _t->_font;
		QStackTextEngine engine(lineText, _f->f);
		engine.option.setTextDirection(_parDirection);
		_e = &engine;

		eItemize();

		QScriptLine line;
		line.from = lineStart;
		line.length = lineLength;
		eShapeLine(line);

		int firstItem = engine.findItem(line.from), lastItem = engine.findItem(line.from + line.length - 1);
	    int nItems = (firstItem >= 0 && lastItem >= firstItem) ? (lastItem - firstItem + 1) : 0;
		if (!nItems) {
			if (elidedLine) restoreAfterElided();
			return true;
		}

		int skipIndex = -1;
		QVarLengthArray<int> visualOrder(nItems);
		QVarLengthArray<uchar> levels(nItems);
		for (int i = 0; i < nItems; ++i) {
			QScriptItem &si(engine.layoutData->items[firstItem + i]);
			while (nextBlock && nextBlock->from() <= _localFrom + si.position) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;
			}
			TextBlockType _type = currentBlock->type();
			if (_type == TextBlockTSkip) {
				levels[i] = si.analysis.bidiLevel = 0;
				skipIndex = i;
			} else {
				levels[i] = si.analysis.bidiLevel;
			}
			if (si.analysis.flags == QScriptAnalysis::Object) {
				if (_type == TextBlockTEmoji || _type == TextBlockTSkip) {
					si.width = currentBlock->f_width() + (nextBlock == _endBlock && (!nextBlock || nextBlock->from() >= trimmedLineEnd) ? 0 : currentBlock->f_rpadding());
				}
			}
		}
	    QTextEngine::bidiReorder(nItems, levels.data(), visualOrder.data());
		if (rtl() && skipIndex == nItems - 1) {
			for (int32 i = nItems; i > 1;) {
				--i;
				visualOrder[i] = visualOrder[i - 1];
			}
			visualOrder[0] = skipIndex;
		}

		blockIndex = _lineStartBlock;
		currentBlock = _t->_blocks[blockIndex];
		nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;

		int32 textY = _y + _yDelta + _t->_font->ascent, emojiY = (_t->_font->height - st::emojiSize) / 2;

		eSetFont(currentBlock);
		if (_p) _p->setPen(blockPen(currentBlock));
		for (int i = 0; i < nItems; ++i) {
			int item = firstItem + visualOrder[i];
			const QScriptItem &si = engine.layoutData->items.at(item);
			bool rtl = (si.analysis.bidiLevel % 2);

			while (blockIndex > _lineStartBlock + 1 && _t->_blocks[blockIndex - 1]->from() > _localFrom + si.position) {
				nextBlock = currentBlock;
				currentBlock = _t->_blocks[--blockIndex - 1];
				if (_p) _p->setPen(blockPen(currentBlock));
				eSetFont(currentBlock);
			}
			while (nextBlock && nextBlock->from() <= _localFrom + si.position) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;
				if (_p) _p->setPen(blockPen(currentBlock));
				eSetFont(currentBlock);
			}
			if (si.analysis.flags >= QScriptAnalysis::TabOrObject) {
				TextBlockType _type = currentBlock->type();
				if (_lnkResult && _lnkX >= x && _lnkX < x + si.width) {
					if (currentBlock->lnkIndex() && _lnkY >= _y + _yDelta && _lnkY < _y + _yDelta + _fontHeight) {
						_lnkResult = &_t->_links.at(currentBlock->lnkIndex() - 1);
					}
					if (_inTextFlag && _type != TextBlockTSkip) {
						*_inTextFlag = true;
					}
					return false;
				} else if (_getSymbol && _lnkX >= x && _lnkX < x + si.width) {
					if (_type == TextBlockTSkip) {
						if (_parDirection == Qt::RightToLeft) {
							*_getSymbol = _lineStart;
							*_getSymbolAfter = false;
							*_getSymbolUpon = false;
						} else {
							*_getSymbol = (trimmedLineEnd > _lineStart) ? (trimmedLineEnd - 1) : _lineStart;
							*_getSymbolAfter = (trimmedLineEnd > _lineStart) ? true : false;
							*_getSymbolUpon = false;
						}
						return false;
					}
					const QChar *chFrom = _str + currentBlock->from(), *chTo = chFrom + ((nextBlock ? nextBlock->from() : _t->_text.size()) - currentBlock->from());
					if (chTo > chFrom && (chTo - 1)->unicode() == QChar::Space) {
						if (rtl) {
							if (_lnkX < x + (si.width - currentBlock->f_width())) {
								*_getSymbol = (chTo - 1 - _str); // up to ending space, included, rtl
								*_getSymbolAfter = (_lnkX < x + (si.width - currentBlock->f_width()) / 2) ? true : false;
								*_getSymbolUpon = true;
								return false;
							}
						} else if (_lnkX >= x + currentBlock->f_width()) {
							*_getSymbol = (chTo - 1 - _str); // up to ending space, inclided, ltr
							*_getSymbolAfter = (_lnkX >= x + currentBlock->f_width() + (currentBlock->f_rpadding() / 2)) ? true : false;
							*_getSymbolUpon = true;
							return false;
						}
						--chTo;
					}
					if (_lnkX < x + (rtl ? (si.width - currentBlock->f_width()) : 0) + (currentBlock->f_width() / 2)) {
						*_getSymbol = ((rtl && chTo > chFrom) ? (chTo - 1) : chFrom) - _str;
						*_getSymbolAfter = (rtl && chTo > chFrom) ? true : false;
						*_getSymbolUpon = true;
					} else {
						*_getSymbol = ((rtl || chTo <= chFrom) ? chFrom : (chTo - 1)) - _str;
						*_getSymbolAfter = (rtl || chTo <= chFrom) ? false : true;
						*_getSymbolUpon = true;
					}
					return false;
				} else if (_p && _type == TextBlockTEmoji) {
					QFixed glyphX = x;
					if (rtl) {
						glyphX += (si.width - currentBlock->f_width());
					}
					if (_localFrom + si.position < _selectedTo) {
						const QChar *chFrom = _str + currentBlock->from(), *chTo = chFrom + ((nextBlock ? nextBlock->from() : _t->_text.size()) - currentBlock->from());
						if (_localFrom + si.position >= _selectedFrom) { // could be without space
							if (chTo == chFrom || (chTo - 1)->unicode() != QChar::Space || _selectedTo >= (chTo - _str)) {
								_p->fillRect(QRectF(x.toReal(), _y + _yDelta, si.width.toReal(), _fontHeight), _textStyle->selectBg->b);
							} else { // or with space
								_p->fillRect(QRectF(glyphX.toReal(), _y + _yDelta, currentBlock->f_width().toReal(), _fontHeight), _textStyle->selectBg->b);
							}
						} else if (chTo > chFrom && (chTo - 1)->unicode() == QChar::Space && (chTo - 1 - _str) >= _selectedFrom) {
							if (rtl) { // rtl space only
								_p->fillRect(QRectF(x.toReal(), _y + _yDelta, (glyphX - x).toReal(), _fontHeight), _textStyle->selectBg->b);
							} else { // ltr space only
								_p->fillRect(QRectF((x + currentBlock->f_width()).toReal(), _y + _yDelta, (si.width - currentBlock->f_width()).toReal(), _fontHeight), _textStyle->selectBg->b);
							}
						}
					}
					emojiDraw(*_p, static_cast<EmojiBlock*>(currentBlock)->emoji, (glyphX + int(st::emojiPadding)).toInt(), _y + _yDelta + emojiY);
//				} else if (_p && currentBlock->type() == TextBlockSkip) { // debug
//					_p->fillRect(QRect(x.toInt(), _y, currentBlock->width(), static_cast<SkipBlock*>(currentBlock)->height()), QColor(0, 0, 0, 32));
				}
				x += si.width;
				continue;
			}

			unsigned short *logClusters = engine.logClusters(&si);
			QGlyphLayout glyphs = engine.shapedGlyphs(&si);

			int itemStart = qMax(line.from, si.position), itemEnd;
			int itemLength = engine.length(item);
			int glyphsStart = logClusters[itemStart - si.position], glyphsEnd;
			if (line.from + line.length < si.position + itemLength) {
				itemEnd = line.from + line.length;
				glyphsEnd = logClusters[itemEnd - si.position];
			} else {
				itemEnd = si.position + itemLength;
				glyphsEnd = si.num_glyphs;
			}

			QFixed itemWidth = 0;
			for (int g = glyphsStart; g < glyphsEnd; ++g)
				itemWidth += glyphs.effectiveAdvance(g);

			if (_lnkResult && _lnkX >= x && _lnkX < x + itemWidth) {
				if (currentBlock->lnkIndex() && _lnkY >= _y + _yDelta && _lnkY < _y + _yDelta + _fontHeight) {
					_lnkResult = &_t->_links.at(currentBlock->lnkIndex() - 1);
				}
				if (_inTextFlag) {
					*_inTextFlag = true;
				}
				return false;
			} else if (_getSymbol && _lnkX >= x && _lnkX < x + itemWidth) {
				QFixed tmpx = rtl ? (x + itemWidth) : x;
				for (int ch = 0, g, itemL = itemEnd - itemStart; ch < itemL;) {
					g = logClusters[itemStart - si.position + ch];
					QFixed gwidth = glyphs.effectiveAdvance(g);
					 // ch2 - glyph end, ch - glyph start, (ch2 - ch) - how much chars it takes
					int ch2 = ch + 1;
					while ((ch2 < itemL) && (g == logClusters[itemStart - si.position + ch2])) {
						++ch2;
					}
					for (int charsCount = (ch2 - ch); ch < ch2; ++ch) {
						QFixed shift1 = QFixed(2 * (charsCount - (ch2 - ch)) + 2) * gwidth / QFixed(2 * charsCount),
						       shift2 = QFixed(2 * (charsCount - (ch2 - ch)) + 1) * gwidth / QFixed(2 * charsCount);
						if ((rtl && _lnkX >= tmpx - shift1) ||
							(!rtl && _lnkX < tmpx + shift1)) {
							*_getSymbol = _localFrom + itemStart + ch;
							if ((rtl && _lnkX >= tmpx - shift2) ||
								(!rtl && _lnkX < tmpx + shift2)) {
								*_getSymbolAfter = false;
							} else {
								*_getSymbolAfter = true;
							}
							*_getSymbolUpon = true;
							return false;
						}
					}
					if (rtl) {
						tmpx -= gwidth;
					} else {
						tmpx += gwidth;
					}
				}
				if (itemEnd > itemStart) {
					*_getSymbol = _localFrom + itemEnd - 1;
					*_getSymbolAfter = true;
				} else {
					*_getSymbol = _localFrom + itemStart;
					*_getSymbolAfter = false;
				}
				*_getSymbolUpon = true;
				return false;
			} else if (_p) {
				QTextCharFormat format;
				QTextItemInt gf(glyphs.mid(glyphsStart, glyphsEnd - glyphsStart),
								&_e->fnt, engine.layoutData->string.unicode() + itemStart,
								itemEnd - itemStart, engine.fontEngine(si), format);
				gf.logClusters = logClusters + itemStart - si.position;
				gf.width = itemWidth;
				gf.justified = false;
				gf.initWithScriptItem(si);

				if (_localFrom + itemStart < _selectedTo && _localFrom + itemEnd > _selectedFrom) {
					QFixed selX = x, selWidth = itemWidth;
					if (_localFrom + itemEnd > _selectedTo || _localFrom + itemStart < _selectedFrom) {
						selWidth = 0;
						int itemL = itemEnd - itemStart;
						int selStart = _selectedFrom - (_localFrom + itemStart), selEnd = _selectedTo - (_localFrom + itemStart);
						if (selStart < 0) selStart = 0;
						if (selEnd > itemL) selEnd = itemL;
						for (int ch = 0, g; ch < selEnd;) {
							g = logClusters[itemStart - si.position + ch];
							QFixed gwidth = glyphs.effectiveAdvance(g);
							// ch2 - glyph end, ch - glyph start, (ch2 - ch) - how much chars it takes
							int ch2 = ch + 1;
							while ((ch2 < itemL) && (g == logClusters[itemStart - si.position + ch2])) {
								++ch2;
							}
							if (ch2 <= selStart) {
								selX += gwidth;
							} else if (ch >= selStart && ch2 <= selEnd) {
								selWidth += gwidth;
							} else {
								int sStart = ch, sEnd = ch2;
								if (ch < selStart) {
									sStart = selStart;
									selX += QFixed(sStart - ch) * gwidth / QFixed(ch2 - ch);
								}
								if (ch2 >= selEnd) {
									sEnd = selEnd;
									selWidth += QFixed(sEnd - sStart) * gwidth / QFixed(ch2 - ch);
									break;
								}
								selWidth += QFixed(sEnd - sStart) * gwidth / QFixed(ch2 - ch);
							}
							ch = ch2;
						}
					}
					if (rtl) selX = x + itemWidth - (selX - x) - selWidth;
					_p->fillRect(QRectF(selX.toReal(), _y + _yDelta, selWidth.toReal(), _fontHeight), _textStyle->selectBg->b);
				}

				_p->drawTextItem(QPointF(x.toReal(), textY), gf);
			}

			x += itemWidth;
		}

		if (elidedLine) restoreAfterElided();
		return true;
	}

	void elideSaveBlock(int32 blockIndex, ITextBlock *&_endBlock, int32 elideStart, int32 elideWidth) {
		_elideSavedIndex = blockIndex;
		_elideSavedBlock = _t->_blocks[blockIndex];
		const_cast<Text*>(_t)->_blocks[blockIndex] = new TextBlock(_t->_font, _t->_text, QFIXED_MAX, elideStart, 0, _elideSavedBlock->flags(), _elideSavedBlock->color(), _elideSavedBlock->lnkIndex());
		_blocksSize = blockIndex + 1;
		_endBlock = (blockIndex + 1 < _t->_blocks.size() ? _t->_blocks[blockIndex + 1] : 0);
	}

	void setElideBidi(int32 elideStart, int32 elideLen) {
		int32 newParLength = elideStart + elideLen - _parStart;
		if (newParLength > _parAnalysis.size()) {
			_parAnalysis.resize(newParLength);
		}
		for (int32 i = elideLen; i > 0; --i) {
			_parAnalysis[newParLength - i].bidiLevel = (_parDirection == Qt::RightToLeft) ? 1 : 0;
		}
	}

	void prepareElidedLine(QString &lineText, int32 lineStart, int32 &lineLength, ITextBlock *&_endBlock, int repeat = 0) {
		static const QString _Elide = qsl("...");

		_f = _t->_font;
		QStackTextEngine engine(lineText, _f->f);
		engine.option.setTextDirection(_parDirection);
		_e = &engine;

		eItemize();

		int blockIndex = _lineStartBlock;
		ITextBlock *currentBlock = _t->_blocks[blockIndex];
		ITextBlock *nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;

		QScriptLine line;
		line.from = lineStart;
		line.length = lineLength;
		eShapeLine(line);

		int32 elideWidth = _f->width(_Elide);
		_wLeft = _w - elideWidth - _elideRemoveFromEnd;

		int firstItem = engine.findItem(line.from), lastItem = engine.findItem(line.from + line.length - 1);
	    int nItems = (firstItem >= 0 && lastItem >= firstItem) ? (lastItem - firstItem + 1) : 0, i;

		for (i = 0; i < nItems; ++i) {
			QScriptItem &si(engine.layoutData->items[firstItem + i]);
			while (nextBlock && nextBlock->from() <= _localFrom + si.position) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;
			}
			TextBlockType _type = currentBlock->type();
			if (si.analysis.flags == QScriptAnalysis::Object) {
				if (_type == TextBlockTEmoji || _type == TextBlockTSkip) {
					si.width = currentBlock->f_width() + currentBlock->f_rpadding();
				}
			}
			if (_type == TextBlockTEmoji || _type == TextBlockTSkip || _type == TextBlockTNewline) {
				if (_wLeft < si.width) {
					lineText = lineText.mid(0, currentBlock->from() - _localFrom) + _Elide;
					lineLength = currentBlock->from() + _Elide.size() - _lineStart;
					setElideBidi(currentBlock->from(), _Elide.size());
					elideSaveBlock(blockIndex - 1, _endBlock, currentBlock->from(), elideWidth);
					return;
				}
				_wLeft -= si.width;
			} else if (_type == TextBlockTText) {
				unsigned short *logClusters = engine.logClusters(&si);
				QGlyphLayout glyphs = engine.shapedGlyphs(&si);

				int itemStart = qMax(line.from, si.position), itemEnd;
				int itemLength = engine.length(firstItem + i);
				int glyphsStart = logClusters[itemStart - si.position], glyphsEnd;
				if (line.from + line.length < si.position + itemLength) {
					itemEnd = line.from + line.length;
					glyphsEnd = logClusters[itemEnd - si.position];
				} else {
					itemEnd = si.position + itemLength;
					glyphsEnd = si.num_glyphs;
				}

				for (int g = glyphsStart; g < glyphsEnd; ++g) {
					QFixed adv = glyphs.effectiveAdvance(g);
					if (_wLeft < adv) {
						int pos = itemStart;
						while (pos < itemEnd && logClusters[pos - si.position] < g) {
							++pos;
						}

						if (lineText.size() <= pos || repeat > 3) {
							lineText += _Elide;
							lineLength = _localFrom + pos + _Elide.size() - _lineStart;
							setElideBidi(_localFrom + pos, _Elide.size());
							_blocksSize = blockIndex;
							_endBlock = nextBlock;
						} else {
							lineText = lineText.mid(0, pos);
							lineLength = _localFrom + pos - _lineStart;
							_blocksSize = blockIndex;
							_endBlock = nextBlock;
							prepareElidedLine(lineText, lineStart, lineLength, _endBlock, repeat + 1);
						}
						return;
					} else {
						_wLeft -= adv;
					}
				}
			}
		}

		int32 elideStart = _lineStart + lineText.length();
		setElideBidi(elideStart, _Elide.size());

		lineText += _Elide;
		lineLength += _Elide.size();

		if (!repeat) {
			for (; blockIndex < _blocksSize && _t->_blocks[blockIndex] != _endBlock && _t->_blocks[blockIndex]->from() < elideStart; ++blockIndex) {
			}
			if (blockIndex < _blocksSize) {
				elideSaveBlock(blockIndex, _endBlock, elideStart, elideWidth);
			}
		}
	}

	void restoreAfterElided() {
		if (_elideSavedBlock) {
			delete _t->_blocks[_elideSavedIndex];
			const_cast<Text*>(_t)->_blocks[_elideSavedIndex] = _elideSavedBlock;
			_elideSavedBlock = 0;
		}
	}

	// COPIED FROM qtextengine.cpp AND MODIFIED
	void eShapeLine(const QScriptLine &line) {
		int item = _e->findItem(line.from), end = _e->findItem(line.from + line.length - 1);
		if (item == -1)
			return;

		int blockIndex = _lineStartBlock;
		ITextBlock *currentBlock = _t->_blocks[blockIndex];
		ITextBlock *nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;
		eSetFont(currentBlock);
		for (item = _e->findItem(line.from); item <= end; ++item) {
			QScriptItem &si = _e->layoutData->items[item];
			while (nextBlock && nextBlock->from() <= _localFrom + si.position) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;
				eSetFont(currentBlock);
			}
			_e->shape(item);
		}
	}

	style::font applyFlags(int32 flags, const style::font &f) {
		style::font result = f;
		if ((flags & TextBlockFPre) || (flags & TextBlockFCode)) {
			result = App::monofont();
			if (result->size() != f->size() || result->flags() != f->flags()) {
				result = style::font(f->size(), f->flags(), result->family());
			}
		} else {
			if (flags & TextBlockFBold) {
				result = result->bold();
			} else if (flags & TextBlockFSemibold) {
				result = st::semiboldFont;
				if (result->size() != f->size() || result->flags() != f->flags()) {
					result = style::font(f->size(), f->flags(), result->family());
				}
			}
			if (flags & TextBlockFItalic) result = result->italic();
			if (flags & TextBlockFUnderline) result = result->underline();
			if (flags & TextBlockFTilde) { // tilde fix in OpenSans
				result = st::semiboldFont;
			}
		}
		return result;
	}

	void eSetFont(ITextBlock *block) {
		style::font newFont = _t->_font;
		int flags = block->flags();
		if (flags) {
			newFont = applyFlags(flags, _t->_font);
		}
		if (block->lnkIndex()) {
			const TextLinkPtr &l(_t->_links.at(block->lnkIndex() - 1));
			if (l == _overLnk) {
				if (l == _downLnk || !_downLnk) {
					if (_t->_font != _textStyle->linkFlagsOver) newFont = _textStyle->linkFlagsOver;
				} else {
					if (_t->_font != _textStyle->linkFlags) newFont = _textStyle->linkFlags;
				}
			} else {
				if (_t->_font != _textStyle->linkFlags) newFont = _textStyle->linkFlags;
			}
		}
		if (newFont != _f) {
			if (newFont->family() == _t->_font->family()) {
				newFont = applyFlags(flags | newFont->flags(), _t->_font);
			}
			_f = newFont;
			_e->fnt = _f->f;
			_e->resetFontEngineCache();
		}
	}

	void eItemize() {
		_e->validate();
		if (_e->layoutData->items.size())
			return;

		int length = _e->layoutData->string.length();
		if (!length)
			return;

		const ushort *string = reinterpret_cast<const ushort*>(_e->layoutData->string.unicode());

		int blockIndex = _lineStartBlock;
		ITextBlock *currentBlock = _t->_blocks[blockIndex];
		ITextBlock *nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;

		_e->layoutData->hasBidi = _parHasBidi;
		QScriptAnalysis *analysis = _parAnalysis.data() + (_localFrom - _parStart);

		{
			QVarLengthArray<uchar> scripts(length);
			QUnicodeTools::initScripts(string, length, scripts.data());
			for (int i = 0; i < length; ++i)
				analysis[i].script = scripts.at(i);
		}

		blockIndex = _lineStartBlock;
		currentBlock = _t->_blocks[blockIndex];
		nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;

		const ushort *start = string;
		const ushort *end = start + length;
		while (start < end) {
			while (nextBlock && nextBlock->from() <= _localFrom + (start - string)) {
				currentBlock = nextBlock;
				nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;
			}
			TextBlockType _type = currentBlock->type();
			if (_type == TextBlockTEmoji || _type == TextBlockTSkip) {
				analysis->script = QChar::Script_Common;
				analysis->flags = QScriptAnalysis::Object;
			} else {
				analysis->flags = QScriptAnalysis::None;
			}
			analysis->script = hbscript_to_script(script_to_hbscript(analysis->script)); // retain the old behavior
			++start;
			++analysis;
		}

		{
			const QString *i_string = &_e->layoutData->string;
			const QScriptAnalysis *i_analysis = _parAnalysis.data() + (_localFrom - _parStart);
			QScriptItemArray *i_items = &_e->layoutData->items;

			blockIndex = _lineStartBlock;
			currentBlock = _t->_blocks[blockIndex];
			nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;
			ITextBlock *startBlock = currentBlock;

			if (!length)
				return;
			int start = 0, end = start + length;
			for (int i = start + 1; i < end; ++i) {
				while (nextBlock && nextBlock->from() <= _localFrom + i) {
					currentBlock = nextBlock;
					nextBlock = (++blockIndex < _blocksSize) ? _t->_blocks[blockIndex] : 0;
				}
				// According to the unicode spec we should be treating characters in the Common script
				// (punctuation, spaces, etc) as being the same script as the surrounding text for the
				// purpose of splitting up text. This is important because, for example, a fullstop
				// (0x2E) can be used to indicate an abbreviation and so must be treated as part of a
				// word.  Thus it must be passed along with the word in languages that have to calculate
				// word breaks.  For example the thai word "ครม." has no word breaks but the word "ครม"
				// does.
				// Unfortuntely because we split up the strings for both wordwrapping and for setting
				// the font and because Japanese and Chinese are also aliases of the script "Common",
				// doing this would break too many things.  So instead we only pass the full stop
				// along, and nothing else.
				if (currentBlock == startBlock
					&& i_analysis[i].bidiLevel == i_analysis[start].bidiLevel
					&& i_analysis[i].flags == i_analysis[start].flags
					&& (i_analysis[i].script == i_analysis[start].script || i_string->at(i) == QLatin1Char('.'))
//					&& i_analysis[i].flags < QScriptAnalysis::SpaceTabOrObject // only emojis are objects here, no tabs
					&& i - start < _MaxItemLength)
					continue;
				i_items->append(QScriptItem(start, i_analysis[start]));
				start = i;
				startBlock = currentBlock;
			}
			i_items->append(QScriptItem(start, i_analysis[start]));
		}
	}

	QChar::Direction eSkipBoundryNeutrals(QScriptAnalysis *analysis,
											const ushort *unicode,
											int &sor, int &eor, BidiControl &control,
											Text::TextBlocks::const_iterator i) {
		Text::TextBlocks::const_iterator e = _t->_blocks.cend(), n = i + 1;

		QChar::Direction dir = control.basicDirection();
		int level = sor > 0 ? analysis[sor - 1].bidiLevel : control.level;
		while (sor <= _parLength) {
			while (i != _parStartBlock && (*i)->from() > _parStart + sor) {
				n = i;
				--i;
			}
			while (n != e && (*n)->from() <= _parStart + sor) {
				i = n;
				++n;
			}

			TextBlockType _itype = (*i)->type();
			if (eor == _parLength)
				dir = control.basicDirection();
			else if (_itype == TextBlockTEmoji)
				dir = QChar::DirCS;
			else if (_itype == TextBlockTSkip)
				dir = QChar::DirCS;
			else
				dir = QChar::direction(unicode[sor]);
			// Keep skipping DirBN as if it doesn't exist
			if (dir != QChar::DirBN)
				break;
			analysis[sor++].bidiLevel = level;
		}

		eor = sor;

		return dir;
	}

	// creates the next QScript items.
	bool eBidiItemize(QScriptAnalysis *analysis, BidiControl &control) {
		bool rightToLeft = (control.basicDirection() == 1);
		bool hasBidi = rightToLeft;

		int sor = 0;
		int eor = -1;

		const ushort *unicode = reinterpret_cast<const ushort*>(_t->_text.unicode()) + _parStart;
		int current = 0;

		QChar::Direction dir = rightToLeft ? QChar::DirR : QChar::DirL;
		BidiStatus status;

		Text::TextBlocks::const_iterator i = _parStartBlock, e = _t->_blocks.cend(), n = i + 1;

		QChar::Direction sdir;
		TextBlockType _stype = (*_parStartBlock)->type();
		if (_stype == TextBlockTEmoji)
			sdir = QChar::DirCS;
		else if (_stype == TextBlockTSkip)
			sdir = QChar::DirCS;
		else
			sdir = QChar::direction(*unicode);
		if (sdir != QChar::DirL && sdir != QChar::DirR && sdir != QChar::DirEN && sdir != QChar::DirAN)
			sdir = QChar::DirON;
		else
			dir = QChar::DirON;

		status.eor = sdir;
		status.lastStrong = rightToLeft ? QChar::DirR : QChar::DirL;
		status.last = status.lastStrong;
		status.dir = sdir;

		while (current <= _parLength) {
			while (n != e && (*n)->from() <= _parStart + current) {
				i = n;
				++n;
			}

			QChar::Direction dirCurrent;
			TextBlockType _itype = (*i)->type();
			if (current == (int)_parLength)
				dirCurrent = control.basicDirection();
			else if (_itype == TextBlockTEmoji)
				dirCurrent = QChar::DirCS;
			else if (_itype == TextBlockTSkip)
				dirCurrent = QChar::DirCS;
			else
				dirCurrent = QChar::direction(unicode[current]);

			switch (dirCurrent) {

				// embedding and overrides (X1-X9 in the BiDi specs)
			case QChar::DirRLE:
			case QChar::DirRLO:
			case QChar::DirLRE:
			case QChar::DirLRO:
				{
					bool rtl = (dirCurrent == QChar::DirRLE || dirCurrent == QChar::DirRLO);
					hasBidi |= rtl;
					bool override = (dirCurrent == QChar::DirLRO || dirCurrent == QChar::DirRLO);

					unsigned int level = control.level+1;
					if ((level%2 != 0) == rtl) ++level;
					if (level < _MaxBidiLevel) {
						eor = current-1;
						eAppendItems(analysis, sor, eor, control, dir);
						eor = current;
						control.embed(rtl, override);
						QChar::Direction edir = (rtl ? QChar::DirR : QChar::DirL);
						dir = status.eor = edir;
						status.lastStrong = edir;
					}
					break;
				}
			case QChar::DirPDF:
				{
					if (control.canPop()) {
						if (dir != control.direction()) {
							eor = current-1;
							eAppendItems(analysis, sor, eor, control, dir);
							dir = control.direction();
						}
						eor = current;
						eAppendItems(analysis, sor, eor, control, dir);
						control.pdf();
						dir = QChar::DirON; status.eor = QChar::DirON;
						status.last = control.direction();
						if (control.override)
							dir = control.direction();
						else
							dir = QChar::DirON;
						status.lastStrong = control.direction();
					}
					break;
				}

				// strong types
			case QChar::DirL:
				if(dir == QChar::DirON)
					dir = QChar::DirL;
				switch(status.last)
					{
					case QChar::DirL:
						eor = current; status.eor = QChar::DirL; break;
					case QChar::DirR:
					case QChar::DirAL:
					case QChar::DirEN:
					case QChar::DirAN:
						if (eor >= 0) {
							eAppendItems(analysis, sor, eor, control, dir);
							status.eor = dir = eSkipBoundryNeutrals(analysis, unicode, sor, eor, control, i);
						} else {
							eor = current; status.eor = dir;
						}
						break;
					case QChar::DirES:
					case QChar::DirET:
					case QChar::DirCS:
					case QChar::DirBN:
					case QChar::DirB:
					case QChar::DirS:
					case QChar::DirWS:
					case QChar::DirON:
						if(dir != QChar::DirL) {
							//last stuff takes embedding dir
							if(control.direction() == QChar::DirR) {
								if(status.eor != QChar::DirR) {
									// AN or EN
									eAppendItems(analysis, sor, eor, control, dir);
									status.eor = QChar::DirON;
									dir = QChar::DirR;
								}
								eor = current - 1;
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = dir = eSkipBoundryNeutrals(analysis, unicode, sor, eor, control, i);
							} else {
								if(status.eor != QChar::DirL) {
									eAppendItems(analysis, sor, eor, control, dir);
									status.eor = QChar::DirON;
									dir = QChar::DirL;
								} else {
									eor = current; status.eor = QChar::DirL; break;
								}
							}
						} else {
							eor = current; status.eor = QChar::DirL;
						}
					default:
						break;
					}
				status.lastStrong = QChar::DirL;
				break;
			case QChar::DirAL:
			case QChar::DirR:
				hasBidi = true;
				if(dir == QChar::DirON) dir = QChar::DirR;
				switch(status.last)
					{
					case QChar::DirL:
					case QChar::DirEN:
					case QChar::DirAN:
						if (eor >= 0)
							eAppendItems(analysis, sor, eor, control, dir);
						// fall through
					case QChar::DirR:
					case QChar::DirAL:
						dir = QChar::DirR; eor = current; status.eor = QChar::DirR; break;
					case QChar::DirES:
					case QChar::DirET:
					case QChar::DirCS:
					case QChar::DirBN:
					case QChar::DirB:
					case QChar::DirS:
					case QChar::DirWS:
					case QChar::DirON:
						if(status.eor != QChar::DirR && status.eor != QChar::DirAL) {
							//last stuff takes embedding dir
							if(control.direction() == QChar::DirR
							   || status.lastStrong == QChar::DirR || status.lastStrong == QChar::DirAL) {
								eAppendItems(analysis, sor, eor, control, dir);
								dir = QChar::DirR; status.eor = QChar::DirON;
								eor = current;
							} else {
								eor = current - 1;
								eAppendItems(analysis, sor, eor, control, dir);
								dir = QChar::DirR; status.eor = QChar::DirON;
							}
						} else {
							eor = current; status.eor = QChar::DirR;
						}
					default:
						break;
					}
				status.lastStrong = dirCurrent;
				break;

				// weak types:

			case QChar::DirNSM:
				if (eor == current-1)
					eor = current;
				break;
			case QChar::DirEN:
				// if last strong was AL change EN to AN
				if(status.lastStrong != QChar::DirAL) {
					if(dir == QChar::DirON) {
						if(status.lastStrong == QChar::DirL)
							dir = QChar::DirL;
						else
							dir = QChar::DirEN;
					}
					switch(status.last)
						{
						case QChar::DirET:
							if (status.lastStrong == QChar::DirR || status.lastStrong == QChar::DirAL) {
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = QChar::DirON;
								dir = QChar::DirAN;
							}
							// fall through
						case QChar::DirEN:
						case QChar::DirL:
							eor = current;
							status.eor = dirCurrent;
							break;
						case QChar::DirR:
						case QChar::DirAL:
						case QChar::DirAN:
							if (eor >= 0)
								eAppendItems(analysis, sor, eor, control, dir);
							else
								eor = current;
							status.eor = QChar::DirEN;
							dir = QChar::DirAN; break;
						case QChar::DirES:
						case QChar::DirCS:
							if(status.eor == QChar::DirEN || dir == QChar::DirAN) {
								eor = current; break;
							}
						case QChar::DirBN:
						case QChar::DirB:
						case QChar::DirS:
						case QChar::DirWS:
						case QChar::DirON:
							if(status.eor == QChar::DirR) {
								// neutrals go to R
								eor = current - 1;
								eAppendItems(analysis, sor, eor, control, dir);
								dir = QChar::DirON; status.eor = QChar::DirEN;
								dir = QChar::DirAN;
							}
							else if(status.eor == QChar::DirL ||
									 (status.eor == QChar::DirEN && status.lastStrong == QChar::DirL)) {
								eor = current; status.eor = dirCurrent;
							} else {
								// numbers on both sides, neutrals get right to left direction
								if(dir != QChar::DirL) {
									eAppendItems(analysis, sor, eor, control, dir);
									dir = QChar::DirON; status.eor = QChar::DirON;
									eor = current - 1;
									dir = QChar::DirR;
									eAppendItems(analysis, sor, eor, control, dir);
									dir = QChar::DirON; status.eor = QChar::DirON;
									dir = QChar::DirAN;
								} else {
									eor = current; status.eor = dirCurrent;
								}
							}
						default:
							break;
						}
					break;
				}
			case QChar::DirAN:
				hasBidi = true;
				dirCurrent = QChar::DirAN;
				if(dir == QChar::DirON) dir = QChar::DirAN;
				switch(status.last)
					{
					case QChar::DirL:
					case QChar::DirAN:
						eor = current; status.eor = QChar::DirAN; break;
					case QChar::DirR:
					case QChar::DirAL:
					case QChar::DirEN:
						if (eor >= 0){
							eAppendItems(analysis, sor, eor, control, dir);
						} else {
							eor = current;
						}
						dir = QChar::DirAN; status.eor = QChar::DirAN;
						break;
					case QChar::DirCS:
						if(status.eor == QChar::DirAN) {
							eor = current; break;
						}
					case QChar::DirES:
					case QChar::DirET:
					case QChar::DirBN:
					case QChar::DirB:
					case QChar::DirS:
					case QChar::DirWS:
					case QChar::DirON:
						if(status.eor == QChar::DirR) {
							// neutrals go to R
							eor = current - 1;
							eAppendItems(analysis, sor, eor, control, dir);
							status.eor = QChar::DirAN;
							dir = QChar::DirAN;
						} else if(status.eor == QChar::DirL ||
								   (status.eor == QChar::DirEN && status.lastStrong == QChar::DirL)) {
							eor = current; status.eor = dirCurrent;
						} else {
							// numbers on both sides, neutrals get right to left direction
							if(dir != QChar::DirL) {
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = QChar::DirON;
								eor = current - 1;
								dir = QChar::DirR;
								eAppendItems(analysis, sor, eor, control, dir);
								status.eor = QChar::DirAN;
								dir = QChar::DirAN;
							} else {
								eor = current; status.eor = dirCurrent;
							}
						}
					default:
						break;
					}
				break;
			case QChar::DirES:
			case QChar::DirCS:
				break;
			case QChar::DirET:
				if(status.last == QChar::DirEN) {
					dirCurrent = QChar::DirEN;
					eor = current; status.eor = dirCurrent;
				}
				break;

				// boundary neutrals should be ignored
			case QChar::DirBN:
				break;
				// neutrals
			case QChar::DirB:
				// ### what do we do with newline and paragraph separators that come to here?
				break;
			case QChar::DirS:
				// ### implement rule L1
				break;
			case QChar::DirWS:
			case QChar::DirON:
				break;
			default:
				break;
			}

			if(current >= (int)_parLength) break;

			// set status.last as needed.
			switch(dirCurrent) {
			case QChar::DirET:
			case QChar::DirES:
			case QChar::DirCS:
			case QChar::DirS:
			case QChar::DirWS:
			case QChar::DirON:
				switch(status.last)
				{
				case QChar::DirL:
				case QChar::DirR:
				case QChar::DirAL:
				case QChar::DirEN:
				case QChar::DirAN:
					status.last = dirCurrent;
					break;
				default:
					status.last = QChar::DirON;
				}
				break;
			case QChar::DirNSM:
			case QChar::DirBN:
				// ignore these
				break;
			case QChar::DirLRO:
			case QChar::DirLRE:
				status.last = QChar::DirL;
				break;
			case QChar::DirRLO:
			case QChar::DirRLE:
				status.last = QChar::DirR;
				break;
			case QChar::DirEN:
				if (status.last == QChar::DirL) {
					status.last = QChar::DirL;
					break;
				}
				// fall through
			default:
				status.last = dirCurrent;
			}

			++current;
		}

		eor = current - 1; // remove dummy char

		if (sor <= eor)
			eAppendItems(analysis, sor, eor, control, dir);

		return hasBidi;
	}

private:

	QPainter *_p;
	const Text *_t;
	bool _elideLast, _breakEverywhere;
	int32 _elideRemoveFromEnd;
	style::align _align;
	QPen _originalPen;
	int32 _yFrom, _yTo, _yToElide;
	uint16 _selectedFrom, _selectedTo;
	const QChar *_str;

	// current paragraph data
	Text::TextBlocks::const_iterator _parStartBlock;
	Qt::LayoutDirection _parDirection;
	int32 _parStart, _parLength;
	bool _parHasBidi;
	QVarLengthArray<QScriptAnalysis, 4096> _parAnalysis;

	// current line data
	QTextEngine *_e;
	style::font _f;
	QFixed _x, _w, _wLeft;
	int32 _y, _yDelta, _lineHeight, _fontHeight;

	// elided hack support
	int32 _blocksSize;
	int32 _elideSavedIndex;
	ITextBlock *_elideSavedBlock;

	int32 _lineStart, _localFrom;
	int32 _lineStartBlock;

	// link and symbol resolve
	QFixed _lnkX;
	int32 _lnkY;
	const TextLinkPtr *_lnkResult;
	bool *_inTextFlag;
	uint16 *_getSymbol;
	bool *_getSymbolAfter, *_getSymbolUpon;

};

const TextParseOptions _defaultOptions = {
	TextParseLinks | TextParseMultiline, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

const TextParseOptions _textPlainOptions = {
	TextParseMultiline, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

Text::Text(int32 minResizeWidth) : _minResizeWidth(minResizeWidth), _maxWidth(0), _minHeight(0), _startDir(Qt::LayoutDirectionAuto) {
}

Text::Text(style::font font, const QString &text, const TextParseOptions &options, int32 minResizeWidth, bool richText) : _minResizeWidth(minResizeWidth) {
	if (richText) {
		setRichText(font, text, options);
	} else {
		setText(font, text, options);
	}
}

Text::Text(const Text &other) :
_minResizeWidth(other._minResizeWidth), _maxWidth(other._maxWidth),
_minHeight(other._minHeight),
_text(other._text),
_font(other._font),
_blocks(other._blocks.size()),
_links(other._links),
_startDir(other._startDir)
{
	for (int32 i = 0, l = _blocks.size(); i < l; ++i) {
		_blocks[i] = other._blocks.at(i)->clone();
	}
}

Text &Text::operator=(const Text &other) {
	_minResizeWidth = other._minResizeWidth;
	_maxWidth = other._maxWidth;
	_minHeight = other._minHeight;
	_text = other._text;
	_font = other._font;
	_blocks = TextBlocks(other._blocks.size());
	_links = other._links;
	_startDir = other._startDir;
	for (int32 i = 0, l = _blocks.size(); i < l; ++i) {
		_blocks[i] = other._blocks.at(i)->clone();
	}
	return *this;
}

void Text::setText(style::font font, const QString &text, const TextParseOptions &options) {
	if (!_textStyle) _initDefault();
	_font = font;
	clean();
	{
		TextParser parser(this, text, options);
	}
	recountNaturalSize(true, options.dir);
}

void Text::recountNaturalSize(bool initial, Qt::LayoutDirection optionsDir) {
	NewlineBlock *lastNewline = 0;

	_maxWidth = _minHeight = 0;
	int32 lineHeight = 0;
	int32 result = 0, lastNewlineStart = 0;
	QFixed _width = 0, last_rBearing = 0, last_rPadding = 0;
	for (TextBlocks::const_iterator i = _blocks.cbegin(), e = _blocks.cend(); i != e; ++i) {
		ITextBlock *b = *i;
		TextBlockType _btype = b->type();
		int32 blockHeight = _blockHeight(b, _font);
		QFixed _rb = _blockRBearing(b);

		if (_btype == TextBlockTNewline) {
			if (!lineHeight) lineHeight = blockHeight;
			if (initial) {
				Qt::LayoutDirection dir = optionsDir;
				if (dir == Qt::LayoutDirectionAuto) {
					dir = TextParser::stringDirection(_text, lastNewlineStart, b->from());
				}
				if (lastNewline) {
					lastNewline->_nextDir = dir;
				} else {
					_startDir = dir;
				}
			}
			lastNewlineStart = b->from();
			lastNewline = static_cast<NewlineBlock*>(b);

			_minHeight += lineHeight;
			lineHeight = 0;
			last_rBearing = _rb;
			last_rPadding = b->f_rpadding();
			if (_maxWidth < _width) {
				_maxWidth = _width;
			}
			_width = (b->f_width() - last_rBearing);
			continue;
		}

		_width += b->f_lpadding();
		_width += last_rBearing + (last_rPadding + b->f_width() - _rb);
		lineHeight = qMax(lineHeight, blockHeight);

		last_rBearing = _rb;
		last_rPadding = b->f_rpadding();
		continue;
	}
	if (initial) {
		Qt::LayoutDirection dir = optionsDir;
		if (dir == Qt::LayoutDirectionAuto) {
			dir = TextParser::stringDirection(_text, lastNewlineStart, _text.size());
		}
		if (lastNewline) {
			lastNewline->_nextDir = dir;
		} else {
			_startDir = dir;
		}
	}
	if (_width > 0) {
		if (!lineHeight) lineHeight = _blockHeight(_blocks.back(), _font);
		_minHeight += lineHeight;
		if (_maxWidth < _width) {
			_maxWidth = _width;
		}
	}
}

void Text::setMarkedText(style::font font, const QString &text, const EntitiesInText &entities, const TextParseOptions &options) {
	if (!_textStyle) _initDefault();
	_font = font;
	clean();
	{
//		QString newText; // utf16 of the text for emoji
//		newText.reserve(8 * text.size());
//		for (const QChar *ch = text.constData(), *e = ch + text.size(); ch != e; ++ch) {
//			if (chIsNewline(*ch)) {
//				newText.append(*ch);
//			} else {
//				if (ch->isHighSurrogate() || ch->isLowSurrogate()) {
//					if (ch->isHighSurrogate() && (ch + 1 != e) && ((ch + 1)->isLowSurrogate())) {
//						newText.append("0x").append(QString::number((uint32(ch->unicode()) << 16) | uint32((ch + 1)->unicode()), 16).toUpper()).append("LLU,");
//						++ch;
//					} else {
//						newText.append("BADx").append(QString::number(ch->unicode(), 16).toUpper()).append("LLU,");
//					}
//				} else {
//					newText.append("0x").append(QString::number(ch->unicode(), 16).toUpper()).append("LLU,");
//				}
//			}
//		}
//		newText.append("\n\n").append(text);
//		TextParser parser(this, newText, EntitiesInText(), options);

//		QString newText; // utf8 of the text for emoji sequences
//		newText.reserve(8 * text.size());
//		QByteArray ba = text.toUtf8();
//		for (int32 i = 0, l = ba.size(); i < l; ++i) {
//			newText.append("\\x").append(QString::number(uchar(ba.at(i)), 16).toLower());
//		}
//		newText.append("\n\n").append(text);
//		TextParser parser(this, newText, EntitiesInText(), options);

		TextParser parser(this, text, entities, options);
	}
	recountNaturalSize(true, options.dir);
}

void Text::setRichText(style::font font, const QString &text, TextParseOptions options, const TextCustomTagsMap &custom) {
	QString parsed;
	parsed.reserve(text.size());
	const QChar *s = text.constData(), *ch = s;
	for (const QChar *b = s, *e = b + text.size(); ch != e; ++ch) {
		if (ch->unicode() == '\\') {
			if (ch > s) parsed.append(s, ch - s);
			s = ch + 1;

			if (s < e) ++ch;
			continue;
		}
		if (ch->unicode() == '[') {
			if (ch > s) parsed.append(s, ch - s);
			s = ch;

			const QChar *tag = ch + 1;
			if (tag >= e) continue;

			bool closing = false, other = false;
			if (tag->unicode() == '/') {
				closing = true;
				if (++tag >= e) continue;
			}

			TextCommands cmd;
			switch (tag->unicode()) {
			case 'b': cmd = closing ? TextCommandNoBold : TextCommandBold; break;
			case 'i': cmd = closing ? TextCommandNoItalic : TextCommandItalic; break;
			case 'u': cmd = closing ? TextCommandNoUnderline : TextCommandUnderline; break;
			default : other = true; break;
			}

			if (!other) {
				if (++tag >= e || tag->unicode() != ']') continue;
				parsed.append(TextCommand).append(QChar(cmd)).append(TextCommand);
				ch = tag;
				s = ch + 1;
				continue;
			}

			if (tag->unicode() != 'a') {
				TextCustomTagsMap::const_iterator i = custom.constFind(*tag);
				if (++tag >= e || tag->unicode() != ']' || i == custom.cend()) continue;
				parsed.append(closing ? i->second : i->first);
				ch = tag;
				s = ch + 1;
				continue;
			}

			if (closing) {
				if (++tag >= e || tag->unicode() != ']') continue;
				parsed.append(textcmdStopLink());
				ch = tag;
				s = ch + 1;
				continue;
			}
			if (++tag >= e || tag->unicode() != ' ') continue;
			while (tag < e && tag->unicode() == ' ') ++tag;
			if (tag + 5 < e && text.midRef(tag - b, 6) == qsl("href=\"")) {
				tag += 6;
				const QChar *tagend = tag;
				while (tagend < e && tagend->unicode() != '"') ++tagend;
				if (++tagend >= e || tagend->unicode() != ']') continue;
				parsed.append(textcmdStartLink(QString(tag, tagend - 1 - tag)));
				ch = tagend;
				s = ch + 1;
				continue;
			}
		}
	}
	if (ch > s) parsed.append(s, ch - s);
	s = ch;

	options.flags |= TextParseRichText;
	setText(font, parsed, options);
}

void Text::setLink(uint16 lnkIndex, const TextLinkPtr &lnk) {
	if (!lnkIndex || lnkIndex > _links.size()) return;
	_links[lnkIndex - 1] = lnk;
}

bool Text::hasLinks() const {
	return !_links.isEmpty();
}

void Text::setSkipBlock(int32 width, int32 height) {
	if (!_blocks.isEmpty() && _blocks.back()->type() == TextBlockTSkip) {
		SkipBlock *block = static_cast<SkipBlock*>(_blocks.back());
		if (block->width() == width && block->height() == height) return;
		_text.resize(block->from());
		_blocks.pop_back();
	}
	_text.push_back('_');
	_blocks.push_back(new SkipBlock(_font, _text, _text.size() - 1, width, height, 0));
	recountNaturalSize(false);
}

void Text::removeSkipBlock() {
	if (!_blocks.isEmpty() && _blocks.back()->type() == TextBlockTSkip) {
		_text.resize(_blocks.back()->from());
		_blocks.pop_back();
		recountNaturalSize(false);
	}
}

int32 Text::countWidth(int32 w) const {
	QFixed width = w;
	if (width < _minResizeWidth) width = _minResizeWidth;
	if (width >= _maxWidth) {
		return _maxWidth.ceil().toInt();
	}

	QFixed minWidthLeft = width, widthLeft = width, last_rBearing = 0, last_rPadding = 0;
	bool longWordLine = true;
	for (TextBlocks::const_iterator i = _blocks.cbegin(), e = _blocks.cend(); i != e; ++i) {
		ITextBlock *b = *i;
		TextBlockType _btype = b->type();
		int32 blockHeight = _blockHeight(b, _font);
		QFixed _rb = _blockRBearing(b);

		if (_btype == TextBlockTNewline) {
			last_rBearing = _rb;
			last_rPadding = b->f_rpadding();
			if (widthLeft < minWidthLeft) {
				minWidthLeft = widthLeft;
			}
			widthLeft = width - (b->f_width() - last_rBearing);

			longWordLine = true;
			continue;
		}
		QFixed lpadding = b->f_lpadding();
		QFixed newWidthLeft = widthLeft - lpadding - last_rBearing - (last_rPadding + b->f_width() - _rb);
		if (newWidthLeft >= 0) {
			last_rBearing = _rb;
			last_rPadding = b->f_rpadding();
			widthLeft = newWidthLeft;

			longWordLine = false;
			continue;
		}

		if (_btype == TextBlockTText) {
			TextBlock *t = static_cast<TextBlock*>(b);
			if (t->_words.isEmpty()) { // no words in this block, spaces only => layout this block in the same line
				last_rPadding += lpadding;

				longWordLine = false;
				continue;
			}

			QFixed f_wLeft = widthLeft;
			for (TextBlock::TextWords::const_iterator j = t->_words.cbegin(), e = t->_words.cend(), f = j; j != e; ++j) {
				bool wordEndsHere = (j->width >= 0);
				QFixed j_width = wordEndsHere ? j->width : -j->width;

				QFixed newWidthLeft = widthLeft - lpadding - last_rBearing - (last_rPadding + j_width - j->f_rbearing());
				lpadding = 0;
				if (newWidthLeft >= 0) {
					last_rBearing = j->f_rbearing();
					last_rPadding = j->rpadding;
					widthLeft = newWidthLeft;

					if (wordEndsHere) {
						longWordLine = false;
					}
					if (wordEndsHere || longWordLine) {
						f_wLeft = widthLeft;
						f = j + 1;
					}
					continue;
				}

				if (f != j) {
					j = f;
					widthLeft = f_wLeft;
					j_width = (j->width >= 0) ? j->width : -j->width;
				}

				last_rBearing = j->f_rbearing();
				last_rPadding = j->rpadding;
				if (widthLeft < minWidthLeft) {
					minWidthLeft = widthLeft;
				}
				widthLeft = width - (j_width - last_rBearing);

				longWordLine = true;
				f = j + 1;
				f_wLeft = widthLeft;
			}
			continue;
		}

		last_rBearing = _rb;
		last_rPadding = b->f_rpadding();
		if (widthLeft < minWidthLeft) {
			minWidthLeft = widthLeft;
		}
		widthLeft = width - (b->f_width() - last_rBearing);

		longWordLine = true;
		continue;
	}
	if (widthLeft < minWidthLeft) {
		minWidthLeft = widthLeft;
	}

	return (width - minWidthLeft).ceil().toInt();
}

int32 Text::countHeight(int32 w) const {
	QFixed width = w;
	if (width < _minResizeWidth) width = _minResizeWidth;
	if (width >= _maxWidth) {
		return _minHeight;
	}

	int32 result = 0, lineHeight = 0;
	QFixed widthLeft = width, last_rBearing = 0, last_rPadding = 0;
	bool longWordLine = true;
	for (TextBlocks::const_iterator i = _blocks.cbegin(), e = _blocks.cend(); i != e; ++i) {
		ITextBlock *b = *i;
		TextBlockType _btype = b->type();
		int32 blockHeight = _blockHeight(b, _font);
		QFixed _rb = _blockRBearing(b);

		if (_btype == TextBlockTNewline) {
			if (!lineHeight) lineHeight = blockHeight;
			result += lineHeight;
			lineHeight = 0;
			last_rBearing = _rb;
			last_rPadding = b->f_rpadding();
			widthLeft = width - (b->f_width() - last_rBearing);

			longWordLine = true;
			continue;
		}
		QFixed lpadding = b->f_lpadding();
		QFixed newWidthLeft = widthLeft - lpadding - last_rBearing - (last_rPadding + b->f_width() - _rb);
		if (newWidthLeft >= 0) {
			last_rBearing = _rb;
			last_rPadding = b->f_rpadding();
			widthLeft = newWidthLeft;

			lineHeight = qMax(lineHeight, blockHeight);

			longWordLine = false;
			continue;
		}

		if (_btype == TextBlockTText) {
			TextBlock *t = static_cast<TextBlock*>(b);
			if (t->_words.isEmpty()) { // no words in this block, spaces only => layout this block in the same line
				last_rPadding += lpadding;

				lineHeight = qMax(lineHeight, blockHeight);

				longWordLine = false;
				continue;
			}

			QFixed f_wLeft = widthLeft;
			int32 f_lineHeight = lineHeight;
			for (TextBlock::TextWords::const_iterator j = t->_words.cbegin(), e = t->_words.cend(), f = j; j != e; ++j) {
				bool wordEndsHere = (j->width >= 0);
				QFixed j_width = wordEndsHere ? j->width : -j->width;

				QFixed newWidthLeft = widthLeft - lpadding - last_rBearing - (last_rPadding + j_width - j->f_rbearing());
				lpadding = 0;
				if (newWidthLeft >= 0) {
					last_rBearing = j->f_rbearing();
					last_rPadding = j->rpadding;
					widthLeft = newWidthLeft;

					lineHeight = qMax(lineHeight, blockHeight);

					if (wordEndsHere) {
						longWordLine = false;
					}
					if (wordEndsHere || longWordLine) {
						f_wLeft = widthLeft;
						f_lineHeight = lineHeight;
						f = j + 1;
					}
					continue;
				}

				if (f != j) {
					j = f;
					widthLeft = f_wLeft;
					lineHeight = f_lineHeight;
					j_width = (j->width >= 0) ? j->width : -j->width;
				}

				result += lineHeight;
				lineHeight = qMax(0, blockHeight);
				last_rBearing = j->f_rbearing();
				last_rPadding = j->rpadding;
				widthLeft = width - (j_width - last_rBearing);

				longWordLine = true;
				f = j + 1;
				f_wLeft = widthLeft;
				f_lineHeight = lineHeight;
			}
			continue;
		}

		result += lineHeight;
		lineHeight = qMax(0, blockHeight);
		last_rBearing = _rb;
		last_rPadding = b->f_rpadding();
		widthLeft = width - (b->f_width() - last_rBearing);

		longWordLine = true;
		continue;
	}
	if (widthLeft < width) {
		result += lineHeight;
	}

	return result;
}

void Text::replaceFont(style::font f) {
	_font = f;
}

void Text::draw(QPainter &painter, int32 left, int32 top, int32 w, style::align align, int32 yFrom, int32 yTo, uint16 selectedFrom, uint16 selectedTo) const {
//	painter.fillRect(QRect(left, top, w, countHeight(w)), QColor(0, 0, 0, 32)); // debug
	TextPainter p(&painter, this);
	p.draw(left, top, w, align, yFrom, yTo, selectedFrom, selectedTo);
}

void Text::drawElided(QPainter &painter, int32 left, int32 top, int32 w, int32 lines, style::align align, int32 yFrom, int32 yTo, int32 removeFromEnd, bool breakEverywhere) const {
//	painter.fillRect(QRect(left, top, w, countHeight(w)), QColor(0, 0, 0, 32)); // debug
	TextPainter p(&painter, this);
	p.drawElided(left, top, w, align, lines, yFrom, yTo, removeFromEnd, breakEverywhere);
}

const TextLinkPtr &Text::link(int32 x, int32 y, int32 width, style::align align) const {
	TextPainter p(0, this);
	return p.link(x, y, width, align);
}

void Text::getState(TextLinkPtr &lnk, bool &inText, int32 x, int32 y, int32 width, style::align align, bool breakEverywhere) const {
	TextPainter p(0, this);
	p.getState(lnk, inText, x, y, width, align, breakEverywhere);
}

void Text::getSymbol(uint16 &symbol, bool &after, bool &upon, int32 x, int32 y, int32 width, style::align align) const {
	TextPainter p(0, this);
	p.getSymbol(symbol, after, upon, x, y, width, align);
}

uint32 Text::adjustSelection(uint16 from, uint16 to, TextSelectType selectType) const {
	if (from < _text.size() && from <= to) {
		if (to > _text.size()) to = _text.size() - 1;
		if (selectType == TextSelectParagraphs) {
			if (!chIsParagraphSeparator(_text.at(from))) {
				while (from > 0 && !chIsParagraphSeparator(_text.at(from - 1))) {
					--from;
				}
			}
			if (to < _text.size()) {
				if (chIsParagraphSeparator(_text.at(to))) {
					++to;
				} else {
					while (to < _text.size() && !chIsParagraphSeparator(_text.at(to))) {
						++to;
					}
				}
			}
		} else if (selectType == TextSelectWords) {
			if (!chIsWordSeparator(_text.at(from))) {
				while (from > 0 && !chIsWordSeparator(_text.at(from - 1))) {
					--from;
				}
			}
			if (to < _text.size()) {
				if (chIsWordSeparator(_text.at(to))) {
					++to;
				} else {
					while (to < _text.size() && !chIsWordSeparator(_text.at(to))) {
						++to;
					}
				}
			}
		}
	}
	return (from << 16) | to;
}

QString Text::original(uint16 selectedFrom, uint16 selectedTo, ExpandLinksMode mode) const {
	QString result;
	result.reserve(_text.size());

	int32 lnkFrom = 0, lnkIndex = 0;
	for (TextBlocks::const_iterator i = _blocks.cbegin(), e = _blocks.cend(); true; ++i) {
		int32 blockLnkIndex = (i == e) ? 0 : (*i)->lnkIndex();
		int32 blockFrom = (i == e) ? _text.size() : (*i)->from();
		if (blockLnkIndex != lnkIndex) {
			if (lnkIndex) { // write link
				const TextLinkPtr &lnk(_links.at(lnkIndex - 1));
				const QString &url(lnk ? lnk->text() : QString());

				int32 rangeFrom = qMax(int32(selectedFrom), lnkFrom), rangeTo = qMin(blockFrom, int32(selectedTo));

				if (rangeTo > rangeFrom) {
					QStringRef r = _text.midRef(rangeFrom, rangeTo - rangeFrom);
					if (url.isEmpty() || mode == ExpandLinksNone || lnkFrom != rangeFrom || blockFrom != rangeTo) {
						result += r;
					} else {
						QUrl u(url);
						QString displayed = (u.isValid() ? u.toDisplayString() : url);
						bool shortened = (r.size() > 3) && (_text.midRef(lnkFrom, r.size() - 3) == displayed.midRef(0, r.size() - 3));
						bool same = (r == displayed.midRef(0, r.size())) || (r == url.midRef(0, r.size()));
						if (same || shortened) {
							result += url;
						} else if (mode == ExpandLinksAll) {
							result.append(r).append(qsl(" ( ")).append(url).append(qsl(" )"));
						} else {
							result += r;
						}
					}
				}
			}
			lnkIndex = blockLnkIndex;
			lnkFrom = blockFrom;
		}
		if (i == e) break;

		TextBlockType type = (*i)->type();
		if (type == TextBlockTSkip) continue;

		if (!blockLnkIndex) {
			int32 rangeFrom = qMax(selectedFrom, (*i)->from()), rangeTo = qMin(selectedTo, uint16((*i)->from() + TextPainter::_blockLength(this, i, e)));
			if (rangeTo > rangeFrom) {
				result += _text.midRef(rangeFrom, rangeTo - rangeFrom);
			}
		}
	}
	return result;
}

EntitiesInText Text::originalEntities() const {
	EntitiesInText result;

	int32 originalLength = 0, lnkStart = 0, italicStart = 0, boldStart = 0, codeStart = 0, preStart = 0;
	int32 lnkFrom = 0, lnkIndex = 0, flags = 0;
	for (TextBlocks::const_iterator i = _blocks.cbegin(), e = _blocks.cend(); true; ++i) {
		int32 blockLnkIndex = (i == e) ? 0 : (*i)->lnkIndex();
		int32 blockFrom = (i == e) ? _text.size() : (*i)->from();
		int32 blockFlags = (i == e) ? 0 : (*i)->flags();
		if (blockFlags != flags) {
			if ((flags & TextBlockFItalic) && !(blockFlags & TextBlockFItalic)) { // write italic
				result.push_back(EntityInText(EntityInTextItalic, italicStart, originalLength - italicStart));
			} else if ((blockFlags & TextBlockFItalic) && !(flags & TextBlockFItalic)) {
				italicStart = originalLength;
			}
			if ((flags & TextBlockFSemibold) && !(blockFlags & TextBlockFSemibold)) {
				result.push_back(EntityInText(EntityInTextBold, boldStart, originalLength - boldStart));
			} else if ((blockFlags & TextBlockFSemibold) && !(flags & TextBlockFSemibold)) {
				boldStart = originalLength;
			}
			if ((flags & TextBlockFCode) && !(blockFlags & TextBlockFCode)) {
				result.push_back(EntityInText(EntityInTextCode, codeStart, originalLength - codeStart));
			} else if ((blockFlags & TextBlockFCode) && !(flags & TextBlockFCode)) {
				codeStart = originalLength;
			}
			if ((flags & TextBlockFPre) && !(blockFlags & TextBlockFPre)) {
				result.push_back(EntityInText(EntityInTextPre, preStart, originalLength - preStart));
			} else if ((blockFlags & TextBlockFPre) && !(flags & TextBlockFPre)) {
				preStart = originalLength;
			}
			flags = blockFlags;
		}
		if (blockLnkIndex != lnkIndex) {
			if (lnkIndex) { // write link
				const TextLinkPtr &lnk(_links.at(lnkIndex - 1));
				const QString &url(lnk ? lnk->text() : QString());

				int32 rangeFrom = lnkFrom, rangeTo = blockFrom;
				if (rangeTo > rangeFrom) {
					QStringRef r = _text.midRef(rangeFrom, rangeTo - rangeFrom);
					if (url.isEmpty()) {
						originalLength += r.size();
					} else {
						QUrl u(url);
						QString displayed = (u.isValid() ? u.toDisplayString() : url);
						bool shortened = (r.size() > 3) && (_text.midRef(lnkFrom, r.size() - 3) == displayed.midRef(0, r.size() - 3));
						bool same = (r == displayed.midRef(0, r.size())) || (r == url.midRef(0, r.size()));
						if (same || shortened) {
							originalLength += url.size();
							if (url.at(0) == '@') {
								result.push_back(EntityInText(EntityInTextMention, lnkStart, originalLength - lnkStart));
							} else if (url.at(0) == '#') {
								result.push_back(EntityInText(EntityInTextHashtag, lnkStart, originalLength - lnkStart));
							} else if (url.at(0) == '/') {
								result.push_back(EntityInText(EntityInTextBotCommand, lnkStart, originalLength - lnkStart));
							} else if (url.indexOf('@') > 0 && url.indexOf('/') <= 0) {
								result.push_back(EntityInText(EntityInTextEmail, lnkStart, originalLength - lnkStart));
							} else {
								result.push_back(EntityInText(EntityInTextUrl, lnkStart, originalLength - lnkStart));
							}
						} else {
							originalLength += r.size();
							result.push_back(EntityInText(EntityInTextCustomUrl, lnkStart, originalLength - lnkStart, url));
						}
					}
				}
			}
			lnkIndex = blockLnkIndex;
			if (lnkIndex) {
				lnkFrom = blockFrom;
				lnkStart = originalLength;
			}
		}
		if (i == e) break;

		TextBlockType type = (*i)->type();
		if (type == TextBlockTSkip) continue;

		if (!blockLnkIndex) {
			int32 rangeFrom = (*i)->from(), rangeTo = uint16((*i)->from() + TextPainter::_blockLength(this, i, e));
			if (rangeTo > rangeFrom) {
				originalLength += rangeTo - rangeFrom;
			}
		}
	}
	return result;
}

void Text::clean() {
	for (TextBlocks::iterator i = _blocks.begin(), e = _blocks.end(); i != e; ++i) {
		delete *i;
	}
	_blocks.clear();
	_links.clear();
	_maxWidth = _minHeight = 0;
	_startDir = Qt::LayoutDirectionAuto;
}

// COPIED FROM qtextlayout.cpp AND MODIFIED
namespace {

	struct ScriptLine {
        ScriptLine() : length(0), textWidth(0) {
        }

		int32 length;
		QFixed textWidth;
	};

    struct LineBreakHelper
    {
        LineBreakHelper()
            : glyphCount(0), maxGlyphs(0), currentPosition(0), fontEngine(0), logClusters(0)
        {
        }


        ScriptLine tmpData;
        ScriptLine spaceData;

        QGlyphLayout glyphs;

        int glyphCount;
        int maxGlyphs;
        int currentPosition;
        glyph_t previousGlyph;

        QFixed rightBearing;

        QFontEngine *fontEngine;
        const unsigned short *logClusters;

        inline glyph_t currentGlyph() const
        {
            Q_ASSERT(currentPosition > 0);
            Q_ASSERT(logClusters[currentPosition - 1] < glyphs.numGlyphs);

            return glyphs.glyphs[logClusters[currentPosition - 1]];
        }

        inline void saveCurrentGlyph()
        {
            previousGlyph = 0;
            if (currentPosition > 0 &&
                logClusters[currentPosition - 1] < glyphs.numGlyphs) {
                previousGlyph = currentGlyph(); // needed to calculate right bearing later
            }
        }

        inline void adjustRightBearing(glyph_t glyph)
        {
            qreal rb;
            fontEngine->getGlyphBearings(glyph, 0, &rb);
            rightBearing = qMin(QFixed(), QFixed::fromReal(rb));
        }

        inline void adjustRightBearing()
        {
            if (currentPosition <= 0)
                return;
            adjustRightBearing(currentGlyph());
        }

        inline void adjustPreviousRightBearing()
        {
            if (previousGlyph > 0)
                adjustRightBearing(previousGlyph);
        }

    };

	static inline void addNextCluster(int &pos, int end, ScriptLine &line, int &glyphCount,
									  const QScriptItem &current, const unsigned short *logClusters,
									  const QGlyphLayout &glyphs)
	{
		int glyphPosition = logClusters[pos];
		do { // got to the first next cluster
			++pos;
			++line.length;
		} while (pos < end && logClusters[pos] == glyphPosition);
		do { // calculate the textWidth for the rest of the current cluster.
			if (!glyphs.attributes[glyphPosition].dontPrint)
				line.textWidth += glyphs.advances[glyphPosition];
			++glyphPosition;
		} while (glyphPosition < current.num_glyphs && !glyphs.attributes[glyphPosition].clusterStart);

		Q_ASSERT((pos == end && glyphPosition == current.num_glyphs) || logClusters[pos] == glyphPosition);

		++glyphCount;
	}

} // anonymous namespace

class BlockParser {
public:

	BlockParser(QTextEngine *e, TextBlock *b, QFixed minResizeWidth, int32 blockFrom, const QString &str)
		: block(b), eng(e), str(str) {
		parseWords(minResizeWidth, blockFrom);
	}

	void parseWords(QFixed minResizeWidth, int32 blockFrom) {
		LineBreakHelper lbh;

		lbh.maxGlyphs = INT_MAX;

		int item = -1;
		int newItem = eng->findItem(0);

		style::align alignment = eng->option.alignment();

		const QCharAttributes *attributes = eng->attributes();
		if (!attributes)
			return;
		lbh.currentPosition = 0;
		int end = 0;
		lbh.logClusters = eng->layoutData->logClustersPtr;
		lbh.previousGlyph = 0;

		block->_lpadding = 0;
		block->_words.clear();

		int wordStart = lbh.currentPosition;

		bool addingEachGrapheme = false;
		int lastGraphemeBoundaryPosition = -1;
		ScriptLine lastGraphemeBoundaryLine;

		while (newItem < eng->layoutData->items.size()) {
			if (newItem != item) {
				item = newItem;
				const QScriptItem &current = eng->layoutData->items[item];
				if (!current.num_glyphs) {
					eng->shape(item);
					attributes = eng->attributes();
					if (!attributes)
						return;
					lbh.logClusters = eng->layoutData->logClustersPtr;
				}
				lbh.currentPosition = current.position;
				end = current.position + eng->length(item);
				lbh.glyphs = eng->shapedGlyphs(&current);
				QFontEngine *fontEngine = eng->fontEngine(current);
				if (lbh.fontEngine != fontEngine) {
					lbh.fontEngine = fontEngine;
				}
			}
			const QScriptItem &current = eng->layoutData->items[item];

			if (attributes[lbh.currentPosition].whiteSpace) {
				while (lbh.currentPosition < end && attributes[lbh.currentPosition].whiteSpace)
					addNextCluster(lbh.currentPosition, end, lbh.spaceData, lbh.glyphCount,
								   current, lbh.logClusters, lbh.glyphs);

				if (block->_words.isEmpty()) {
					block->_lpadding = lbh.spaceData.textWidth;
				} else {
					block->_words.back().rpadding += lbh.spaceData.textWidth;
					block->_width += lbh.spaceData.textWidth;
				}
				lbh.spaceData.length = 0;
				lbh.spaceData.textWidth = 0;

				wordStart = lbh.currentPosition;

				addingEachGrapheme = false;
				lastGraphemeBoundaryPosition = -1;
				lastGraphemeBoundaryLine = ScriptLine();
			} else {
				do {
					addNextCluster(lbh.currentPosition, end, lbh.tmpData, lbh.glyphCount,
								   current, lbh.logClusters, lbh.glyphs);

					if (lbh.currentPosition >= eng->layoutData->string.length()
						|| attributes[lbh.currentPosition].whiteSpace
						|| isLineBreak(attributes, lbh.currentPosition)) {
						lbh.adjustRightBearing();
						block->_words.push_back(TextWord(wordStart + blockFrom, lbh.tmpData.textWidth, qMin(QFixed(), lbh.rightBearing)));
						block->_width += lbh.tmpData.textWidth;
						lbh.tmpData.textWidth = 0;
						lbh.tmpData.length = 0;
						wordStart = lbh.currentPosition;
						break;
					} else if (attributes[lbh.currentPosition].graphemeBoundary) {
						if (!addingEachGrapheme && lbh.tmpData.textWidth > minResizeWidth) {
							if (lastGraphemeBoundaryPosition >= 0) {
								lbh.adjustPreviousRightBearing();
								block->_words.push_back(TextWord(wordStart + blockFrom, -lastGraphemeBoundaryLine.textWidth, qMin(QFixed(), lbh.rightBearing)));
								block->_width += lastGraphemeBoundaryLine.textWidth;
								lbh.tmpData.textWidth -= lastGraphemeBoundaryLine.textWidth;
								lbh.tmpData.length -= lastGraphemeBoundaryLine.length;
								wordStart = lastGraphemeBoundaryPosition;
							}
							addingEachGrapheme = true;
						}
						if (addingEachGrapheme) {
							lbh.adjustRightBearing();
							block->_words.push_back(TextWord(wordStart + blockFrom, -lbh.tmpData.textWidth, qMin(QFixed(), lbh.rightBearing)));
							block->_width += lbh.tmpData.textWidth;
							lbh.tmpData.textWidth = 0;
							lbh.tmpData.length = 0;
							wordStart = lbh.currentPosition;
						} else {
							lastGraphemeBoundaryPosition = lbh.currentPosition;
							lastGraphemeBoundaryLine = lbh.tmpData;
							lbh.saveCurrentGlyph();
						}
					}
				} while (lbh.currentPosition < end);
			}
			if (lbh.currentPosition == end)
				newItem = item + 1;
		}
		if (block->_words.isEmpty()) {
			block->_rpadding = 0;
		} else {
			block->_rpadding = block->_words.back().rpadding;
			block->_width -= block->_rpadding;
			block->_words.squeeze();
		}
	}

	bool isLineBreak(const QCharAttributes *attributes, int32 index) {
		bool lineBreak = attributes[index].lineBreak;
		if (lineBreak && block->lnkIndex() > 0 && index > 0 && str.at(index - 1) == '/') {
			return false; // don't break after / in links
		}
		return lineBreak;
	}

private:

	TextBlock *block;
	QTextEngine *eng;
	const QString &str;

};

TextBlock::TextBlock(const style::font &font, const QString &str, QFixed minResizeWidth, uint16 from, uint16 length, uchar flags, const style::color &color, uint16 lnkIndex) : ITextBlock(font, str, from, length, flags, color, lnkIndex) {
	_flags |= ((TextBlockTText & 0x0F) << 8);
	if (length) {
		style::font blockFont = font;
		if (!flags && lnkIndex) {
			// should use textStyle lnkFlags somehow.. not supported
		}

		if ((flags & TextBlockFPre) || (flags & TextBlockFCode)) {
			blockFont = App::monofont();
			if (blockFont->size() != font->size() || blockFont->flags() != font->flags()) {
				blockFont = style::font(font->size(), font->flags(), blockFont->family());
			}
		} else {
			if (flags & TextBlockFBold) {
				blockFont = blockFont->bold();
			}
			else if (flags & TextBlockFSemibold) {
				blockFont = st::semiboldFont;
				if (blockFont->size() != font->size() || blockFont->flags() != font->flags()) {
					blockFont = style::font(font->size(), font->flags(), blockFont->family());
				}
			}
			if (flags & TextBlockFItalic) blockFont = blockFont->italic();
			if (flags & TextBlockFUnderline) blockFont = blockFont->underline();
			if (flags & TextBlockFTilde) { // tilde fix in OpenSans
				blockFont = st::semiboldFont;
			}
		}

		QString part = str.mid(_from, length);
		QStackTextEngine engine(part, blockFont->f);
		engine.itemize();

		QTextLayout layout(&engine);
		layout.beginLayout();
		layout.createLine();

		BlockParser parser(&engine, this, minResizeWidth, _from, part);

		layout.endLayout();
	}
}

EmojiBlock::EmojiBlock(const style::font &font, const QString &str, uint16 from, uint16 length, uchar flags, const style::color &color, uint16 lnkIndex, const EmojiData *emoji) : ITextBlock(font, str, from, length, flags, color, lnkIndex), emoji(emoji) {
	_flags |= ((TextBlockTEmoji & 0x0F) << 8);
	_width = int(st::emojiSize + 2 * st::emojiPadding);
}

SkipBlock::SkipBlock(const style::font &font, const QString &str, uint16 from, int32 w, int32 h, uint16 lnkIndex) : ITextBlock(font, str, from, 1, 0, style::color(), lnkIndex), _height(h) {
	_flags |= ((TextBlockTSkip & 0x0F) << 8);
	_width = w;
}

namespace {
	void regOneProtocol(const QString &protocol) {
		_validProtocols.insert(hashCrc32(protocol.constData(), protocol.size() * sizeof(QChar)));
	}
	void regOneTopDomain(const QString &domain) {
		_validTopDomains.insert(hashCrc32(domain.constData(), domain.size() * sizeof(QChar)));
	}
}

const QSet<int32> &validProtocols() {
	return _validProtocols;
}
const QSet<int32> &validTopDomains() {
	return _validTopDomains;
}

void initLinkSets() {
	if (!_validProtocols.isEmpty() || !_validTopDomains.isEmpty()) return;

	regOneProtocol(qsl("itmss")); // itunes
	regOneProtocol(qsl("http"));
	regOneProtocol(qsl("https"));
	regOneProtocol(qsl("ftp"));
	regOneProtocol(qsl("tg")); // local urls

	regOneTopDomain(qsl("ac"));
	regOneTopDomain(qsl("ad"));
	regOneTopDomain(qsl("ae"));
	regOneTopDomain(qsl("af"));
	regOneTopDomain(qsl("ag"));
	regOneTopDomain(qsl("ai"));
	regOneTopDomain(qsl("al"));
	regOneTopDomain(qsl("am"));
	regOneTopDomain(qsl("an"));
	regOneTopDomain(qsl("ao"));
	regOneTopDomain(qsl("aq"));
	regOneTopDomain(qsl("ar"));
	regOneTopDomain(qsl("as"));
	regOneTopDomain(qsl("at"));
	regOneTopDomain(qsl("au"));
	regOneTopDomain(qsl("aw"));
	regOneTopDomain(qsl("ax"));
	regOneTopDomain(qsl("az"));
	regOneTopDomain(qsl("ba"));
	regOneTopDomain(qsl("bb"));
	regOneTopDomain(qsl("bd"));
	regOneTopDomain(qsl("be"));
	regOneTopDomain(qsl("bf"));
	regOneTopDomain(qsl("bg"));
	regOneTopDomain(qsl("bh"));
	regOneTopDomain(qsl("bi"));
	regOneTopDomain(qsl("bj"));
	regOneTopDomain(qsl("bm"));
	regOneTopDomain(qsl("bn"));
	regOneTopDomain(qsl("bo"));
	regOneTopDomain(qsl("br"));
	regOneTopDomain(qsl("bs"));
	regOneTopDomain(qsl("bt"));
	regOneTopDomain(qsl("bv"));
	regOneTopDomain(qsl("bw"));
	regOneTopDomain(qsl("by"));
	regOneTopDomain(qsl("bz"));
	regOneTopDomain(qsl("ca"));
	regOneTopDomain(qsl("cc"));
	regOneTopDomain(qsl("cd"));
	regOneTopDomain(qsl("cf"));
	regOneTopDomain(qsl("cg"));
	regOneTopDomain(qsl("ch"));
	regOneTopDomain(qsl("ci"));
	regOneTopDomain(qsl("ck"));
	regOneTopDomain(qsl("cl"));
	regOneTopDomain(qsl("cm"));
	regOneTopDomain(qsl("cn"));
	regOneTopDomain(qsl("co"));
	regOneTopDomain(qsl("cr"));
	regOneTopDomain(qsl("cu"));
	regOneTopDomain(qsl("cv"));
	regOneTopDomain(qsl("cx"));
	regOneTopDomain(qsl("cy"));
	regOneTopDomain(qsl("cz"));
	regOneTopDomain(qsl("de"));
	regOneTopDomain(qsl("dj"));
	regOneTopDomain(qsl("dk"));
	regOneTopDomain(qsl("dm"));
	regOneTopDomain(qsl("do"));
	regOneTopDomain(qsl("dz"));
	regOneTopDomain(qsl("ec"));
	regOneTopDomain(qsl("ee"));
	regOneTopDomain(qsl("eg"));
	regOneTopDomain(qsl("eh"));
	regOneTopDomain(qsl("er"));
	regOneTopDomain(qsl("es"));
	regOneTopDomain(qsl("et"));
	regOneTopDomain(qsl("eu"));
	regOneTopDomain(qsl("fi"));
	regOneTopDomain(qsl("fj"));
	regOneTopDomain(qsl("fk"));
	regOneTopDomain(qsl("fm"));
	regOneTopDomain(qsl("fo"));
	regOneTopDomain(qsl("fr"));
	regOneTopDomain(qsl("ga"));
	regOneTopDomain(qsl("gd"));
	regOneTopDomain(qsl("ge"));
	regOneTopDomain(qsl("gf"));
	regOneTopDomain(qsl("gg"));
	regOneTopDomain(qsl("gh"));
	regOneTopDomain(qsl("gi"));
	regOneTopDomain(qsl("gl"));
	regOneTopDomain(qsl("gm"));
	regOneTopDomain(qsl("gn"));
	regOneTopDomain(qsl("gp"));
	regOneTopDomain(qsl("gq"));
	regOneTopDomain(qsl("gr"));
	regOneTopDomain(qsl("gs"));
	regOneTopDomain(qsl("gt"));
	regOneTopDomain(qsl("gu"));
	regOneTopDomain(qsl("gw"));
	regOneTopDomain(qsl("gy"));
	regOneTopDomain(qsl("hk"));
	regOneTopDomain(qsl("hm"));
	regOneTopDomain(qsl("hn"));
	regOneTopDomain(qsl("hr"));
	regOneTopDomain(qsl("ht"));
	regOneTopDomain(qsl("hu"));
	regOneTopDomain(qsl("id"));
	regOneTopDomain(qsl("ie"));
	regOneTopDomain(qsl("il"));
	regOneTopDomain(qsl("im"));
	regOneTopDomain(qsl("in"));
	regOneTopDomain(qsl("io"));
	regOneTopDomain(qsl("iq"));
	regOneTopDomain(qsl("ir"));
	regOneTopDomain(qsl("is"));
	regOneTopDomain(qsl("it"));
	regOneTopDomain(qsl("je"));
	regOneTopDomain(qsl("jm"));
	regOneTopDomain(qsl("jo"));
	regOneTopDomain(qsl("jp"));
	regOneTopDomain(qsl("ke"));
	regOneTopDomain(qsl("kg"));
	regOneTopDomain(qsl("kh"));
	regOneTopDomain(qsl("ki"));
	regOneTopDomain(qsl("km"));
	regOneTopDomain(qsl("kn"));
	regOneTopDomain(qsl("kp"));
	regOneTopDomain(qsl("kr"));
	regOneTopDomain(qsl("kw"));
	regOneTopDomain(qsl("ky"));
	regOneTopDomain(qsl("kz"));
	regOneTopDomain(qsl("la"));
	regOneTopDomain(qsl("lb"));
	regOneTopDomain(qsl("lc"));
	regOneTopDomain(qsl("li"));
	regOneTopDomain(qsl("lk"));
	regOneTopDomain(qsl("lr"));
	regOneTopDomain(qsl("ls"));
	regOneTopDomain(qsl("lt"));
	regOneTopDomain(qsl("lu"));
	regOneTopDomain(qsl("lv"));
	regOneTopDomain(qsl("ly"));
	regOneTopDomain(qsl("ma"));
	regOneTopDomain(qsl("mc"));
	regOneTopDomain(qsl("md"));
	regOneTopDomain(qsl("me"));
	regOneTopDomain(qsl("mg"));
	regOneTopDomain(qsl("mh"));
	regOneTopDomain(qsl("mk"));
	regOneTopDomain(qsl("ml"));
	regOneTopDomain(qsl("mm"));
	regOneTopDomain(qsl("mn"));
	regOneTopDomain(qsl("mo"));
	regOneTopDomain(qsl("mp"));
	regOneTopDomain(qsl("mq"));
	regOneTopDomain(qsl("mr"));
	regOneTopDomain(qsl("ms"));
	regOneTopDomain(qsl("mt"));
	regOneTopDomain(qsl("mu"));
	regOneTopDomain(qsl("mv"));
	regOneTopDomain(qsl("mw"));
	regOneTopDomain(qsl("mx"));
	regOneTopDomain(qsl("my"));
	regOneTopDomain(qsl("mz"));
	regOneTopDomain(qsl("na"));
	regOneTopDomain(qsl("nc"));
	regOneTopDomain(qsl("ne"));
	regOneTopDomain(qsl("nf"));
	regOneTopDomain(qsl("ng"));
	regOneTopDomain(qsl("ni"));
	regOneTopDomain(qsl("nl"));
	regOneTopDomain(qsl("no"));
	regOneTopDomain(qsl("np"));
	regOneTopDomain(qsl("nr"));
	regOneTopDomain(qsl("nu"));
	regOneTopDomain(qsl("nz"));
	regOneTopDomain(qsl("om"));
	regOneTopDomain(qsl("pa"));
	regOneTopDomain(qsl("pe"));
	regOneTopDomain(qsl("pf"));
	regOneTopDomain(qsl("pg"));
	regOneTopDomain(qsl("ph"));
	regOneTopDomain(qsl("pk"));
	regOneTopDomain(qsl("pl"));
	regOneTopDomain(qsl("pm"));
	regOneTopDomain(qsl("pn"));
	regOneTopDomain(qsl("pr"));
	regOneTopDomain(qsl("ps"));
	regOneTopDomain(qsl("pt"));
	regOneTopDomain(qsl("pw"));
	regOneTopDomain(qsl("py"));
	regOneTopDomain(qsl("qa"));
	regOneTopDomain(qsl("re"));
	regOneTopDomain(qsl("ro"));
	regOneTopDomain(qsl("ru"));
	regOneTopDomain(qsl("rs"));
	regOneTopDomain(qsl("rw"));
	regOneTopDomain(qsl("sa"));
	regOneTopDomain(qsl("sb"));
	regOneTopDomain(qsl("sc"));
	regOneTopDomain(qsl("sd"));
	regOneTopDomain(qsl("se"));
	regOneTopDomain(qsl("sg"));
	regOneTopDomain(qsl("sh"));
	regOneTopDomain(qsl("si"));
	regOneTopDomain(qsl("sj"));
	regOneTopDomain(qsl("sk"));
	regOneTopDomain(qsl("sl"));
	regOneTopDomain(qsl("sm"));
	regOneTopDomain(qsl("sn"));
	regOneTopDomain(qsl("so"));
	regOneTopDomain(qsl("sr"));
	regOneTopDomain(qsl("ss"));
	regOneTopDomain(qsl("st"));
	regOneTopDomain(qsl("su"));
	regOneTopDomain(qsl("sv"));
	regOneTopDomain(qsl("sx"));
	regOneTopDomain(qsl("sy"));
	regOneTopDomain(qsl("sz"));
	regOneTopDomain(qsl("tc"));
	regOneTopDomain(qsl("td"));
	regOneTopDomain(qsl("tf"));
	regOneTopDomain(qsl("tg"));
	regOneTopDomain(qsl("th"));
	regOneTopDomain(qsl("tj"));
	regOneTopDomain(qsl("tk"));
	regOneTopDomain(qsl("tl"));
	regOneTopDomain(qsl("tm"));
	regOneTopDomain(qsl("tn"));
	regOneTopDomain(qsl("to"));
	regOneTopDomain(qsl("tp"));
	regOneTopDomain(qsl("tr"));
	regOneTopDomain(qsl("tt"));
	regOneTopDomain(qsl("tv"));
	regOneTopDomain(qsl("tw"));
	regOneTopDomain(qsl("tz"));
	regOneTopDomain(qsl("ua"));
	regOneTopDomain(qsl("ug"));
	regOneTopDomain(qsl("uk"));
	regOneTopDomain(qsl("um"));
	regOneTopDomain(qsl("us"));
	regOneTopDomain(qsl("uy"));
	regOneTopDomain(qsl("uz"));
	regOneTopDomain(qsl("va"));
	regOneTopDomain(qsl("vc"));
	regOneTopDomain(qsl("ve"));
	regOneTopDomain(qsl("vg"));
	regOneTopDomain(qsl("vi"));
	regOneTopDomain(qsl("vn"));
	regOneTopDomain(qsl("vu"));
	regOneTopDomain(qsl("wf"));
	regOneTopDomain(qsl("ws"));
	regOneTopDomain(qsl("ye"));
	regOneTopDomain(qsl("yt"));
	regOneTopDomain(qsl("yu"));
	regOneTopDomain(qsl("za"));
	regOneTopDomain(qsl("zm"));
	regOneTopDomain(qsl("zw"));
	regOneTopDomain(qsl("arpa"));
	regOneTopDomain(qsl("aero"));
	regOneTopDomain(qsl("asia"));
	regOneTopDomain(qsl("biz"));
	regOneTopDomain(qsl("cat"));
	regOneTopDomain(qsl("com"));
	regOneTopDomain(qsl("coop"));
	regOneTopDomain(qsl("info"));
	regOneTopDomain(qsl("int"));
	regOneTopDomain(qsl("jobs"));
	regOneTopDomain(qsl("mobi"));
	regOneTopDomain(qsl("museum"));
	regOneTopDomain(qsl("name"));
	regOneTopDomain(qsl("net"));
	regOneTopDomain(qsl("org"));
	regOneTopDomain(qsl("post"));
	regOneTopDomain(qsl("pro"));
	regOneTopDomain(qsl("tel"));
	regOneTopDomain(qsl("travel"));
	regOneTopDomain(qsl("xxx"));
	regOneTopDomain(qsl("edu"));
	regOneTopDomain(qsl("gov"));
	regOneTopDomain(qsl("mil"));
	regOneTopDomain(qsl("local"));
	regOneTopDomain(qsl("xn--lgbbat1ad8j"));
	regOneTopDomain(qsl("xn--54b7fta0cc"));
	regOneTopDomain(qsl("xn--fiqs8s"));
	regOneTopDomain(qsl("xn--fiqz9s"));
	regOneTopDomain(qsl("xn--wgbh1c"));
	regOneTopDomain(qsl("xn--node"));
	regOneTopDomain(qsl("xn--j6w193g"));
	regOneTopDomain(qsl("xn--h2brj9c"));
	regOneTopDomain(qsl("xn--mgbbh1a71e"));
	regOneTopDomain(qsl("xn--fpcrj9c3d"));
	regOneTopDomain(qsl("xn--gecrj9c"));
	regOneTopDomain(qsl("xn--s9brj9c"));
	regOneTopDomain(qsl("xn--xkc2dl3a5ee0h"));
	regOneTopDomain(qsl("xn--45brj9c"));
	regOneTopDomain(qsl("xn--mgba3a4f16a"));
	regOneTopDomain(qsl("xn--mgbayh7gpa"));
	regOneTopDomain(qsl("xn--80ao21a"));
	regOneTopDomain(qsl("xn--mgbx4cd0ab"));
	regOneTopDomain(qsl("xn--l1acc"));
	regOneTopDomain(qsl("xn--mgbc0a9azcg"));
	regOneTopDomain(qsl("xn--mgb9awbf"));
	regOneTopDomain(qsl("xn--mgbai9azgqp6j"));
	regOneTopDomain(qsl("xn--ygbi2ammx"));
	regOneTopDomain(qsl("xn--wgbl6a"));
	regOneTopDomain(qsl("xn--p1ai"));
	regOneTopDomain(qsl("xn--mgberp4a5d4ar"));
	regOneTopDomain(qsl("xn--90a3ac"));
	regOneTopDomain(qsl("xn--yfro4i67o"));
	regOneTopDomain(qsl("xn--clchc0ea0b2g2a9gcd"));
	regOneTopDomain(qsl("xn--3e0b707e"));
	regOneTopDomain(qsl("xn--fzc2c9e2c"));
	regOneTopDomain(qsl("xn--xkc2al3hye2a"));
	regOneTopDomain(qsl("xn--mgbtf8fl"));
	regOneTopDomain(qsl("xn--kprw13d"));
	regOneTopDomain(qsl("xn--kpry57d"));
	regOneTopDomain(qsl("xn--o3cw4h"));
	regOneTopDomain(qsl("xn--pgbs0dh"));
	regOneTopDomain(qsl("xn--j1amh"));
	regOneTopDomain(qsl("xn--mgbaam7a8h"));
	regOneTopDomain(qsl("xn--mgb2ddes"));
	regOneTopDomain(qsl("xn--ogbpf8fl"));
	regOneTopDomain(QString::fromUtf8("рф"));
}

namespace {
	// accent char list taken from https://github.com/aristus/accent-folding
	inline QChar chNoAccent(int32 code) {
		switch (code) {
		case 7834: return QChar(97);
		case 193: return QChar(97);
		case 225: return QChar(97);
		case 192: return QChar(97);
		case 224: return QChar(97);
		case 258: return QChar(97);
		case 259: return QChar(97);
		case 7854: return QChar(97);
		case 7855: return QChar(97);
		case 7856: return QChar(97);
		case 7857: return QChar(97);
		case 7860: return QChar(97);
		case 7861: return QChar(97);
		case 7858: return QChar(97);
		case 7859: return QChar(97);
		case 194: return QChar(97);
		case 226: return QChar(97);
		case 7844: return QChar(97);
		case 7845: return QChar(97);
		case 7846: return QChar(97);
		case 7847: return QChar(97);
		case 7850: return QChar(97);
		case 7851: return QChar(97);
		case 7848: return QChar(97);
		case 7849: return QChar(97);
		case 461: return QChar(97);
		case 462: return QChar(97);
		case 197: return QChar(97);
		case 229: return QChar(97);
		case 506: return QChar(97);
		case 507: return QChar(97);
		case 196: return QChar(97);
		case 228: return QChar(97);
		case 478: return QChar(97);
		case 479: return QChar(97);
		case 195: return QChar(97);
		case 227: return QChar(97);
		case 550: return QChar(97);
		case 551: return QChar(97);
		case 480: return QChar(97);
		case 481: return QChar(97);
		case 260: return QChar(97);
		case 261: return QChar(97);
		case 256: return QChar(97);
		case 257: return QChar(97);
		case 7842: return QChar(97);
		case 7843: return QChar(97);
		case 512: return QChar(97);
		case 513: return QChar(97);
		case 514: return QChar(97);
		case 515: return QChar(97);
		case 7840: return QChar(97);
		case 7841: return QChar(97);
		case 7862: return QChar(97);
		case 7863: return QChar(97);
		case 7852: return QChar(97);
		case 7853: return QChar(97);
		case 7680: return QChar(97);
		case 7681: return QChar(97);
		case 570: return QChar(97);
		case 11365: return QChar(97);
		case 508: return QChar(97);
		case 509: return QChar(97);
		case 482: return QChar(97);
		case 483: return QChar(97);
		case 7682: return QChar(98);
		case 7683: return QChar(98);
		case 7684: return QChar(98);
		case 7685: return QChar(98);
		case 7686: return QChar(98);
		case 7687: return QChar(98);
		case 579: return QChar(98);
		case 384: return QChar(98);
		case 7532: return QChar(98);
		case 385: return QChar(98);
		case 595: return QChar(98);
		case 386: return QChar(98);
		case 387: return QChar(98);
		case 262: return QChar(99);
		case 263: return QChar(99);
		case 264: return QChar(99);
		case 265: return QChar(99);
		case 268: return QChar(99);
		case 269: return QChar(99);
		case 266: return QChar(99);
		case 267: return QChar(99);
		case 199: return QChar(99);
		case 231: return QChar(99);
		case 7688: return QChar(99);
		case 7689: return QChar(99);
		case 571: return QChar(99);
		case 572: return QChar(99);
		case 391: return QChar(99);
		case 392: return QChar(99);
		case 597: return QChar(99);
		case 270: return QChar(100);
		case 271: return QChar(100);
		case 7690: return QChar(100);
		case 7691: return QChar(100);
		case 7696: return QChar(100);
		case 7697: return QChar(100);
		case 7692: return QChar(100);
		case 7693: return QChar(100);
		case 7698: return QChar(100);
		case 7699: return QChar(100);
		case 7694: return QChar(100);
		case 7695: return QChar(100);
		case 272: return QChar(100);
		case 273: return QChar(100);
		case 7533: return QChar(100);
		case 393: return QChar(100);
		case 598: return QChar(100);
		case 394: return QChar(100);
		case 599: return QChar(100);
		case 395: return QChar(100);
		case 396: return QChar(100);
		case 545: return QChar(100);
		case 240: return QChar(100);
		case 201: return QChar(101);
		case 399: return QChar(101);
		case 398: return QChar(101);
		case 477: return QChar(101);
		case 233: return QChar(101);
		case 200: return QChar(101);
		case 232: return QChar(101);
		case 276: return QChar(101);
		case 277: return QChar(101);
		case 202: return QChar(101);
		case 234: return QChar(101);
		case 7870: return QChar(101);
		case 7871: return QChar(101);
		case 7872: return QChar(101);
		case 7873: return QChar(101);
		case 7876: return QChar(101);
		case 7877: return QChar(101);
		case 7874: return QChar(101);
		case 7875: return QChar(101);
		case 282: return QChar(101);
		case 283: return QChar(101);
		case 203: return QChar(101);
		case 235: return QChar(101);
		case 7868: return QChar(101);
		case 7869: return QChar(101);
		case 278: return QChar(101);
		case 279: return QChar(101);
		case 552: return QChar(101);
		case 553: return QChar(101);
		case 7708: return QChar(101);
		case 7709: return QChar(101);
		case 280: return QChar(101);
		case 281: return QChar(101);
		case 274: return QChar(101);
		case 275: return QChar(101);
		case 7702: return QChar(101);
		case 7703: return QChar(101);
		case 7700: return QChar(101);
		case 7701: return QChar(101);
		case 7866: return QChar(101);
		case 7867: return QChar(101);
		case 516: return QChar(101);
		case 517: return QChar(101);
		case 518: return QChar(101);
		case 519: return QChar(101);
		case 7864: return QChar(101);
		case 7865: return QChar(101);
		case 7878: return QChar(101);
		case 7879: return QChar(101);
		case 7704: return QChar(101);
		case 7705: return QChar(101);
		case 7706: return QChar(101);
		case 7707: return QChar(101);
		case 582: return QChar(101);
		case 583: return QChar(101);
		case 602: return QChar(101);
		case 605: return QChar(101);
		case 7710: return QChar(102);
		case 7711: return QChar(102);
		case 7534: return QChar(102);
		case 401: return QChar(102);
		case 402: return QChar(102);
		case 500: return QChar(103);
		case 501: return QChar(103);
		case 286: return QChar(103);
		case 287: return QChar(103);
		case 284: return QChar(103);
		case 285: return QChar(103);
		case 486: return QChar(103);
		case 487: return QChar(103);
		case 288: return QChar(103);
		case 289: return QChar(103);
		case 290: return QChar(103);
		case 291: return QChar(103);
		case 7712: return QChar(103);
		case 7713: return QChar(103);
		case 484: return QChar(103);
		case 485: return QChar(103);
		case 403: return QChar(103);
		case 608: return QChar(103);
		case 292: return QChar(104);
		case 293: return QChar(104);
		case 542: return QChar(104);
		case 543: return QChar(104);
		case 7718: return QChar(104);
		case 7719: return QChar(104);
		case 7714: return QChar(104);
		case 7715: return QChar(104);
		case 7720: return QChar(104);
		case 7721: return QChar(104);
		case 7716: return QChar(104);
		case 7717: return QChar(104);
		case 7722: return QChar(104);
		case 7723: return QChar(104);
		case 817: return QChar(104);
		case 7830: return QChar(104);
		case 294: return QChar(104);
		case 295: return QChar(104);
		case 11367: return QChar(104);
		case 11368: return QChar(104);
		case 205: return QChar(105);
		case 237: return QChar(105);
		case 204: return QChar(105);
		case 236: return QChar(105);
		case 300: return QChar(105);
		case 301: return QChar(105);
		case 206: return QChar(105);
		case 238: return QChar(105);
		case 463: return QChar(105);
		case 464: return QChar(105);
		case 207: return QChar(105);
		case 239: return QChar(105);
		case 7726: return QChar(105);
		case 7727: return QChar(105);
		case 296: return QChar(105);
		case 297: return QChar(105);
		case 304: return QChar(105);
		case 302: return QChar(105);
		case 303: return QChar(105);
		case 298: return QChar(105);
		case 299: return QChar(105);
		case 7880: return QChar(105);
		case 7881: return QChar(105);
		case 520: return QChar(105);
		case 521: return QChar(105);
		case 522: return QChar(105);
		case 523: return QChar(105);
		case 7882: return QChar(105);
		case 7883: return QChar(105);
		case 7724: return QChar(105);
		case 7725: return QChar(105);
		case 305: return QChar(105);
		case 407: return QChar(105);
		case 616: return QChar(105);
		case 308: return QChar(106);
		case 309: return QChar(106);
		case 780: return QChar(106);
		case 496: return QChar(106);
		case 567: return QChar(106);
		case 584: return QChar(106);
		case 585: return QChar(106);
		case 669: return QChar(106);
		case 607: return QChar(106);
		case 644: return QChar(106);
		case 7728: return QChar(107);
		case 7729: return QChar(107);
		case 488: return QChar(107);
		case 489: return QChar(107);
		case 310: return QChar(107);
		case 311: return QChar(107);
		case 7730: return QChar(107);
		case 7731: return QChar(107);
		case 7732: return QChar(107);
		case 7733: return QChar(107);
		case 408: return QChar(107);
		case 409: return QChar(107);
		case 11369: return QChar(107);
		case 11370: return QChar(107);
		case 313: return QChar(97);
		case 314: return QChar(108);
		case 317: return QChar(108);
		case 318: return QChar(108);
		case 315: return QChar(108);
		case 316: return QChar(108);
		case 7734: return QChar(108);
		case 7735: return QChar(108);
		case 7736: return QChar(108);
		case 7737: return QChar(108);
		case 7740: return QChar(108);
		case 7741: return QChar(108);
		case 7738: return QChar(108);
		case 7739: return QChar(108);
		case 321: return QChar(108);
		case 322: return QChar(108);
		case 803: return QChar(108);
		case 319: return QChar(108);
		case 320: return QChar(108);
		case 573: return QChar(108);
		case 410: return QChar(108);
		case 11360: return QChar(108);
		case 11361: return QChar(108);
		case 11362: return QChar(108);
		case 619: return QChar(108);
		case 620: return QChar(108);
		case 621: return QChar(108);
		case 564: return QChar(108);
		case 7742: return QChar(109);
		case 7743: return QChar(109);
		case 7744: return QChar(109);
		case 7745: return QChar(109);
		case 7746: return QChar(109);
		case 7747: return QChar(109);
		case 625: return QChar(109);
		case 323: return QChar(110);
		case 324: return QChar(110);
		case 504: return QChar(110);
		case 505: return QChar(110);
		case 327: return QChar(110);
		case 328: return QChar(110);
		case 209: return QChar(110);
		case 241: return QChar(110);
		case 7748: return QChar(110);
		case 7749: return QChar(110);
		case 325: return QChar(110);
		case 326: return QChar(110);
		case 7750: return QChar(110);
		case 7751: return QChar(110);
		case 7754: return QChar(110);
		case 7755: return QChar(110);
		case 7752: return QChar(110);
		case 7753: return QChar(110);
		case 413: return QChar(110);
		case 626: return QChar(110);
		case 544: return QChar(110);
		case 414: return QChar(110);
		case 627: return QChar(110);
		case 565: return QChar(110);
		case 776: return QChar(116);
		case 211: return QChar(111);
		case 243: return QChar(111);
		case 210: return QChar(111);
		case 242: return QChar(111);
		case 334: return QChar(111);
		case 335: return QChar(111);
		case 212: return QChar(111);
		case 244: return QChar(111);
		case 7888: return QChar(111);
		case 7889: return QChar(111);
		case 7890: return QChar(111);
		case 7891: return QChar(111);
		case 7894: return QChar(111);
		case 7895: return QChar(111);
		case 7892: return QChar(111);
		case 7893: return QChar(111);
		case 465: return QChar(111);
		case 466: return QChar(111);
		case 214: return QChar(111);
		case 246: return QChar(111);
		case 554: return QChar(111);
		case 555: return QChar(111);
		case 336: return QChar(111);
		case 337: return QChar(111);
		case 213: return QChar(111);
		case 245: return QChar(111);
		case 7756: return QChar(111);
		case 7757: return QChar(111);
		case 7758: return QChar(111);
		case 7759: return QChar(111);
		case 556: return QChar(111);
		case 557: return QChar(111);
		case 558: return QChar(111);
		case 559: return QChar(111);
		case 560: return QChar(111);
		case 561: return QChar(111);
		case 216: return QChar(111);
		case 248: return QChar(111);
		case 510: return QChar(111);
		case 511: return QChar(111);
		case 490: return QChar(111);
		case 491: return QChar(111);
		case 492: return QChar(111);
		case 493: return QChar(111);
		case 332: return QChar(111);
		case 333: return QChar(111);
		case 7762: return QChar(111);
		case 7763: return QChar(111);
		case 7760: return QChar(111);
		case 7761: return QChar(111);
		case 7886: return QChar(111);
		case 7887: return QChar(111);
		case 524: return QChar(111);
		case 525: return QChar(111);
		case 526: return QChar(111);
		case 527: return QChar(111);
		case 416: return QChar(111);
		case 417: return QChar(111);
		case 7898: return QChar(111);
		case 7899: return QChar(111);
		case 7900: return QChar(111);
		case 7901: return QChar(111);
		case 7904: return QChar(111);
		case 7905: return QChar(111);
		case 7902: return QChar(111);
		case 7903: return QChar(111);
		case 7906: return QChar(111);
		case 7907: return QChar(111);
		case 7884: return QChar(111);
		case 7885: return QChar(111);
		case 7896: return QChar(111);
		case 7897: return QChar(111);
		case 415: return QChar(111);
		case 629: return QChar(111);
		case 7764: return QChar(112);
		case 7765: return QChar(112);
		case 7766: return QChar(112);
		case 7767: return QChar(112);
		case 11363: return QChar(112);
		case 420: return QChar(112);
		case 421: return QChar(112);
		case 771: return QChar(112);
		case 672: return QChar(113);
		case 586: return QChar(113);
		case 587: return QChar(113);
		case 340: return QChar(114);
		case 341: return QChar(114);
		case 344: return QChar(114);
		case 345: return QChar(114);
		case 7768: return QChar(114);
		case 7769: return QChar(114);
		case 342: return QChar(114);
		case 343: return QChar(114);
		case 528: return QChar(114);
		case 529: return QChar(114);
		case 530: return QChar(114);
		case 531: return QChar(114);
		case 7770: return QChar(114);
		case 7771: return QChar(114);
		case 7772: return QChar(114);
		case 7773: return QChar(114);
		case 7774: return QChar(114);
		case 7775: return QChar(114);
		case 588: return QChar(114);
		case 589: return QChar(114);
		case 7538: return QChar(114);
		case 636: return QChar(114);
		case 11364: return QChar(114);
		case 637: return QChar(114);
		case 638: return QChar(114);
		case 7539: return QChar(114);
		case 223: return QChar(115);
		case 346: return QChar(115);
		case 347: return QChar(115);
		case 7780: return QChar(115);
		case 7781: return QChar(115);
		case 348: return QChar(115);
		case 349: return QChar(115);
		case 352: return QChar(115);
		case 353: return QChar(115);
		case 7782: return QChar(115);
		case 7783: return QChar(115);
		case 7776: return QChar(115);
		case 7777: return QChar(115);
		case 7835: return QChar(115);
		case 350: return QChar(115);
		case 351: return QChar(115);
		case 7778: return QChar(115);
		case 7779: return QChar(115);
		case 7784: return QChar(115);
		case 7785: return QChar(115);
		case 536: return QChar(115);
		case 537: return QChar(115);
		case 642: return QChar(115);
		case 809: return QChar(115);
		case 222: return QChar(116);
		case 254: return QChar(116);
		case 356: return QChar(116);
		case 357: return QChar(116);
		case 7831: return QChar(116);
		case 7786: return QChar(116);
		case 7787: return QChar(116);
		case 354: return QChar(116);
		case 355: return QChar(116);
		case 7788: return QChar(116);
		case 7789: return QChar(116);
		case 538: return QChar(116);
		case 539: return QChar(116);
		case 7792: return QChar(116);
		case 7793: return QChar(116);
		case 7790: return QChar(116);
		case 7791: return QChar(116);
		case 358: return QChar(116);
		case 359: return QChar(116);
		case 574: return QChar(116);
		case 11366: return QChar(116);
		case 7541: return QChar(116);
		case 427: return QChar(116);
		case 428: return QChar(116);
		case 429: return QChar(116);
		case 430: return QChar(116);
		case 648: return QChar(116);
		case 566: return QChar(116);
		case 218: return QChar(117);
		case 250: return QChar(117);
		case 217: return QChar(117);
		case 249: return QChar(117);
		case 364: return QChar(117);
		case 365: return QChar(117);
		case 219: return QChar(117);
		case 251: return QChar(117);
		case 467: return QChar(117);
		case 468: return QChar(117);
		case 366: return QChar(117);
		case 367: return QChar(117);
		case 220: return QChar(117);
		case 252: return QChar(117);
		case 471: return QChar(117);
		case 472: return QChar(117);
		case 475: return QChar(117);
		case 476: return QChar(117);
		case 473: return QChar(117);
		case 474: return QChar(117);
		case 469: return QChar(117);
		case 470: return QChar(117);
		case 368: return QChar(117);
		case 369: return QChar(117);
		case 360: return QChar(117);
		case 361: return QChar(117);
		case 7800: return QChar(117);
		case 7801: return QChar(117);
		case 370: return QChar(117);
		case 371: return QChar(117);
		case 362: return QChar(117);
		case 363: return QChar(117);
		case 7802: return QChar(117);
		case 7803: return QChar(117);
		case 7910: return QChar(117);
		case 7911: return QChar(117);
		case 532: return QChar(117);
		case 533: return QChar(117);
		case 534: return QChar(117);
		case 535: return QChar(117);
		case 431: return QChar(117);
		case 432: return QChar(117);
		case 7912: return QChar(117);
		case 7913: return QChar(117);
		case 7914: return QChar(117);
		case 7915: return QChar(117);
		case 7918: return QChar(117);
		case 7919: return QChar(117);
		case 7916: return QChar(117);
		case 7917: return QChar(117);
		case 7920: return QChar(117);
		case 7921: return QChar(117);
		case 7908: return QChar(117);
		case 7909: return QChar(117);
		case 7794: return QChar(117);
		case 7795: return QChar(117);
		case 7798: return QChar(117);
		case 7799: return QChar(117);
		case 7796: return QChar(117);
		case 7797: return QChar(117);
		case 580: return QChar(117);
		case 649: return QChar(117);
		case 7804: return QChar(118);
		case 7805: return QChar(118);
		case 7806: return QChar(118);
		case 7807: return QChar(118);
		case 434: return QChar(118);
		case 651: return QChar(118);
		case 7810: return QChar(119);
		case 7811: return QChar(119);
		case 7808: return QChar(119);
		case 7809: return QChar(119);
		case 372: return QChar(119);
		case 373: return QChar(119);
		case 778: return QChar(121);
		case 7832: return QChar(119);
		case 7812: return QChar(119);
		case 7813: return QChar(119);
		case 7814: return QChar(119);
		case 7815: return QChar(119);
		case 7816: return QChar(119);
		case 7817: return QChar(119);
		case 7820: return QChar(120);
		case 7821: return QChar(120);
		case 7818: return QChar(120);
		case 7819: return QChar(120);
		case 221: return QChar(121);
		case 253: return QChar(121);
		case 7922: return QChar(121);
		case 7923: return QChar(121);
		case 374: return QChar(121);
		case 375: return QChar(121);
		case 7833: return QChar(121);
		case 376: return QChar(121);
		case 255: return QChar(121);
		case 7928: return QChar(121);
		case 7929: return QChar(121);
		case 7822: return QChar(121);
		case 7823: return QChar(121);
		case 562: return QChar(121);
		case 563: return QChar(121);
		case 7926: return QChar(121);
		case 7927: return QChar(121);
		case 7924: return QChar(121);
		case 7925: return QChar(121);
		case 655: return QChar(121);
		case 590: return QChar(121);
		case 591: return QChar(121);
		case 435: return QChar(121);
		case 436: return QChar(121);
		case 377: return QChar(122);
		case 378: return QChar(122);
		case 7824: return QChar(122);
		case 7825: return QChar(122);
		case 381: return QChar(122);
		case 382: return QChar(122);
		case 379: return QChar(122);
		case 380: return QChar(122);
		case 7826: return QChar(122);
		case 7827: return QChar(122);
		case 7828: return QChar(122);
		case 7829: return QChar(122);
		case 437: return QChar(122);
		case 438: return QChar(122);
		case 548: return QChar(122);
		case 549: return QChar(122);
		case 656: return QChar(122);
		case 657: return QChar(122);
		case 11371: return QChar(122);
		case 11372: return QChar(122);
		case 494: return QChar(122);
		case 495: return QChar(122);
		case 442: return QChar(122);
		case 65298: return QChar(50);
		case 65302: return QChar(54);
		case 65314: return QChar(66);
		case 65318: return QChar(70);
		case 65322: return QChar(74);
		case 65326: return QChar(78);
		case 65330: return QChar(82);
		case 65334: return QChar(86);
		case 65338: return QChar(90);
		case 65346: return QChar(98);
		case 65350: return QChar(102);
		case 65354: return QChar(106);
		case 65358: return QChar(110);
		case 65362: return QChar(114);
		case 65366: return QChar(118);
		case 65370: return QChar(122);
		case 65297: return QChar(49);
		case 65301: return QChar(53);
		case 65305: return QChar(57);
		case 65313: return QChar(65);
		case 65317: return QChar(69);
		case 65321: return QChar(73);
		case 65325: return QChar(77);
		case 65329: return QChar(81);
		case 65333: return QChar(85);
		case 65337: return QChar(89);
		case 65345: return QChar(97);
		case 65349: return QChar(101);
		case 65353: return QChar(105);
		case 65357: return QChar(109);
		case 65361: return QChar(113);
		case 65365: return QChar(117);
		case 65369: return QChar(121);
		case 65296: return QChar(48);
		case 65300: return QChar(52);
		case 65304: return QChar(56);
		case 65316: return QChar(68);
		case 65320: return QChar(72);
		case 65324: return QChar(76);
		case 65328: return QChar(80);
		case 65332: return QChar(84);
		case 65336: return QChar(88);
		case 65348: return QChar(100);
		case 65352: return QChar(104);
		case 65356: return QChar(108);
		case 65360: return QChar(112);
		case 65364: return QChar(116);
		case 65368: return QChar(120);
		case 65299: return QChar(51);
		case 65303: return QChar(55);
		case 65315: return QChar(67);
		case 65319: return QChar(71);
		case 65323: return QChar(75);
		case 65327: return QChar(79);
		case 65331: return QChar(83);
		case 65335: return QChar(87);
		case 65347: return QChar(99);
		case 65351: return QChar(103);
		case 65355: return QChar(107);
		case 65359: return QChar(111);
		case 65363: return QChar(115);
		case 65367: return QChar(119);
		default:
			break;
		}
		return QChar(0);
	}
}

QString textAccentFold(const QString &text) {
	QString result(text);
	bool copying = false;
	int32 i = 0;
	for (const QChar *s = text.unicode(), *ch = s, *e = text.unicode() + text.size(); ch != e; ++ch, ++i) {
		if (ch->unicode() < 128) {
			if (copying) result[i] = *ch;
			continue;
		}
		if (chIsDiac(*ch)) {
			copying = true;
			--i;
			continue;
		}
		if (ch->isHighSurrogate() && ch + 1 < e && (ch + 1)->isLowSurrogate()) {
			QChar noAccent = chNoAccent(QChar::surrogateToUcs4(*ch, *(ch + 1)));
			if (noAccent.unicode() > 0) {
				copying = true;
				result[i] = noAccent;
			} else {
				if (copying) result[i] = *ch;
				++ch, ++i;
				if (copying) result[i] = *ch;
			}
		} else {
			QChar noAccent = chNoAccent(ch->unicode());
			if (noAccent.unicode() > 0 && noAccent != *ch) {
				result[i] = noAccent;
			} else if (copying) {
				result[i] = *ch;
			}
		}
	}
	return (i < result.size()) ? result.mid(0, i) : result;
}

QString textSearchKey(const QString &text) {
	return textAccentFold(text.trimmed().toLower());
}

bool textSplit(QString &sendingText, EntitiesInText &sendingEntities, QString &leftText, EntitiesInText &leftEntities, int32 limit) {
	if (leftText.isEmpty() || !limit) return false;

	int32 currentEntity = 0, goodEntity = currentEntity, entityCount = leftEntities.size();
	bool goodInEntity = false, goodCanBreakEntity = false;

	int32 s = 0, half = limit / 2, goodLevel = 0;
	for (const QChar *start = leftText.constData(), *ch = start, *end = leftText.constEnd(), *good = ch; ch != end; ++ch, ++s) {
		while (currentEntity < entityCount && ch >= start + leftEntities[currentEntity].offset + leftEntities[currentEntity].length) {
			++currentEntity;
		}

#define MARK_GOOD_AS_LEVEL(level) \
if (goodLevel <= (level)) {\
goodLevel = (level);\
good = ch;\
goodEntity = currentEntity;\
goodInEntity = inEntity;\
goodCanBreakEntity = canBreakEntity;\
}

		if (s > half) {
			bool inEntity = (currentEntity < entityCount) && (ch > start + leftEntities[currentEntity].offset) && (ch < start + leftEntities[currentEntity].offset + leftEntities[currentEntity].length);
			EntityInTextType entityType = (currentEntity < entityCount) ? leftEntities[currentEntity].type : EntityInTextBold;
			bool canBreakEntity = (entityType == EntityInTextPre || entityType == EntityInTextCode);
			int32 noEntityLevel = inEntity ? 0 : 1;
			if (inEntity && !canBreakEntity) {
				MARK_GOOD_AS_LEVEL(0);
			} else {
				if (chIsNewline(*ch)) {
					if (inEntity) {
						if (ch + 1 < end && chIsNewline(*(ch + 1))) {
							MARK_GOOD_AS_LEVEL(12);
						} else {
							MARK_GOOD_AS_LEVEL(11);
						}
					} else if (ch + 1 < end && chIsNewline(*(ch + 1))) {
						MARK_GOOD_AS_LEVEL(15);
					} else if (currentEntity < entityCount && ch + 1 == start + leftEntities[currentEntity].offset && leftEntities[currentEntity].type == EntityInTextPre) {
						MARK_GOOD_AS_LEVEL(14);
					} else if (currentEntity > 0 && ch == start + leftEntities[currentEntity - 1].offset + leftEntities[currentEntity - 1].length && leftEntities[currentEntity - 1].type == EntityInTextPre) {
						MARK_GOOD_AS_LEVEL(14);
					} else {
						MARK_GOOD_AS_LEVEL(13);
					}
				} else if (chIsSpace(*ch)) {
					if (chIsSentenceEnd(*(ch - 1))) {
						MARK_GOOD_AS_LEVEL(9 + noEntityLevel);
					} else if (chIsSentencePartEnd(*(ch - 1))) {
						MARK_GOOD_AS_LEVEL(7 + noEntityLevel);
					} else {
						MARK_GOOD_AS_LEVEL(5 + noEntityLevel);
					}
				} else if (chIsWordSeparator(*(ch - 1))) {
					MARK_GOOD_AS_LEVEL(3 + noEntityLevel);
				} else {
					MARK_GOOD_AS_LEVEL(1 + noEntityLevel);
				}
			}
		}

#undef MARK_GOOD_AS_LEVEL

		int elen = 0;
		if (EmojiPtr e = emojiFromText(ch, end, &elen)) {
			for (int i = 0; i < elen; ++i, ++ch, ++s) {
				if (ch->isHighSurrogate() && i + 1 < elen && (ch + 1)->isLowSurrogate()) {
					++ch;
					++i;
				}
			}
			--ch;
			--s;
		} else if (ch->isHighSurrogate() && ch + 1 < end && (ch + 1)->isLowSurrogate()) {
			++ch;
		}
		if (s >= limit) {
			sendingText = leftText.mid(0, good - start);
			leftText = leftText.mid(good - start);
			if (goodInEntity) {
				if (goodCanBreakEntity) {
					sendingEntities = leftEntities.mid(0, goodEntity + 1);
					sendingEntities.back().length = good - start - sendingEntities.back().offset;
					leftEntities = leftEntities.mid(goodEntity);
					for (EntitiesInText::iterator i = leftEntities.begin(), e = leftEntities.end(); i != e; ++i) {
						i->offset -= good - start;
						if (i->offset < 0) {
							i->length += i->offset;
							i->offset = 0;
						}
					}
				} else {
					sendingEntities = leftEntities.mid(0, goodEntity);
					leftEntities = leftEntities.mid(goodEntity + 1);
				}
			} else {
				sendingEntities = leftEntities.mid(0, goodEntity);
				leftEntities = leftEntities.mid(goodEntity);
				for (EntitiesInText::iterator i = leftEntities.begin(), e = leftEntities.end(); i != e; ++i) {
					i->offset -= good - start;
				}
			}
			return true;
		}
	}
	sendingText = leftText;
	leftText = QString();
	sendingEntities = leftEntities;
	leftEntities = EntitiesInText();
	return true;
}

bool textcmdStartsLink(const QChar *start, int32 len, int32 commandOffset) {
	if (commandOffset + 2 < len) {
		if (*(start + commandOffset + 1) == TextCommandLinkIndex) {
			return (*(start + commandOffset + 2) != 0);
		}
		return (*(start + commandOffset + 1) != TextCommandLinkText);
	}
	return false;
}

bool checkTagStartInCommand(const QChar *start, int32 len, int32 tagStart, int32 &commandOffset, bool &commandIsLink, bool &inLink) {
	bool inCommand = false;
	const QChar *commandEnd = start + commandOffset;
	while (commandOffset < len && tagStart > commandOffset) { // skip commands, evaluating are we in link or not
		commandEnd = textSkipCommand(start + commandOffset, start + len);
		if (commandEnd > start + commandOffset) {
			if (tagStart < (commandEnd - start)) {
				inCommand = true;
				break;
			}
			for (commandOffset = commandEnd - start; commandOffset < len; ++commandOffset) {
				if (*(start + commandOffset) == TextCommand) {
					inLink = commandIsLink;
					commandIsLink = textcmdStartsLink(start, len, commandOffset);
					break;
				}
			}
			if (commandOffset >= len) {
				inLink = commandIsLink;
				commandIsLink = false;
			}
		} else {
			break;
		}
	}
	if (inCommand) {
		commandOffset = commandEnd - start;
	}
	return inCommand;
}

EntitiesInText textParseEntities(QString &text, int32 flags, bool rich) { // some code is duplicated in flattextarea.cpp!
	EntitiesInText result, mono;

	bool withHashtags = (flags & TextParseHashtags);
	bool withMentions = (flags & TextParseMentions);
	bool withBotCommands = (flags & TextParseBotCommands);
	bool withMono = (flags & TextParseMono);

	if (withMono) { // parse mono entities (code and pre)
		QString newText;

		int32 offset = 0, matchOffset = offset, len = text.size(), commandOffset = rich ? 0 : len;
		bool inLink = false, commandIsLink = false;
		const QChar *start = text.constData();
		for (; matchOffset < len;) {
			if (commandOffset <= matchOffset) {
				for (commandOffset = matchOffset; commandOffset < len; ++commandOffset) {
					if (*(start + commandOffset) == TextCommand) {
						inLink = commandIsLink;
						commandIsLink = textcmdStartsLink(start, len, commandOffset);
						break;
					}
				}
				if (commandOffset >= len) {
					inLink = commandIsLink;
					commandIsLink = false;
				}
			}
			QRegularExpressionMatch mPre = _rePre.match(text, matchOffset);
			QRegularExpressionMatch mCode = _reCode.match(text, matchOffset), mTag;
			if (!mPre.hasMatch() && !mCode.hasMatch()) break;

			int32 preStart = mPre.hasMatch() ? mPre.capturedStart() : INT_MAX,
				preEnd = mPre.hasMatch() ? mPre.capturedEnd() : INT_MAX,
				codeStart = mCode.hasMatch() ? mCode.capturedStart() : INT_MAX,
				codeEnd = mCode.hasMatch() ? mCode.capturedEnd() : INT_MAX,
				tagStart, tagEnd;
			if (mPre.hasMatch()) {
				if (!mPre.capturedRef(1).isEmpty()) {
					++preStart;
				}
				if (!mPre.capturedRef(4).isEmpty()) {
					--preEnd;
				}
			}
			if (mCode.hasMatch()) {
				if (!mCode.capturedRef(1).isEmpty()) {
					++codeStart;
				}
				if (!mCode.capturedRef(4).isEmpty()) {
					--codeEnd;
				}
			}

			bool pre = (preStart <= codeStart);
			if (pre) {
				tagStart = preStart;
				tagEnd = preEnd;
				mTag = mPre;
			} else {
				tagStart = codeStart;
				tagEnd = codeEnd;
				mTag = mCode;
			}

			bool inCommand = checkTagStartInCommand(start, len, tagStart, commandOffset, commandIsLink, inLink);
			if (inCommand || inLink) {
				matchOffset = commandOffset;
				continue;
			}

			if (newText.isEmpty()) newText.reserve(text.size());

			bool addNewlineBefore = false, addNewlineAfter = false;
			int32 outerStart = tagStart, outerEnd = tagEnd;
			int32 innerStart = tagStart + mTag.capturedLength(2), innerEnd = tagEnd - mTag.capturedLength(3);
			if (pre) {
				while (outerStart > 0 && chIsSpace(*(start + outerStart - 1), rich) && !chIsNewline(*(start + outerStart - 1))) {
					--outerStart;
				}
				addNewlineBefore = (outerStart > 0 && !chIsNewline(*(start + outerStart - 1)));

				for (int32 testInnerStart = innerStart; testInnerStart < innerEnd; ++testInnerStart) {
					if (chIsNewline(*(start + testInnerStart))) {
						innerStart = testInnerStart + 1;
						break;
					} else if (!chIsSpace(*(start + testInnerStart))) {
						break;
					}
				}
				for (int32 testInnerEnd = innerEnd; innerStart < testInnerEnd;) {
					--testInnerEnd;
					if (chIsNewline(*(start + testInnerEnd))) {
						innerEnd = testInnerEnd;
						break;
					} else if (!chIsSpace(*(start + testInnerEnd))) {
						break;
					}
				}

				while (outerEnd < len && chIsSpace(*(start + outerEnd)) && !chIsNewline(*(start + outerEnd))) {
					++outerEnd;
				}
				addNewlineAfter = (outerEnd < len && !chIsNewline(*(start + outerEnd)));
			}
			if (outerStart > offset) newText.append(start + offset, outerStart - offset);
			if (addNewlineBefore) newText.append('\n');

			int32 tagLength = innerEnd - innerStart;
			mono.push_back(EntityInText(pre ? EntityInTextPre : EntityInTextCode, newText.size(), tagLength));

			newText.append(start + innerStart, tagLength);
			if (addNewlineAfter) newText.append('\n');

			offset = matchOffset = outerEnd;
		}
		if (!newText.isEmpty()) {
			newText.append(start + offset, len - offset);
			text = newText;
		}
	}
	int32 monoEntity = 0, monoCount = mono.size(), monoTill = 0;

	initLinkSets();
	int32 len = text.size(), commandOffset = rich ? 0 : len;
	bool inLink = false, commandIsLink = false;
	const QChar *start = text.constData(), *end = start + text.size();
	for (int32 offset = 0, matchOffset = offset, mentionSkip = 0; offset < len;) {
		if (commandOffset <= offset) {
			for (commandOffset = offset; commandOffset < len; ++commandOffset) {
				if (*(start + commandOffset) == TextCommand) {
					inLink = commandIsLink;
					commandIsLink = textcmdStartsLink(start, len, commandOffset);
					break;
				}
			}
		}
		QRegularExpressionMatch mDomain = _reDomain.match(text, matchOffset);
		QRegularExpressionMatch mExplicitDomain = _reExplicitDomain.match(text, matchOffset);
		QRegularExpressionMatch mHashtag = withHashtags ? _reHashtag.match(text, matchOffset) : QRegularExpressionMatch();
		QRegularExpressionMatch mMention = withMentions ? _reMention.match(text, qMax(mentionSkip, matchOffset)) : QRegularExpressionMatch();
		QRegularExpressionMatch mBotCommand = withBotCommands ? _reBotCommand.match(text, matchOffset) : QRegularExpressionMatch();

		EntityInTextType lnkType = EntityInTextUrl;
		int32 lnkStart = 0, lnkLength = 0;
		int32 domainStart = mDomain.hasMatch() ? mDomain.capturedStart() : INT_MAX,
			domainEnd = mDomain.hasMatch() ? mDomain.capturedEnd() : INT_MAX,
			explicitDomainStart = mExplicitDomain.hasMatch() ? mExplicitDomain.capturedStart() : INT_MAX,
			explicitDomainEnd = mExplicitDomain.hasMatch() ? mExplicitDomain.capturedEnd() : INT_MAX,
			hashtagStart = mHashtag.hasMatch() ? mHashtag.capturedStart() : INT_MAX,
			hashtagEnd = mHashtag.hasMatch() ? mHashtag.capturedEnd() : INT_MAX,
			mentionStart = mMention.hasMatch() ? mMention.capturedStart() : INT_MAX,
			mentionEnd = mMention.hasMatch() ? mMention.capturedEnd() : INT_MAX,
			botCommandStart = mBotCommand.hasMatch() ? mBotCommand.capturedStart() : INT_MAX,
			botCommandEnd = mBotCommand.hasMatch() ? mBotCommand.capturedEnd() : INT_MAX;
		if (mHashtag.hasMatch()) {
			if (!mHashtag.capturedRef(1).isEmpty()) {
				++hashtagStart;
			}
			if (!mHashtag.capturedRef(2).isEmpty()) {
				--hashtagEnd;
			}
		}
		while (mMention.hasMatch()) {
			if (!mMention.capturedRef(1).isEmpty()) {
				++mentionStart;
			}
			if (!mMention.capturedRef(2).isEmpty()) {
				--mentionEnd;
			}
			if (!(start + mentionStart + 1)->isLetter() || !(start + mentionEnd - 1)->isLetterOrNumber()) {
				mentionSkip = mentionEnd;
				mMention = _reMention.match(text, qMax(mentionSkip, matchOffset));
				if (mMention.hasMatch()) {
					mentionStart = mMention.capturedStart();
					mentionEnd = mMention.capturedEnd();
				} else {
					mentionStart = INT_MAX;
					mentionEnd = INT_MAX;
				}
			} else {
				break;
			}
		}
		if (mBotCommand.hasMatch()) {
			if (!mBotCommand.capturedRef(1).isEmpty()) {
				++botCommandStart;
			}
			if (!mBotCommand.capturedRef(3).isEmpty()) {
				--botCommandEnd;
			}
		}
		if (!mDomain.hasMatch() && !mExplicitDomain.hasMatch() && !mHashtag.hasMatch() && !mMention.hasMatch() && !mBotCommand.hasMatch()) {
			break;
		}

		if (explicitDomainStart < domainStart) {
			domainStart = explicitDomainStart;
			domainEnd = explicitDomainEnd;
			mDomain = mExplicitDomain;
		}
		if (mentionStart < hashtagStart && mentionStart < domainStart && mentionStart < botCommandStart) {
			bool inCommand = checkTagStartInCommand(start, len, mentionStart, commandOffset, commandIsLink, inLink);
			if (inCommand || inLink) {
				offset = matchOffset = commandOffset;
				continue;
			}

			lnkType = EntityInTextMention;
			lnkStart = mentionStart;
			lnkLength = mentionEnd - mentionStart;
		} else if (hashtagStart < domainStart && hashtagStart < botCommandStart) {
			bool inCommand = checkTagStartInCommand(start, len, hashtagStart, commandOffset, commandIsLink, inLink);
			if (inCommand || inLink) {
				offset = matchOffset = commandOffset;
				continue;
			}

			lnkType = EntityInTextHashtag;
			lnkStart = hashtagStart;
			lnkLength = hashtagEnd - hashtagStart;
		} else if (botCommandStart < domainStart) {
			bool inCommand = checkTagStartInCommand(start, len, botCommandStart, commandOffset, commandIsLink, inLink);
			if (inCommand || inLink) {
				offset = matchOffset = commandOffset;
				continue;
			}

			lnkType = EntityInTextBotCommand;
			lnkStart = botCommandStart;
			lnkLength = botCommandEnd - botCommandStart;
		} else {
			bool inCommand = checkTagStartInCommand(start, len, domainStart, commandOffset, commandIsLink, inLink);
			if (inCommand || inLink) {
				offset = matchOffset = commandOffset;
				continue;
			}

			QString protocol = mDomain.captured(1).toLower();
			QString topDomain = mDomain.captured(3).toLower();

			bool isProtocolValid = protocol.isEmpty() || _validProtocols.contains(hashCrc32(protocol.constData(), protocol.size() * sizeof(QChar)));
			bool isTopDomainValid = !protocol.isEmpty() || _validTopDomains.contains(hashCrc32(topDomain.constData(), topDomain.size() * sizeof(QChar)));

			if (protocol.isEmpty() && domainStart > offset + 1 && *(start + domainStart - 1) == QChar('@')) {
				QString forMailName = text.mid(offset, domainStart - offset - 1);
				QRegularExpressionMatch mMailName = _reMailName.match(forMailName);
				if (mMailName.hasMatch()) {
					int32 mailStart = offset + mMailName.capturedStart();
					if (mailStart < offset) {
						mailStart = offset;
					}
					lnkType = EntityInTextEmail;
					lnkStart = mailStart;
					lnkLength = domainEnd - mailStart;
				}
			}
			if (lnkType == EntityInTextUrl && !lnkLength) {
				if (!isProtocolValid || !isTopDomainValid) {
					matchOffset = domainEnd;
					continue;
				}
				lnkStart = domainStart;

				QStack<const QChar*> parenth;
				const QChar *domainEnd = start + mDomain.capturedEnd(), *p = domainEnd;
				for (; p < end; ++p) {
					QChar ch(*p);
					if (chIsLinkEnd(ch)) break; // link finished
					if (chIsAlmostLinkEnd(ch)) {
						const QChar *endTest = p + 1;
						while (endTest < end && chIsAlmostLinkEnd(*endTest)) {
							++endTest;
						}
						if (endTest >= end || chIsLinkEnd(*endTest)) {
							break; // link finished at p
						}
						p = endTest;
						ch = *p;
					}
					if (ch == '(' || ch == '[' || ch == '{' || ch == '<') {
						parenth.push(p);
					} else if (ch == ')' || ch == ']' || ch == '}' || ch == '>') {
						if (parenth.isEmpty()) break;
						const QChar *q = parenth.pop(), open(*q);
						if ((ch == ')' && open != '(') || (ch == ']' && open != '[') || (ch == '}' && open != '{') || (ch == '>' && open != '<')) {
							p = q;
							break;
						}
					}
				}
				if (p > domainEnd) { // check, that domain ended
					if (domainEnd->unicode() != '/' && domainEnd->unicode() != '?') {
						matchOffset = domainEnd - start;
						continue;
					}
				}
				lnkLength = (p - start) - lnkStart;
			}
		}
		for (; monoEntity < monoCount && mono[monoEntity].offset <= lnkStart; ++monoEntity) {
			monoTill = qMax(monoTill, mono[monoEntity].offset + mono[monoEntity].length);
			result.push_back(mono[monoEntity]);
		}
		if (lnkStart >= monoTill) {
			result.push_back(EntityInText(lnkType, lnkStart, lnkLength));
		}

		offset = matchOffset = lnkStart + lnkLength;
	}
	for (; monoEntity < monoCount; ++monoEntity) {
		monoTill = qMax(monoTill, mono[monoEntity].offset + mono[monoEntity].length);
		result.push_back(mono[monoEntity]);
	}

	return result;
}

QString textApplyEntities(const QString &text, const EntitiesInText &entities) {
	if (entities.isEmpty()) return text;

	QMultiMap<int32, QString> closingTags;
	QString code(qsl("`")), pre(qsl("```"));

	QString result;
	int32 size = text.size();
	const QChar *b = text.constData(), *already = b, *e = b + size;
	EntitiesInText::const_iterator entity = entities.cbegin(), end = entities.cend();
	while (entity != end && ((entity->type != EntityInTextCode && entity->type != EntityInTextPre) || entity->length <= 0 || entity->offset >= size)) {
		++entity;
	}
	while (entity != end || !closingTags.isEmpty()) {
		int32 nextOpenEntity = (entity == end) ? (size + 1) : entity->offset;
		int32 nextCloseEntity = closingTags.isEmpty() ? (size + 1) : closingTags.cbegin().key();
		if (nextOpenEntity <= nextCloseEntity) {
			QString tag = (entity->type == EntityInTextCode) ? code : pre;
			if (result.isEmpty()) result.reserve(text.size() + entities.size() * pre.size() * 2);

			const QChar *offset = b + nextOpenEntity;
			if (offset > already) {
				result.append(already, offset - already);
				already = offset;
			}
			result.append(tag);
			closingTags.insert(qMin(entity->offset + entity->length, size), tag);

			++entity;
			while (entity != end && ((entity->type != EntityInTextCode && entity->type != EntityInTextPre) || entity->length <= 0 || entity->offset >= size)) {
				++entity;
			}
		} else {
			const QChar *offset = b + nextCloseEntity;
			if (offset > already) {
				result.append(already, offset - already);
				already = offset;
			}
			result.append(closingTags.cbegin().value());
			closingTags.erase(closingTags.begin());
		}
	}
	if (result.isEmpty()) {
		return text;
	}
	const QChar *offset = b + size;
	if (offset > already) {
		result.append(already, offset - already);
	}
	return result;
}

void emojiDraw(QPainter &p, EmojiPtr e, int x, int y) {
	p.drawPixmap(QPoint(x, y), App::emoji(), QRect(e->x * ESize, e->y * ESize, ESize, ESize));
}

void replaceStringWithEntities(const QLatin1String &from, QChar to, QString &result, EntitiesInText &entities, bool checkSpace = false) {
	int32 len = from.size(), s = result.size(), offset = 0, length = 0;
	EntitiesInText::iterator i = entities.begin(), e = entities.end();
	for (QChar *start = result.data(); offset < s;) {
		int32 nextOffset = result.indexOf(from, offset);
		if (nextOffset < 0) {
			moveStringPart(start, length, offset, s - offset, entities);
			break;
		}

		if (checkSpace) {
			bool spaceBefore = (nextOffset > 0) && (start + nextOffset - 1)->isSpace();
			bool spaceAfter = (nextOffset + len < s) && (start + nextOffset + len)->isSpace();
			if (!spaceBefore && !spaceAfter) {
				moveStringPart(start, length, offset, nextOffset - offset + len + 1, entities);
				continue;
			}
		}

		bool skip = false;
		for (; i != e; ++i) { // find and check next finishing entity
			if (i->offset + i->length > nextOffset) {
				skip = (i->offset < nextOffset + len);
				break;
			}
		}
		if (skip) {
			moveStringPart(start, length, offset, nextOffset - offset + len, entities);
			continue;
		}

		moveStringPart(start, length, offset, nextOffset - offset, entities);

		*(start + length) = to;
		++length;
		offset += len;
	}
	if (length < s) result.resize(length);
}

QString prepareTextWithEntities(QString result, EntitiesInText &entities, int32 flags) {
	cleanTextWithEntities(result, entities);

	if (flags) {
		entities = textParseEntities(result, flags);
	}

	replaceStringWithEntities(qstr("--"), QChar(8212), result, entities, true);
	replaceStringWithEntities(qstr("<<"), QChar(171), result, entities);
	replaceStringWithEntities(qstr(">>"), QChar(187), result, entities);

	if (cReplaceEmojis()) {
		result = replaceEmojis(result, entities);
	}

	trimTextWithEntities(result, entities);

	return result;
}
