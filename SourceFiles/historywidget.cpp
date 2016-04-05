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
#include "style.h"
#include "lang.h"

#include "application.h"
#include "boxes/confirmbox.h"
#include "historywidget.h"
#include "gui/filedialog.h"
#include "boxes/photosendbox.h"
#include "mainwidget.h"
#include "window.h"
#include "passcodewidget.h"
#include "window.h"
#include "fileuploader.h"
#include "audio.h"

#include "localstorage.h"

// flick scroll taken from http://qt-project.org/doc/qt-4.8/demos-embedded-anomaly-src-flickcharm-cpp.html

HistoryInner::HistoryInner(HistoryWidget *historyWidget, ScrollArea *scroll, History *history) : TWidget(0)
, _peer(history->peer)
, _migrated(history->peer->migrateFrom() ? App::history(history->peer->migrateFrom()->id) : 0)
, _history(history)
, _historyOffset(0)
, _historySkipHeight(0)
, _botInfo(history->peer->isUser() ? history->peer->asUser()->botInfo : 0)
, _botDescWidth(0)
, _botDescHeight(0)
, _widget(historyWidget)
, _scroll(scroll)
, _curHistory(0)
, _curBlock(0)
, _curItem(0)
, _firstLoading(false)
, _cursor(style::cur_default)
, _dragAction(NoDrag)
, _dragSelType(TextSelectLetters)
, _dragItem(0)
, _dragCursorState(HistoryDefaultCursorState)
, _dragWasInactive(false)
, _dragSelFrom(0)
, _dragSelTo(0)
, _dragSelecting(false)
, _wasSelectedText(false)
, _touchScroll(false)
, _touchSelect(false)
, _touchInProgress(false)
, _touchScrollState(TouchScrollManual)
, _touchPrevPosValid(false)
, _touchWaitingAcceleration(false)
, _touchSpeedTime(0)
, _touchAccelerationTime(0)
, _touchTime(0)
, _menu(0) {
	connect(App::wnd(), SIGNAL(imageLoaded()), this, SLOT(update()));

	_touchSelectTimer.setSingleShot(true);
	connect(&_touchSelectTimer, SIGNAL(timeout()), this, SLOT(onTouchSelect()));

	setAttribute(Qt::WA_AcceptTouchEvents);
	connect(&_touchScrollTimer, SIGNAL(timeout()), this, SLOT(onTouchScrollTimer()));

	_trippleClickTimer.setSingleShot(true);

	if (_botInfo && !_botInfo->inited && App::api()) {
		App::api()->requestFullPeer(_peer);
	}

	setMouseTracking(true);
}

void HistoryInner::messagesReceived(PeerData *peer, const QVector<MTPMessage> &messages, const QVector<MTPMessageGroup> *collapsed) {
	if (_history && _history->peer == peer) {
		_history->addOlderSlice(messages, collapsed);
	} else if (_migrated && _migrated->peer == peer) {
		bool newLoaded = (_migrated && _migrated->isEmpty() && !_history->isEmpty());
		_migrated->addOlderSlice(messages, collapsed);
		if (newLoaded) {
			_migrated->addNewerSlice(QVector<MTPMessage>(), 0);
		}
	}
}

void HistoryInner::messagesReceivedDown(PeerData *peer, const QVector<MTPMessage> &messages, const QVector<MTPMessageGroup> *collapsed) {
	if (_history && _history->peer == peer) {
		bool oldLoaded = (_migrated && _history->isEmpty() && !_migrated->isEmpty());
		_history->addNewerSlice(messages, collapsed);
		if (oldLoaded) {
			_history->addOlderSlice(QVector<MTPMessage>(), 0);
		}
	} else if (_migrated && _migrated->peer == peer) {
		_migrated->addNewerSlice(messages, collapsed);
	}
}

void HistoryInner::repaintItem(const HistoryItem *item) {
	if (!item || item->detached() || !_history) return;
	int32 msgy = itemTop(item);
	if (msgy >= 0) {
		update(0, msgy, width(), item->height());
	}
}

void HistoryInner::paintEvent(QPaintEvent *e) {
	if (App::wnd() && App::wnd()->contentOverlapped(this, e)) return;

	if (!App::main()) return;

	Painter p(this);
	QRect r(e->rect());
	bool trivial = (rect() == r);
	if (!trivial) {
		p.setClipRect(r);
	}
	uint64 ms = getms();

	if (!_firstLoading && _botInfo && !_botInfo->text.isEmpty() && _botDescHeight > 0) {
		if (r.y() < _botDescRect.y() + _botDescRect.height() && r.y() + r.height() > _botDescRect.y()) {
			textstyleSet(&st::inTextStyle);
			App::roundRect(p, _botDescRect, st::msgInBg, MessageInCorners, &st::msgInShadow);

			p.setFont(st::msgNameFont->f);
			p.setPen(st::black->p);
			p.drawText(_botDescRect.left() + st::msgPadding.left(), _botDescRect.top() + st::msgPadding.top() + st::msgNameFont->ascent, lang(lng_bot_description));

			_botInfo->text.draw(p, _botDescRect.left() + st::msgPadding.left(), _botDescRect.top() + st::msgPadding.top() + st::msgNameFont->height + st::botDescSkip, _botDescWidth);

			textstyleRestore();
		}
	} else if (_firstLoading || (_history->isEmpty() && (!_migrated || _migrated->isEmpty()))) {
		QPoint dogPos((width() - st::msgDogImg.pxWidth()) / 2, ((height() - st::msgDogImg.pxHeight()) * 4) / 9);
		p.drawPixmap(dogPos, *cChatDogImage());
	}
	if (!_firstLoading) {
		adjustCurrent(r.top());

		SelectedItems::const_iterator selEnd = _selected.cend();
		bool hasSel = !_selected.isEmpty();

		int32 drawToY = r.y() + r.height();

		int32 selfromy = itemTop(_dragSelFrom), seltoy = itemTop(_dragSelTo);
		if (selfromy < 0 || seltoy < 0) {
			selfromy = seltoy = -1;
		} else {
			seltoy += _dragSelTo->height();
		}

		int32 mtop = migratedTop(), htop = historyTop(), hdrawtop = historyDrawTop();
		if (mtop >= 0) {
			int32 iBlock = (_curHistory == _migrated ? _curBlock : (_migrated->blocks.size() - 1));
			HistoryBlock *block = _migrated->blocks[iBlock];
			int32 iItem = (_curHistory == _migrated ? _curItem : (block->items.size() - 1));
			HistoryItem *item = block->items[iItem];

			int32 y = mtop + block->y + item->y;
			p.save();
			p.translate(0, y);
			if (r.y() < y + item->height()) while (y < drawToY) {
				uint32 sel = 0;
				if (y >= selfromy && y < seltoy) {
					sel = (_dragSelecting && !item->serviceMsg() && item->id > 0) ? FullSelection : 0;
				} else if (hasSel) {
					SelectedItems::const_iterator i = _selected.constFind(item);
					if (i != selEnd) {
						sel = i.value();
					}
				}
				item->draw(p, r.translated(0, -y), sel, ms);

				if (item->hasViews()) {
					App::main()->scheduleViewIncrement(item);
				}

				int32 h = item->height();
				p.translate(0, h);
				y += h;

				++iItem;
				if (iItem == block->items.size()) {
					iItem = 0;
					++iBlock;
					if (iBlock == _migrated->blocks.size()) {
						break;
					}
					block = _migrated->blocks[iBlock];
				}
				item = block->items[iItem];
			}
			p.restore();
		}
		if (htop >= 0) {
			int32 iBlock = (_curHistory == _history ? _curBlock : 0);
			HistoryBlock *block = _history->blocks[iBlock];
			int32 iItem = (_curHistory == _history ? _curItem : 0);
			HistoryItem *item = block->items[iItem];

			int32 y = htop + block->y + item->y;
			p.save();
			p.translate(0, y);
			while (y < drawToY) {
				int32 h = item->height();
				if (r.y() < y + h && hdrawtop < y + h) {
					uint32 sel = 0;
					if (y >= selfromy && y < seltoy) {
						sel = (_dragSelecting && !item->serviceMsg() && item->id > 0) ? FullSelection : 0;
					} else if (hasSel) {
						SelectedItems::const_iterator i = _selected.constFind(item);
						if (i != selEnd) {
							sel = i.value();
						}
					}
					item->draw(p, r.translated(0, -y), sel, ms);

					if (item->hasViews()) {
						App::main()->scheduleViewIncrement(item);
					}
				}
				p.translate(0, h);
				y += h;

				++iItem;
				if (iItem == block->items.size()) {
					iItem = 0;
					++iBlock;
					if (iBlock == _history->blocks.size()) {
						break;
					}
					block = _history->blocks[iBlock];
				}
				item = block->items[iItem];
			}
			p.restore();
		}
	}
}

bool HistoryInner::event(QEvent *e) {
	if (e->type() == QEvent::TouchBegin || e->type() == QEvent::TouchUpdate || e->type() == QEvent::TouchEnd || e->type() == QEvent::TouchCancel) {
		QTouchEvent *ev = static_cast<QTouchEvent*>(e);
		if (ev->device()->type() == QTouchDevice::TouchScreen) {
			touchEvent(ev);
  			return true;
		}
	}
	return QWidget::event(e);
}

void HistoryInner::onTouchScrollTimer() {
	uint64 nowTime = getms();
	if (_touchScrollState == TouchScrollAcceleration && _touchWaitingAcceleration && (nowTime - _touchAccelerationTime) > 40) {
		_touchScrollState = TouchScrollManual;
		touchResetSpeed();
	} else if (_touchScrollState == TouchScrollAuto || _touchScrollState == TouchScrollAcceleration) {
		int32 elapsed = int32(nowTime - _touchTime);
		QPoint delta = _touchSpeed * elapsed / 1000;
		bool hasScrolled = _widget->touchScroll(delta);

		if (_touchSpeed.isNull() || !hasScrolled) {
			_touchScrollState = TouchScrollManual;
			_touchScroll = false;
			_touchScrollTimer.stop();
		} else {
			_touchTime = nowTime;
		}
		touchDeaccelerate(elapsed);
	}
}

void HistoryInner::touchUpdateSpeed() {
	const uint64 nowTime = getms();
	if (_touchPrevPosValid) {
		const int elapsed = nowTime - _touchSpeedTime;
		if (elapsed) {
			const QPoint newPixelDiff = (_touchPos - _touchPrevPos);
			const QPoint pixelsPerSecond = newPixelDiff * (1000 / elapsed);

			// fingers are inacurates, we ignore small changes to avoid stopping the autoscroll because
			// of a small horizontal offset when scrolling vertically
			const int newSpeedY = (qAbs(pixelsPerSecond.y()) > FingerAccuracyThreshold) ? pixelsPerSecond.y() : 0;
			const int newSpeedX = (qAbs(pixelsPerSecond.x()) > FingerAccuracyThreshold) ? pixelsPerSecond.x() : 0;
			if (_touchScrollState == TouchScrollAuto) {
				const int oldSpeedY = _touchSpeed.y();
				const int oldSpeedX = _touchSpeed.x();
				if ((oldSpeedY <= 0 && newSpeedY <= 0) || ((oldSpeedY >= 0 && newSpeedY >= 0)
					&& (oldSpeedX <= 0 && newSpeedX <= 0)) || (oldSpeedX >= 0 && newSpeedX >= 0)) {
					_touchSpeed.setY(snap((oldSpeedY + (newSpeedY / 4)), -MaxScrollAccelerated, +MaxScrollAccelerated));
					_touchSpeed.setX(snap((oldSpeedX + (newSpeedX / 4)), -MaxScrollAccelerated, +MaxScrollAccelerated));
				} else {
					_touchSpeed = QPoint();
				}
			} else {
				// we average the speed to avoid strange effects with the last delta
				if (!_touchSpeed.isNull()) {
					_touchSpeed.setX(snap((_touchSpeed.x() / 4) + (newSpeedX * 3 / 4), -MaxScrollFlick, +MaxScrollFlick));
					_touchSpeed.setY(snap((_touchSpeed.y() / 4) + (newSpeedY * 3 / 4), -MaxScrollFlick, +MaxScrollFlick));
				} else {
					_touchSpeed  = QPoint(newSpeedX, newSpeedY);
				}
			}
		}
	} else {
		_touchPrevPosValid = true;
	}
	_touchSpeedTime = nowTime;
	_touchPrevPos = _touchPos;
}

void HistoryInner::touchResetSpeed() {
	_touchSpeed = QPoint();
	_touchPrevPosValid = false;
}

void HistoryInner::touchDeaccelerate(int32 elapsed) {
	int32 x = _touchSpeed.x();
	int32 y = _touchSpeed.y();
	_touchSpeed.setX((x == 0) ? x : (x > 0) ? qMax(0, x - elapsed) : qMin(0, x + elapsed));
	_touchSpeed.setY((y == 0) ? y : (y > 0) ? qMax(0, y - elapsed) : qMin(0, y + elapsed));
}

void HistoryInner::touchEvent(QTouchEvent *e) {
	const Qt::TouchPointStates &states(e->touchPointStates());
	if (e->type() == QEvent::TouchCancel) { // cancel
		if (!_touchInProgress) return;
		_touchInProgress = false;
		_touchSelectTimer.stop();
		_touchScroll = _touchSelect = false;
		_touchScrollState = TouchScrollManual;
		dragActionCancel();
		return;
	}

	if (!e->touchPoints().isEmpty()) {
		_touchPrevPos = _touchPos;
		_touchPos = e->touchPoints().cbegin()->screenPos().toPoint();
	}

	switch (e->type()) {
	case QEvent::TouchBegin:
		if (_menu) {
			e->accept();
			return; // ignore mouse press, that was hiding context menu
		}
		if (_touchInProgress) return;
		if (e->touchPoints().isEmpty()) return;

		_touchInProgress = true;
		if (_touchScrollState == TouchScrollAuto) {
			_touchScrollState = TouchScrollAcceleration;
			_touchWaitingAcceleration = true;
			_touchAccelerationTime = getms();
			touchUpdateSpeed();
			_touchStart = _touchPos;
		} else {
			_touchScroll = false;
			_touchSelectTimer.start(QApplication::startDragTime());
		}
		_touchSelect = false;
		_touchStart = _touchPrevPos = _touchPos;
	break;

	case QEvent::TouchUpdate:
		if (!_touchInProgress) return;
		if (_touchSelect) {
			dragActionUpdate(_touchPos);
		} else if (!_touchScroll && (_touchPos - _touchStart).manhattanLength() >= QApplication::startDragDistance()) {
			_touchSelectTimer.stop();
			_touchScroll = true;
			touchUpdateSpeed();
		}
		if (_touchScroll) {
			if (_touchScrollState == TouchScrollManual) {
				touchScrollUpdated(_touchPos);
			} else if (_touchScrollState == TouchScrollAcceleration) {
				touchUpdateSpeed();
				_touchAccelerationTime = getms();
				if (_touchSpeed.isNull()) {
					_touchScrollState = TouchScrollManual;
				}
			}
		}
	break;

	case QEvent::TouchEnd:
		if (!_touchInProgress) return;
		_touchInProgress = false;
		if (_touchSelect) {
			dragActionFinish(_touchPos, Qt::RightButton);
			QContextMenuEvent contextMenu(QContextMenuEvent::Mouse, mapFromGlobal(_touchPos), _touchPos);
			showContextMenu(&contextMenu, true);
			_touchScroll = false;
		} else if (_touchScroll) {
			if (_touchScrollState == TouchScrollManual) {
				_touchScrollState = TouchScrollAuto;
				_touchPrevPosValid = false;
				_touchScrollTimer.start(15);
				_touchTime = getms();
			} else if (_touchScrollState == TouchScrollAuto) {
				_touchScrollState = TouchScrollManual;
				_touchScroll = false;
				touchResetSpeed();
			} else if (_touchScrollState == TouchScrollAcceleration) {
				_touchScrollState = TouchScrollAuto;
				_touchWaitingAcceleration = false;
				_touchPrevPosValid = false;
			}
		} else { // one short tap -- like mouse click
			dragActionStart(_touchPos);
			dragActionFinish(_touchPos);
		}
		_touchSelectTimer.stop();
		_touchSelect = false;
		break;
	}
}

void HistoryInner::mouseMoveEvent(QMouseEvent *e) {
	if (!(e->buttons() & (Qt::LeftButton | Qt::MiddleButton)) && (textlnkDown() || _dragAction != NoDrag)) {
		mouseReleaseEvent(e);
	}
	dragActionUpdate(e->globalPos());
}

void HistoryInner::dragActionUpdate(const QPoint &screenPos) {
	_dragPos = screenPos;
	onUpdateSelected();
}

void HistoryInner::touchScrollUpdated(const QPoint &screenPos) {
	_touchPos = screenPos;
	_widget->touchScroll(_touchPos - _touchPrevPos);
	touchUpdateSpeed();
}

QPoint HistoryInner::mapMouseToItem(QPoint p, HistoryItem *item) {
	int32 msgy = itemTop(item);
	if (msgy < 0) return QPoint(0, 0);

	p.setY(p.y() - msgy);
	return p;
}

void HistoryInner::mousePressEvent(QMouseEvent *e) {
	if (_menu) {
		e->accept();
		return; // ignore mouse press, that was hiding context menu
	}
	dragActionStart(e->globalPos(), e->button());
}

void HistoryInner::dragActionStart(const QPoint &screenPos, Qt::MouseButton button) {
	dragActionUpdate(screenPos);
	if (button != Qt::LeftButton) return;

	if (App::pressedItem() != App::hoveredItem()) {
		repaintItem(App::pressedItem());
		App::pressedItem(App::hoveredItem());
		repaintItem(App::pressedItem());
	}
	if (textlnkDown() != textlnkOver()) {
		repaintItem(App::pressedLinkItem());
		textlnkDown(textlnkOver());
		App::pressedLinkItem(App::hoveredLinkItem());
		repaintItem(App::pressedLinkItem());
		repaintItem(App::pressedItem());
	}

	_dragAction = NoDrag;
	_dragItem = App::mousedItem();
	_dragStartPos = mapMouseToItem(mapFromGlobal(screenPos), _dragItem);
	_dragWasInactive = App::wnd()->inactivePress();
	if (_dragWasInactive) App::wnd()->inactivePress(false);
	if (textlnkDown()) {
		_dragAction = PrepareDrag;
	} else if (!_selected.isEmpty()) {
		if (_selected.cbegin().value() == FullSelection) {
			if (_selected.constFind(_dragItem) != _selected.cend() && App::hoveredItem()) {
				_dragAction = PrepareDrag; // start items drag
			} else if (!_dragWasInactive) {
				_dragAction = PrepareSelect; // start items select
			}
		}
	}
	if (_dragAction == NoDrag && _dragItem) {
		bool afterDragSymbol, uponSymbol;
		uint16 symbol;
		if (_trippleClickTimer.isActive() && (screenPos - _trippleClickPoint).manhattanLength() < QApplication::startDragDistance()) {
			_dragItem->getSymbol(symbol, afterDragSymbol, uponSymbol, _dragStartPos.x(), _dragStartPos.y());
			if (uponSymbol) {
				uint32 selStatus = (symbol << 16) | symbol;
				if (selStatus != FullSelection && (_selected.isEmpty() || _selected.cbegin().value() != FullSelection)) {
					if (!_selected.isEmpty()) {
						repaintItem(_selected.cbegin().key());
						_selected.clear();
					}
					_selected.insert(_dragItem, selStatus);
					_dragSymbol = symbol;
					_dragAction = Selecting;
					_dragSelType = TextSelectParagraphs;
					dragActionUpdate(_dragPos);
				    _trippleClickTimer.start(QApplication::doubleClickInterval());
				}
			}
		} else if (App::pressedItem()) {
			_dragItem->getSymbol(symbol, afterDragSymbol, uponSymbol, _dragStartPos.x(), _dragStartPos.y());
		}
		if (_dragSelType != TextSelectParagraphs) {
			if (App::pressedItem()) {
				_dragSymbol = symbol;
				bool uponSelected = uponSymbol;
				if (uponSelected) {
					if (_selected.isEmpty() ||
						_selected.cbegin().value() == FullSelection ||
						_selected.cbegin().key() != _dragItem
					) {
						uponSelected = false;
					} else {
						uint16 selFrom = (_selected.cbegin().value() >> 16) & 0xFFFF, selTo = _selected.cbegin().value() & 0xFFFF;
						if (_dragSymbol < selFrom || _dragSymbol >= selTo) {
							uponSelected = false;
						}
					}
				}
				if (uponSelected) {
					_dragAction = PrepareDrag; // start text drag
				} else if (!_dragWasInactive) {
					if (dynamic_cast<HistorySticker*>(App::pressedItem()->getMedia()) || _dragCursorState == HistoryInDateCursorState) {
						_dragAction = PrepareDrag; // start sticker drag or by-date drag
					} else {
						if (afterDragSymbol) ++_dragSymbol;
						uint32 selStatus = (_dragSymbol << 16) | _dragSymbol;
						if (selStatus != FullSelection && (_selected.isEmpty() || _selected.cbegin().value() != FullSelection)) {
							if (!_selected.isEmpty()) {
								repaintItem(_selected.cbegin().key());
								_selected.clear();
							}
							_selected.insert(_dragItem, selStatus);
							_dragAction = Selecting;
							repaintItem(_dragItem);
						} else {
							_dragAction = PrepareSelect;
						}
					}
				}
			} else if (!_dragWasInactive) {
				_dragAction = PrepareSelect; // start items select
			}
		}
	}

	if (!_dragItem) {
		_dragAction = NoDrag;
	} else if (_dragAction == NoDrag) {
		_dragItem = 0;
	}
}

void HistoryInner::dragActionCancel() {
	_dragItem = 0;
	_dragAction = NoDrag;
	_dragStartPos = QPoint(0, 0);
	_dragSelFrom = _dragSelTo = 0;
	_wasSelectedText = false;
	_widget->noSelectingScroll();
}

void HistoryInner::onDragExec() {
	if (_dragAction != Dragging) return;

	bool uponSelected = false;
	if (_dragItem) {
		bool afterDragSymbol;
		uint16 symbol;
		if (!_selected.isEmpty() && _selected.cbegin().value() == FullSelection) {
			uponSelected = _selected.contains(_dragItem);
		} else {
			_dragItem->getSymbol(symbol, afterDragSymbol, uponSelected, _dragStartPos.x(), _dragStartPos.y());
			if (uponSelected) {
				if (_selected.isEmpty() ||
					_selected.cbegin().value() == FullSelection ||
					_selected.cbegin().key() != _dragItem
					) {
					uponSelected = false;
				} else {
					uint16 selFrom = (_selected.cbegin().value() >> 16) & 0xFFFF, selTo = _selected.cbegin().value() & 0xFFFF;
					if (symbol < selFrom || symbol >= selTo) {
						uponSelected = false;
					}
				}
			}
		}
	}
	QString sel;
	QList<QUrl> urls;
	if (uponSelected) {
		sel = getSelectedText();
	} else if (textlnkDown()) {
		sel = textlnkDown()->encoded();
		if (!sel.isEmpty() && sel.at(0) != '/' && sel.at(0) != '@' && sel.at(0) != '#') {
//			urls.push_back(QUrl::fromEncoded(sel.toUtf8())); // Google Chrome crashes in Mac OS X O_o
		}
	}
	if (!sel.isEmpty()) {
		updateDragSelection(0, 0, false);
		_widget->noSelectingScroll();

		QDrag *drag = new QDrag(App::wnd());
		QMimeData *mimeData = new QMimeData;

		mimeData->setText(sel);
		if (!urls.isEmpty()) mimeData->setUrls(urls);
		if (uponSelected && !_selected.isEmpty() && _selected.cbegin().value() == FullSelection && !Adaptive::OneColumn()) {
			mimeData->setData(qsl("application/x-td-forward-selected"), "1");
		}
		drag->setMimeData(mimeData);
		drag->exec(Qt::CopyAction);
		if (App::main()) App::main()->updateAfterDrag();
		return;
	} else {
		HistoryItem *pressedLnkItem = App::pressedLinkItem(), *pressedItem = App::pressedItem();
		QLatin1String lnkType = (textlnkDown() && pressedLnkItem) ? textlnkDown()->type() : qstr("");
		bool lnkPhoto = (lnkType == qstr("PhotoLink")),
			lnkVideo = (lnkType == qstr("VideoOpenLink")),
			lnkAudio = (lnkType == qstr("AudioOpenLink")),
			lnkDocument = (lnkType == qstr("DocumentOpenLink") || lnkType == qstr("GifOpenLink")),
			lnkContact = (lnkType == qstr("PeerLink") && dynamic_cast<HistoryContact*>(pressedLnkItem->getMedia())),
			dragSticker = dynamic_cast<HistorySticker*>(pressedItem ? pressedItem->getMedia() : 0),
			dragByDate = (_dragCursorState == HistoryInDateCursorState);
		if (lnkPhoto || lnkVideo || lnkAudio || lnkDocument || lnkContact || dragSticker || dragByDate) {
			QDrag *drag = new QDrag(App::wnd());
			QMimeData *mimeData = new QMimeData;

			if (lnkPhoto || lnkVideo || lnkAudio || lnkDocument || lnkContact) {
				mimeData->setData(qsl("application/x-td-forward-pressed-link"), "1");
			} else {
				mimeData->setData(qsl("application/x-td-forward-pressed"), "1");
			}
			if (lnkDocument) {
				QString already = static_cast<DocumentOpenLink*>(textlnkDown().data())->document()->already(true);
				if (!already.isEmpty()) {
					QList<QUrl> urls;
					urls.push_back(QUrl::fromLocalFile(already));
					mimeData->setUrls(urls);
				}
			}

			drag->setMimeData(mimeData);
			drag->exec(Qt::CopyAction);
			if (App::main()) App::main()->updateAfterDrag();
			return;
		}
	}
}

void HistoryInner::itemRemoved(HistoryItem *item) {
	SelectedItems::iterator i = _selected.find(item);
	if (i != _selected.cend()) {
		_selected.erase(i);
		_widget->updateTopBarSelection();
	}

	if (_dragItem == item) {
		dragActionCancel();
	}

	if (_dragSelFrom == item || _dragSelTo == item) {
		_dragSelFrom = 0;
		_dragSelTo = 0;
		update();
	}
	onUpdateSelected();
}

void HistoryInner::dragActionFinish(const QPoint &screenPos, Qt::MouseButton button) {
	TextLinkPtr needClick;

	dragActionUpdate(screenPos);

	if (textlnkOver()) {
		if (textlnkDown() == textlnkOver() && _dragAction != Dragging) {
			needClick = textlnkDown();

			QLatin1String lnkType = needClick->type();
			bool lnkPhoto = (lnkType == qstr("PhotoLink")),
				lnkVideo = (lnkType == qstr("VideoOpenLink")),
				lnkAudio = (lnkType == qstr("AudioOpenLink")),
				lnkDocument = (lnkType == qstr("DocumentOpenLink") || lnkType == qstr("GifOpenLink")),
				lnkContact = (lnkType == qstr("PeerLink") && dynamic_cast<HistoryContact*>(App::pressedLinkItem() ? App::pressedLinkItem()->getMedia() : 0));
			if (_dragAction == PrepareDrag && !_dragWasInactive && !_selected.isEmpty() && _selected.cbegin().value() == FullSelection && button != Qt::RightButton) {
				if (lnkPhoto || lnkVideo || lnkAudio || lnkDocument || lnkContact) {
					needClick = TextLinkPtr();
				}
			}
		}
	}
	if (textlnkDown()) {
		repaintItem(App::pressedLinkItem());
		textlnkDown(TextLinkPtr());
		App::pressedLinkItem(0);
		if (!textlnkOver() && _cursor != style::cur_default) {
			_cursor = style::cur_default;
			setCursor(_cursor);
		}
	}
	if (App::pressedItem()) {
		repaintItem(App::pressedItem());
		App::pressedItem(0);
	}

	_wasSelectedText = false;

	if (needClick) {
		DEBUG_LOG(("Will click link: %1 (%2) %3").arg(needClick->text()).arg(needClick->readable()).arg(needClick->encoded()));
		dragActionCancel();
		App::activateTextLink(needClick, button);
		return;
	}
	if (_dragAction == PrepareSelect && !_dragWasInactive && !_selected.isEmpty() && _selected.cbegin().value() == FullSelection) {
		SelectedItems::iterator i = _selected.find(_dragItem);
		if (i == _selected.cend() && !_dragItem->serviceMsg() && _dragItem->id > 0) {
			if (_selected.size() < MaxSelectedItems) {
				if (!_selected.isEmpty() && _selected.cbegin().value() != FullSelection) {
					_selected.clear();
				}
				_selected.insert(_dragItem, FullSelection);
			}
		} else {
			_selected.erase(i);
		}
		repaintItem(_dragItem);
	} else if (_dragAction == PrepareDrag && !_dragWasInactive && button != Qt::RightButton) {
		SelectedItems::iterator i = _selected.find(_dragItem);
		if (i != _selected.cend() && i.value() == FullSelection) {
			_selected.erase(i);
			repaintItem(_dragItem);
		} else if (i == _selected.cend() && !_dragItem->serviceMsg() && _dragItem->id > 0 && !_selected.isEmpty() && _selected.cbegin().value() == FullSelection) {
			if (_selected.size() < MaxSelectedItems) {
				_selected.insert(_dragItem, FullSelection);
				repaintItem(_dragItem);
			}
		} else {
			_selected.clear();
			update();
		}
	} else if (_dragAction == Selecting) {
		if (_dragSelFrom && _dragSelTo) {
			applyDragSelection();
			_dragSelFrom = _dragSelTo = 0;
		} else if (!_selected.isEmpty() && !_dragWasInactive) {
			uint32 sel = _selected.cbegin().value();
			if (sel != FullSelection && (sel & 0xFFFF) == ((sel >> 16) & 0xFFFF)) {
				_selected.clear();
				if (App::wnd()) App::wnd()->setInnerFocus();
			}
		}
	}
	_dragAction = NoDrag;
	_dragItem = 0;
	_dragSelType = TextSelectLetters;
	_widget->noSelectingScroll();
	_widget->updateTopBarSelection();
}

void HistoryInner::mouseReleaseEvent(QMouseEvent *e) {
	dragActionFinish(e->globalPos(), e->button());
	if (!rect().contains(e->pos())) {
		leaveEvent(e);
	}
}

void HistoryInner::mouseDoubleClickEvent(QMouseEvent *e) {
	if (!_history) return;

	dragActionStart(e->globalPos(), e->button());
	if (((_dragAction == Selecting && !_selected.isEmpty() && _selected.cbegin().value() != FullSelection) || (_dragAction == NoDrag && (_selected.isEmpty() || _selected.cbegin().value() != FullSelection))) && _dragSelType == TextSelectLetters && _dragItem) {
		bool afterDragSymbol, uponSelected;
		uint16 symbol;
		_dragItem->getSymbol(symbol, afterDragSymbol, uponSelected, _dragStartPos.x(), _dragStartPos.y());
		if (uponSelected) {
			_dragSymbol = symbol;
			_dragSelType = TextSelectWords;
			if (_dragAction == NoDrag) {
				_dragAction = Selecting;
				uint32 selStatus = (symbol << 16) | symbol;
				if (!_selected.isEmpty()) {
					repaintItem(_selected.cbegin().key());
					_selected.clear();
				}
				_selected.insert(_dragItem, selStatus);
			}
			mouseMoveEvent(e);

	        _trippleClickPoint = e->globalPos();
	        _trippleClickTimer.start(QApplication::doubleClickInterval());
		}
	}
}

void HistoryInner::showContextMenu(QContextMenuEvent *e, bool showFromTouch) {
	if (_menu) {
		_menu->deleteLater();
		_menu = 0;
	}
	if (e->reason() == QContextMenuEvent::Mouse) {
		dragActionUpdate(e->globalPos());
	}

	int32 selectedForForward, selectedForDelete;
	getSelectionState(selectedForForward, selectedForDelete);
	bool canSendMessages = _widget->canSendMessages(_peer);

	// -2 - has full selected items, but not over, -1 - has selection, but no over, 0 - no selection, 1 - over text, 2 - over full selected items
	int32 isUponSelected = 0, hasSelected = 0;;
	if (!_selected.isEmpty()) {
		isUponSelected = -1;
		if (_selected.cbegin().value() == FullSelection) {
			hasSelected = 2;
			if (App::hoveredItem() && _selected.constFind(App::hoveredItem()) != _selected.cend()) {
				isUponSelected = 2;
			} else {
				isUponSelected = -2;
			}
		} else {
			uint16 symbol, selFrom = (_selected.cbegin().value() >> 16) & 0xFFFF, selTo = _selected.cbegin().value() & 0xFFFF;
			hasSelected = (selTo > selFrom) ? 1 : 0;
			if (App::mousedItem() && App::mousedItem() == App::hoveredItem()) {
				QPoint mousePos(mapMouseToItem(mapFromGlobal(_dragPos), App::mousedItem()));
				bool afterDragSymbol, uponSymbol;
				App::mousedItem()->getSymbol(symbol, afterDragSymbol, uponSymbol, mousePos.x(), mousePos.y());
				if (uponSymbol && symbol >= selFrom && symbol < selTo) {
					isUponSelected = 1;
				}
			}
		}
	}
	if (showFromTouch && hasSelected && isUponSelected < hasSelected) {
		isUponSelected = hasSelected;
	}

	_menu = new PopupMenu();

	_contextMenuLnk = textlnkOver();
	HistoryItem *item = App::hoveredItem() ? App::hoveredItem() : App::hoveredLinkItem();
	PhotoLink *lnkPhoto = dynamic_cast<PhotoLink*>(_contextMenuLnk.data());
    DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data());
	bool lnkIsVideo = lnkDocument ? lnkDocument->document()->isVideo() : false;
	bool lnkIsAudio = lnkDocument ? (lnkDocument->document()->voice() != 0) : false;
	if (lnkPhoto || lnkDocument) {
		if (isUponSelected > 0) {
			_menu->addAction(lang(lng_context_copy_selected), this, SLOT(copySelectedText()))->setEnabled(true);
		}
		if (item && item->id > 0 && isUponSelected != 2 && isUponSelected != -2) {
			if (canSendMessages) {
				_menu->addAction(lang(lng_context_reply_msg), _widget, SLOT(onReplyToMessage()));
			}
			if (item->canEdit(::date(unixtime()))) {
				_menu->addAction(lang(lng_context_edit_msg), _widget, SLOT(onEditMessage()));
			}
			if (item->canPin()) {
				bool ispinned = (item->history()->peer->asChannel()->mgInfo->pinnedMsgId == item->id);
				_menu->addAction(lang(ispinned ? lng_context_unpin_msg : lng_context_pin_msg), _widget, ispinned ? SLOT(onUnpinMessage()) : SLOT(onPinMessage()));
			}
		}
		if (lnkPhoto) {
			_menu->addAction(lang(lng_context_save_image), this, SLOT(saveContextImage()))->setEnabled(true);
			_menu->addAction(lang(lng_context_copy_image), this, SLOT(copyContextImage()))->setEnabled(true);
		} else {
			if (lnkDocument && lnkDocument->document()->loading()) {
				_menu->addAction(lang(lng_context_cancel_download), this, SLOT(cancelContextDownload()))->setEnabled(true);
			} else {
				if (lnkDocument && lnkDocument->document()->loaded() && lnkDocument->document()->isGifv()) {
					_menu->addAction(lang(lng_context_save_gif), this, SLOT(saveContextGif()))->setEnabled(true);
				}
				if (lnkDocument && !lnkDocument->document()->already(true).isEmpty()) {
					_menu->addAction(lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_context_show_in_finder : lng_context_show_in_folder), this, SLOT(showContextInFolder()))->setEnabled(true);
				}
				_menu->addAction(lang(lnkIsVideo ? lng_context_save_video : (lnkIsAudio ? lng_context_save_audio : lng_context_save_file)), this, SLOT(saveContextFile()))->setEnabled(true);
			}
		}
		if (item && item->hasDirectLink() && isUponSelected != 2 && isUponSelected != -2) {
			_menu->addAction(lang(lng_context_copy_post_link), _widget, SLOT(onCopyPostLink()));
		}
		if (isUponSelected > 1) {
			_menu->addAction(lang(lng_context_forward_selected), _widget, SLOT(onForwardSelected()));
			if (selectedForDelete == selectedForForward) {
				_menu->addAction(lang(lng_context_delete_selected), _widget, SLOT(onDeleteSelected()));
			}
			_menu->addAction(lang(lng_context_clear_selection), _widget, SLOT(onClearSelected()));
		} else if (App::hoveredLinkItem()) {
			if (isUponSelected != -2) {
				if (dynamic_cast<HistoryMessage*>(App::hoveredLinkItem()) && App::hoveredLinkItem()->id > 0) {
					_menu->addAction(lang(lng_context_forward_msg), _widget, SLOT(forwardMessage()))->setEnabled(true);
				}
				if (App::hoveredLinkItem()->canDelete()) {
					_menu->addAction(lang(lng_context_delete_msg), _widget, SLOT(deleteMessage()))->setEnabled(true);
				}
			}
			if (App::hoveredLinkItem()->id > 0 && !App::hoveredLinkItem()->serviceMsg()) {
				_menu->addAction(lang(lng_context_select_msg), _widget, SLOT(selectMessage()))->setEnabled(true);
			}
			App::contextItem(App::hoveredLinkItem());
		}
	} else { // maybe cursor on some text history item?
		bool canDelete = (item && item->type() == HistoryItemMsg) && item->canDelete();
		bool canForward = (item && item->type() == HistoryItemMsg) && (item->id > 0) && !item->serviceMsg();

		HistoryMessage *msg = dynamic_cast<HistoryMessage*>(item);
		HistoryServiceMsg *srv = dynamic_cast<HistoryServiceMsg*>(item);

		if (isUponSelected > 0) {
			_menu->addAction(lang(lng_context_copy_selected), this, SLOT(copySelectedText()))->setEnabled(true);
			if (item && item->id > 0 && isUponSelected != 2) {
				if (canSendMessages) {
					_menu->addAction(lang(lng_context_reply_msg), _widget, SLOT(onReplyToMessage()));
				}
				if (item->canEdit(::date(unixtime()))) {
					_menu->addAction(lang(lng_context_edit_msg), _widget, SLOT(onEditMessage()));
				}
				if (item->canPin()) {
					bool ispinned = (item->history()->peer->asChannel()->mgInfo->pinnedMsgId == item->id);
					_menu->addAction(lang(ispinned ? lng_context_unpin_msg : lng_context_pin_msg), _widget, ispinned ? SLOT(onUnpinMessage()) : SLOT(onPinMessage()));
				}
			}
		} else {
			if (item && item->id > 0 && isUponSelected != -2) {
				if (canSendMessages) {
					_menu->addAction(lang(lng_context_reply_msg), _widget, SLOT(onReplyToMessage()));
				}
				if (item->canEdit(::date(unixtime()))) {
					_menu->addAction(lang(lng_context_edit_msg), _widget, SLOT(onEditMessage()));
				}
				if (item->canPin()) {
					bool ispinned = (item->history()->peer->asChannel()->mgInfo->pinnedMsgId == item->id);
					_menu->addAction(lang(ispinned ? lng_context_unpin_msg : lng_context_pin_msg), _widget, ispinned ? SLOT(onUnpinMessage()) : SLOT(onPinMessage()));
				}
			}
			if (item && !isUponSelected && !_contextMenuLnk) {
				if (HistoryMedia *media = (msg ? msg->getMedia() : 0)) {
					if (media->type() == MediaTypeWebPage && static_cast<HistoryWebPage*>(media)->attach()) {
						media = static_cast<HistoryWebPage*>(media)->attach();
					}
					if (media->type() == MediaTypeSticker) {
						DocumentData *doc = media->getDocument();
						if (doc && doc->sticker() && doc->sticker()->set.type() != mtpc_inputStickerSetEmpty) {
							_menu->addAction(lang(doc->sticker()->setInstalled() ? lng_context_pack_info : lng_context_pack_add), _widget, SLOT(onStickerPackInfo()));
						}

						_menu->addAction(lang(lng_context_save_image), this, SLOT(saveContextFile()))->setEnabled(true);
					} else if (media->type() == MediaTypeGif) {
						DocumentData *doc = media->getDocument();
						if (doc) {
							if (doc->loading()) {
								_menu->addAction(lang(lng_context_cancel_download), this, SLOT(cancelContextDownload()))->setEnabled(true);
							} else {
								if (doc->isGifv()) {
									_menu->addAction(lang(lng_context_save_gif), this, SLOT(saveContextGif()))->setEnabled(true);
								}
								if (!doc->already(true).isEmpty()) {
									_menu->addAction(lang((cPlatform() == dbipMac || cPlatform() == dbipMacOld) ? lng_context_show_in_finder : lng_context_show_in_folder), this, SLOT(showContextInFolder()))->setEnabled(true);
								}
								_menu->addAction(lang(lng_context_save_file), this, SLOT(saveContextFile()))->setEnabled(true);
							}
						}
					}
				}
				QString contextMenuText = item->selectedText(FullSelection);
				if (!contextMenuText.isEmpty() && (!msg || !msg->getMedia() || (msg->getMedia()->type() != MediaTypeSticker && msg->getMedia()->type() != MediaTypeGif))) {
					_menu->addAction(lang(lng_context_copy_text), this, SLOT(copyContextText()))->setEnabled(true);
				}
			}
		}

		QLatin1String linktype = _contextMenuLnk ? _contextMenuLnk->type() : qstr("");
		if (linktype == qstr("TextLink") || linktype == qstr("LocationLink")) {
			_menu->addAction(lang(lng_context_copy_link), this, SLOT(copyContextUrl()))->setEnabled(true);
		} else if (linktype == qstr("EmailLink")) {
			_menu->addAction(lang(lng_context_copy_email), this, SLOT(copyContextUrl()))->setEnabled(true);
		} else if (linktype == qstr("MentionLink")) {
			_menu->addAction(lang(lng_context_copy_mention), this, SLOT(copyContextUrl()))->setEnabled(true);
		} else if (linktype == qstr("HashtagLink")) {
			_menu->addAction(lang(lng_context_copy_hashtag), this, SLOT(copyContextUrl()))->setEnabled(true);
		} else {
		}
		if (item && item->hasDirectLink() && isUponSelected != 2 && isUponSelected != -2) {
			_menu->addAction(lang(lng_context_copy_post_link), _widget, SLOT(onCopyPostLink()));
		}
		if (isUponSelected > 1) {
			_menu->addAction(lang(lng_context_forward_selected), _widget, SLOT(onForwardSelected()));
			if (selectedForDelete == selectedForForward) {
				_menu->addAction(lang(lng_context_delete_selected), _widget, SLOT(onDeleteSelected()));
			}
			_menu->addAction(lang(lng_context_clear_selection), _widget, SLOT(onClearSelected()));
		} else if (item && ((isUponSelected != -2 && (canForward || canDelete)) || item->id > 0)) {
			if (isUponSelected != -2) {
				if (canForward) {
					_menu->addAction(lang(lng_context_forward_msg), _widget, SLOT(forwardMessage()))->setEnabled(true);
				}

				if (canDelete) {
					_menu->addAction(lang((msg && msg->uploading()) ? lng_context_cancel_upload : lng_context_delete_msg), _widget, SLOT(deleteMessage()))->setEnabled(true);
				}
			}
			if (item->id > 0 && !item->serviceMsg()) {
				_menu->addAction(lang(lng_context_select_msg), _widget, SLOT(selectMessage()))->setEnabled(true);
			}
		} else {
			if (App::mousedItem() && !App::mousedItem()->serviceMsg() && App::mousedItem()->id > 0) {
				_menu->addAction(lang(lng_context_select_msg), _widget, SLOT(selectMessage()))->setEnabled(true);
				item = App::mousedItem();
			}
		}
		App::contextItem(item);
	}

	if (_menu->actions().isEmpty()) {
		delete _menu;
		_menu = 0;
	} else {
		connect(_menu, SIGNAL(destroyed(QObject*)), this, SLOT(onMenuDestroy(QObject*)));
		_menu->popup(e->globalPos());
		e->accept();
	}
}

void HistoryInner::onMenuDestroy(QObject *obj) {
	if (_menu == obj) {
		_menu = 0;
	}
}

void HistoryInner::copySelectedText() {
	QString sel = getSelectedText();
	if (!sel.isEmpty()) {
		QApplication::clipboard()->setText(sel);
	}
}

void HistoryInner::copyContextUrl() {
	QString enc = _contextMenuLnk->encoded();
	if (!enc.isEmpty()) {
		QApplication::clipboard()->setText(enc);
	}
}

void HistoryInner::saveContextImage() {
    PhotoLink *lnk = dynamic_cast<PhotoLink*>(_contextMenuLnk.data());
	if (!lnk) return;

	PhotoData *photo = lnk->photo();
	if (!photo || !photo->date || !photo->loaded()) return;

	QString file;
	if (filedialogGetSaveFile(file, lang(lng_save_photo), qsl("JPEG Image (*.jpg);;All files (*.*)"), filedialogDefaultName(qsl("photo"), qsl(".jpg")))) {
		if (!file.isEmpty()) {
			photo->full->pix().toImage().save(file, "JPG");
		}
	}
}

void HistoryInner::copyContextImage() {
    PhotoLink *lnk = dynamic_cast<PhotoLink*>(_contextMenuLnk.data());
	if (!lnk) return;

	PhotoData *photo = lnk->photo();
	if (!photo || !photo->date || !photo->loaded()) return;

	QApplication::clipboard()->setPixmap(photo->full->pix());
}

void HistoryInner::cancelContextDownload() {
	if (DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data())) {
		lnkDocument->document()->cancel();
	} else if (HistoryItem *item = App::contextItem()) {
		if (HistoryMedia *media = item->getMedia()) {
			if (DocumentData *doc = media->getDocument()) {
				doc->cancel();
			}
		}
	}
}

void HistoryInner::showContextInFolder() {
	QString already;
	if (DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data())) {
		already = lnkDocument->document()->already(true);
	} else if (HistoryItem *item = App::contextItem()) {
		if (HistoryMedia *media = item->getMedia()) {
			if (DocumentData *doc = media->getDocument()) {
				already = doc->already(true);
			}
		}
	}
	if (!already.isEmpty()) psShowInFolder(already);
}

void HistoryInner::saveContextFile() {
	if (DocumentLink *lnkDocument = dynamic_cast<DocumentLink*>(_contextMenuLnk.data())) {
		DocumentSaveLink::doSave(lnkDocument->document(), true);
	} else if (HistoryItem *item = App::contextItem()) {
		if (HistoryMedia *media = item->getMedia()) {
			if (DocumentData *doc = media->getDocument()) {
				DocumentSaveLink::doSave(doc, true);
			}
		}
	}
}

void HistoryInner::saveContextGif() {
	if (HistoryItem *item = App::contextItem()) {
		if (HistoryMedia *media = item->getMedia()) {
			if (DocumentData *doc = media->getDocument()) {
				_widget->saveGif(doc);
			}
		}
	}
}

void HistoryInner::copyContextText() {
	HistoryItem *item = App::contextItem();
	if (!item || (item->getMedia() && item->getMedia()->type() == MediaTypeSticker)) {
		return;
	}

	QString contextMenuText = item->selectedText(FullSelection);
	if (!contextMenuText.isEmpty()) {
		QApplication::clipboard()->setText(contextMenuText);
	}
}

void HistoryInner::resizeEvent(QResizeEvent *e) {
	onUpdateSelected();
}

QString HistoryInner::getSelectedText() const {
	SelectedItems sel = _selected;

	if (_dragAction == Selecting && _dragSelFrom && _dragSelTo) {
		applyDragSelection(&sel);
	}

	if (sel.isEmpty()) return QString();
	if (sel.cbegin().value() != FullSelection) {
		return sel.cbegin().key()->selectedText(sel.cbegin().value());
	}

	int32 fullSize = 0, mtop = migratedTop(), htop = historyTop();
	QString timeFormat(qsl(", [dd.MM.yy hh:mm]\n"));
	QMap<int32, QString> texts;
	for (SelectedItems::const_iterator i = sel.cbegin(), e = sel.cend(); i != e; ++i) {
		HistoryItem *item = i.key();
		if (item->detached()) continue;

		QString text, sel = item->selectedText(FullSelection), time = item->date.toString(timeFormat);
		int32 size = item->author()->name.size() + time.size() + sel.size();
		text.reserve(size);

		int32 y = itemTop(item);
		if (y >= 0) {
			texts.insert(y, text.append(item->author()->name).append(time).append(sel));
			fullSize += size;
		}
	}

	QString result, sep(qsl("\n\n"));
	result.reserve(fullSize + (texts.size() - 1) * 2);
	for (QMap<int32, QString>::const_iterator i = texts.cbegin(), e = texts.cend(); i != e; ++i) {
		result.append(i.value());
		if (i + 1 != e) {
			result.append(sep);
		}
	}
	return result;
}

void HistoryInner::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		_widget->onListEscapePressed();
	} else if (e == QKeySequence::Copy && !_selected.isEmpty()) {
		copySelectedText();
	} else if (e == QKeySequence::Delete) {
		int32 selectedForForward, selectedForDelete;
		getSelectionState(selectedForForward, selectedForDelete);
		if (!_selected.isEmpty() && selectedForDelete == selectedForForward) {
			_widget->onDeleteSelected();
		}
	} else {
		e->ignore();
	}
}

int32 HistoryInner::recountHeight(const HistoryItem *resizedItem) {
	int32 htop = historyTop(), mtop = migratedTop();
	int32 st1 = (htop >= 0) ? (_history->lastScrollTop - htop) : -1, st2 = (_migrated && mtop >= 0) ? (_history->lastScrollTop - mtop) : -1;

	int32 ph = _scroll->height(), minadd = 0;
	int32 wasYSkip = ph - historyHeight() - st::historyPadding;
	if (_botInfo && !_botInfo->text.isEmpty()) {
		minadd = st::msgMargin.top() + st::msgMargin.bottom() + st::msgPadding.top() + st::msgPadding.bottom() + st::msgNameFont->height + st::botDescSkip + _botDescHeight;
	}
	if (wasYSkip < minadd) wasYSkip = minadd;

	if (resizedItem) {
		if (resizedItem->history() == _history) {
			_history->geomResize(_scroll->width(), &st1, resizedItem);
		} else if (_migrated && resizedItem->history() == _migrated) {
			_migrated->geomResize(_scroll->width(), &st2, resizedItem);
		}
	} else {
		_history->geomResize(_scroll->width(), &st1, resizedItem);
		if (_migrated) {
			_migrated->geomResize(_scroll->width(), &st2, resizedItem);
		}
	}
	int32 skip = 0;
	if (_migrated) { // check first messages of _history - maybe no need to display them
		if (!_migrated->isEmpty() && !_history->isEmpty() && _migrated->loadedAtBottom() && _history->loadedAtTop()) {
			if (_migrated->blocks.back()->items.back()->date.date() == _history->blocks.front()->items.front()->date.date()) {
				skip += _history->blocks.front()->items.front()->height();
				if (_migrated->blocks.back()->items.back()->isGroupMigrate() && _history->blocks.front()->items.size() == 1 && _history->blocks.size() > 1 && _history->blocks.at(1)->items.front()->isGroupMigrate()) {
					skip += _history->blocks.at(1)->items.at(0)->height();
				}
			}
			if (skip > migratedTop() + _migrated->height) {
				skip = migratedTop() + _migrated->height; // should not happen, just check.. we need historyTop() >= 0
			}
		}
	}
	if (skip != _historySkipHeight) {
		if (st1 >= 0) st1 -= (skip - _historySkipHeight);
		_historySkipHeight = skip;
	}
	updateBotInfo(false);
	if (_botInfo && !_botInfo->text.isEmpty()) {
		int32 tw = _scroll->width() - st::msgMargin.left() - st::msgMargin.right();
		if (tw > st::msgMaxWidth) tw = st::msgMaxWidth;
		tw -= st::msgPadding.left() + st::msgPadding.right();
		int32 mw = qMax(_botInfo->text.maxWidth(), st::msgNameFont->width(lang(lng_bot_description)));
		if (tw > mw) tw = mw;

		_botDescWidth = tw;
		_botDescHeight = _botInfo->text.countHeight(_botDescWidth);

		int32 descH = st::msgMargin.top() + st::msgPadding.top() + st::msgNameFont->height + st::botDescSkip + _botDescHeight + st::msgPadding.bottom() + st::msgMargin.bottom();
		int32 descAtX = (_scroll->width() - _botDescWidth) / 2 - st::msgPadding.left();
		int32 descAtY = qMin(_historyOffset - descH, qMax(0, (_scroll->height() - descH) / 2)) + st::msgMargin.top();

		_botDescRect = QRect(descAtX, descAtY, _botDescWidth + st::msgPadding.left() + st::msgPadding.right(), descH - st::msgMargin.top() - st::msgMargin.bottom());
	} else {
		_botDescWidth = _botDescHeight = 0;
		_botDescRect = QRect();
	}

	int32 newYSkip = ph - historyHeight() - st::historyPadding;
	if (_botInfo && !_botInfo->text.isEmpty()) {
		minadd = st::msgMargin.top() + st::msgMargin.bottom() + st::msgPadding.top() + st::msgPadding.bottom() + st::msgNameFont->height + st::botDescSkip + _botDescHeight;
	}
	if (newYSkip < minadd) newYSkip = minadd;

	return ((st1 >= 0 || st2 < 0) ? (st1 + htop) : (st2 + mtop)) + (newYSkip - wasYSkip);
}

void HistoryInner::updateBotInfo(bool recount) {
	int32 newh = 0;
	if (_botInfo && !_botInfo->description.isEmpty()) {
		if (_botInfo->text.isEmpty()) {
			_botInfo->text.setText(st::msgFont, _botInfo->description, _historyBotNoMonoOptions);
			if (recount) {
				int32 tw = _scroll->width() - st::msgMargin.left() - st::msgMargin.right();
				if (tw > st::msgMaxWidth) tw = st::msgMaxWidth;
				tw -= st::msgPadding.left() + st::msgPadding.right();
				int32 mw = qMax(_botInfo->text.maxWidth(), st::msgNameFont->width(lang(lng_bot_description)));
				if (tw > mw) tw = mw;

				_botDescWidth = tw;
				newh = _botInfo->text.countHeight(_botDescWidth);
			}
		} else if (recount) {
			newh = _botDescHeight;
		}
	}
	if (recount) {
		if (_botDescHeight != newh) {
			_botDescHeight = newh;
			updateSize();
		}
		if (_botDescHeight > 0) {
			int32 descH = st::msgMargin.top() + st::msgPadding.top() + st::msgNameFont->height + st::botDescSkip + _botDescHeight + st::msgPadding.bottom() + st::msgMargin.bottom();
			int32 descAtX = (_scroll->width() - _botDescWidth) / 2 - st::msgPadding.left();
			int32 descAtY = qMin(_historyOffset - descH, (_scroll->height() - descH) / 2) + st::msgMargin.top();

			_botDescRect = QRect(descAtX, descAtY, _botDescWidth + st::msgPadding.left() + st::msgPadding.right(), descH - st::msgMargin.top() - st::msgMargin.bottom());
		} else {
			_botDescWidth = 0;
			_botDescRect = QRect();
		}
	}
}

bool HistoryInner::wasSelectedText() const {
	return _wasSelectedText;
}

void HistoryInner::setFirstLoading(bool loading) {
	_firstLoading = loading;
	update();
}

HistoryItem *HistoryInner::atTopImportantMsg(int32 top, int32 height, int32 &bottomUnderScrollTop) const {
	adjustCurrent(top);
	if (!_curHistory || _curHistory->isEmpty() || _curHistory != _history) return 0;

	for (int32 blockIndex = _curBlock + 1, itemIndex = _curItem + 1; blockIndex > 0;) {
		--blockIndex;
		HistoryBlock *block = _history->blocks[blockIndex];
		if (!itemIndex) itemIndex = block->items.size();
		for (; itemIndex > 0;) {
			--itemIndex;
			HistoryItem *item = block->items[itemIndex];
			if (item->isImportant()) {
				bottomUnderScrollTop = qMin(0, itemTop(item) + item->height() - top);
				return item;
			}
		}
		itemIndex = 0;
	}
	for (int32 blockIndex = _curBlock, itemIndex = _curItem + 1; blockIndex < _history->blocks.size(); ++blockIndex) {
		HistoryBlock *block = _history->blocks[blockIndex];
		for (; itemIndex < block->items.size(); ++itemIndex) {
			HistoryItem *item = block->items[itemIndex];
			if (item->isImportant()) {
				bottomUnderScrollTop = qMin(0, itemTop(item) + item->height() - top);
				return item;
			}
		}
		itemIndex = 0;
	}
	return 0;
}

void HistoryInner::updateSize() {
	int32 ph = _scroll->height(), minadd = 0;
	int32 newYSkip = ph - historyHeight() - st::historyPadding;
	if (_botInfo && !_botInfo->text.isEmpty()) {
		minadd = st::msgMargin.top() + st::msgMargin.bottom() + st::msgPadding.top() + st::msgPadding.bottom() + st::msgNameFont->height + st::botDescSkip + _botDescHeight;
	}
	if (newYSkip < minadd) newYSkip = minadd;

	if (_botDescHeight > 0) {
		int32 descH = st::msgMargin.top() + st::msgPadding.top() + st::msgNameFont->height + st::botDescSkip + _botDescHeight + st::msgPadding.bottom() + st::msgMargin.bottom();
		int32 descAtX = (_scroll->width() - _botDescWidth) / 2 - st::msgPadding.left();
		int32 descAtY = qMin(newYSkip - descH, qMax(0, (_scroll->height() - descH) / 2)) + st::msgMargin.top();

		_botDescRect = QRect(descAtX, descAtY, _botDescWidth + st::msgPadding.left() + st::msgPadding.right(), descH - st::msgMargin.top() - st::msgMargin.bottom());
	}

	int32 yAdded = newYSkip - _historyOffset;
	_historyOffset = newYSkip;

	int32 nh = _historyOffset + historyHeight() + st::historyPadding;
	if (width() != _scroll->width() || height() != nh) {
		resize(_scroll->width(), nh);

		dragActionUpdate(QCursor::pos());
	} else {
		update();
	}
}

void HistoryInner::enterEvent(QEvent *e) {
	return QWidget::enterEvent(e);
}

void HistoryInner::leaveEvent(QEvent *e) {
	if (HistoryItem *item = App::hoveredItem()) {
		repaintItem(item);
		App::hoveredItem(0);
	}
	if (textlnkOver()) {
		if (HistoryItem *item = App::hoveredLinkItem()) {
			item->linkOut(textlnkOver());
			repaintItem(item);
			App::hoveredLinkItem(0);
		}
		textlnkOver(TextLinkPtr());
		if (!textlnkDown() && _cursor != style::cur_default) {
			_cursor = style::cur_default;
			setCursor(_cursor);
		}
	}
	return QWidget::leaveEvent(e);
}

HistoryInner::~HistoryInner() {
	delete _menu;
	_dragAction = NoDrag;
}

void HistoryInner::adjustCurrent(int32 y) const {
	int32 htop = historyTop(), hdrawtop = historyDrawTop(), mtop = migratedTop();
	_curHistory = 0;
	if (mtop >= 0) {
		adjustCurrent(y - mtop, _migrated);
	}
	if (htop >= 0 && hdrawtop >= 0 && (mtop < 0 || y >= hdrawtop)) {
		adjustCurrent(y - htop, _history);
	}
}

void HistoryInner::adjustCurrent(int32 y, History *history) const {
	_curHistory = history;
	if (_curBlock >= history->blocks.size()) {
		_curBlock = history->blocks.size() - 1;
		_curItem = 0;
	}
	while (history->blocks[_curBlock]->y > y && _curBlock > 0) {
		--_curBlock;
		_curItem = 0;
	}
	while (history->blocks[_curBlock]->y + history->blocks[_curBlock]->height <= y && _curBlock + 1 < history->blocks.size()) {
		++_curBlock;
		_curItem = 0;
	}
	HistoryBlock *block = history->blocks[_curBlock];
	if (_curItem >= block->items.size()) {
		_curItem = block->items.size() - 1;
	}
	int32 by = block->y;
	while (block->items[_curItem]->y + by > y && _curItem > 0) {
		--_curItem;
	}
	while (block->items[_curItem]->y + block->items[_curItem]->height() + by <= y && _curItem + 1 < block->items.size()) {
		++_curItem;
	}
}

HistoryItem *HistoryInner::prevItem(HistoryItem *item) {
	if (!item) return 0;
	HistoryBlock *block = item->block();
	int32 blockIndex = item->history()->blocks.indexOf(block), itemIndex = block->items.indexOf(item);
	if (blockIndex < 0  || itemIndex < 0) return 0;
	if (itemIndex > 0) {
		return block->items[itemIndex - 1];
	}
	if (blockIndex > 0) {
		return item->history()->blocks[blockIndex - 1]->items.back();
	}
	if (item->history() == _history && _migrated && _history->loadedAtTop() && !_migrated->isEmpty() && _migrated->loadedAtBottom()) {
		return _migrated->blocks.back()->items.back();
	}
	return 0;
}

HistoryItem *HistoryInner::nextItem(HistoryItem *item) {
	if (!item) return 0;
	HistoryBlock *block = item->block();
	int32 blockIndex = item->history()->blocks.indexOf(block), itemIndex = block->items.indexOf(item);
	if (blockIndex < 0  || itemIndex < 0) return 0;
	if (itemIndex + 1 < block->items.size()) {
		return block->items[itemIndex + 1];
	}
	if (blockIndex + 1 < item->history()->blocks.size()) {
		return item->history()->blocks[blockIndex + 1]->items.front();
	}
	if (item->history() == _migrated && _history && _migrated->loadedAtBottom() && _history->loadedAtTop() && !_history->isEmpty()) {
		return _history->blocks.front()->items.front();
	}
	return 0;
}

bool HistoryInner::canCopySelected() const {
	return !_selected.isEmpty();
}

bool HistoryInner::canDeleteSelected() const {
	if (_selected.isEmpty() || _selected.cbegin().value() != FullSelection) return false;
	int32 selectedForForward, selectedForDelete;
	getSelectionState(selectedForForward, selectedForDelete);
	return (selectedForForward == selectedForDelete);
}

void HistoryInner::getSelectionState(int32 &selectedForForward, int32 &selectedForDelete) const {
	selectedForForward = selectedForDelete = 0;
	for (SelectedItems::const_iterator i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
		if (i.key()->type() == HistoryItemMsg && i.value() == FullSelection) {
			if (i.key()->canDelete()) {
				++selectedForDelete;
			}
			++selectedForForward;
		}
	}
	if (!selectedForDelete && !selectedForForward && !_selected.isEmpty()) { // text selection
		selectedForForward = -1;
	}
}

void HistoryInner::clearSelectedItems(bool onlyTextSelection) {
	if (!_selected.isEmpty() && (!onlyTextSelection || _selected.cbegin().value() != FullSelection)) {
		_selected.clear();
		_widget->updateTopBarSelection();
		_widget->update();
	}
}

void HistoryInner::fillSelectedItems(SelectedItemSet &sel, bool forDelete) {
	if (_selected.isEmpty() || _selected.cbegin().value() != FullSelection) return;

	for (SelectedItems::const_iterator i = _selected.cbegin(), e = _selected.cend(); i != e; ++i) {
		HistoryItem *item = i.key();
		if (item && item->toHistoryMessage() && item->id > 0) {
			if (item->history() == _migrated) {
				sel.insert(item->id - ServerMaxMsgId, item);
			} else {
				sel.insert(item->id, item);
			}
		}
	}
}

void HistoryInner::selectItem(HistoryItem *item) {
	if (!_selected.isEmpty() && _selected.cbegin().value() != FullSelection) {
		_selected.clear();
	} else if (_selected.size() == MaxSelectedItems && _selected.constFind(item) == _selected.cend()) {
		return;
	}
	_selected.insert(item, FullSelection);
	_widget->updateTopBarSelection();
	_widget->update();
}

void HistoryInner::onTouchSelect() {
	_touchSelect = true;
	dragActionStart(_touchPos);
}

void HistoryInner::onUpdateSelected() {
	if (!_history) return;

	QPoint mousePos(mapFromGlobal(_dragPos));
	QPoint point(_widget->clampMousePosition(mousePos));

	HistoryBlock *block = 0;
	HistoryItem *item = 0;
	QPoint m;

	adjustCurrent(point.y());
	if (_curHistory && !_curHistory->isEmpty()) {
		block = _curHistory->blocks[_curBlock];
		item = block->items[_curItem];

		App::mousedItem(item);
		m = mapMouseToItem(point, item);
		if (item->hasPoint(m.x(), m.y())) {
			if (App::hoveredItem() != item) {
				repaintItem(App::hoveredItem());
				App::hoveredItem(item);
				repaintItem(App::hoveredItem());
			}
		} else if (App::hoveredItem()) {
			repaintItem(App::hoveredItem());
			App::hoveredItem(0);
		}
	}
	if (_dragItem && _dragItem->detached()) {
		dragActionCancel();
	}

	Qt::CursorShape cur = style::cur_default;
	HistoryCursorState cursorState = HistoryDefaultCursorState;
	bool lnkChanged = false, lnkInDesc = false;

	TextLinkPtr lnk;
	if (point.y() < _historyOffset) {
		if (_botInfo && !_botInfo->text.isEmpty() && _botDescHeight > 0) {
			bool inText = false;
			_botInfo->text.getState(lnk, inText, point.x() - _botDescRect.left() - st::msgPadding.left(), point.y() - _botDescRect.top() - st::msgPadding.top() - st::botDescSkip - st::msgNameFont->height, _botDescWidth);
			cursorState = inText ? HistoryInTextCursorState : HistoryDefaultCursorState;
			lnkInDesc = true;
		}
	} else if (item) {
		item->getState(lnk, cursorState, m.x(), m.y());
	}
	if (lnk != textlnkOver()) {
		lnkChanged = true;
		if (textlnkOver()) {
			if (HistoryItem *item = App::hoveredLinkItem()) {
				item->linkOut(textlnkOver());
				repaintItem(item);
			} else {
				update(_botDescRect);
			}
		}
		textlnkOver(lnk);
		PopupTooltip::Hide();
		App::hoveredLinkItem((lnk && !lnkInDesc) ? item : 0);
		if (textlnkOver()) {
			if (HistoryItem *item = App::hoveredLinkItem()) {
				item->linkOver(textlnkOver());
				repaintItem(item);
			} else {
				update(_botDescRect);
			}
		}
	}
	if (cursorState != _dragCursorState) {
		PopupTooltip::Hide();
	}
	if (lnk || cursorState == HistoryInDateCursorState || cursorState == HistoryInForwardedCursorState) {
		PopupTooltip::Show(1000, this);
	}

	if (_dragAction == NoDrag) {
		_dragCursorState = cursorState;
		if (lnk) {
			cur = style::cur_pointer;
		} else if (_dragCursorState == HistoryInTextCursorState && (_selected.isEmpty() || _selected.cbegin().value() != FullSelection)) {
			cur = style::cur_text;
		} else if (_dragCursorState == HistoryInDateCursorState) {
//			cur = style::cur_cross;
		}
	} else if (item) {
		if (item != _dragItem || (m - _dragStartPos).manhattanLength() >= QApplication::startDragDistance()) {
			if (_dragAction == PrepareDrag) {
				_dragAction = Dragging;
				QTimer::singleShot(1, this, SLOT(onDragExec()));
			} else if (_dragAction == PrepareSelect) {
				_dragAction = Selecting;
			}
		}
		cur = textlnkDown() ? style::cur_pointer : style::cur_default;
		if (_dragAction == Selecting) {
			bool canSelectMany = (_history != 0);
			if (item == _dragItem && item == App::hoveredItem() && !_selected.isEmpty() && _selected.cbegin().value() != FullSelection) {
				bool afterSymbol, uponSymbol;
				uint16 second;
				_dragItem->getSymbol(second, afterSymbol, uponSymbol, m.x(), m.y());
				if (afterSymbol && _dragSelType == TextSelectLetters) ++second;
				uint32 selState = _dragItem->adjustSelection(qMin(second, _dragSymbol), qMax(second, _dragSymbol), _dragSelType);
				if (_selected[_dragItem] != selState) {
					_selected[_dragItem] = selState;
					repaintItem(_dragItem);
				}
				if (!_wasSelectedText && (selState == FullSelection || (selState & 0xFFFF) != ((selState >> 16) & 0xFFFF))) {
					_wasSelectedText = true;
					setFocus();
				}
				updateDragSelection(0, 0, false);
			} else if (canSelectMany) {
				bool selectingDown = (itemTop(_dragItem) < itemTop(item)) || (_dragItem == item && _dragStartPos.y() < m.y());
				HistoryItem *dragSelFrom = _dragItem, *dragSelTo = item;
				if (!dragSelFrom->hasPoint(_dragStartPos.x(), _dragStartPos.y())) { // maybe exclude dragSelFrom
					if (selectingDown) {
						if (_dragStartPos.y() >= dragSelFrom->height() - st::msgMargin.bottom() || ((item == dragSelFrom) && (m.y() < _dragStartPos.y() + QApplication::startDragDistance()))) {
							dragSelFrom = (dragSelFrom == dragSelTo) ? 0 : nextItem(dragSelFrom);
						}
					} else {
						if (_dragStartPos.y() < st::msgMargin.top() || ((item == dragSelFrom) && (m.y() >= _dragStartPos.y() - QApplication::startDragDistance()))) {
							dragSelFrom = (dragSelFrom == dragSelTo) ? 0 : prevItem(dragSelFrom);
						}
					}
				}
				if (_dragItem != item) { // maybe exclude dragSelTo
					if (selectingDown) {
						if (m.y() < st::msgMargin.top()) {
							dragSelTo = (dragSelFrom == dragSelTo) ? 0 : prevItem(dragSelTo);
						}
					} else {
						if (m.y() >= dragSelTo->height() - st::msgMargin.bottom()) {
							dragSelTo = (dragSelFrom == dragSelTo) ? 0 : nextItem(dragSelTo);
						}
					}
				}
				bool dragSelecting = false;
				HistoryItem *dragFirstAffected = dragSelFrom;
				while (dragFirstAffected && (dragFirstAffected->id < 0 || dragFirstAffected->serviceMsg())) {
					dragFirstAffected = (dragFirstAffected == dragSelTo) ? 0 : (selectingDown ? nextItem(dragFirstAffected) : prevItem(dragFirstAffected));
				}
				if (dragFirstAffected) {
					SelectedItems::const_iterator i = _selected.constFind(dragFirstAffected);
					dragSelecting = (i == _selected.cend() || i.value() != FullSelection);
				}
				updateDragSelection(dragSelFrom, dragSelTo, dragSelecting);
			}
		} else if (_dragAction == Dragging) {
		}

		if (textlnkDown()) {
			cur = style::cur_pointer;
		} else if (_dragAction == Selecting && !_selected.isEmpty() && _selected.cbegin().value() != FullSelection) {
			if (!_dragSelFrom || !_dragSelTo) {
				cur = style::cur_text;
			}
		}
	}
	if (_dragAction == Selecting) {
		_widget->checkSelectingScroll(mousePos);
	} else {
		updateDragSelection(0, 0, false);
		_widget->noSelectingScroll();
	}

	if (_dragAction == NoDrag && (lnkChanged || cur != _cursor)) {
		setCursor(_cursor = cur);
	}
}

void HistoryInner::updateDragSelection(HistoryItem *dragSelFrom, HistoryItem *dragSelTo, bool dragSelecting, bool force) {
	if (_dragSelFrom != dragSelFrom || _dragSelTo != dragSelTo || _dragSelecting != dragSelecting) {
		_dragSelFrom = dragSelFrom;
		_dragSelTo = dragSelTo;
		int32 fromy = itemTop(_dragSelFrom), toy = itemTop(_dragSelTo);
		if (fromy >= 0 && toy >= 0 && fromy > toy) {
			qSwap(_dragSelFrom, _dragSelTo);
		}
		_dragSelecting = dragSelecting;
		if (!_wasSelectedText && _dragSelFrom && _dragSelTo && _dragSelecting) {
			_wasSelectedText = true;
			setFocus();
		}
		force = true;
	}
	if (!force) return;

	update();
}

int32 HistoryInner::historyHeight() const {
	int32 result = 0;
	if (!_history || _history->isEmpty()) {
		result += _migrated ? _migrated->height : 0;
	} else {
		result += _history->height - _historySkipHeight + (_migrated ? _migrated->height : 0);
	}
	return result;
}

int32 HistoryInner::migratedTop() const {
	return (_migrated && !_migrated->isEmpty()) ? _historyOffset : -1;
}

int32 HistoryInner::historyTop() const {
	int32 mig = migratedTop();
	return (_history && !_history->isEmpty()) ? (mig >= 0 ? (mig + _migrated->height - _historySkipHeight) : _historyOffset) : -1;
}

int32 HistoryInner::historyDrawTop() const {
	int32 his = historyTop();
	return (his >= 0) ? (his + _historySkipHeight) : -1;
}

int32 HistoryInner::itemTop(const HistoryItem *item) const { // -1 if should not be visible, -2 if bad history()
	if (!item) return -2;
	if (item->detached()) return -1;

	int32 top = (item->history() == _history) ? historyTop() : (item->history() == _migrated ? migratedTop() : -2);
	return (top < 0) ? top : (top + item->y + item->block()->y);
}

void HistoryInner::notifyIsBotChanged() {
	_botInfo = (_history && _history->peer->isUser()) ? _history->peer->asUser()->botInfo : 0;
	if (_botInfo && !_botInfo->inited && App::api()) {
		App::api()->requestFullPeer(_peer);
	}
}

void HistoryInner::notifyMigrateUpdated() {
	_migrated = _peer->migrateFrom() ? App::history(_peer->migrateFrom()->id) : 0;
}

void HistoryInner::applyDragSelection() {
	applyDragSelection(&_selected);
}

void HistoryInner::addSelectionRange(SelectedItems *toItems, int32 fromblock, int32 fromitem, int32 toblock, int32 toitem, History *h) const {
	if (fromblock >= 0 && fromitem >= 0 && toblock >= 0 && toitem >= 0) {
		for (; fromblock <= toblock; ++fromblock) {
			HistoryBlock *block = h->blocks[fromblock];
			for (int32 cnt = (fromblock < toblock) ? block->items.size() : (toitem + 1); fromitem < cnt; ++fromitem) {
				HistoryItem *item = block->items[fromitem];
				SelectedItems::iterator i = toItems->find(item);
				if (item->id > 0 && !item->serviceMsg()) {
					if (i == toItems->cend()) {
						if (toItems->size() >= MaxSelectedItems) break;
						toItems->insert(item, FullSelection);
					} else if (i.value() != FullSelection) {
						*i = FullSelection;
					}
				} else {
					if (i != toItems->cend()) {
						toItems->erase(i);
					}
				}
			}
			if (toItems->size() >= MaxSelectedItems) break;
			fromitem = 0;
		}
	}
}

void HistoryInner::applyDragSelection(SelectedItems *toItems) const {
	int32 selfromy = itemTop(_dragSelFrom), seltoy = itemTop(_dragSelTo);
	if (selfromy < 0 || seltoy < 0) {
		return;
	}
	seltoy += _dragSelTo->height();

	if (!toItems->isEmpty() && toItems->cbegin().value() != FullSelection) {
		toItems->clear();
	}
	if (_dragSelecting) {
		int32 fromblock = _dragSelFrom->history()->blocks.indexOf(_dragSelFrom->block()), fromitem = _dragSelFrom->block()->items.indexOf(_dragSelFrom);
		int32 toblock = _dragSelTo->history()->blocks.indexOf(_dragSelTo->block()), toitem = _dragSelTo->block()->items.indexOf(_dragSelTo);
		if (_migrated) {
			if (_dragSelFrom->history() == _migrated) {
				if (_dragSelTo->history() == _migrated) {
					addSelectionRange(toItems, fromblock, fromitem, toblock, toitem, _migrated);
					toblock = -1;
					toitem = -1;
				} else {
					addSelectionRange(toItems, fromblock, fromitem, _migrated->blocks.size() - 1, _migrated->blocks.back()->items.size() - 1, _migrated);
				}
				fromblock = 0;
				fromitem = 0;
			} else if (_dragSelTo->history() == _migrated) { // wtf
				toblock = -1;
				toitem = -1;
			}
		}
		addSelectionRange(toItems, fromblock, fromitem, toblock, toitem, _history);
	} else {
		for (SelectedItems::iterator i = toItems->begin(); i != toItems->cend();) {
			int32 iy = itemTop(i.key());
			if (iy < 0) {
				if (iy < -1) i = toItems->erase(i);
				continue;
			}
			if (iy >= selfromy && iy < seltoy) {
				i = toItems->erase(i);
			} else {
				++i;
			}
		}
	}
}

QString HistoryInner::tooltipText() const {
	TextLinkPtr lnk = textlnkOver();
	if (lnk && !lnk->fullDisplayed()) {
		return lnk->readable();
	} else if (_dragCursorState == HistoryInDateCursorState && _dragAction == NoDrag) {
		if (App::hoveredItem()) {
			return App::hoveredItem()->date.toString(QLocale::system().dateTimeFormat(QLocale::LongFormat));
		}
	} else if (_dragCursorState == HistoryInForwardedCursorState && _dragAction == NoDrag) {
		if (App::hoveredItem()) {
			if (HistoryMessageForwarded *fwd = App::hoveredItem()->Get<HistoryMessageForwarded>()) {
				return fwd->_text.original(0, 0xFFFF, Text::ExpandLinksNone);
			}
		}
	}
	return QString();
}

QPoint HistoryInner::tooltipPos() const {
	return _dragPos;
}

void HistoryInner::onParentGeometryChanged() {
	bool needToUpdate = (_dragAction != NoDrag || _touchScroll || rect().contains(mapFromGlobal(QCursor::pos())));
	if (needToUpdate) {
		dragActionUpdate(QCursor::pos());
	}
}

MessageField::MessageField(HistoryWidget *history, const style::flatTextarea &st, const QString &ph, const QString &val) : FlatTextarea(history, st, ph, val), history(history) {
	setMinHeight(st::btnSend.height - 2 * st::sendPadding);
	setMaxHeight(st::maxFieldHeight);
}

bool MessageField::hasSendText() const {
	const QString &text(getLastText());
	for (const QChar *ch = text.constData(), *e = ch + text.size(); ch != e; ++ch) {
		ushort code = ch->unicode();
		if (code != ' ' && code != '\n' && code != '\r' && !chReplacedBySpace(code)) {
			return true;
		}
	}
	return false;
}

void MessageField::onEmojiInsert(EmojiPtr emoji) {
	if (isHidden()) return;
	insertEmoji(emoji, textCursor());
}

void MessageField::dropEvent(QDropEvent *e) {
	FlatTextarea::dropEvent(e);
	if (e->isAccepted()) {
		App::wnd()->activateWindow();
	}
}

bool MessageField::canInsertFromMimeData(const QMimeData *source) const {
	if (source->hasUrls()) {
		int32 files = 0;
		for (int32 i = 0; i < source->urls().size(); ++i) {
			if (source->urls().at(i).isLocalFile()) {
				++files;
			}
		}
		if (files > 1) return false; // multiple confirm with "compressed" checkbox
	}
	if (source->hasImage()) return true;
	return FlatTextarea::canInsertFromMimeData(source);
}

void MessageField::insertFromMimeData(const QMimeData *source) {
	if (source->hasUrls()) {
		int32 files = 0;
		QUrl url;
		for (int32 i = 0; i < source->urls().size(); ++i) {
			if (source->urls().at(i).isLocalFile()) {
				url = source->urls().at(i);
				++files;
			}
		}
		if (files == 1) {
			history->uploadFile(url.toLocalFile(), PrepareAuto, FileLoadAlwaysConfirm);
			return;
		}
		if (files > 1) return;
		//if (files > 1) return uploadFiles(files, PrepareAuto); // multiple confirm with "compressed" checkbox
	}
	if (source->hasImage()) {
		QImage img = qvariant_cast<QImage>(source->imageData());
		if (!img.isNull()) {
			history->uploadImage(img, PrepareAuto, FileLoadAlwaysConfirm, source->text());
			return;
		}
	}
	FlatTextarea::insertFromMimeData(source);
}

void MessageField::focusInEvent(QFocusEvent *e) {
	FlatTextarea::focusInEvent(e);
	emit focused();
}

ReportSpamPanel::ReportSpamPanel(HistoryWidget *parent) : TWidget(parent),
_report(this, lang(lng_report_spam), st::reportSpamHide),
_hide(this, lang(lng_report_spam_hide), st::reportSpamHide),
_clear(this, lang(lng_profile_delete_conversation)) {
	resize(parent->width(), _hide.height() + st::lineWidth);

	connect(&_report, SIGNAL(clicked()), this, SIGNAL(reportClicked()));
	connect(&_hide, SIGNAL(clicked()), this, SIGNAL(hideClicked()));
	connect(&_clear, SIGNAL(clicked()), this, SIGNAL(clearClicked()));

	_clear.hide();
}

void ReportSpamPanel::resizeEvent(QResizeEvent *e) {
	_report.resize(width() - (_hide.width() + st::reportSpamSeparator) * 2, _report.height());
	_report.moveToLeft(_hide.width() + st::reportSpamSeparator, 0);
	_hide.moveToRight(0, 0);
	_clear.move((width() - _clear.width()) / 2, height() - _clear.height() - ((height() - st::msgFont->height - _clear.height()) / 2));
}

void ReportSpamPanel::paintEvent(QPaintEvent *e) {
	Painter p(this);
	p.fillRect(QRect(0, 0, width(), height() - st::lineWidth), st::reportSpamBg->b);
	p.fillRect(Adaptive::OneColumn() ? 0 : st::lineWidth, height() - st::lineWidth, width() - (Adaptive::OneColumn() ? 0 : st::lineWidth), st::lineWidth, st::shadowColor->b);
	if (!_clear.isHidden()) {
		p.setPen(st::black->p);
		p.setFont(st::msgFont->f);
		p.drawText(QRect(_report.x(), (_clear.y() - st::msgFont->height) / 2, _report.width(), st::msgFont->height), lang(lng_report_spam_thanks), style::al_top);
	}
}

void ReportSpamPanel::setReported(bool reported, PeerData *onPeer) {
	if (reported) {
		_report.hide();
		_clear.setText(lang(onPeer->isChannel() ? (onPeer->isMegagroup() ? lng_profile_leave_group : lng_profile_leave_channel) : lng_profile_delete_conversation));
		_clear.show();
	} else {
		_report.show();
		_clear.hide();
	}
	update();
}

BotKeyboard::BotKeyboard() : TWidget()
, _height(0)
, _maxOuterHeight(0)
, _maximizeSize(false)
, _singleUse(false)
, _forceReply(false)
, _sel(-1)
, _down(-1)
, _a_selected(animation(this, &BotKeyboard::step_selected))
, _st(&st::botKbButton) {
	setGeometry(0, 0, _st->margin, _st->margin);
	_height = _st->margin;
	setMouseTracking(true);
}

void BotKeyboard::paintEvent(QPaintEvent *e) {
	Painter p(this);

	QRect r(e->rect());
	p.setClipRect(r);
	p.fillRect(r, st::white->b);

	p.setPen(st::botKbColor->p);
	p.setFont(st::botKbFont->f);
	for (int32 i = 0, l = _btns.size(); i != l; ++i) {
		int32 j = 0, s = _btns.at(i).size();
		for (; j != s; ++j) {
			const Button &btn(_btns.at(i).at(j));
			QRect rect(btn.rect);
			if (rect.y() >= r.y() + r.height()) break;
			if (rect.y() + rect.height() < r.y()) continue;

			if (rtl()) rect.moveLeft(width() - rect.left() - rect.width());

			int32 tx = rect.x(), tw = rect.width();
			if (tw > st::botKbFont->elidew + _st->padding * 2) {
				tx += _st->padding;
				tw -= _st->padding * 2;
			} else if (tw > st::botKbFont->elidew) {
				tx += (tw - st::botKbFont->elidew) / 2;
				tw = st::botKbFont->elidew;
			}
			if (_down == i * MatrixRowShift + j) {
				App::roundRect(p, rect, st::botKbDownBg, BotKeyboardDownCorners);
				btn.text.drawElided(p, tx, rect.y() + _st->downTextTop + ((rect.height() - _st->height) / 2), tw, 1, style::al_top);
			} else {
				App::roundRect(p, rect, st::botKbBg, BotKeyboardCorners);
				float64 hover = btn.hover;
				if (hover > 0) {
					p.setOpacity(hover);
					App::roundRect(p, rect, st::botKbOverBg, BotKeyboardOverCorners);
					p.setOpacity(1);
				}
				btn.text.drawElided(p, tx, rect.y() + _st->textTop + ((rect.height() - _st->height) / 2), tw, 1, style::al_top);
			}
		}
		if (j < s) break;
	}
}

void BotKeyboard::resizeEvent(QResizeEvent *e) {
	updateStyle();

	_height = (_btns.size() + 1) * _st->margin + _btns.size() * _st->height;
	if (_maximizeSize) _height = qMax(_height, _maxOuterHeight);
	if (height() != _height) {
		resize(width(), _height);
		return;
	}

	float64 y = _st->margin, btnh = _btns.isEmpty() ? _st->height : (float64(_height - _st->margin) / _btns.size());
	for (int32 i = 0, l = _btns.size(); i != l; ++i) {
		int32 j = 0, s = _btns.at(i).size();

		float64 widthForText = width() - (s * _st->margin + st::botKbScroll.width + s * 2 * _st->padding), widthOfText = 0.;
		for (; j != s; ++j) {
			Button &btn(_btns[i][j]);
			if (btn.text.isEmpty()) btn.text.setText(st::botKbFont, textOneLine(btn.cmd), _textPlainOptions);
			if (!btn.cwidth) btn.cwidth = btn.cmd.size();
			if (!btn.cwidth) btn.cwidth = 1;
			widthOfText += qMax(btn.text.maxWidth(), 1);
		}

		float64 x = _st->margin, coef = widthForText / widthOfText;
		for (j = 0; j != s; ++j) {
			Button &btn(_btns[i][j]);
			float64 tw = widthForText / float64(s), w = 2 * _st->padding + tw;
			if (w < _st->padding) w = _st->padding;

			btn.rect = QRect(qRound(x), qRound(y), qRound(w), qRound(btnh - _st->margin));
			x += w + _st->margin;

			btn.full = tw >= btn.text.maxWidth();
		}
		y += btnh;
	}
}

void BotKeyboard::mousePressEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
	_down = _sel;
	update();
}

void BotKeyboard::mouseMoveEvent(QMouseEvent *e) {
	_lastMousePos = e->globalPos();
	updateSelected();
}

void BotKeyboard::mouseReleaseEvent(QMouseEvent *e) {
	int32 down = _down;
	_down = -1;

	_lastMousePos = e->globalPos();
	updateSelected();
	if (_sel == down && down >= 0) {
		int row = (down / MatrixRowShift), col = down % MatrixRowShift;
		QString cmd(_btns.at(row).at(col).cmd);
		App::sendBotCommand(cmd, _wasForMsgId.msg);
	}
}

void BotKeyboard::leaveEvent(QEvent *e) {
	_lastMousePos = QPoint(-1, -1);
	updateSelected();
}

bool BotKeyboard::updateMarkup(HistoryItem *to) {
	if (to && to->hasReplyMarkup()) {
		if (_wasForMsgId == FullMsgId(to->channelId(), to->id)) return false;

		_wasForMsgId = FullMsgId(to->channelId(), to->id);
		clearSelection();
		_btns.clear();
		const ReplyMarkup &markup(App::replyMarkup(to->channelId(), to->id));
		_forceReply = markup.flags & MTPDreplyKeyboardMarkup_flag_FORCE_REPLY;
		_maximizeSize = !(markup.flags & MTPDreplyKeyboardMarkup::flag_resize);
		_singleUse = _forceReply || (markup.flags & MTPDreplyKeyboardMarkup::flag_single_use);

		const ReplyMarkup::Commands &commands(markup.commands);
		if (!commands.isEmpty()) {
			int32 i = 0, l = qMin(commands.size(), 512);
			_btns.reserve(l);
			for (; i != l; ++i) {
				const QList<QString> &row(commands.at(i));
				QList<Button> btns;
				int32 j = 0, s = qMin(row.size(), 16);
				btns.reserve(s);
				for (; j != s; ++j) {
					btns.push_back(Button(row.at(j)));
				}
				if (!btns.isEmpty()) _btns.push_back(btns);
			}

			updateStyle();
			_height = (_btns.size() + 1) * _st->margin + _btns.size() * _st->height;
			if (_maximizeSize) _height = qMax(_height, _maxOuterHeight);
			if (height() != _height) {
				resize(width(), _height);
			} else {
				resizeEvent(0);
			}
		}
		return true;
	}
	if (_wasForMsgId.msg) {
		_maximizeSize = _singleUse = _forceReply = false;
		_wasForMsgId = FullMsgId();
		clearSelection();
		_btns.clear();
		return true;
	}
	return false;
}

bool BotKeyboard::hasMarkup() const {
	return !_btns.isEmpty();
}

bool BotKeyboard::forceReply() const {
	return _forceReply;
}

void  BotKeyboard::step_selected(uint64 ms, bool timer) {
	for (Animations::iterator i = _animations.begin(); i != _animations.end();) {
		int index = qAbs(i.key()) - 1, row = (index / MatrixRowShift), col = index % MatrixRowShift;
		float64 dt = float64(ms - i.value()) / st::botKbDuration;
		if (dt >= 1) {
			_btns[row][col].hover = (i.key() > 0) ? 1 : 0;
			i = _animations.erase(i);
		} else {
			_btns[row][col].hover = (i.key() > 0) ? dt : (1 - dt);
			++i;
		}
	}
	if (timer) update();
	if (_animations.isEmpty()) {
		_a_selected.stop();
	}
}

void BotKeyboard::resizeToWidth(int32 width, int32 maxOuterHeight) {
	updateStyle(width);
	_height = (_btns.size() + 1) * _st->margin + _btns.size() * _st->height;
	_maxOuterHeight = maxOuterHeight;

	if (_maximizeSize) _height = qMax(_height, _maxOuterHeight);
	resize(width, _height);
}

bool BotKeyboard::maximizeSize() const {
	return _maximizeSize;
}

bool BotKeyboard::singleUse() const {
	return _singleUse;
}

void BotKeyboard::updateStyle(int32 w) {
	if (w < 0) w = width();
	_st = &st::botKbButton;
	for (int32 i = 0, l = _btns.size(); i != l; ++i) {
		int32 j = 0, s = _btns.at(i).size();
		int32 widthLeft = w - (s * _st->margin + st::botKbScroll.width + s * 2 * _st->padding);
		for (; j != s; ++j) {
			Button &btn(_btns[i][j]);
			if (btn.text.isEmpty()) btn.text.setText(st::botKbFont, textOneLine(btn.cmd), _textPlainOptions);
			widthLeft -= qMax(btn.text.maxWidth(), 1);
			if (widthLeft < 0) break;
		}
		if (j != s && s > 3) {
			_st = &st::botKbTinyButton;
			break;
		}
	}
}

void BotKeyboard::clearSelection() {
	for (Animations::const_iterator i = _animations.cbegin(), e = _animations.cend(); i != e; ++i) {
		int index = qAbs(i.key()) - 1, row = (index / MatrixRowShift), col = index % MatrixRowShift;
		_btns[row][col].hover = 0;
	}
	_animations.clear();
	_a_selected.stop();
	if (_sel >= 0) {
		int row = (_sel / MatrixRowShift), col = _sel % MatrixRowShift;
		_btns[row][col].hover = 0;
		_sel = -1;
	}
}

QPoint BotKeyboard::tooltipPos() const {
	return _lastMousePos;
}

QString BotKeyboard::tooltipText() const {
	if (_sel >= 0) {
		int row = (_sel / MatrixRowShift), col = _sel % MatrixRowShift;
		if (!_btns.at(row).at(col).full) {
			return _btns.at(row).at(col).cmd;
		}
	}
	return QString();
}

void BotKeyboard::updateSelected() {
	PopupTooltip::Show(1000, this);

	if (_down >= 0) return;

	QPoint p(mapFromGlobal(_lastMousePos));
	int32 newSel = -1;
	for (int32 i = 0, l = _btns.size(); i != l; ++i) {
		for (int32 j = 0, s = _btns.at(i).size(); j != s; ++j) {
			QRect r(_btns.at(i).at(j).rect);

			if (rtl()) r.moveLeft(width() - r.left() - r.width());

			if (r.contains(p)) {
				newSel = i * MatrixRowShift + j;
				break;
			}
		}
		if (newSel >= 0) break;
	}
	if (newSel != _sel) {
		PopupTooltip::Hide();
		if (newSel < 0) {
			setCursor(style::cur_default);
		} else if (_sel < 0) {
			setCursor(style::cur_pointer);
		}
		bool startanim = false;
		if (_sel >= 0) {
			_animations.remove(_sel + 1);
			if (_animations.find(-_sel - 1) == _animations.end()) {
				if (_animations.isEmpty()) startanim = true;
				_animations.insert(-_sel - 1, getms());
			}
		}
		_sel = newSel;
		if (_sel >= 0) {
			_animations.remove(-_sel - 1);
			if (_animations.find(_sel + 1) == _animations.end()) {
				if (_animations.isEmpty()) startanim = true;
				_animations.insert(_sel + 1, getms());
			}
		}
		if (startanim && !_a_selected.animating()) _a_selected.start();
	}
}

HistoryHider::HistoryHider(MainWidget *parent, bool forwardSelected) : TWidget(parent)
, _sharedContact(0)
, _forwardSelected(forwardSelected)
, _sendPath(false)
, _send(this, lang(lng_forward_send), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, offered(0)
, a_opacity(0, 1)
, _a_appearance(animation(this, &HistoryHider::step_appearance))
, hiding(false)
, _forwardRequest(0)
, toTextWidth(0)
, shadow(st::boxShadow) {
	init();
}

HistoryHider::HistoryHider(MainWidget *parent, UserData *sharedContact) : TWidget(parent)
, _sharedContact(sharedContact)
, _forwardSelected(false)
, _sendPath(false)
, _send(this, lang(lng_forward_send), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, offered(0)
, a_opacity(0, 1)
, _a_appearance(animation(this, &HistoryHider::step_appearance))
, hiding(false)
, _forwardRequest(0)
, toTextWidth(0)
, shadow(st::boxShadow) {
	init();
}

HistoryHider::HistoryHider(MainWidget *parent) : TWidget(parent)
, _sharedContact(0)
, _forwardSelected(false)
, _sendPath(true)
, _send(this, lang(lng_forward_send), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, offered(0)
, a_opacity(0, 1)
, _a_appearance(animation(this, &HistoryHider::step_appearance))
, hiding(false)
, _forwardRequest(0)
, toTextWidth(0)
, shadow(st::boxShadow) {
	init();
}

HistoryHider::HistoryHider(MainWidget *parent, const QString &url, const QString &text) : TWidget(parent)
, _sharedContact(0)
, _forwardSelected(false)
, _sendPath(false)
, _shareUrl(url)
, _shareText(text)
, _send(this, lang(lng_forward_send), st::defaultBoxButton)
, _cancel(this, lang(lng_cancel), st::cancelBoxButton)
, offered(0)
, a_opacity(0, 1)
, _a_appearance(animation(this, &HistoryHider::step_appearance))
, hiding(false)
, _forwardRequest(0)
, toTextWidth(0)
, shadow(st::boxShadow) {
	init();
}

void HistoryHider::init() {
	connect(&_send, SIGNAL(clicked()), this, SLOT(forward()));
	connect(&_cancel, SIGNAL(clicked()), this, SLOT(startHide()));
	connect(App::wnd()->getTitle(), SIGNAL(hiderClicked()), this, SLOT(startHide()));

	_chooseWidth = st::forwardFont->width(lang(lng_forward_choose));

	resizeEvent(0);
	_a_appearance.start();
}

void HistoryHider::step_appearance(float64 ms, bool timer) {
	float64 dt = ms / 200;
	if (dt >= 1) {
		_a_appearance.stop();
		a_opacity.finish();
		if (hiding)	{
			QTimer::singleShot(0, this, SLOT(deleteLater()));
		}
	} else {
		a_opacity.update(dt, anim::linear);
	}
	App::wnd()->getTitle()->setHideLevel(a_opacity.current());
	if (timer) update();
}

bool HistoryHider::withConfirm() const {
	return _sharedContact || _sendPath;
}

void HistoryHider::paintEvent(QPaintEvent *e) {
	Painter p(this);
	if (!hiding || !cacheForAnim.isNull() || !offered) {
		p.setOpacity(a_opacity.current() * st::layerAlpha);
		p.fillRect(rect(), st::layerBg->b);
		p.setOpacity(a_opacity.current());
	}
	if (cacheForAnim.isNull() || !offered) {
		p.setFont(st::forwardFont->f);
		if (offered) {
			shadow.paint(p, box, st::boxShadowShift);

			// fill bg
			p.fillRect(box, st::boxBg->b);

			p.setPen(st::black->p);
			toText.drawElided(p, box.left() + st::boxPadding.left(), box.top() + st::boxPadding.top(), toTextWidth + 2);
		} else {
			int32 w = st::forwardMargins.left() + _chooseWidth + st::forwardMargins.right(), h = st::forwardMargins.top() + st::forwardFont->height + st::forwardMargins.bottom();
			App::roundRect(p, (width() - w) / 2, (height() - h) / 2, w, h, st::forwardBg, ForwardCorners);

			p.setPen(st::white->p);
			p.drawText(box, lang(lng_forward_choose), QTextOption(style::al_center));
		}
	} else {
		p.drawPixmap(box.left(), box.top(), cacheForAnim);
	}
}

void HistoryHider::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		if (offered) {
			offered = 0;
			resizeEvent(0);
			update();
			App::main()->dialogsActivate();
		} else {
			startHide();
		}
	} else if (e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) {
		if (offered) {
			forward();
		}
	}
}

void HistoryHider::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		if (!box.contains(e->pos())) {
			startHide();
		}
	}
}

void HistoryHider::startHide() {
	if (hiding) return;
	hiding = true;
	if (Adaptive::OneColumn()) {
		QTimer::singleShot(0, this, SLOT(deleteLater()));
	} else {
		if (offered) cacheForAnim = myGrab(this, box);
		if (_forwardRequest) MTP::cancel(_forwardRequest);
		a_opacity.start(0);
		_send.hide();
		_cancel.hide();
		_a_appearance.start();
	}
}

void HistoryHider::forward() {
	if (!hiding && offered) {
		if (_sharedContact) {
			parent()->onShareContact(offered->id, _sharedContact);
		} else if (_sendPath) {
			parent()->onSendPaths(offered->id);
		} else if (!_shareUrl.isEmpty()) {
			parent()->onShareUrl(offered->id, _shareUrl, _shareText);
		} else {
			parent()->onForward(offered->id, _forwardSelected ? ForwardSelectedMessages : ForwardContextMessage);
		}
	}
	emit forwarded();
}

void HistoryHider::forwardDone() {
	_forwardRequest = 0;
	startHide();
}

MainWidget *HistoryHider::parent() {
	return static_cast<MainWidget*>(parentWidget());
}

void HistoryHider::resizeEvent(QResizeEvent *e) {
	int32 w = st::boxWidth, h = st::boxPadding.top() + st::boxPadding.bottom();
	if (offered) {
		if (!hiding) {
			_send.show();
			_cancel.show();
		}
		h += st::boxTextFont->height + st::boxButtonPadding.top() + _send.height() + st::boxButtonPadding.bottom();
	} else {
		h += st::forwardFont->height;
		_send.hide();
		_cancel.hide();
	}
	box = QRect((width() - w) / 2, (height() - h) / 2, w, h);
	_send.moveToRight(width() - (box.x() + box.width()) + st::boxButtonPadding.right(), box.y() + h - st::boxButtonPadding.bottom() - _send.height());
	_cancel.moveToRight(width() - (box.x() + box.width()) + st::boxButtonPadding.right() + _send.width() + st::boxButtonPadding.left(), _send.y());
}

bool HistoryHider::offerPeer(PeerId peer) {
	if (!peer) {
		offered = 0;
		toText.setText(st::boxTextFont, QString());
		toTextWidth = 0;
		resizeEvent(0);
		return false;
	}
	offered = App::peer(peer);
	LangString phrase;
	QString recipient = offered->isUser() ? offered->name : '\xAB' + offered->name + '\xBB';
	if (_sharedContact) {
		phrase = lng_forward_share_contact(lt_recipient, recipient);
	} else if (_sendPath) {
		if (cSendPaths().size() > 1) {
			phrase = lng_forward_send_files_confirm(lt_recipient, recipient);
		} else {
			QString name(QFileInfo(cSendPaths().front()).fileName());
			if (name.size() > 10) {
				name = name.mid(0, 8) + '.' + '.';
			}
			phrase = lng_forward_send_file_confirm(lt_name, name, lt_recipient, recipient);
		}
	} else if (!_shareUrl.isEmpty()) {
		PeerId to = offered->id;
		offered = 0;
		if (parent()->onShareUrl(to, _shareUrl, _shareText)) {
			startHide();
		}
		return false;
	} else {
		PeerId to = offered->id;
		offered = 0;
		if (parent()->onForward(to, _forwardSelected ? ForwardSelectedMessages : ForwardContextMessage)) {
			startHide();
		}
		return false;
	}

	toText.setText(st::boxTextFont, phrase, _textNameOptions);
	toTextWidth = toText.maxWidth();
	if (toTextWidth > box.width() - st::boxPadding.left() - st::boxButtonPadding.right()) {
		toTextWidth = box.width() - st::boxPadding.left() - st::boxButtonPadding.right();
	}

	resizeEvent(0);
	update();
	setFocus();

	return true;
}

QString HistoryHider::offeredText() const {
	return toText.original();
}

bool HistoryHider::wasOffered() const {
	return !!offered;
}

HistoryHider::~HistoryHider() {
	if (_sendPath) cSetSendPaths(QStringList());
	if (App::wnd()) App::wnd()->getTitle()->setHideLevel(0);
	parent()->noHider(this);
}

CollapseButton::CollapseButton(QWidget *parent) : FlatButton(parent, lang(lng_channel_hide_comments), st::collapseButton) {
}

void CollapseButton::paintEvent(QPaintEvent *e) {
	Painter p(this);
	App::roundRect(p, rect(), App::msgServiceBg(), ServiceCorners);
	FlatButton::paintEvent(e);
}

SilentToggle::SilentToggle(QWidget *parent) : FlatCheckbox(parent, QString(), false, st::silentToggle) {
	setMouseTracking(true);
}

void SilentToggle::mouseMoveEvent(QMouseEvent *e) {
	FlatCheckbox::mouseMoveEvent(e);
	if (rect().contains(e->pos())) {
		PopupTooltip::Show(1000, this);
	} else {
		PopupTooltip::Hide();
	}
}

void SilentToggle::leaveEvent(QEvent *e) {
	PopupTooltip::Hide();
}

void SilentToggle::mouseReleaseEvent(QMouseEvent *e) {
	FlatCheckbox::mouseReleaseEvent(e);
	PopupTooltip::Show(0, this);
	PeerData *p = App::main() ? App::main()->peer() : nullptr;
	if (p && p->isChannel() && p->notify != UnknownNotifySettings) {
		App::main()->updateNotifySetting(p, NotifySettingDontChange, checked() ? SilentNotifiesSetSilent : SilentNotifiesSetNotify);
	}
}

QString SilentToggle::tooltipText() const {
	return lang(checked() ? lng_wont_be_notified : lng_will_be_notified);
}

QPoint SilentToggle::tooltipPos() const {
	return QCursor::pos();
}

HistoryWidget::HistoryWidget(QWidget *parent) : TWidget(parent)
, _replyToId(0)
, _replyToNameVersion(0)
, _editMsgId(0)
, _replyEditMsg(0)
, _fieldBarCancel(this, st::replyCancel)
, _pinnedBar(0)
, _saveEditMsgRequestId(0)
, _reportSpamStatus(dbiprsUnknown)
, _reportSpamSettingRequestId(ReportSpamRequestNeeded)
, _previewData(0)
, _previewRequest(0)
, _previewCancelled(false)
, _replyForwardPressed(false)
, _replyReturn(0)
, _stickersUpdateRequest(0)
, _savedGifsUpdateRequest(0)
, _peer(0)
, _clearPeer(0)
, _channel(NoChannel)
, _showAtMsgId(0)
, _fixedInScrollMsgId(0)
, _fixedInScrollMsgTop(0)
, _firstLoadRequest(0), _preloadRequest(0), _preloadDownRequest(0)
, _delayedShowAtMsgId(-1)
, _delayedShowAtRequest(0)
, _activeAnimMsgId(0)
, _scroll(this, st::historyScroll, false)
, _list(0)
, _migrated(0)
, _history(0)
, _histInited(false)
, _lastScroll(0)
, _lastScrolled(0)
, _toHistoryEnd(this, st::historyToEnd)
, _collapseComments(this)
, _attachMention(this)
, _inlineBot(0)
, _inlineBotResolveRequestId(0)
, _reportSpamPanel(this)
, _send(this, lang(lng_send_button), st::btnSend)
, _unblock(this, lang(lng_unblock_button), st::btnUnblock)
, _botStart(this, lang(lng_bot_start), st::btnSend)
, _joinChannel(this, lang(lng_channel_join), st::btnSend)
, _muteUnmute(this, lang(lng_channel_mute), st::btnSend)
, _unblockRequest(0)
, _reportSpamRequest(0)
, _attachDocument(this, st::btnAttachDocument)
, _attachPhoto(this, st::btnAttachPhoto)
, _attachEmoji(this, st::btnAttachEmoji)
, _kbShow(this, st::btnBotKbShow)
, _kbHide(this, st::btnBotKbHide)
, _cmdStart(this, st::btnBotCmdStart)
, _broadcast(this, QString(), true, st::broadcastToggle)
, _silent(this)
, _cmdStartShown(false)
, _field(this, st::taMsgField, lang(lng_message_ph))
, _a_record(animation(this, &HistoryWidget::step_record))
, _a_recording(animation(this, &HistoryWidget::step_recording))
, _recording(false)
, _inRecord(false)
, _inField(false)
, _inReplyEdit(false)
, _inPinnedMsg(false)
, a_recordingLevel(0, 0)
, _recordingSamples(0)
, a_recordOver(0, 0)
, a_recordDown(0, 0)
, a_recordCancel(st::recordCancel->c, st::recordCancel->c)
, _recordCancelWidth(st::recordFont->width(lang(lng_record_cancel)))
, _kbShown(false)
, _kbReplyTo(0)
, _kbScroll(this, st::botKbScroll)
, _keyboard()
, _attachType(this)
, _emojiPan(this)
, _attachDrag(DragStateNone)
, _attachDragDocument(this)
, _attachDragPhoto(this)
, _fileLoader(this, FileLoaderQueueStopTimeout)
, _textUpdateEventsFlags(TextUpdateEventsSaveDraft | TextUpdateEventsSendTyping)
, _serviceImageCacheSize(0)
, _confirmWithTextId(0)
, _titlePeerTextWidth(0)
, _a_show(animation(this, &HistoryWidget::step_show))
, _scrollDelta(0)
, _saveDraftStart(0)
, _saveDraftText(false)
, _sideShadow(this, st::shadowColor)
, _topShadow(this, st::shadowColor)
, _inGrab(false) {
	_scroll.setFocusPolicy(Qt::NoFocus);

	setAcceptDrops(true);

	connect(App::wnd(), SIGNAL(imageLoaded()), this, SLOT(updateField()));
	connect(&_scroll, SIGNAL(scrolled()), this, SLOT(onListScroll()));
	connect(&_reportSpamPanel, SIGNAL(reportClicked()), this, SLOT(onReportSpamClicked()));
	connect(&_reportSpamPanel, SIGNAL(hideClicked()), this, SLOT(onReportSpamHide()));
	connect(&_reportSpamPanel, SIGNAL(clearClicked()), this, SLOT(onReportSpamClear()));
	connect(&_toHistoryEnd, SIGNAL(clicked()), this, SLOT(onHistoryToEnd()));
	connect(&_collapseComments, SIGNAL(clicked()), this, SLOT(onCollapseComments()));
	connect(&_fieldBarCancel, SIGNAL(clicked()), this, SLOT(onFieldBarCancel()));
	connect(&_send, SIGNAL(clicked()), this, SLOT(onSend()));
	connect(&_unblock, SIGNAL(clicked()), this, SLOT(onUnblock()));
	connect(&_botStart, SIGNAL(clicked()), this, SLOT(onBotStart()));
	connect(&_joinChannel, SIGNAL(clicked()), this, SLOT(onJoinChannel()));
	connect(&_muteUnmute, SIGNAL(clicked()), this, SLOT(onMuteUnmute()));
	connect(&_broadcast, SIGNAL(changed()), this, SLOT(onBroadcastSilentChange()));
	connect(&_silent, SIGNAL(clicked()), this, SLOT(onBroadcastSilentChange()));
	connect(&_attachDocument, SIGNAL(clicked()), this, SLOT(onDocumentSelect()));
	connect(&_attachPhoto, SIGNAL(clicked()), this, SLOT(onPhotoSelect()));
	connect(&_field, SIGNAL(submitted(bool)), this, SLOT(onSend(bool)));
	connect(&_field, SIGNAL(cancelled()), this, SLOT(onCancel()));
	connect(&_field, SIGNAL(tabbed()), this, SLOT(onFieldTabbed()));
	connect(&_field, SIGNAL(resized()), this, SLOT(onFieldResize()));
	connect(&_field, SIGNAL(focused()), this, SLOT(onFieldFocused()));
	connect(&_field, SIGNAL(changed()), this, SLOT(onTextChange()));
	connect(&_field, SIGNAL(spacedReturnedPasted()), this, SLOT(onPreviewParse()));
	connect(&_field, SIGNAL(linksChanged()), this, SLOT(onPreviewCheck()));
	connect(App::wnd()->windowHandle(), SIGNAL(visibleChanged(bool)), this, SLOT(onVisibleChanged()));
	connect(&_scrollTimer, SIGNAL(timeout()), this, SLOT(onScrollTimer()));
	connect(&_emojiPan, SIGNAL(emojiSelected(EmojiPtr)), &_field, SLOT(onEmojiInsert(EmojiPtr)));
	connect(&_emojiPan, SIGNAL(stickerSelected(DocumentData*)), this, SLOT(onStickerSend(DocumentData*)));
	connect(&_emojiPan, SIGNAL(photoSelected(PhotoData*)), this, SLOT(onPhotoSend(PhotoData*)));
	connect(&_emojiPan, SIGNAL(inlineResultSelected(InlineResult*,UserData*)), this, SLOT(onInlineResultSend(InlineResult*,UserData*)));
	connect(&_emojiPan, SIGNAL(updateStickers()), this, SLOT(updateStickers()));
	connect(&_sendActionStopTimer, SIGNAL(timeout()), this, SLOT(onCancelSendAction()));
	connect(&_previewTimer, SIGNAL(timeout()), this, SLOT(onPreviewTimeout()));
	if (audioCapture()) {
		connect(audioCapture(), SIGNAL(onError()), this, SLOT(onRecordError()));
		connect(audioCapture(), SIGNAL(onUpdate(quint16,qint32)), this, SLOT(onRecordUpdate(quint16,qint32)));
		connect(audioCapture(), SIGNAL(onDone(QByteArray,VoiceWaveform,qint32)), this, SLOT(onRecordDone(QByteArray,VoiceWaveform,qint32)));
	}

	_updateHistoryItems.setSingleShot(true);
	connect(&_updateHistoryItems, SIGNAL(timeout()), this, SLOT(onUpdateHistoryItems()));

	_scrollTimer.setSingleShot(false);

	_sendActionStopTimer.setSingleShot(true);

	_animActiveTimer.setSingleShot(false);
	connect(&_animActiveTimer, SIGNAL(timeout()), this, SLOT(onAnimActiveStep()));

	_saveDraftTimer.setSingleShot(true);
	connect(&_saveDraftTimer, SIGNAL(timeout()), this, SLOT(onDraftSave()));
	connect(_field.verticalScrollBar(), SIGNAL(valueChanged(int)), this, SLOT(onDraftSaveDelayed()));
	connect(&_field, SIGNAL(cursorPositionChanged()), this, SLOT(onDraftSaveDelayed()));
	connect(&_field, SIGNAL(cursorPositionChanged()), this, SLOT(onCheckMentionDropdown()), Qt::QueuedConnection);

	_fieldBarCancel.hide();

	_scroll.hide();
	_collapseComments.setParent(&_scroll);

	_kbScroll.setFocusPolicy(Qt::NoFocus);
	_kbScroll.viewport()->setFocusPolicy(Qt::NoFocus);
	_kbScroll.setWidget(&_keyboard);
	_kbScroll.hide();

	connect(&_kbScroll, SIGNAL(scrolled()), &_keyboard, SLOT(updateSelected()));

	updateScrollColors();

	_toHistoryEnd.hide();
	_toHistoryEnd.installEventFilter(this);

	_collapseComments.hide();
	_collapseComments.installEventFilter(this);

	_attachMention.hide();
	connect(&_attachMention, SIGNAL(chosen(QString)), this, SLOT(onMentionHashtagOrBotCommandInsert(QString)));
	connect(&_attachMention, SIGNAL(stickerSelected(DocumentData*)), this, SLOT(onStickerSend(DocumentData*)));
	_field.installEventFilter(&_attachMention);
	_field.setCtrlEnterSubmit(cCtrlEnter());

	_field.hide();
	_send.hide();
	_unblock.hide();
	_botStart.hide();
	_joinChannel.hide();
	_muteUnmute.hide();

	_reportSpamPanel.move(0, 0);
	_reportSpamPanel.hide();

	_attachDocument.hide();
	_attachPhoto.hide();
	_attachEmoji.hide();
	_kbShow.hide();
	_kbHide.hide();
	_broadcast.hide();
	_silent.hide();
	_cmdStart.hide();

	_attachDocument.installEventFilter(&_attachType);
	_attachPhoto.installEventFilter(&_attachType);
	_attachEmoji.installEventFilter(&_emojiPan);

	connect(&_kbShow, SIGNAL(clicked()), this, SLOT(onKbToggle()));
	connect(&_kbHide, SIGNAL(clicked()), this, SLOT(onKbToggle()));
	connect(&_cmdStart, SIGNAL(clicked()), this, SLOT(onCmdStart()));

	connect(_attachType.addButton(new IconedButton(this, st::dropdownAttachDocument, lang(lng_attach_file))), SIGNAL(clicked()), this, SLOT(onDocumentSelect()));
	connect(_attachType.addButton(new IconedButton(this, st::dropdownAttachPhoto, lang(lng_attach_photo))), SIGNAL(clicked()), this, SLOT(onPhotoSelect()));
	_attachType.hide();
	_emojiPan.hide();
	_attachDragDocument.hide();
	_attachDragPhoto.hide();

	_topShadow.hide();
	_sideShadow.setVisible(!Adaptive::OneColumn());

	connect(&_attachDragDocument, SIGNAL(dropped(const QMimeData*)), this, SLOT(onDocumentDrop(const QMimeData*)));
	connect(&_attachDragPhoto, SIGNAL(dropped(const QMimeData*)), this, SLOT(onPhotoDrop(const QMimeData*)));
}

void HistoryWidget::start() {
	connect(App::main(), SIGNAL(stickersUpdated()), this, SLOT(onStickersUpdated()));
	connect(App::main(), SIGNAL(savedGifsUpdated()), &_emojiPan, SLOT(refreshSavedGifs()));

	updateRecentStickers();
	if (App::main()) emit App::main()->savedGifsUpdated();

	connect(App::api(), SIGNAL(fullPeerUpdated(PeerData*)), this, SLOT(onFullPeerUpdated(PeerData*)));
}

void HistoryWidget::onStickersUpdated() {
	_emojiPan.refreshStickers();
	updateStickersByEmoji();
}

void HistoryWidget::onMentionHashtagOrBotCommandInsert(QString str) {
	if (str.at(0) == '/') { // bot command
		App::sendBotCommand(str);
		setFieldText(_field.getLastText().mid(_field.textCursor().position()));
	} else {
		_field.onMentionHashtagOrBotCommandInsert(str);
	}
}

void HistoryWidget::updateInlineBotQuery() {
	UserData *bot = _inlineBot;
	bool start = false;
	QString inlineBotUsername(_inlineBotUsername);
	QString query = _field.getInlineBotQuery(_inlineBot, _inlineBotUsername);
	if (inlineBotUsername != _inlineBotUsername) {
		if (_inlineBotResolveRequestId) {
//			Notify::inlineBotRequesting(false);
			MTP::cancel(_inlineBotResolveRequestId);
			_inlineBotResolveRequestId = 0;
		}
		if (_inlineBot == InlineBotLookingUpData) {
//			Notify::inlineBotRequesting(true);
			_inlineBotResolveRequestId = MTP::send(MTPcontacts_ResolveUsername(MTP_string(_inlineBotUsername)), rpcDone(&HistoryWidget::inlineBotResolveDone), rpcFail(&HistoryWidget::inlineBotResolveFail, _inlineBotUsername));
			return;
		}
	} else if (_inlineBot == InlineBotLookingUpData) {
		return;
	}

	if (_inlineBot) {
		if (_inlineBot != bot) {
			updateFieldPlaceholder();
		}
		if (_inlineBot->username == cInlineGifBotUsername() && query.isEmpty()) {
			_emojiPan.clearInlineBot();
		} else {
			_emojiPan.queryInlineBot(_inlineBot, query);
		}
		if (!_attachMention.isHidden()) {
			_attachMention.hideStart();
		}
	} else {
		if (_inlineBot != bot) {
			updateFieldPlaceholder();
			_field.finishPlaceholder();
		}
		_emojiPan.clearInlineBot();
		onCheckMentionDropdown();
	}
}

void HistoryWidget::updateStickersByEmoji() {
	int32 len = 0;
	if (EmojiPtr emoji = emojiFromText(_field.getLastText(), &len)) {
		if (_field.getLastText().size() <= len) {
			_attachMention.showStickers(emoji);
		} else {
			len = 0;
		}
	}
	if (!len) {
		_attachMention.showStickers(EmojiPtr(0));
	}
}

void HistoryWidget::onTextChange() {
	updateInlineBotQuery();
	updateStickersByEmoji();

	if (_peer && (!_peer->isChannel() || _peer->isMegagroup() || !_peer->asChannel()->canPublish() || (!_peer->asChannel()->isBroadcast() && !_broadcast.checked()))) {
		if (!_inlineBot && !_editMsgId && (_textUpdateEventsFlags & TextUpdateEventsSendTyping)) {
			updateSendAction(_history, SendActionTyping);
		}
	}

	if (cHasAudioCapture()) {
		if (!_field.hasSendText() && !readyToForward() && !_editMsgId) {
			_previewCancelled = false;
			_send.hide();
			updateMouseTracking();
			mouseMoveEvent(0);
		} else if (!_field.isHidden() && _send.isHidden()) {
			_send.show();
			updateMouseTracking();
			_a_record.stop();
			_inRecord = _inField = false;
			a_recordOver = a_recordDown = anim::fvalue(0, 0);
			a_recordCancel = anim::cvalue(st::recordCancel->c, st::recordCancel->c);
		}
	}
	if (updateCmdStartShown()) {
		updateControlsVisibility();
		resizeEvent(0);
		update();
	}

	if (!_peer || !(_textUpdateEventsFlags & TextUpdateEventsSaveDraft)) return;
	_saveDraftText = true;
	onDraftSave(true);
}

void HistoryWidget::onDraftSaveDelayed() {
	if (!_peer || !(_textUpdateEventsFlags & TextUpdateEventsSaveDraft)) return;
	if (!_field.textCursor().anchor() && !_field.textCursor().position() && !_field.verticalScrollBar()->value()) {
		if (!Local::hasDraftCursors(_peer->id)) {
			return;
		}
	}
	onDraftSave(true);
}

void HistoryWidget::onDraftSave(bool delayed) {
	if (!_peer) return;
	if (delayed) {
		uint64 ms = getms();
		if (!_saveDraftStart) {
			_saveDraftStart = ms;
			return _saveDraftTimer.start(SaveDraftTimeout);
		} else if (ms - _saveDraftStart < SaveDraftAnywayTimeout) {
			return _saveDraftTimer.start(SaveDraftTimeout);
		}
	}
	writeDrafts(nullptr, nullptr);
}

void HistoryWidget::writeDrafts(HistoryDraft **msgDraft, HistoryEditDraft **editDraft) {
	if (!msgDraft && _editMsgId) msgDraft = &_history->msgDraft;

	bool save = _peer && (_saveDraftStart > 0);
	_saveDraftStart = 0;
	_saveDraftTimer.stop();
	if (_saveDraftText) {
		if (save) {
			Local::MessageDraft localMsgDraft, localEditDraft;
			if (msgDraft) {
				if (*msgDraft) {
					localMsgDraft = Local::MessageDraft((*msgDraft)->msgId, (*msgDraft)->text, (*msgDraft)->previewCancelled);
				}
			} else {
				localMsgDraft = Local::MessageDraft(_replyToId, _field.getLastText(), _previewCancelled);
			}
			if (editDraft) {
				if (*editDraft) {
					localEditDraft = Local::MessageDraft((*editDraft)->msgId, (*editDraft)->text, (*editDraft)->previewCancelled);
				}
			} else if (_editMsgId) {
				localEditDraft = Local::MessageDraft(_editMsgId, _field.getLastText(), _previewCancelled);
			}
			Local::writeDrafts(_peer->id, localMsgDraft, localEditDraft);
			if (_migrated) {
				Local::writeDrafts(_migrated->peer->id, Local::MessageDraft(), Local::MessageDraft());
			}
		}
		_saveDraftText = false;
	}
	if (save) {
		MessageCursor msgCursor, editCursor;
		if (msgDraft) {
			if (*msgDraft) {
				msgCursor = (*msgDraft)->cursor;
			}
		} else {
			msgCursor = MessageCursor(_field);
		}
		if (editDraft) {
			if (*editDraft) {
				editCursor = (*editDraft)->cursor;
			}
		} else if (_editMsgId) {
			editCursor = MessageCursor(_field);
		}
		Local::writeDraftCursors(_peer->id, msgCursor, editCursor);
		if (_migrated) {
			Local::writeDraftCursors(_migrated->peer->id, MessageCursor(), MessageCursor());
		}
	}
}

void HistoryWidget::writeDrafts(History *history) {
	Local::MessageDraft localMsgDraft, localEditDraft;
	MessageCursor msgCursor, editCursor;
	if (history->msgDraft) {
		localMsgDraft = Local::MessageDraft(history->msgDraft->msgId, history->msgDraft->text, history->msgDraft->previewCancelled);
		msgCursor = history->msgDraft->cursor;
	}
	if (history->editDraft) {
		localEditDraft = Local::MessageDraft(history->editDraft->msgId, history->editDraft->text, history->editDraft->previewCancelled);
		editCursor = history->editDraft->cursor;
	}
	Local::writeDrafts(history->peer->id, localMsgDraft, localEditDraft);
	Local::writeDraftCursors(history->peer->id, msgCursor, editCursor);
}

void HistoryWidget::cancelSendAction(History *history, SendActionType type) {
	QMap<QPair<History*, SendActionType>, mtpRequestId>::iterator i = _sendActionRequests.find(qMakePair(history, type));
	if (i != _sendActionRequests.cend()) {
		MTP::cancel(i.value());
		_sendActionRequests.erase(i);
	}
}

void HistoryWidget::onCancelSendAction() {
	cancelSendAction(_history, SendActionTyping);
}

void HistoryWidget::updateSendAction(History *history, SendActionType type, int32 progress) {
	if (!history) return;

	bool doing = (progress >= 0);

	uint64 ms = getms(true) + 10000;
	QMap<SendActionType, uint64>::iterator i = history->mySendActions.find(type);
	if (doing && i != history->mySendActions.cend() && i.value() + 5000 > ms) return;
	if (!doing && (i == history->mySendActions.cend() || i.value() + 5000 <= ms)) return;

	if (doing) {
		if (i == history->mySendActions.cend()) {
			history->mySendActions.insert(type, ms);
		} else {
			i.value() = ms;
		}
	} else if (i != history->mySendActions.cend()) {
		history->mySendActions.erase(i);
	}

	cancelSendAction(history, type);
	if (doing) {
		MTPsendMessageAction action;
		switch (type) {
		case SendActionTyping: action = MTP_sendMessageTypingAction(); break;
		case SendActionRecordVideo: action = MTP_sendMessageRecordVideoAction(); break;
		case SendActionUploadVideo: action = MTP_sendMessageUploadVideoAction(MTP_int(progress)); break;
		case SendActionRecordVoice: action = MTP_sendMessageRecordAudioAction(); break;
		case SendActionUploadVoice: action = MTP_sendMessageUploadAudioAction(MTP_int(progress)); break;
		case SendActionUploadPhoto: action = MTP_sendMessageUploadPhotoAction(MTP_int(progress)); break;
		case SendActionUploadFile: action = MTP_sendMessageUploadDocumentAction(MTP_int(progress)); break;
		case SendActionChooseLocation: action = MTP_sendMessageGeoLocationAction(); break;
		case SendActionChooseContact: action = MTP_sendMessageChooseContactAction(); break;
		}
		_sendActionRequests.insert(qMakePair(history, type), MTP::send(MTPmessages_SetTyping(history->peer->input, action), rpcDone(&HistoryWidget::sendActionDone)));
		if (type == SendActionTyping) _sendActionStopTimer.start(5000);
	}
}

void HistoryWidget::updateRecentStickers() {
	_emojiPan.refreshStickers();
}

void HistoryWidget::stickersInstalled(uint64 setId) {
	_emojiPan.stickersInstalled(setId);
}

void HistoryWidget::sendActionDone(const MTPBool &result, mtpRequestId req) {
	for (QMap<QPair<History*, SendActionType>, mtpRequestId>::iterator i = _sendActionRequests.begin(), e = _sendActionRequests.end(); i != e; ++i) {
		if (i.value() == req) {
			_sendActionRequests.erase(i);
			break;
		}
	}
}

void HistoryWidget::activate() {
	if (_history) updateListSize(true);
	if (App::wnd()) App::wnd()->setInnerFocus();
}

void HistoryWidget::setInnerFocus() {
	if (_scroll.isHidden()) {
		setFocus();
	} else if (_list) {
		if (_selCount || (_list && _list->wasSelectedText()) || _recording || isBotStart() || isBlocked() || !_canSendMessages) {
			_list->setFocus();
		} else {
			_field.setFocus();
		}
	}
}

void HistoryWidget::onRecordError() {
	stopRecording(false);
}

void HistoryWidget::onRecordDone(QByteArray result, VoiceWaveform waveform, qint32 samples) {
	if (!_peer) return;

	App::wnd()->activateWindow();
	int32 duration = samples / AudioVoiceMsgFrequency;
	_fileLoader.addTask(new FileLoadTask(result, duration, waveform, FileLoadTo(_peer->id, _broadcast.checked(), _silent.checked(), replyToId())));
	cancelReply(lastForceReplyReplied());
}

void HistoryWidget::onRecordUpdate(quint16 level, qint32 samples) {
	if (!_recording) {
		return;
	}

	a_recordingLevel.start(level);
	_a_recording.start();
	_recordingSamples = samples;
	if (samples < 0 || samples >= AudioVoiceMsgFrequency * AudioVoiceMsgMaxLength) {
		stopRecording(_peer && samples > 0 && _inField);
	}
	updateField();
	if (_peer && (!_peer->isChannel() || _peer->isMegagroup() || !_peer->asChannel()->canPublish() || (!_peer->asChannel()->isBroadcast() && !_broadcast.checked()))) {
		updateSendAction(_history, SendActionRecordVoice);
	}
}

void HistoryWidget::updateStickers() {
	if (!cLastStickersUpdate() || getms(true) >= cLastStickersUpdate() + StickersUpdateTimeout) {
		if (!_stickersUpdateRequest) {
			_stickersUpdateRequest = MTP::send(MTPmessages_GetAllStickers(MTP_int(Local::countStickersHash(true))), rpcDone(&HistoryWidget::stickersGot), rpcFail(&HistoryWidget::stickersFailed));
		}
	}
	if (!cLastSavedGifsUpdate() || getms(true) >= cLastSavedGifsUpdate() + StickersUpdateTimeout) {
		if (!_savedGifsUpdateRequest) {
			_savedGifsUpdateRequest = MTP::send(MTPmessages_GetSavedGifs(MTP_int(Local::countSavedGifsHash())), rpcDone(&HistoryWidget::savedGifsGot), rpcFail(&HistoryWidget::savedGifsFailed));
		}
	}
}

void HistoryWidget::notify_botCommandsChanged(UserData *user) {
	if (_peer && (_peer == user || !_peer->isUser())) {
		if (_attachMention.clearFilteredBotCommands()) {
			onCheckMentionDropdown();
		}
	}
}

void HistoryWidget::notify_inlineBotRequesting(bool requesting) {
	_attachEmoji.setLoading(requesting);
}

void HistoryWidget::notify_userIsBotChanged(UserData *user) {
	if (_peer && _peer == user) {
		_list->notifyIsBotChanged();
		_list->updateBotInfo();
		updateControlsVisibility();
		resizeEvent(0);
	}
}

void HistoryWidget::notify_migrateUpdated(PeerData *peer) {
	if (_peer) {
		if (_peer == peer) {
			if (peer->migrateTo()) {
				showHistory(peer->migrateTo()->id, (_showAtMsgId > 0) ? (-_showAtMsgId) : _showAtMsgId, true);
			} else if ((_migrated ? _migrated->peer : 0) != peer->migrateFrom()) {
				History *migrated = peer->migrateFrom() ? App::history(peer->migrateFrom()->id) : 0;
				if (_migrated || (migrated && migrated->unreadCount > 0)) {
					showHistory(peer->id, peer->migrateFrom() ? _showAtMsgId : ((_showAtMsgId < 0 && -_showAtMsgId < ServerMaxMsgId) ? ShowAtUnreadMsgId : _showAtMsgId), true);
				} else {
					_migrated = migrated;
					_list->notifyMigrateUpdated();
					updateListSize();
				}
			}
		} else if (_migrated && _migrated->peer == peer && peer->migrateTo() != _peer) {
			showHistory(_peer->id, _showAtMsgId, true);
		}
	}
}

void HistoryWidget::notify_clipStopperHidden(ClipStopperType type) {
	if (_list) _list->update();
}

void HistoryWidget::notify_historyItemResized(const HistoryItem *row, bool scrollToIt) {
	updateListSize(false, false, { ScrollChangeNone, 0 }, row, scrollToIt);
}

void HistoryWidget::cmd_search() {
	if (!inFocusChain() || !_peer) return;

	App::main()->searchInPeer(_peer);
}

void HistoryWidget::cmd_next_chat() {
	PeerData *p = 0;
	MsgId m = 0;
	App::main()->peerAfter(_peer, qMax(_showAtMsgId, 0), p, m);
	if (p) Ui::showPeerHistory(p, m);
}

void HistoryWidget::cmd_previous_chat() {
	PeerData *p = 0;
	MsgId m = 0;
	App::main()->peerBefore(_peer, qMax(_showAtMsgId, 0), p, m);
	if (p) Ui::showPeerHistory(p, m);
}

void HistoryWidget::stickersGot(const MTPmessages_AllStickers &stickers) {
	cSetLastStickersUpdate(getms(true));
	_stickersUpdateRequest = 0;

	if (stickers.type() != mtpc_messages_allStickers) return;
	const MTPDmessages_allStickers &d(stickers.c_messages_allStickers());

	const QVector<MTPStickerSet> &d_sets(d.vsets.c_vector().v);

	StickerSetsOrder &setsOrder(cRefStickerSetsOrder());
	setsOrder.clear();

	StickerSets &sets(cRefStickerSets());
	QMap<uint64, uint64> setsToRequest;
	for (StickerSets::iterator i = sets.begin(), e = sets.end(); i != e; ++i) {
		i->access = 0; // mark for removing
	}
	for (int32 i = 0, l = d_sets.size(); i != l; ++i) {
		if (d_sets.at(i).type() == mtpc_stickerSet) {
			const MTPDstickerSet &set(d_sets.at(i).c_stickerSet());
			StickerSets::iterator it = sets.find(set.vid.v);
			QString title = stickerSetTitle(set);
			if (it == sets.cend()) {
				it = sets.insert(set.vid.v, StickerSet(set.vid.v, set.vaccess_hash.v, title, qs(set.vshort_name), set.vcount.v, set.vhash.v, set.vflags.v | MTPDstickerSet_flag_NOT_LOADED));
			} else {
				it->access = set.vaccess_hash.v;
				it->title = title;
				it->shortName = qs(set.vshort_name);
				it->flags = set.vflags.v;
				if (it->count != set.vcount.v || it->hash != set.vhash.v || it->emoji.isEmpty()) {
					it->count = set.vcount.v;
					it->hash = set.vhash.v;
					it->flags |= MTPDstickerSet_flag_NOT_LOADED; // need to request this set
				}
			}
			if (!(it->flags & MTPDstickerSet::flag_disabled) || (it->flags & MTPDstickerSet::flag_official)) {
				setsOrder.push_back(set.vid.v);
				if (it->stickers.isEmpty() || (it->flags & MTPDstickerSet_flag_NOT_LOADED)) {
					setsToRequest.insert(set.vid.v, set.vaccess_hash.v);
				}
			}
		}
	}
	bool writeRecent = false;
	RecentStickerPack &recent(cGetRecentStickers());
	for (StickerSets::iterator it = sets.begin(), e = sets.end(); it != e;) {
		if (it->id == CustomStickerSetId || it->access != 0) {
			++it;
		} else {
			for (RecentStickerPack::iterator i = recent.begin(); i != recent.cend();) {
				if (it->stickers.indexOf(i->first) >= 0) {
					i = recent.erase(i);
					writeRecent = true;
				} else {
					++i;
				}
			}
			it = sets.erase(it);
		}
	}

	if (Local::countStickersHash() != d.vhash.v) {
		LOG(("API Error: received stickers hash %1 while counted hash is %2").arg(d.vhash.v).arg(Local::countStickersHash()));
	}

	if (!setsToRequest.isEmpty() && App::api()) {
		for (QMap<uint64, uint64>::const_iterator i = setsToRequest.cbegin(), e = setsToRequest.cend(); i != e; ++i) {
			App::api()->scheduleStickerSetRequest(i.key(), i.value());
		}
		App::api()->requestStickerSets();
	}

	Local::writeStickers();
	if (writeRecent) Local::writeUserSettings();

	if (App::main()) emit App::main()->stickersUpdated();
}

bool HistoryWidget::stickersFailed(const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	LOG(("App Fail: Failed to get stickers!"));

	cSetLastStickersUpdate(getms(true));
	_stickersUpdateRequest = 0;
	return true;
}

void HistoryWidget::savedGifsGot(const MTPmessages_SavedGifs &gifs) {
	cSetLastSavedGifsUpdate(getms(true));
	_savedGifsUpdateRequest = 0;

	if (gifs.type() != mtpc_messages_savedGifs) return;
	const MTPDmessages_savedGifs &d(gifs.c_messages_savedGifs());

	const QVector<MTPDocument> &d_gifs(d.vgifs.c_vector().v);

	SavedGifs &saved(cRefSavedGifs());
	saved.clear();

	saved.reserve(d_gifs.size());
	for (int32 i = 0, l = d_gifs.size(); i != l; ++i) {
		DocumentData *doc = App::feedDocument(d_gifs.at(i));
		if (!doc || !doc->isAnimation()) {
			LOG(("API Error: bad document returned in HistoryWidget::savedGifsGot!"));
			continue;
		}

		saved.push_back(doc);
	}
	if (Local::countSavedGifsHash() != d.vhash.v) {
		LOG(("API Error: received saved gifs hash %1 while counted hash is %2").arg(d.vhash.v).arg(Local::countSavedGifsHash()));
	}

	Local::writeSavedGifs();

	if (App::main()) emit App::main()->savedGifsUpdated();
}

void HistoryWidget::saveGif(DocumentData *doc) {
	if (doc->isGifv() && cSavedGifs().indexOf(doc) != 0) {
		MTP::send(MTPmessages_SaveGif(MTP_inputDocument(MTP_long(doc->id), MTP_long(doc->access)), MTP_bool(false)), rpcDone(&HistoryWidget::saveGifDone, doc));
	}
}

void HistoryWidget::saveGifDone(DocumentData *doc, const MTPBool &result) {
	if (mtpIsTrue(result)) {
		App::addSavedGif(doc);
	}
}

bool HistoryWidget::savedGifsFailed(const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	LOG(("App Fail: Failed to get saved gifs!"));

	cSetLastSavedGifsUpdate(getms(true));
	_savedGifsUpdateRequest = 0;
	return true;
}

void HistoryWidget::clearReplyReturns() {
	_replyReturns.clear();
	_replyReturn = 0;
}

void HistoryWidget::pushReplyReturn(HistoryItem *item) {
	if (!item) return;
	if (item->history() == _history) {
		_replyReturns.push_back(item->id);
	} else if (item->history() == _migrated) {
		_replyReturns.push_back(-item->id);
	} else {
		return;
	}
	_replyReturn = item;
	updateControlsVisibility();
}

QList<MsgId> HistoryWidget::replyReturns() {
	return _replyReturns;
}

void HistoryWidget::setReplyReturns(PeerId peer, const QList<MsgId> &replyReturns) {
	if (!_peer || _peer->id != peer) return;

	_replyReturns = replyReturns;
	if (_replyReturns.isEmpty()) {
		_replyReturn = 0;
	} else if (_replyReturns.back() < 0 && -_replyReturns.back() < ServerMaxMsgId) {
		_replyReturn = App::histItemById(0, -_replyReturns.back());
	} else {
		_replyReturn = App::histItemById(_channel, _replyReturns.back());
	}
	while (!_replyReturns.isEmpty() && !_replyReturn) {
		_replyReturns.pop_back();
		if (_replyReturns.isEmpty()) {
			_replyReturn = 0;
		} else if (_replyReturns.back() < 0 && -_replyReturns.back() < ServerMaxMsgId) {
			_replyReturn = App::histItemById(0, -_replyReturns.back());
		} else {
			_replyReturn = App::histItemById(_channel, _replyReturns.back());
		}
	}
	updateControlsVisibility();
}

void HistoryWidget::calcNextReplyReturn() {
	_replyReturn = 0;
	while (!_replyReturns.isEmpty() && !_replyReturn) {
		_replyReturns.pop_back();
		if (_replyReturns.isEmpty()) {
			_replyReturn = 0;
		} else if (_replyReturns.back() < 0 && -_replyReturns.back() < ServerMaxMsgId) {
			_replyReturn = App::histItemById(0, -_replyReturns.back());
		} else {
			_replyReturn = App::histItemById(_channel, _replyReturns.back());
		}
	}
	if (!_replyReturn) updateControlsVisibility();
}

void HistoryWidget::fastShowAtEnd(History *h) {
	if (h == _history) {
		h->getReadyFor(ShowAtTheEndMsgId, _fixedInScrollMsgId, _fixedInScrollMsgTop);

		clearAllLoadRequests();

		setMsgId(ShowAtUnreadMsgId);
		_histInited = false;

		if (h->isReadyFor(_showAtMsgId, _fixedInScrollMsgId, _fixedInScrollMsgTop)) {
			historyLoaded();
		} else {
			firstLoadMessages();
			doneShow();
		}
	} else if (h) {
		MsgId fixInScrollMsgId = 0;
		int32 fixInScrollMsgTop = 0;
		h->getReadyFor(ShowAtTheEndMsgId, fixInScrollMsgId, fixInScrollMsgTop);
	}
}

void HistoryWidget::applyDraft(bool parseLinks) {
	HistoryDraft *draft = _history ? _history->draft() : 0;
	if (!draft) {
		setFieldText(QString());
		_field.setFocus();
		_editMsgId = _replyToId = 0;
		return;
	}

	_textUpdateEventsFlags = 0;
	setFieldText(draft->text);
	_field.setFocus();
	draft->cursor.applyTo(_field);
	_textUpdateEventsFlags = TextUpdateEventsSaveDraft | TextUpdateEventsSendTyping;
	_previewCancelled = draft->previewCancelled;
	if (_history->editDraft) {
		_editMsgId = _history->editDraft->msgId;
		_replyToId = 0;
	} else {
		_editMsgId = 0;
		_replyToId = readyToForward() ? 0 : _history->msgDraft->msgId;
	}
	if (parseLinks) {
		onPreviewParse();
	}
	if (_editMsgId || _replyToId) {
		updateReplyEditTexts();
		if (!_replyEditMsg && App::api()) {
			App::api()->requestMessageData(_peer->asChannel(), _editMsgId ? _editMsgId : _replyToId, new ReplyEditMessageDataCallback());
		}
	}
}

void HistoryWidget::showHistory(const PeerId &peerId, MsgId showAtMsgId, bool reload) {
	MsgId wasMsgId = _showAtMsgId;
	History *wasHistory = _history;

	if (_history) {
		if (_peer->id == peerId && !reload) {
			_history->lastWidth = 0;

			bool wasOnlyImportant = _history->isChannel() ? _history->asChannelHistory()->onlyImportant() : true;

			bool canShowNow = _history->isReadyFor(showAtMsgId, _fixedInScrollMsgId, _fixedInScrollMsgTop);
			if (_fixedInScrollMsgId) {
				_fixedInScrollMsgTop += _list->height() - _scroll.scrollTop() - st::historyPadding;
			}
			if (!canShowNow) {
				delayedShowAt(showAtMsgId);
			} else {
				if (_history->isChannel() && wasOnlyImportant != _history->asChannelHistory()->onlyImportant()) {
					clearAllLoadRequests();
				}

				clearDelayedShowAt();
				if (_replyReturn) {
					if (_replyReturn->history() == _history && _replyReturn->id == showAtMsgId) {
						calcNextReplyReturn();
					} else if (_replyReturn->history() == _migrated && -_replyReturn->id == showAtMsgId) {
						calcNextReplyReturn();
					}
				}

				_showAtMsgId = showAtMsgId;
				_histInited = false;

				historyLoaded();
			}
			App::main()->dlgUpdated(wasHistory, wasMsgId);
			emit historyShown(_history, _showAtMsgId);

			App::main()->topBar()->update();
			update();
			return;
		}
		if (_history->mySendActions.contains(SendActionTyping)) {
			updateSendAction(_history, SendActionTyping, -1);
		}
	}

	if (!cAutoPlayGif()) {
		App::stopGifItems();
	}
	clearReplyReturns();

	clearAllLoadRequests();

	if (_history) {
		if (_editMsgId) {
			_history->setEditDraft(new HistoryEditDraft(_field, _editMsgId, _previewCancelled, _saveEditMsgRequestId));
		} else {
			if (_replyToId || !_field.getLastText().isEmpty()) {
				_history->setMsgDraft(new HistoryDraft(_field, _replyToId, _previewCancelled));
			} else {
				_history->setMsgDraft(nullptr);
			}
			_history->setEditDraft(nullptr);
		}
		if (_migrated) {
			_migrated->setMsgDraft(nullptr); // use migrated draft only once
			_migrated->setEditDraft(nullptr);
		}

		writeDrafts(&_history->msgDraft, &_history->editDraft);

		if (_scroll.scrollTop() + 1 <= _scroll.scrollTopMax()) {
			_history->lastWidth = _list->width();
			_history->lastShowAtMsgId = _showAtMsgId;
		} else {
			_history->lastWidth = 0;
			_history->lastShowAtMsgId = ShowAtUnreadMsgId;
		}
		_history->lastScrollTop = _scroll.scrollTop();
		if (_history->unreadBar) {
			_history->unreadBar->destroy();
		}
		if (_migrated && _migrated->unreadBar) {
			_migrated->unreadBar->destroy();
		}
		if (_pinnedBar) {
			delete _pinnedBar;
			_pinnedBar = nullptr;
			_inPinnedMsg = false;
		}
		_history = _migrated = 0;
		updateBotKeyboard();
	}

	_editMsgId = 0;
	_saveEditMsgRequestId = 0;
	_replyToId = 0;
	_replyEditMsg = 0;
	_previewData = 0;
	_previewCache.clear();
	_fieldBarCancel.hide();

	if (_list) _list->deleteLater();
	_list = 0;
	_scroll.takeWidget();
	updateTopBarSelection();

	if (_inlineBot) {
		_inlineBot = 0;
		_emojiPan.clearInlineBot();
		updateFieldPlaceholder();
	}

	_showAtMsgId = showAtMsgId;
	_histInited = false;

	_peer = peerId ? App::peer(peerId) : 0;
	_channel = _peer ? peerToChannel(_peer->id) : NoChannel;
	_canSendMessages = canSendMessages(_peer);
	if (_peer && _peer->isChannel()) {
		_peer->asChannel()->updateFull();
		_joinChannel.setText(lang(_peer->isMegagroup() ? lng_group_invite_join : lng_channel_join));
	}

	_unblockRequest = _reportSpamRequest = 0;
	if (_reportSpamSettingRequestId > 0) {
		MTP::cancel(_reportSpamSettingRequestId);
	}
	_reportSpamSettingRequestId = ReportSpamRequestNeeded;

	_titlePeerText = QString();
	_titlePeerTextWidth = 0;

	noSelectingScroll();
	_selCount = 0;
	App::main()->topBar()->showSelected(0);

	App::hoveredItem(0);
	App::pressedItem(0);
	App::hoveredLinkItem(0);
	App::pressedLinkItem(0);
	App::contextItem(0);
	App::mousedItem(0);

	if (_peer) {
		App::forgetMedia();
		_serviceImageCacheSize = imageCacheSize();
		MTP::clearLoaderPriorities();

		_history = App::history(_peer->id);
		_migrated = _peer->migrateFrom() ? App::history(_peer->migrateFrom()->id) : 0;

		if (_channel) {
			updateNotifySettings();
			if (_peer->notify == UnknownNotifySettings) {
				App::wnd()->getNotifySetting(MTP_inputNotifyPeer(_peer->input));
			}
		}

		if (_showAtMsgId == ShowAtUnreadMsgId) {
			if (_history->lastWidth) {
				_showAtMsgId = _history->lastShowAtMsgId;
			}
		} else {
			_history->lastWidth = 0;
		}

		_list = new HistoryInner(this, &_scroll, _history);
		_list->hide();
		_scroll.hide();
		_scroll.setWidget(_list);
		_list->show();

		_updateHistoryItems.stop();

		pinnedMsgVisibilityUpdated();
		if (_history->lastWidth || _history->isReadyFor(_showAtMsgId, _fixedInScrollMsgId, _fixedInScrollMsgTop)) {
			_fixedInScrollMsgId = 0;
			_fixedInScrollMsgTop = 0;
			historyLoaded();
		} else {
			firstLoadMessages();
			doneShow();
		}

		emit App::main()->peerUpdated(_peer);

		Local::readDraftsWithCursors(_history);
		if (_migrated) {
			Local::readDraftsWithCursors(_migrated);
			_migrated->setEditDraft(nullptr);
			if (_migrated->msgDraft && !_migrated->msgDraft->text.isEmpty()) {
				_migrated->msgDraft->msgId = 0; // edit and reply to drafts can't migrate
				if (!_history->msgDraft) {
					_history->setMsgDraft(new HistoryDraft(*_migrated->msgDraft));
				}
			}
			_migrated->setMsgDraft(nullptr);
		}
		applyDraft(false);

		resizeEvent(0);
		if (!_previewCancelled) {
			onPreviewParse();
		}

		connect(&_scroll, SIGNAL(geometryChanged()), _list, SLOT(onParentGeometryChanged()));
		connect(&_scroll, SIGNAL(scrolled()), _list, SLOT(onUpdateSelected()));
	} else {
		setFieldText(QString());
		doneShow();
	}

	if (App::wnd()) QTimer::singleShot(0, App::wnd(), SLOT(setInnerFocus()));

	App::main()->dlgUpdated(wasHistory, wasMsgId);
	emit historyShown(_history, _showAtMsgId);

	App::main()->topBar()->update();
	update();
}

void HistoryWidget::clearDelayedShowAt() {
	_delayedShowAtMsgId = -1;
	if (_delayedShowAtRequest) {
		MTP::cancel(_delayedShowAtRequest);
		_delayedShowAtRequest = 0;
	}
}

void HistoryWidget::clearAllLoadRequests() {
	clearDelayedShowAt();
	if (_firstLoadRequest) MTP::cancel(_firstLoadRequest);
	if (_preloadRequest) MTP::cancel(_preloadRequest);
	if (_preloadDownRequest) MTP::cancel(_preloadDownRequest);
	_preloadRequest = _preloadDownRequest = _firstLoadRequest = 0;
}

void HistoryWidget::contactsReceived() {
	if (!_peer) return;
	updateReportSpamStatus();
	updateControlsVisibility();
}

void HistoryWidget::updateAfterDrag() {
	if (_list) _list->dragActionUpdate(QCursor::pos());
}

void HistoryWidget::ctrlEnterSubmitUpdated() {
	_field.setCtrlEnterSubmit(cCtrlEnter());
}

void HistoryWidget::updateNotifySettings() {
	if (!_peer || !_peer->isChannel()) return;

	_muteUnmute.setText(lang(_history->mute ? lng_channel_unmute : lng_channel_mute));
	if (_peer->notify != UnknownNotifySettings) {
		_silent.setChecked(_peer->notify != EmptyNotifySettings && (_peer->notify->flags & MTPDpeerNotifySettings::flag_silent));
		if (_silent.isHidden() && hasSilentToggle()) {
			updateControlsVisibility();
		}
	}
}

bool HistoryWidget::contentOverlapped(const QRect &globalRect) {
	return (_attachDragDocument.overlaps(globalRect) ||
			_attachDragPhoto.overlaps(globalRect) ||
			_attachType.overlaps(globalRect) ||
			_attachMention.overlaps(globalRect) ||
			_emojiPan.overlaps(globalRect));
}

void HistoryWidget::updateReportSpamStatus() {
	if (!_peer || (_peer->isUser() && (peerToUser(_peer->id) == MTP::authedId() || isNotificationsUser(_peer->id) || isServiceUser(_peer->id) || _peer->asUser()->botInfo))) {
		_reportSpamStatus = dbiprsHidden;
		return;
	} else if (!_firstLoadRequest && _history->isEmpty()) {
		_reportSpamStatus = dbiprsNoButton;
		if (cReportSpamStatuses().contains(_peer->id)) {
			cRefReportSpamStatuses().remove(_peer->id);
			Local::writeReportSpamStatuses();
		}
		return;
	} else {
		ReportSpamStatuses::const_iterator i = cReportSpamStatuses().constFind(_peer->id);
		if (i != cReportSpamStatuses().cend()) {
			_reportSpamStatus = i.value();
			if (_reportSpamStatus == dbiprsNoButton) {
				_reportSpamStatus = dbiprsHidden;
				if (!_peer->isUser() || _peer->asUser()->contact < 1) {
					MTP::send(MTPmessages_HideReportSpam(_peer->input));
				}

				cRefReportSpamStatuses().insert(_peer->id, _reportSpamStatus);
				Local::writeReportSpamStatuses();
			} else if (_reportSpamStatus == dbiprsShowButton) {
				requestReportSpamSetting();
			}
			_reportSpamPanel.setReported(_reportSpamStatus == dbiprsReportSent, _peer);
			return;
		} else if (_peer->migrateFrom()) { // migrate report status
			i = cReportSpamStatuses().constFind(_peer->migrateFrom()->id);
			if (i != cReportSpamStatuses().cend()) {
				_reportSpamStatus = i.value();
				if (_reportSpamStatus == dbiprsNoButton) {
					_reportSpamStatus = dbiprsHidden;
					if (!_peer->isUser() || _peer->asUser()->contact < 1) {
						MTP::send(MTPmessages_HideReportSpam(_peer->input));
					}
				} else if (_reportSpamStatus == dbiprsShowButton) {
					requestReportSpamSetting();
				}
				cRefReportSpamStatuses().insert(_peer->id, _reportSpamStatus);
				Local::writeReportSpamStatuses();

				_reportSpamPanel.setReported(_reportSpamStatus == dbiprsReportSent, _peer);
				return;
			}
		}
	}
	if (!cContactsReceived() || _firstLoadRequest) {
		_reportSpamStatus = dbiprsUnknown;
	} else if (_peer->isUser() && _peer->asUser()->contact > 0) {
		_reportSpamStatus = dbiprsHidden;
	} else {
		_reportSpamStatus = dbiprsRequesting;
		requestReportSpamSetting();
	}
	if (_reportSpamStatus == dbiprsHidden) {
		_reportSpamPanel.setReported(false, _peer);
		cRefReportSpamStatuses().insert(_peer->id, _reportSpamStatus);
		Local::writeReportSpamStatuses();
	}
}

void HistoryWidget::requestReportSpamSetting() {
	if (_reportSpamSettingRequestId >= 0 || !_peer) return;

	_reportSpamSettingRequestId = MTP::send(MTPmessages_GetPeerSettings(_peer->input), rpcDone(&HistoryWidget::reportSpamSettingDone), rpcFail(&HistoryWidget::reportSpamSettingFail));
}

void HistoryWidget::reportSpamSettingDone(const MTPPeerSettings &result, mtpRequestId req) {
	if (req != _reportSpamSettingRequestId) return;

	_reportSpamSettingRequestId = 0;
	if (result.type() == mtpc_peerSettings) {
		const MTPDpeerSettings &d(result.c_peerSettings());
		DBIPeerReportSpamStatus status = d.is_report_spam() ? dbiprsShowButton : dbiprsHidden;
		if (status != _reportSpamStatus) {
			_reportSpamStatus = status;
			_reportSpamPanel.setReported(false, _peer);

			cRefReportSpamStatuses().insert(_peer->id, _reportSpamStatus);
			Local::writeReportSpamStatuses();

			updateControlsVisibility();
		}
	}
}

bool HistoryWidget::reportSpamSettingFail(const RPCError &error, mtpRequestId req) {
	if (mtpIsFlood(error)) return false;

	if (req == _reportSpamSettingRequestId) {
		req = 0;
	}
	return true;
}

void HistoryWidget::updateControlsVisibility() {
	_topShadow.setVisible(_peer ? true : false);
	if (!_history || _a_show.animating()) {
		_reportSpamPanel.hide();
		_scroll.hide();
		_kbScroll.hide();
		_send.hide();
		_unblock.hide();
		_botStart.hide();
		_joinChannel.hide();
		_muteUnmute.hide();
		_attachMention.hide();
		_field.hide();
		_fieldBarCancel.hide();
		_attachDocument.hide();
		_attachPhoto.hide();
		_attachEmoji.hide();
		_broadcast.hide();
		_silent.hide();
		_toHistoryEnd.hide();
		_collapseComments.hide();
		_kbShow.hide();
		_kbHide.hide();
		_cmdStart.hide();
		_attachType.hide();
		_emojiPan.hide();
		if (_pinnedBar) {
			_pinnedBar->cancel.hide();
			_pinnedBar->shadow.hide();
		}
		return;
	}

	updateToEndVisibility();
	if (_pinnedBar) {
		_pinnedBar->cancel.show();
		_pinnedBar->shadow.show();
	}
	if (_firstLoadRequest) {
		_scroll.hide();
	} else {
		_scroll.show();
	}
	if (_reportSpamStatus == dbiprsShowButton || _reportSpamStatus == dbiprsReportSent) {
		_reportSpamPanel.show();
	} else {
		_reportSpamPanel.hide();
	}
	if (isBlocked() || isJoinChannel() || isMuteUnmute()) {
		if (isBlocked()) {
			_joinChannel.hide();
			_muteUnmute.hide();
			if (_unblock.isHidden()) {
				_unblock.clearState();
				_unblock.show();
			}
		} else if (isJoinChannel()) {
			_unblock.hide();
			_muteUnmute.hide();
			if (_joinChannel.isHidden()) {
				_joinChannel.clearState();
				_joinChannel.show();
			}
		} else if (isMuteUnmute()) {
			_unblock.hide();
			_joinChannel.hide();
			if (_muteUnmute.isHidden()) {
				_muteUnmute.clearState();
				_muteUnmute.show();
			}
		}
		_kbShown = false;
		_attachMention.hide();
		_send.hide();
		_botStart.hide();
		_attachDocument.hide();
		_attachPhoto.hide();
		_broadcast.hide();
		_silent.hide();
		_kbScroll.hide();
		_fieldBarCancel.hide();
		_attachDocument.hide();
		_attachPhoto.hide();
		_attachEmoji.hide();
		_kbShow.hide();
		_kbHide.hide();
		_cmdStart.hide();
		_attachType.hide();
		_emojiPan.hide();
		if (!_field.isHidden()) {
			_field.hide();
			resizeEvent(0);
			update();
		}
	} else if (_canSendMessages) {
		onCheckMentionDropdown();
		if (isBotStart()) {
			if (isBotStart()) {
				_unblock.hide();
				_joinChannel.hide();
				_muteUnmute.hide();
				if (_botStart.isHidden()) {
					_botStart.clearState();
					_botStart.show();
				}
			}
			_kbShown = false;
			_send.hide();
			_field.hide();
			_attachEmoji.hide();
			_kbShow.hide();
			_kbHide.hide();
			_cmdStart.hide();
			_attachDocument.hide();
			_attachPhoto.hide();
			_broadcast.hide();
			_silent.hide();
			_kbScroll.hide();
			_fieldBarCancel.hide();
		} else {
			_unblock.hide();
			_botStart.hide();
			_joinChannel.hide();
			_muteUnmute.hide();
			if (cHasAudioCapture() && !_field.hasSendText() && !readyToForward()) {
				_send.hide();
				mouseMoveEvent(0);
			} else {
				_send.show();
				_a_record.stop();
				_inRecord = _inField = false;
				a_recordOver = anim::fvalue(0, 0);
			}
			if (_recording) {
				_field.hide();
				_attachEmoji.hide();
				_kbShow.hide();
				_kbHide.hide();
				_cmdStart.hide();
				_attachDocument.hide();
				_attachPhoto.hide();
				_broadcast.hide();
				_silent.hide();
				if (_kbShown) {
					_kbScroll.show();
				} else {
					_kbScroll.hide();
				}
			} else {
				_field.show();
				if (_kbShown) {
					_kbScroll.show();
					_attachEmoji.hide();
					_kbHide.show();
					_kbShow.hide();
					_cmdStart.hide();
				} else if (_kbReplyTo) {
					_kbScroll.hide();
					_attachEmoji.show();
					_kbHide.hide();
					_kbShow.hide();
					_cmdStart.hide();
				} else {
					_kbScroll.hide();
					_attachEmoji.show();
					_kbHide.hide();
					if (_keyboard.hasMarkup()) {
						_kbShow.show();
						_cmdStart.hide();
					} else {
						_kbShow.hide();
						if (_cmdStartShown) {
							_cmdStart.show();
						} else {
							_cmdStart.hide();
						}
					}
				}
				if (cDefaultAttach() == dbidaPhoto) {
					_attachDocument.hide();
					_attachPhoto.show();
				} else {
					_attachDocument.show();
					_attachPhoto.hide();
				}
				if (hasBroadcastToggle()) {
					_broadcast.show();
				} else {
					_broadcast.hide();
				}
				if (hasSilentToggle()) {
					_silent.show();
				} else {
					_silent.hide();
				}
				updateFieldPlaceholder();
			}
			if (_editMsgId || _replyToId || readyToForward() || (_previewData && _previewData->pendingTill >= 0) || _kbReplyTo) {
				if (_fieldBarCancel.isHidden()) {
					_fieldBarCancel.show();
					resizeEvent(0);
					update();
				}
			} else {
				_fieldBarCancel.hide();
			}
		}
	} else {
		_attachMention.hide();
		_send.hide();
		_unblock.hide();
		_botStart.hide();
		_joinChannel.hide();
		_muteUnmute.hide();
		_attachDocument.hide();
		_attachPhoto.hide();
		_broadcast.hide();
		_silent.hide();
		_kbScroll.hide();
		_fieldBarCancel.hide();
		_attachDocument.hide();
		_attachPhoto.hide();
		_attachEmoji.hide();
		_kbShow.hide();
		_kbHide.hide();
		_cmdStart.hide();
		_attachType.hide();
		_emojiPan.hide();
		_kbScroll.hide();
		if (!_field.isHidden()) {
			_field.hide();
			resizeEvent(0);
			update();
		}
	}
	updateMouseTracking();
}

void HistoryWidget::updateMouseTracking() {
	bool trackMouse = !_fieldBarCancel.isHidden() || _pinnedBar || (cHasAudioCapture() && _send.isHidden() && !_field.isHidden());
	setMouseTracking(trackMouse);
}

void HistoryWidget::newUnreadMsg(History *history, HistoryItem *item) {
	if (App::wnd()->historyIsActive()) {
		if (_history == history) {
			historyWasRead();
			if (_scroll.scrollTop() + 1 > _scroll.scrollTopMax()) {
				if (history->unreadBar) history->unreadBar->destroy();
			}
		} else {
			App::wnd()->notifySchedule(history, item);
			history->setUnreadCount(history->unreadCount + 1);
		}
	} else {
		if (_history == history) {
			if (_scroll.scrollTop() + 1 > _scroll.scrollTopMax()) {
				if (history->unreadBar) history->unreadBar->destroy();
				if (_migrated && _migrated->unreadBar) _migrated->unreadBar->destroy();
			}
		}
		App::wnd()->notifySchedule(history, item);
		history->setUnreadCount(history->unreadCount + 1);
	}
}

void HistoryWidget::historyToDown(History *history) {
	history->lastScrollTop = ScrollMax;
	if (history == _history) {
		_scroll.scrollToY(_scroll.scrollTopMax());
	}
}

void HistoryWidget::historyWasRead(bool force) {
	App::main()->readServerHistory(_history, force);
	if (_migrated) App::main()->readServerHistory(_migrated, force);
}

void HistoryWidget::historyCleared(History *history) {
	if (history == _history) {
		_list->dragActionCancel();
	}
}

bool HistoryWidget::messagesFailed(const RPCError &error, mtpRequestId requestId) {
	if (mtpIsFlood(error)) return false;

	if (error.type() == qstr("CHANNEL_PRIVATE") || error.type() == qstr("CHANNEL_PUBLIC_GROUP_NA") || error.type() == qstr("USER_BANNED_IN_CHANNEL")) {
		PeerData *was = _peer;
		Ui::showChatsList();
		Ui::showLayer(new InformBox(lang((was && was->isMegagroup()) ? lng_group_not_accessible : lng_channel_not_accessible)));
		return true;
	}

	LOG(("RPC Error: %1 %2: %3").arg(error.code()).arg(error.type()).arg(error.description()));
	if (_preloadRequest == requestId) {
		_preloadRequest = 0;
	} else if (_preloadDownRequest == requestId) {
		_preloadDownRequest = 0;
	} else if (_firstLoadRequest == requestId) {
		_firstLoadRequest = 0;
		Ui::showChatsList();
	} else if (_delayedShowAtRequest == requestId) {
		_delayedShowAtRequest = 0;
	}
	return true;
}

void HistoryWidget::messagesReceived(PeerData *peer, const MTPmessages_Messages &messages, mtpRequestId requestId) {
	if (!_history) {
		_preloadRequest = _preloadDownRequest = _firstLoadRequest = _delayedShowAtRequest = 0;
		return;
	}

	bool toMigrated = (peer == _peer->migrateFrom());
	if (peer != _peer && !toMigrated) {
		_preloadRequest = _preloadDownRequest = _firstLoadRequest = _delayedShowAtRequest = 0;
		return;
	}

	int32 count = 0;
	const QVector<MTPMessage> emptyList, *histList = &emptyList;
	const QVector<MTPMessageGroup> *histCollapsed = 0;
	switch (messages.type()) {
	case mtpc_messages_messages: {
		const MTPDmessages_messages &d(messages.c_messages_messages());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		histList = &d.vmessages.c_vector().v;
		count = histList->size();
	} break;
	case mtpc_messages_messagesSlice: {
		const MTPDmessages_messagesSlice &d(messages.c_messages_messagesSlice());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		histList = &d.vmessages.c_vector().v;
		count = d.vcount.v;
	} break;
	case mtpc_messages_channelMessages: {
		const MTPDmessages_channelMessages &d(messages.c_messages_channelMessages());
		if (peer && peer->isChannel()) {
			peer->asChannel()->ptsReceived(d.vpts.v);
		} else {
			LOG(("API Error: received messages.channelMessages when no channel was passed! (HistoryWidget::messagesReceived)"));
		}
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
		histList = &d.vmessages.c_vector().v;
		if (d.has_collapsed()) histCollapsed = &d.vcollapsed.c_vector().v;
		count = d.vcount.v;
	} break;
	}

	if (_preloadRequest == requestId) {
		addMessagesToFront(peer, *histList, histCollapsed);
		_preloadRequest = 0;
		onListScroll();
		if (_reportSpamStatus == dbiprsUnknown) {
			updateReportSpamStatus();
			if (_reportSpamStatus != dbiprsUnknown) updateControlsVisibility();
		}
	} else if (_preloadDownRequest == requestId) {
		addMessagesToBack(peer, *histList, histCollapsed);
		_preloadDownRequest = 0;
		onListScroll();
		if (_history->loadedAtBottom() && App::wnd()) App::wnd()->checkHistoryActivation();
	} else if (_firstLoadRequest == requestId) {
		if (toMigrated) {
			_history->clear(true);
		} else if (_migrated) {
			_migrated->clear(true);
		}
		addMessagesToFront(peer, *histList, histCollapsed);
		if (_fixedInScrollMsgId && _history->isChannel()) {
			_history->asChannelHistory()->insertCollapseItem(_fixedInScrollMsgId);
		}
		_firstLoadRequest = 0;
		if (_history->loadedAtTop()) {
			if (_history->unreadCount > count) {
				_history->setUnreadCount(count);
			}
			if (_history->isEmpty() && count > 0) {
				firstLoadMessages();
				return;
			}
		}

		historyLoaded();
	} else if (_delayedShowAtRequest == requestId) {
		if (toMigrated) {
			_history->clear(true);
		} else if (_migrated) {
			_migrated->clear(true);
		}
		_delayedShowAtRequest = 0;
		bool wasOnlyImportant = _history->isChannel() ? _history->asChannelHistory()->onlyImportant() : true;
		_history->getReadyFor(_delayedShowAtMsgId, _fixedInScrollMsgId, _fixedInScrollMsgTop);
		if (_fixedInScrollMsgId) {
			_fixedInScrollMsgTop += _list->height() - _scroll.scrollTop() - st::historyPadding;
		}
		if (_history->isEmpty()) {
			if (_preloadRequest) MTP::cancel(_preloadRequest);
			if (_preloadDownRequest) MTP::cancel(_preloadDownRequest);
			if (_firstLoadRequest) MTP::cancel(_firstLoadRequest);
			_preloadRequest = _preloadDownRequest = 0;
			_firstLoadRequest = -1; // hack - don't updateListSize yet
			addMessagesToFront(peer, *histList, histCollapsed);
			if (_fixedInScrollMsgId && _history->isChannel()) {
				_history->asChannelHistory()->insertCollapseItem(_fixedInScrollMsgId);
			}
			_firstLoadRequest = 0;
			if (_history->loadedAtTop()) {
				if (_history->unreadCount > count) {
					_history->setUnreadCount(count);
				}
				if (_history->isEmpty() && count > 0) {
					firstLoadMessages();
					return;
				}
			}
		}
		if (_replyReturn) {
			if (_replyReturn->history() == _history && _replyReturn->id == _delayedShowAtMsgId) {
				calcNextReplyReturn();
			} else if (_replyReturn->history() == _migrated && -_replyReturn->id == _delayedShowAtMsgId) {
				calcNextReplyReturn();
			}
		}

		setMsgId(_delayedShowAtMsgId);

		_histInited = false;

		if (_history->isChannel() && wasOnlyImportant != _history->asChannelHistory()->onlyImportant()) {
			clearAllLoadRequests();
		}

		historyLoaded();
	}
}

void HistoryWidget::historyLoaded() {
	countHistoryShowFrom();
	if (_history->unreadBar) {
		_history->unreadBar->destroy();
	}
	if (_migrated && _migrated->unreadBar) {
		_migrated->unreadBar->destroy();
	}
	doneShow();
}

void HistoryWidget::windowShown() {
	resizeEvent(0);
}

bool HistoryWidget::isActive() const {
	if (!_history) return true;
	if (_firstLoadRequest || _a_show.animating()) return false;
	if (_history->loadedAtBottom()) return true;
	if (_history->showFrom && !_history->showFrom->detached() && _history->unreadBar) return true;
	if (_migrated && _migrated->showFrom && !_migrated->showFrom->detached() && _migrated->unreadBar) return true;
	return false;
}

void HistoryWidget::firstLoadMessages() {
	if (!_history || _firstLoadRequest) return;

	bool loadImportant = (_history->isChannel() && !_history->isMegagroup()) ? _history->asChannelHistory()->onlyImportant() : false;
	bool wasOnlyImportant = loadImportant;
	PeerData *from = _peer;
	int32 offset_id = 0, offset = 0, loadCount = MessagesPerPage;
	if (_showAtMsgId == ShowAtUnreadMsgId) {
		if (_migrated && _migrated->unreadCount) {
			_history->getReadyFor(_showAtMsgId, _fixedInScrollMsgId, _fixedInScrollMsgTop);
			from = _migrated->peer;
			offset = -loadCount / 2;
			offset_id = _migrated->inboxReadBefore;
		} else if (_history->unreadCount) {
			_history->getReadyFor(_showAtMsgId, _fixedInScrollMsgId, _fixedInScrollMsgTop);
			offset = -loadCount / 2;
			offset_id = _history->inboxReadBefore;
		} else {
			_history->getReadyFor(ShowAtTheEndMsgId, _fixedInScrollMsgId, _fixedInScrollMsgTop);
		}
	} else if (_showAtMsgId == ShowAtTheEndMsgId) {
		_history->getReadyFor(_showAtMsgId, _fixedInScrollMsgId, _fixedInScrollMsgTop);
		loadCount = MessagesFirstLoad;
	} else if (_showAtMsgId > 0) {
		_history->getReadyFor(_showAtMsgId, _fixedInScrollMsgId, _fixedInScrollMsgTop);
		offset = -loadCount / 2;
		offset_id = _showAtMsgId;
	} else if (_showAtMsgId < 0 && _history->isChannel()) {
		if (_showAtMsgId < 0 && -_showAtMsgId < ServerMaxMsgId && _migrated) {
			_history->getReadyFor(_showAtMsgId, _fixedInScrollMsgId, _fixedInScrollMsgTop);
			from = _migrated->peer;
			offset = -loadCount / 2;
			offset_id = -_showAtMsgId;
		} else if (_showAtMsgId == SwitchAtTopMsgId) {
			_history->getReadyFor(_showAtMsgId, _fixedInScrollMsgId, _fixedInScrollMsgTop);
			loadImportant = true;
		} else if (HistoryItem *item = App::histItemById(_channel, _showAtMsgId)) {
			if (item->type() == HistoryItemGroup) {
				_history->getReadyFor(_showAtMsgId, _fixedInScrollMsgId, _fixedInScrollMsgTop);
				offset = -loadCount / 2;
				offset_id = qMax(static_cast<HistoryGroup*>(item)->minId(), 1);
				loadImportant = false;
			} else if (item->type() == HistoryItemCollapse) {
				_history->getReadyFor(_showAtMsgId, _fixedInScrollMsgId, _fixedInScrollMsgTop);
				offset = -loadCount / 2;
				offset_id = qMax(static_cast<HistoryCollapse*>(item)->wasMinId(), 1);
				loadImportant = true;
			}
		}
		if (_fixedInScrollMsgId) {
			_fixedInScrollMsgTop += _list->height() - _scroll.scrollTop() - st::historyPadding;
		}
		if (_history->isMegagroup()) {
			loadImportant = false;
		}
		if (_history->isEmpty() || wasOnlyImportant != loadImportant) {
			clearAllLoadRequests();
		}
	}

	if (loadImportant) {
		_firstLoadRequest = MTP::send(MTPchannels_GetImportantHistory(from->asChannel()->inputChannel, MTP_int(offset_id), MTP_int(0), MTP_int(offset), MTP_int(loadCount), MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::messagesReceived, from), rpcFail(&HistoryWidget::messagesFailed));
	} else {
		_firstLoadRequest = MTP::send(MTPmessages_GetHistory(from->input, MTP_int(offset_id), MTP_int(0), MTP_int(offset), MTP_int(loadCount), MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::messagesReceived, from), rpcFail(&HistoryWidget::messagesFailed));
	}
}

void HistoryWidget::loadMessages() {
	if (!_history || _preloadRequest) return;

	if (_history->isEmpty() && _migrated && _migrated->isEmpty()) {
		return firstLoadMessages();
	}

	bool loadMigrated = _migrated && (_history->isEmpty() || _history->loadedAtTop() || (!_migrated->isEmpty() && !_migrated->loadedAtBottom()));
	History *from = loadMigrated ? _migrated : _history;
	if (from->loadedAtTop()) {
		return;
	}

	bool loadImportant = (from->isChannel() && !from->isMegagroup()) ? from->asChannelHistory()->onlyImportant() : false;
	MsgId offset_id = from->minMsgId();
	int32 offset = 0, loadCount = offset_id ? MessagesPerPage : MessagesFirstLoad;

	if (loadImportant) {
		_preloadRequest = MTP::send(MTPchannels_GetImportantHistory(from->peer->asChannel()->inputChannel, MTP_int(offset_id), MTP_int(0), MTP_int(offset), MTP_int(loadCount), MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::messagesReceived, from->peer), rpcFail(&HistoryWidget::messagesFailed));
	} else {
		_preloadRequest = MTP::send(MTPmessages_GetHistory(from->peer->input, MTP_int(offset_id), MTP_int(0), MTP_int(offset), MTP_int(loadCount), MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::messagesReceived, from->peer), rpcFail(&HistoryWidget::messagesFailed));
	}
}

void HistoryWidget::loadMessagesDown() {
	if (!_history || _preloadDownRequest) return;

	if (_history->isEmpty() && _migrated && _migrated->isEmpty()) {
		return firstLoadMessages();
	}

	bool loadMigrated = _migrated && !(_migrated->isEmpty() || _migrated->loadedAtBottom() || (!_history->isEmpty() && !_history->loadedAtTop()));
	History *from = loadMigrated ? _migrated : _history;
	if (from->loadedAtBottom()) {
		return;
	}

	bool loadImportant = (from->isChannel() && !from->isMegagroup()) ? from->asChannelHistory()->onlyImportant() : false;
	int32 loadCount = MessagesPerPage, offset = -loadCount;

	MsgId offset_id = from->maxMsgId();
	if (!offset_id) {
		if (loadMigrated || !_migrated) return;
		++offset_id;
		++offset;
	}

	if (loadImportant) {
		_preloadDownRequest = MTP::send(MTPchannels_GetImportantHistory(from->peer->asChannel()->inputChannel, MTP_int(offset_id + 1), MTP_int(0), MTP_int(offset), MTP_int(loadCount), MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::messagesReceived, from->peer), rpcFail(&HistoryWidget::messagesFailed));
	} else {
		_preloadDownRequest = MTP::send(MTPmessages_GetHistory(from->peer->input, MTP_int(offset_id + 1), MTP_int(0), MTP_int(offset), MTP_int(loadCount), MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::messagesReceived, from->peer), rpcFail(&HistoryWidget::messagesFailed));
	}
}

void HistoryWidget::delayedShowAt(MsgId showAtMsgId) {
	if (!_history || (_delayedShowAtRequest && _delayedShowAtMsgId == showAtMsgId)) return;

	clearDelayedShowAt();
	_delayedShowAtMsgId = showAtMsgId;

	bool loadImportant = (_history->isChannel() && !_history->isMegagroup()) ? _history->asChannelHistory()->onlyImportant() : false;
	PeerData *from = _peer;
	int32 offset_id = 0, offset = 0, loadCount = MessagesPerPage;
	if (_delayedShowAtMsgId == ShowAtUnreadMsgId) {
		if (_migrated && _migrated->unreadCount) {
			from = _migrated->peer;
			offset = -loadCount / 2;
			offset_id = _migrated->inboxReadBefore;
		} else if (_history->unreadCount) {
			offset = -loadCount / 2;
			offset_id = _history->inboxReadBefore;
		} else {
			loadCount = MessagesFirstLoad;
		}
	} else if (_delayedShowAtMsgId == ShowAtTheEndMsgId) {
		loadCount = MessagesFirstLoad;
	} else if (_delayedShowAtMsgId > 0) {
		offset = -loadCount / 2;
		offset_id = _delayedShowAtMsgId;
		if (HistoryItem *item = App::histItemById(_channel, _delayedShowAtMsgId)) {
			if (!item->isImportant()) {
				loadImportant = false;
			}
		}
	} else if (_delayedShowAtMsgId < 0 && _history->isChannel()) {
		if (_delayedShowAtMsgId < 0 && -_delayedShowAtMsgId < ServerMaxMsgId && _migrated) {
			from = _migrated->peer;
			offset = -loadCount / 2;
			offset_id = -_delayedShowAtMsgId;
		} else if (_delayedShowAtMsgId == SwitchAtTopMsgId) {
			loadImportant = true;
		} else if (HistoryItem *item = App::histItemById(_channel, _delayedShowAtMsgId)) {
			if (item->type() == HistoryItemGroup) {
				offset = -loadCount / 2;
				offset_id = qMax(static_cast<HistoryGroup*>(item)->minId(), 1);
				loadImportant = false;
			} else if (item->type() == HistoryItemCollapse) {
				offset = -loadCount / 2;
				offset_id = qMax(static_cast<HistoryCollapse*>(item)->wasMinId(), 1);
				loadImportant = true;
			}
		}
		if (_history->isMegagroup()) {
			loadImportant = false;
		}
	}

	if (loadImportant) {
		_delayedShowAtRequest = MTP::send(MTPchannels_GetImportantHistory(from->asChannel()->inputChannel, MTP_int(offset_id), MTP_int(0), MTP_int(offset), MTP_int(loadCount), MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::messagesReceived, from), rpcFail(&HistoryWidget::messagesFailed));
	} else {
		_delayedShowAtRequest = MTP::send(MTPmessages_GetHistory(from->input, MTP_int(offset_id), MTP_int(0), MTP_int(offset), MTP_int(loadCount), MTP_int(0), MTP_int(0)), rpcDone(&HistoryWidget::messagesReceived, from), rpcFail(&HistoryWidget::messagesFailed));
	}
}

void HistoryWidget::onListScroll() {
	App::checkImageCacheSize();
	if (_firstLoadRequest || _scroll.isHidden() || !_peer) return;

	updateToEndVisibility();
	updateCollapseCommentsVisibility();

	int st = _scroll.scrollTop(), stm = _scroll.scrollTopMax(), sh = _scroll.height();
	if (st + PreloadHeightsCount * sh > stm) {
		loadMessagesDown();
	}

	if (st < PreloadHeightsCount * sh) {
		loadMessages();
	}

	while (_replyReturn) {
		bool below = (_replyReturn->detached() && _replyReturn->history() == _history && !_history->isEmpty() && _replyReturn->id < _history->blocks.back()->items.back()->id);
		if (!below) below = (_replyReturn->detached() && _replyReturn->history() == _migrated && !_history->isEmpty());
		if (!below) below = (_replyReturn->detached() && _migrated && _replyReturn->history() == _migrated && !_migrated->isEmpty() && _replyReturn->id < _migrated->blocks.back()->items.back()->id);
		if (!below && !_replyReturn->detached()) below = (st >= stm) || (_list->itemTop(_replyReturn) < st + sh / 2);
		if (below) {
			calcNextReplyReturn();
		} else {
			break;
		}
	}

	if (st != _lastScroll) {
		_lastScrolled = getms();
		_lastScroll = st;
	}
}

void HistoryWidget::onVisibleChanged() {
	QTimer::singleShot(0, this, SLOT(onListScroll()));
}

void HistoryWidget::onHistoryToEnd() {
	if (_replyReturn && _replyReturn->history() == _history) {
		showHistory(_peer->id, _replyReturn->id);
	} else if (_replyReturn && _replyReturn->history() == _migrated) {
		showHistory(_peer->id, -_replyReturn->id);
	} else if (_peer) {
		showHistory(_peer->id, ShowAtUnreadMsgId);
	}
}

void HistoryWidget::onCollapseComments() {
	if (!_peer) return;

	MsgId switchAt = SwitchAtTopMsgId;
	bool collapseCommentsVisible = !_a_show.animating() && _history && !_firstLoadRequest && _history->isChannel() && !_history->asChannelHistory()->onlyImportant();
	if (collapseCommentsVisible) {
		if (HistoryItem *collapse = _history->asChannelHistory()->collapse()) {
			if (!collapse->detached()) {
				int32 collapseY = _list->itemTop(collapse) - _scroll.scrollTop();
				if (collapseY >= 0 && collapseY < _scroll.height()) {
					switchAt = collapse->id;
				}
			}
		}
	}
	showHistory(_peer->id, switchAt);
}

void HistoryWidget::saveEditMsg() {
	if (_saveEditMsgRequestId) return;

	WebPageId webPageId = _previewCancelled ? CancelledWebPageId : ((_previewData && _previewData->pendingTill >= 0) ? _previewData->id : 0);

	EntitiesInText sendingEntities, leftEntities;
	QString sendingText, leftText = prepareTextWithEntities(_field.getLastText(), leftEntities, itemTextOptions(_history, App::self()).flags);

	if (!textSplit(sendingText, sendingEntities, leftText, leftEntities, MaxMessageSize)) {
		_field.selectAll();
		_field.setFocus();
		return;
	} else if (!leftText.isEmpty()) {
		Ui::showLayer(new InformBox(lang(lng_edit_too_long)));
		return;
	}

	int32 sendFlags = 0;
	if (webPageId == CancelledWebPageId) {
		sendFlags |= MTPmessages_SendMessage::flag_no_webpage;
	}
	MTPVector<MTPMessageEntity> localEntities = linksToMTP(sendingEntities), sentEntities = linksToMTP(sendingEntities, true);
	if (!sentEntities.c_vector().v.isEmpty()) {
		sendFlags |= MTPmessages_SendMessage::flag_entities;
	}
	_saveEditMsgRequestId = MTP::send(MTPchannels_EditMessage(MTP_int(sendFlags), _history->peer->asChannel()->inputChannel, MTP_int(_editMsgId), MTP_string(sendingText), sentEntities), rpcDone(&HistoryWidget::saveEditMsgDone, _history), rpcFail(&HistoryWidget::saveEditMsgFail, _history));
}

void HistoryWidget::saveEditMsgDone(History *history, const MTPUpdates &updates, mtpRequestId req) {
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
	if (req == _saveEditMsgRequestId) {
		_saveEditMsgRequestId = 0;
		cancelEdit();
	}
	if (history->editDraft && history->editDraft->saveRequest == req) {
		history->setEditDraft(nullptr);
		writeDrafts(history);
	}
}

bool HistoryWidget::saveEditMsgFail(History *history, const RPCError &error, mtpRequestId req) {
	if (mtpIsFlood(error)) return false;
	if (req == _saveEditMsgRequestId) {
		_saveEditMsgRequestId = 0;
	}
	if (history->editDraft && history->editDraft->saveRequest == req) {
		history->editDraft->saveRequest = 0;
	}

	QString err = error.type();
	if (err == qstr("MESSAGE_ID_INVALID") || err == qstr("CHAT_ADMIN_REQUIRED") || err == qstr("MESSAGE_EDIT_TIME_EXPIRED")) {
		Ui::showLayer(new InformBox(lang(lng_edit_error)));
	} else if (err == qstr("MESSAGE_NOT_MODIFIED")) {
		cancelEdit();
	} else if (err == qstr("MESSAGE_EMPTY")) {
		_field.selectAll();
		_field.setFocus();
	} else {
		Ui::showLayer(new InformBox(lang(lng_edit_error)));
	}
	update();
	return true;
}

void HistoryWidget::onSend(bool ctrlShiftEnter, MsgId replyTo) {
	if (!_history) return;

	if (_editMsgId) {
		saveEditMsg();
		return;
	}

	bool lastKeyboardUsed = lastForceReplyReplied(FullMsgId(_channel, replyTo));

	WebPageId webPageId = _previewCancelled ? CancelledWebPageId : ((_previewData && _previewData->pendingTill >= 0) ? _previewData->id : 0);

	App::main()->sendMessage(_history, _field.getLastText(), replyTo, _broadcast.checked(), _silent.checked(), webPageId);

	setFieldText(QString());
	_saveDraftText = true;
	_saveDraftStart = getms();
	onDraftSave();

	if (!_attachMention.isHidden()) _attachMention.hideStart();
	if (!_attachType.isHidden()) _attachType.hideStart();
	if (!_emojiPan.isHidden()) _emojiPan.hideStart();

	if (replyTo < 0) cancelReply(lastKeyboardUsed);
	if (_previewData && _previewData->pendingTill) previewCancel();
	_field.setFocus();

	if (!_keyboard.hasMarkup() && _keyboard.forceReply() && !_kbReplyTo) onKbToggle();
}

void HistoryWidget::onUnblock() {
	if (_unblockRequest) return;
	if (!_peer || !_peer->isUser() || _peer->asUser()->blocked != UserIsBlocked) {
		updateControlsVisibility();
		return;
	}

	_unblockRequest = MTP::send(MTPcontacts_Unblock(_peer->asUser()->inputUser), rpcDone(&HistoryWidget::unblockDone, _peer), rpcFail(&HistoryWidget::unblockFail));
}

void HistoryWidget::unblockDone(PeerData *peer, const MTPBool &result, mtpRequestId req) {
	if (!peer->isUser()) return;
	if (_unblockRequest == req) _unblockRequest = 0;
	peer->asUser()->blocked = UserIsNotBlocked;
	emit App::main()->peerUpdated(peer);
}

bool HistoryWidget::unblockFail(const RPCError &error, mtpRequestId req) {
	if (mtpIsFlood(error)) return false;

	if (_unblockRequest == req) _unblockRequest = 0;
	return false;
}

void HistoryWidget::blockDone(PeerData *peer, const MTPBool &result) {
	if (!peer->isUser()) return;

	peer->asUser()->blocked = UserIsBlocked;
	emit App::main()->peerUpdated(peer);
}

void HistoryWidget::onBotStart() {
	if (!_peer || !_peer->isUser() || !_peer->asUser()->botInfo || !_canSendMessages) {
		updateControlsVisibility();
		return;
	}

	QString token = _peer->asUser()->botInfo->startToken;
	if (token.isEmpty()) {
		sendBotCommand(qsl("/start"), 0);
	} else {
		uint64 randomId = MTP::nonce<uint64>();
		MTP::send(MTPmessages_StartBot(_peer->asUser()->inputUser, MTP_inputPeerEmpty(), MTP_long(randomId), MTP_string(token)), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::addParticipantFail, _peer->asUser()));

		_peer->asUser()->botInfo->startToken = QString();
		if (_keyboard.hasMarkup()) {
			if (_keyboard.singleUse() && _keyboard.forMsgId() == FullMsgId(_channel, _history->lastKeyboardId) && _history->lastKeyboardUsed) {
				_history->lastKeyboardHiddenId = _history->lastKeyboardId;
			}
			if (!kbWasHidden()) _kbShown = _keyboard.hasMarkup();
		}
	}
	updateControlsVisibility();
	resizeEvent(0);
}

void HistoryWidget::onJoinChannel() {
	if (_unblockRequest) return;
	if (!_peer || !_peer->isChannel() || !isJoinChannel()) {
		updateControlsVisibility();
		return;
	}

	_unblockRequest = MTP::send(MTPchannels_JoinChannel(_peer->asChannel()->inputChannel), rpcDone(&HistoryWidget::joinDone), rpcFail(&HistoryWidget::joinFail));
}

void HistoryWidget::joinDone(const MTPUpdates &result, mtpRequestId req) {
	if (_unblockRequest == req) _unblockRequest = 0;
	if (App::main()) App::main()->sentUpdatesReceived(result);
}

bool HistoryWidget::joinFail(const RPCError &error, mtpRequestId req) {
	if (mtpIsFlood(error)) return false;

	if (_unblockRequest == req) _unblockRequest = 0;
	if (error.type() == qstr("CHANNEL_PRIVATE") || error.type() == qstr("CHANNEL_PUBLIC_GROUP_NA") || error.type() == qstr("USER_BANNED_IN_CHANNEL")) {
		Ui::showLayer(new InformBox(lang((_peer && _peer->isMegagroup()) ? lng_group_not_accessible : lng_channel_not_accessible)));
		return true;
	}
	return false;
}

void HistoryWidget::onMuteUnmute() {
	App::main()->updateNotifySetting(_peer, _history->mute ? NotifySettingSetNotify : NotifySettingSetMuted);
}

void HistoryWidget::onBroadcastSilentChange() {
	updateFieldPlaceholder();
}

void HistoryWidget::onShareContact(const PeerId &peer, UserData *contact) {
	if (!contact || contact->phone.isEmpty()) return;

	Ui::showPeerHistory(peer, ShowAtTheEndMsgId);
	if (!_history) return;

	shareContact(peer, contact->phone, contact->firstName, contact->lastName, replyToId(), peerToUser(contact->id));
}

void HistoryWidget::shareContact(const PeerId &peer, const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, int32 userId) {
	History *h = App::history(peer);

	uint64 randomId = MTP::nonce<uint64>();
	FullMsgId newId(peerToChannel(peer), clientMsgId());

	App::main()->readServerHistory(h, false);
	fastShowAtEnd(h);

	PeerData *p = App::peer(peer);
	int32 flags = newMessageFlags(p) | MTPDmessage::flag_media; // unread, out

	bool lastKeyboardUsed = lastForceReplyReplied(FullMsgId(peerToChannel(peer), replyTo));

	int32 sendFlags = 0;
	if (replyTo) {
		flags |= MTPDmessage::flag_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
	}

	bool channelPost = p->isChannel() && !p->isMegagroup() && p->asChannel()->canPublish() && (p->asChannel()->isBroadcast() || _broadcast.checked());
	bool showFromName = !channelPost || p->asChannel()->addsSignature();
	bool silentPost = channelPost && _silent.checked();
	if (channelPost) {
		sendFlags |= MTPmessages_SendMedia::flag_broadcast;
		flags |= MTPDmessage::flag_views;
		flags |= MTPDmessage::flag_post;
	}
	if (showFromName) {
		flags |= MTPDmessage::flag_from_id;
	}
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::flag_silent;
	}
	h->addNewMessage(MTP_message(MTP_int(flags), MTP_int(newId.msg), MTP_int(showFromName ? MTP::authedId() : 0), peerToMTP(peer), MTPnullFwdHeader, MTPint(), MTP_int(replyToId()), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaContact(MTP_string(phone), MTP_string(fname), MTP_string(lname), MTP_int(userId)), MTPnullMarkup, MTPnullEntities, MTP_int(1), MTPint()), NewMessageUnread);
	h->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), p->input, MTP_int(replyTo), MTP_inputMediaContact(MTP_string(phone), MTP_string(fname), MTP_string(lname)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, h->sendRequestId);

	App::historyRegRandom(randomId, newId);

	App::main()->finishForwarding(h, _broadcast.checked(), _silent.checked());
	cancelReply(lastKeyboardUsed);
}

void HistoryWidget::onSendPaths(const PeerId &peer) {
	Ui::showPeerHistory(peer, ShowAtTheEndMsgId);
	if (!_history) return;

	if (cSendPaths().size() == 1) {
		uploadFile(cSendPaths().at(0), PrepareAuto);
	} else {
		uploadFiles(cSendPaths(), PrepareDocument);
	}
}

History *HistoryWidget::history() const {
	return _history;
}

PeerData *HistoryWidget::peer() const {
	return _peer;
}

void HistoryWidget::setMsgId(MsgId showAtMsgId) { // sometimes _showAtMsgId is set directly
	if (_showAtMsgId != showAtMsgId) {
		MsgId wasMsgId = _showAtMsgId;
		_showAtMsgId = showAtMsgId;
		App::main()->dlgUpdated(_history, wasMsgId);
		emit historyShown(_history, _showAtMsgId);
	}
}

MsgId HistoryWidget::msgId() const {
	return _showAtMsgId;
}

HistoryItem *HistoryWidget::atTopImportantMsg(int32 &bottomUnderScrollTop) const {
	if (!_list || !_history->isChannel()) {
		bottomUnderScrollTop = 0;
		return 0;
	}
	return _list->atTopImportantMsg(_scroll.scrollTop(), _scroll.height(), bottomUnderScrollTop);
}

void HistoryWidget::animShow(const QPixmap &bgAnimCache, const QPixmap &bgAnimTopBarCache, bool back) {
	if (App::app()) App::app()->mtpPause();

	(back ? _cacheOver : _cacheUnder) = bgAnimCache;
	(back ? _cacheTopBarOver : _cacheTopBarUnder) = bgAnimTopBarCache;
	(back ? _cacheUnder : _cacheOver) = myGrab(this);
	App::main()->topBar()->stopAnim();
	(back ? _cacheTopBarUnder : _cacheTopBarOver) = myGrab(App::main()->topBar());
	App::main()->topBar()->startAnim();

	_scroll.hide();
	_kbScroll.hide();
	_reportSpamPanel.hide();
	_toHistoryEnd.hide();
	_collapseComments.hide();
	_attachDocument.hide();
	_attachPhoto.hide();
	_attachEmoji.hide();
	_attachMention.hide();
	_broadcast.hide();
	_silent.hide();
	_kbShow.hide();
	_kbHide.hide();
	_cmdStart.hide();
	_field.hide();
	_fieldBarCancel.hide();
	_send.hide();
	_unblock.hide();
	_botStart.hide();
	_joinChannel.hide();
	_muteUnmute.hide();
	_topShadow.hide();
	if (_pinnedBar) {
		_pinnedBar->shadow.hide();
		_pinnedBar->cancel.hide();
	}

	a_coordUnder = back ? anim::ivalue(-qFloor(st::slideShift * width()), 0) : anim::ivalue(0, -qFloor(st::slideShift * width()));
	a_coordOver = back ? anim::ivalue(0, width()) : anim::ivalue(width(), 0);
	a_shadow = back ? anim::fvalue(1, 0) : anim::fvalue(0, 1);
	_a_show.start();

	App::main()->topBar()->update();
	activate();
}

void HistoryWidget::step_show(float64 ms, bool timer) {
	float64 dt = ms / st::slideDuration;
	if (dt >= 1) {
		_a_show.stop();
		_sideShadow.setVisible(!Adaptive::OneColumn());
		_topShadow.setVisible(_peer ? true : false);

		a_coordUnder.finish();
		a_coordOver.finish();
		a_shadow.finish();
		_cacheUnder = _cacheOver = _cacheTopBarUnder = _cacheTopBarOver = QPixmap();
		App::main()->topBar()->stopAnim();
		doneShow();

		if (App::app()) App::app()->mtpUnpause();
	} else {
		a_coordUnder.update(dt, st::slideFunction);
		a_coordOver.update(dt, st::slideFunction);
		a_shadow.update(dt, st::slideFunction);
	}
	if (timer) {
		update();
		App::main()->topBar()->update();
	}
}

void HistoryWidget::doneShow() {
	updateReportSpamStatus();
	updateBotKeyboard();
	updateControlsVisibility();
	updateListSize(true);
	onListScroll();
	if (App::wnd()) {
		App::wnd()->checkHistoryActivation();
		App::wnd()->setInnerFocus();
	}
}

void HistoryWidget::updateAdaptiveLayout() {
	_sideShadow.setVisible(!Adaptive::OneColumn());
	update();
}

void HistoryWidget::animStop() {
	if (!_a_show.animating()) return;
	_a_show.stop();
	_sideShadow.setVisible(!Adaptive::OneColumn());
	_topShadow.setVisible(_peer ? true : false);
}

void HistoryWidget::step_record(float64 ms, bool timer) {
	float64 dt = ms / st::btnSend.duration;
	if (dt >= 1 || !_send.isHidden() || isBotStart() || isBlocked()) {
		_a_record.stop();
		a_recordOver.finish();
		a_recordDown.finish();
		a_recordCancel.finish();
	} else {
		a_recordOver.update(dt, anim::linear);
		a_recordDown.update(dt, anim::linear);
		a_recordCancel.update(dt, anim::linear);
	}
	if (timer) {
		if (_recording) {
			updateField();
		} else {
			update(_send.geometry());
		}
	}
}

void HistoryWidget::step_recording(float64 ms, bool timer) {
	float64 dt = ms / AudioVoiceMsgUpdateView;
	if (dt >= 1) {
		_a_recording.stop();
		a_recordingLevel.finish();
	} else {
		a_recordingLevel.update(dt, anim::linear);
	}
	if (timer) update(_attachDocument.geometry());
}

void HistoryWidget::onPhotoSelect() {
	if (!_history) return;

	_attachDocument.clearState();
	_attachDocument.hide();
	_attachPhoto.show();
	_attachType.fastHide();

	if (cDefaultAttach() != dbidaPhoto) {
		cSetDefaultAttach(dbidaPhoto);
		Local::writeUserSettings();
	}

	QStringList photoExtensions(cPhotoExtensions());
	QStringList imgExtensions(cImgExtensions());
	QString filter(qsl("Image files (*") + imgExtensions.join(qsl(" *")) + qsl(");;Photo files (*") + photoExtensions.join(qsl(" *")) + qsl(");;All files (*.*)"));

	QStringList files;
	QByteArray content;
	if (filedialogGetOpenFiles(files, content, lang(lng_choose_images), filter)) {
		if (!content.isEmpty()) {
			uploadFileContent(content, PreparePhoto);
		} else {
			uploadFiles(files, PreparePhoto);
		}
	}
}

void HistoryWidget::onDocumentSelect() {
	if (!_history) return;

	_attachPhoto.clearState();
	_attachPhoto.hide();
	_attachDocument.show();
	_attachType.fastHide();

	if (cDefaultAttach() != dbidaDocument) {
		cSetDefaultAttach(dbidaDocument);
		Local::writeUserSettings();
	}

	QStringList photoExtensions(cPhotoExtensions());
	QStringList imgExtensions(cImgExtensions());
	QString filter(qsl("All files (*.*);;Image files (*") + imgExtensions.join(qsl(" *")) + qsl(");;Photo files (*") + photoExtensions.join(qsl(" *")) + qsl(")"));

	QStringList files;
	QByteArray content;
	if (filedialogGetOpenFiles(files, content, lang(lng_choose_images), filter)) {
		if (!content.isEmpty()) {
			uploadFileContent(content, PrepareDocument);
		} else {
			uploadFiles(files, PrepareDocument);
		}
	}
}

void HistoryWidget::dragEnterEvent(QDragEnterEvent *e) {
	if (!_history) return;

	if (_peer && !_canSendMessages) return;

	_attachDrag = getDragState(e->mimeData());
	updateDragAreas();

	if (_attachDrag) {
		e->setDropAction(Qt::IgnoreAction);
		e->accept();
	}
}

void HistoryWidget::dragLeaveEvent(QDragLeaveEvent *e) {
	if (_attachDrag != DragStateNone || !_attachDragPhoto.isHidden() || !_attachDragDocument.isHidden()) {
		_attachDrag = DragStateNone;
		updateDragAreas();
	}
}

void HistoryWidget::leaveEvent(QEvent *e) {
	if (_attachDrag != DragStateNone || !_attachDragPhoto.isHidden() || !_attachDragDocument.isHidden()) {
		_attachDrag = DragStateNone;
		updateDragAreas();
	}
	if (hasMouseTracking()) mouseMoveEvent(0);
}

void HistoryWidget::mouseMoveEvent(QMouseEvent *e) {
	QPoint pos(e ? e->pos() : mapFromGlobal(QCursor::pos()));
	bool inRecord = _send.geometry().contains(pos);
	bool inField = pos.y() >= (_scroll.y() + _scroll.height()) && pos.y() < height() && pos.x() >= 0 && pos.x() < width();
	bool inReplyEdit = QRect(st::replySkip, _field.y() - st::sendPadding - st::replyHeight, width() - st::replySkip - _fieldBarCancel.width(), st::replyHeight).contains(pos) && (_editMsgId || replyToId());
	bool inPinnedMsg = QRect(0, 0, width(), st::replyHeight).contains(pos) && _pinnedBar;
	bool startAnim = false;
	if (inRecord != _inRecord) {
		_inRecord = inRecord;
		a_recordOver.start(_inRecord ? 1 : 0);
		a_recordDown.restart();
		a_recordCancel.restart();
		startAnim = true;
	}
	if (inField != _inField && _recording) {
		_inField = inField;
		a_recordOver.restart();
		a_recordDown.start(_inField ? 1 : 0);
		a_recordCancel.start(_inField ? st::recordCancel->c : st::recordCancelActive->c);
		startAnim = true;
	}
	if (inReplyEdit != _inReplyEdit) {
		_inReplyEdit = inReplyEdit;
		setCursor(inReplyEdit ? style::cur_pointer : style::cur_default);
	}
	if (inPinnedMsg != _inPinnedMsg) {
		_inPinnedMsg = inPinnedMsg;
		setCursor(inPinnedMsg ? style::cur_pointer : style::cur_default);
	}
	if (startAnim) _a_record.start();
}

void HistoryWidget::leaveToChildEvent(QEvent *e) { // e -- from enterEvent() of child TWidget
	if (hasMouseTracking()) mouseMoveEvent(0);
}

void HistoryWidget::mouseReleaseEvent(QMouseEvent *e) {
	if (_replyForwardPressed) {
		_replyForwardPressed = false;
		update(0, _field.y() - st::sendPadding - st::replyHeight, width(), st::replyHeight);
	}
	if (_attachDrag != DragStateNone || !_attachDragPhoto.isHidden() || !_attachDragDocument.isHidden()) {
		_attachDrag = DragStateNone;
		updateDragAreas();
	}
	if (_recording && cHasAudioCapture()) {
		stopRecording(_peer && _inField);
	}
}

void HistoryWidget::stopRecording(bool send) {
	audioCapture()->stop(send);

	a_recordingLevel = anim::ivalue(0, 0);
	_a_recording.stop();

	_recording = false;
	_recordingSamples = 0;
	if (_peer && (!_peer->isChannel() || _peer->isMegagroup() || !_peer->asChannel()->canPublish() || (!_peer->asChannel()->isBroadcast() && !_broadcast.checked()))) {
		updateSendAction(_history, SendActionRecordVoice, -1);
	}

	updateControlsVisibility();
	activate();

	updateField();

	a_recordDown.start(0);
	a_recordOver.restart();
	a_recordCancel = anim::cvalue(st::recordCancel->c, st::recordCancel->c);
	_a_record.start();
}

void HistoryWidget::sendBotCommand(const QString &cmd, MsgId replyTo) { // replyTo != 0 from ReplyKeyboardMarkup, == 0 from cmd links
	if (!_history) return;

	bool lastKeyboardUsed = (_keyboard.forMsgId() == FullMsgId(_channel, _history->lastKeyboardId)) && (_keyboard.forMsgId() == FullMsgId(_channel, replyTo));

	QString toSend = cmd;
	PeerData *bot = _peer->isUser() ? _peer : (App::hoveredLinkItem() ? App::hoveredLinkItem()->fromOriginal() : 0);
	if (bot && (!bot->isUser() || !bot->asUser()->botInfo)) bot = 0;
	QString username = bot ? bot->asUser()->username : QString();
	int32 botStatus = _peer->isChat() ? _peer->asChat()->botStatus : (_peer->isMegagroup() ? _peer->asChannel()->mgInfo->botStatus : -1);
	if (!replyTo && toSend.indexOf('@') < 2 && !username.isEmpty() && (botStatus == 0 || botStatus == 2)) {
		toSend += '@' + username;
	}

	App::main()->sendMessage(_history, toSend, replyTo ? ((!_peer->isUser()/* && (botStatus == 0 || botStatus == 2)*/) ? replyTo : -1) : 0, false, false);
	if (replyTo) {
		cancelReply();
		if (_keyboard.singleUse() && _keyboard.hasMarkup() && lastKeyboardUsed) {
			if (_kbShown) onKbToggle(false);
			_history->lastKeyboardUsed = true;
		}
	}

	_field.setFocus();
}

bool HistoryWidget::insertBotCommand(const QString &cmd, bool specialGif) {
	if (!_history) return false;

	QString toInsert = cmd;
	if (!toInsert.isEmpty() && toInsert.at(0) != '@') {
		PeerData *bot = _peer->isUser() ? _peer : (App::hoveredLinkItem() ? App::hoveredLinkItem()->fromOriginal() : 0);
		if (!bot->isUser() || !bot->asUser()->botInfo) bot = 0;
		QString username = bot ? bot->asUser()->username : QString();
		int32 botStatus = _peer->isChat() ? _peer->asChat()->botStatus : (_peer->isMegagroup() ? _peer->asChannel()->mgInfo->botStatus : -1);
		if (toInsert.indexOf('@') < 0 && !username.isEmpty() && (botStatus == 0 || botStatus == 2)) {
			toInsert += '@' + username;
		}
	}
	toInsert += ' ';

	if (toInsert.at(0) != '@') {
		QString text = _field.getLastText();
		if (specialGif) {
			if (text.trimmed() == '@' + cInlineGifBotUsername() && text.at(0) == '@') {
				setFieldText(QString(), TextUpdateEventsSaveDraft, false);
			}
		} else {
			QRegularExpressionMatch m = QRegularExpression(qsl("^/[A-Za-z_0-9]{0,64}(@[A-Za-z_0-9]{0,32})?(\\s|$)")).match(text);
			if (m.hasMatch()) {
				text = toInsert + text.mid(m.capturedLength());
			} else {
				text = toInsert + text;
			}
			_field.setTextFast(text);

			QTextCursor cur(_field.textCursor());
			cur.movePosition(QTextCursor::End);
			_field.setTextCursor(cur);
		}
	} else {
		if (!specialGif || _field.getLastText().isEmpty()) {
			setFieldText(toInsert, TextUpdateEventsSaveDraft, false);
			_field.setFocus();
			return true;
		}
	}
	return false;
}

bool HistoryWidget::eventFilter(QObject *obj, QEvent *e) {
	if ((obj == &_toHistoryEnd || obj == &_collapseComments) && e->type() == QEvent::Wheel) {
		return _scroll.viewportEvent(e);
	}
	return TWidget::eventFilter(obj, e);
}

DragState HistoryWidget::getDragState(const QMimeData *d) {
	if (!d || d->hasFormat(qsl("application/x-td-forward-pressed-link"))) return DragStateNone;

	if (d->hasImage()) return DragStateImage;

	QString uriListFormat(qsl("text/uri-list"));
	if (!d->hasFormat(uriListFormat)) return DragStateNone;

	QStringList imgExtensions(cImgExtensions()), files;

	const QList<QUrl> &urls(d->urls());
	if (urls.isEmpty()) return DragStateNone;

	bool allAreSmallImages = true;
	for (QList<QUrl>::const_iterator i = urls.cbegin(), en = urls.cend(); i != en; ++i) {
		if (!i->isLocalFile()) return DragStateNone;

		QString file(i->toLocalFile());
		if (file.startsWith(qsl("/.file/id="))) file = psConvertFileUrl(file);

		QFileInfo info(file);
		if (info.isDir()) return DragStateNone;

		quint64 s = info.size();
		if (s >= MaxUploadDocumentSize) {
			return DragStateNone;
		}
		if (allAreSmallImages) {
			if (s >= MaxUploadPhotoSize) {
				allAreSmallImages = false;
			} else {
				bool foundImageExtension = false;
				for (QStringList::const_iterator j = imgExtensions.cbegin(), end = imgExtensions.cend(); j != end; ++j) {
					if (file.right(j->size()).toLower() == (*j).toLower()) {
						foundImageExtension = true;
						break;
					}
				}
				if (!foundImageExtension) {
					allAreSmallImages = false;
				}
			}
		}
	}
	return allAreSmallImages ? DragStatePhotoFiles : DragStateFiles;
}

void HistoryWidget::updateDragAreas() {
	_field.setAcceptDrops(!_attachDrag);
	switch (_attachDrag) {
	case DragStateNone:
		_attachDragDocument.otherLeave();
		_attachDragPhoto.otherLeave();
	break;
	case DragStateFiles:
		_attachDragDocument.otherEnter();
		_attachDragDocument.setText(lang(lng_drag_files_here), lang(lng_drag_to_send_files));
		_attachDragPhoto.fastHide();
	break;
	case DragStatePhotoFiles:
		_attachDragDocument.otherEnter();
		_attachDragDocument.setText(lang(lng_drag_images_here), lang(lng_drag_to_send_no_compression));
		_attachDragPhoto.otherEnter();
		_attachDragPhoto.setText(lang(lng_drag_photos_here), lang(lng_drag_to_send_quick));
	break;
	case DragStateImage:
		_attachDragDocument.fastHide();
		_attachDragPhoto.otherEnter();
		_attachDragPhoto.setText(lang(lng_drag_images_here), lang(lng_drag_to_send_quick));
	break;
	};
	resizeEvent(0);
}

bool HistoryWidget::canSendMessages(PeerData *peer) const {
	return peer && peer->canWrite();
}

bool HistoryWidget::readyToForward() const {
	return _canSendMessages && App::main()->hasForwardingItems();
}

bool HistoryWidget::hasBroadcastToggle() const {
	return _peer && _peer->isChannel() && !_peer->isMegagroup() && _peer->asChannel()->canPublish() && !_peer->asChannel()->isBroadcast();
}

bool HistoryWidget::hasSilentToggle() const {
	return _peer && _peer->isChannel() && !_peer->isMegagroup() && _peer->asChannel()->canPublish() && _peer->notify != UnknownNotifySettings;
}

void HistoryWidget::inlineBotResolveDone(const MTPcontacts_ResolvedPeer &result) {
	_inlineBotResolveRequestId = 0;
//	Notify::inlineBotRequesting(false);
	_inlineBotUsername = QString();
	if (result.type() == mtpc_contacts_resolvedPeer) {
		const MTPDcontacts_resolvedPeer &d(result.c_contacts_resolvedPeer());
		App::feedUsers(d.vusers);
		App::feedChats(d.vchats);
	}
	updateInlineBotQuery();
}

bool HistoryWidget::inlineBotResolveFail(QString name, const RPCError &error) {
	if (mtpIsFlood(error)) return false;

	_inlineBotResolveRequestId = 0;
//	Notify::inlineBotRequesting(false);
	if (name == _inlineBotUsername) {
		_inlineBot = 0;
		onCheckMentionDropdown();
	}
	return true;
}

bool HistoryWidget::isBotStart() const {
	if (!_peer || !_peer->isUser() || !_peer->asUser()->botInfo || !_canSendMessages) return false;
	return !_peer->asUser()->botInfo->startToken.isEmpty() || (_history->isEmpty() && !_history->lastMsg);
}

bool HistoryWidget::isBlocked() const {
	return _peer && _peer->isUser() && _peer->asUser()->blocked == UserIsBlocked;
}

bool HistoryWidget::isJoinChannel() const {
	return _peer && _peer->isChannel() && !_peer->asChannel()->amIn();
}

bool HistoryWidget::isMuteUnmute() const {
	return _peer && _peer->isChannel() && _peer->asChannel()->isBroadcast() && !_peer->asChannel()->canPublish();
}

bool HistoryWidget::updateCmdStartShown() {
	bool cmdStartShown = false;
	if (_history && _peer && ((_peer->isChat() && _peer->asChat()->botStatus > 0) || (_peer->isMegagroup() && _peer->asChannel()->mgInfo->botStatus > 0) || (_peer->isUser() && _peer->asUser()->botInfo))) {
		if (!isBotStart() && !isBlocked() && !_keyboard.hasMarkup() && !_keyboard.forceReply()) {
			if (!_field.hasSendText()) {
				cmdStartShown = true;
			}
		}
	}
	if (_cmdStartShown != cmdStartShown) {
		_cmdStartShown = cmdStartShown;
		return true;
	}
	return false;
}

bool HistoryWidget::kbWasHidden() const {
	return _history && (_keyboard.forMsgId() == FullMsgId(_history->channelId(), _history->lastKeyboardHiddenId));
}

void HistoryWidget::dropEvent(QDropEvent *e) {
	_attachDrag = DragStateNone;
	updateDragAreas();
	e->acceptProposedAction();
}

void HistoryWidget::onPhotoDrop(const QMimeData *data) {
	if (!_history) return;

	QStringList files = getMediasFromMime(data);
	if (files.isEmpty()) {
		if (data->hasImage()) {
			QImage image = qvariant_cast<QImage>(data->imageData());
			if (image.isNull()) return;

			uploadImage(image, PreparePhoto, FileLoadNoForceConfirm, data->text());
		}
		return;
	}

	uploadFiles(files, PreparePhoto);
}

void HistoryWidget::onDocumentDrop(const QMimeData *data) {
	if (!_history) return;

	if (_peer && !_canSendMessages) return;

	QStringList files = getMediasFromMime(data);
	if (files.isEmpty()) return;

	uploadFiles(files, PrepareDocument);
}

void HistoryWidget::onFilesDrop(const QMimeData *data) {

	if (_peer && !_canSendMessages) return;

	QStringList files = getMediasFromMime(data);
	if (files.isEmpty()) {
		if (data->hasImage()) {
			QImage image = qvariant_cast<QImage>(data->imageData());
			if (image.isNull()) return;

			uploadImage(image, PrepareAuto, FileLoadNoForceConfirm, data->text());
		}
		return;
	}

	if (files.size() == 1 && !QFileInfo(files.at(0)).isDir()) {
		uploadFile(files.at(0), PrepareAuto);
	}
//  uploadFiles(files, PrepareAuto); // multiple confirm with "compressed" checkbox
}

void HistoryWidget::onKbToggle(bool manual) {
	if (_kbShown || _kbReplyTo) {
		_kbHide.hide();
		if (_kbShown) {
			_kbShow.show();
			if (manual && _history) {
				_history->lastKeyboardHiddenId = _keyboard.forMsgId().msg;
			}

			_kbScroll.hide();
			_kbShown = false;

			_field.setMaxHeight(st::maxFieldHeight);

			_kbReplyTo = 0;
			if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_editMsgId && !_replyToId) {
				_fieldBarCancel.hide();
				updateMouseTracking();
			}
		} else {
			if (_history) {
				_history->clearLastKeyboard();
			}
			updateBotKeyboard();
		}
	} else if (!_keyboard.hasMarkup() && _keyboard.forceReply()) {
		_kbHide.hide();
		_kbShow.hide();
		_cmdStart.show();
		_kbScroll.hide();
		_kbShown = false;

		_field.setMaxHeight(st::maxFieldHeight);

		_kbReplyTo = (_peer->isChat() || _peer->isChannel() || _keyboard.forceReply()) ? App::histItemById(_keyboard.forMsgId()) : 0;
		if (_kbReplyTo && !_editMsgId && !_replyToId) {
			updateReplyToName();
			_replyEditMsgText.setText(st::msgFont, _kbReplyTo->inDialogsText(), _textDlgOptions);
			_fieldBarCancel.show();
			updateMouseTracking();
		}
		if (manual && _history) {
			_history->lastKeyboardHiddenId = 0;
		}
	} else {
		_kbHide.show();
		_kbShow.hide();
		_kbScroll.show();
		_kbShown = true;

		int32 maxh = qMin(_keyboard.height(), int(st::maxFieldHeight) - (int(st::maxFieldHeight) / 2));
		_field.setMaxHeight(st::maxFieldHeight - maxh);

		_kbReplyTo = (_peer->isChat() || _peer->isChannel() || _keyboard.forceReply()) ? App::histItemById(_keyboard.forMsgId()) : 0;
		if (_kbReplyTo && !_editMsgId && !_replyToId) {
			updateReplyToName();
			_replyEditMsgText.setText(st::msgFont, _kbReplyTo->inDialogsText(), _textDlgOptions);
			_fieldBarCancel.show();
			updateMouseTracking();
		}
		if (manual && _history) {
			_history->lastKeyboardHiddenId = 0;
		}
	}
	resizeEvent(0);
	if (_kbHide.isHidden()) {
		_attachEmoji.show();
	} else {
		_attachEmoji.hide();
	}
	updateField();
}

void HistoryWidget::onCmdStart() {
	setFieldText(qsl("/"));
	_field.moveCursor(QTextCursor::End);
}

void HistoryWidget::contextMenuEvent(QContextMenuEvent *e) {
	if (!_list) return;

	return _list->showContextMenu(e);
}

void HistoryWidget::deleteMessage() {
	HistoryItem *item = App::contextItem();
	if (!item || item->type() != HistoryItemMsg) return;

	HistoryMessage *msg = dynamic_cast<HistoryMessage*>(item);
	App::main()->deleteLayer((msg && msg->uploading()) ? -2 : -1);
}

void HistoryWidget::forwardMessage() {
	HistoryItem *item = App::contextItem();
	if (!item || item->type() != HistoryItemMsg || item->serviceMsg()) return;

	App::main()->forwardLayer();
}

void HistoryWidget::selectMessage() {
	HistoryItem *item = App::contextItem();
	if (!item || item->type() != HistoryItemMsg || item->serviceMsg()) return;

	if (_list) _list->selectItem(item);
}

void HistoryWidget::onForwardHere() {
	HistoryItem *item = App::contextItem();
	if (!item || item->type() != HistoryItemMsg || item->serviceMsg()) return;

	App::forward(_peer->id, ForwardContextMessage);
}

void HistoryWidget::paintTopBar(QPainter &p, float64 over, int32 decreaseWidth) {
	if (_a_show.animating()) {
		p.drawPixmap(a_coordUnder.current(), 0, _cacheTopBarUnder);
		p.drawPixmap(a_coordOver.current(), 0, _cacheTopBarOver);
		p.setOpacity(a_shadow.current());
		p.drawPixmap(QRect(a_coordOver.current() - st::slideShadow.pxWidth(), 0, st::slideShadow.pxWidth(), st::topBarHeight), App::sprite(), st::slideShadow);
		return;
	}

	if (!_history) return;

	int32 increaseLeft = Adaptive::OneColumn() ? (st::topBarForwardPadding.right() - st::topBarForwardPadding.left()) : 0;
	decreaseWidth += increaseLeft;
	QRect rectForName(st::topBarForwardPadding.left() + increaseLeft, st::topBarForwardPadding.top(), width() - decreaseWidth - st::topBarForwardPadding.left() - st::topBarForwardPadding.right(), st::msgNameFont->height);
	p.setFont(st::dlgHistFont->f);
	if (_history->typing.isEmpty() && _history->sendActions.isEmpty()) {
		p.setPen(st::titleStatusColor->p);
		p.drawText(rectForName.x(), st::topBarHeight - st::topBarForwardPadding.bottom() - st::dlgHistFont->height + st::dlgHistFont->ascent, _titlePeerText);
	} else {
		p.setPen(st::titleTypingColor->p);
		_history->typingText.drawElided(p, rectForName.x(), st::topBarHeight - st::topBarForwardPadding.bottom() - st::dlgHistFont->height, rectForName.width());
	}

	p.setPen(st::dlgNameColor->p);
	_peer->dialogName().drawElided(p, rectForName.left(), rectForName.top(), rectForName.width());

	if (Adaptive::OneColumn()) {
		p.setOpacity(st::topBarForwardAlpha + (1 - st::topBarForwardAlpha) * over);
		p.drawPixmap(QPoint((st::topBarForwardPadding.right() - st::topBarBackwardImg.pxWidth()) / 2, (st::topBarHeight - st::topBarBackwardImg.pxHeight()) / 2), App::sprite(), st::topBarBackwardImg);
	} else {
		p.setOpacity(st::topBarForwardAlpha + (1 - st::topBarForwardAlpha) * over);
		p.drawPixmap(QPoint(width() - (st::topBarForwardPadding.right() + st::topBarForwardImg.pxWidth()) / 2, (st::topBarHeight - st::topBarForwardImg.pxHeight()) / 2), App::sprite(), st::topBarForwardImg);
	}
}

void HistoryWidget::topBarClick() {
	if (Adaptive::OneColumn()) {
		Ui::showChatsList();
	} else {
		if (_history) App::main()->showPeerProfile(_peer);
	}
}

void HistoryWidget::updateOnlineDisplay(int32 x, int32 w) {
	if (!_history) return;

	QString text;
	int32 t = unixtime();
	if (_peer->isUser()) {
		text = App::onlineText(_peer->asUser(), t);
	} else if (_peer->isChat()) {
		ChatData *chat = _peer->asChat();
		if (!chat->amIn()) {
			text = lang(lng_chat_status_unaccessible);
		} else if (chat->participants.isEmpty()) {
			text = _titlePeerText.isEmpty() ? lng_chat_status_members(lt_count, chat->count < 0 ? 0 : chat->count) : _titlePeerText;
		} else {
			int32 onlineCount = 0;
			bool onlyMe = true;
			for (ChatData::Participants::const_iterator i = chat->participants.cbegin(), e = chat->participants.cend(); i != e; ++i) {
				if (i.key()->onlineTill > t) {
					++onlineCount;
					if (onlyMe && i.key() != App::self()) onlyMe = false;
				}
			}
			if (onlineCount && !onlyMe) {
				text = lng_chat_status_members_online(lt_count, chat->participants.size(), lt_count_online, onlineCount);
			} else {
				text = lng_chat_status_members(lt_count, chat->participants.size());
			}
		}
	} else if (_peer->isChannel()) {
		if (_peer->isMegagroup() && _peer->asChannel()->count > 0 && _peer->asChannel()->count <= Global::ChatSizeMax()) {
			if (_peer->asChannel()->mgInfo->lastParticipants.size() < _peer->asChannel()->count || _peer->asChannel()->lastParticipantsCountOutdated()) {
				if (App::api()) App::api()->requestLastParticipants(_peer->asChannel());
			}
			int32 onlineCount = 0;
			bool onlyMe = true;
			for (MentionRows::const_iterator i = _peer->asChannel()->mgInfo->lastParticipants.cbegin(), e = _peer->asChannel()->mgInfo->lastParticipants.cend(); i != e; ++i) {
				if ((*i)->onlineTill > t) {
					++onlineCount;
					if (onlyMe && (*i) != App::self()) onlyMe = false;
				}
			}
			if (onlineCount && !onlyMe) {
				text = lng_chat_status_members_online(lt_count, _peer->asChannel()->count, lt_count_online, onlineCount);
			} else {
				text = lng_chat_status_members(lt_count, _peer->asChannel()->count);
			}
		} else {
			text = _peer->asChannel()->count ? lng_chat_status_members(lt_count, _peer->asChannel()->count) : lang(_peer->isMegagroup() ? lng_group_status : lng_channel_status);
		}
	}
	if (_titlePeerText != text) {
		_titlePeerText = text;
		_titlePeerTextWidth = st::dlgHistFont->width(_titlePeerText);
		if (App::main()) {
			App::main()->topBar()->update();
		}
	}
	updateOnlineDisplayTimer();
}

void HistoryWidget::updateOnlineDisplayTimer() {
	if (!_history) return;

	int32 t = unixtime(), minIn = 86400;
	if (_peer->isUser()) {
		minIn = App::onlineWillChangeIn(_peer->asUser(), t);
	} else if (_peer->isChat()) {
		ChatData *chat = _peer->asChat();
		if (chat->participants.isEmpty()) return;

		for (ChatData::Participants::const_iterator i = chat->participants.cbegin(), e = chat->participants.cend(); i != e; ++i) {
			int32 onlineWillChangeIn = App::onlineWillChangeIn(i.key(), t);
			if (onlineWillChangeIn < minIn) {
				minIn = onlineWillChangeIn;
			}
		}
	} else if (_peer->isChannel()) {
	}
	App::main()->updateOnlineDisplayIn(minIn * 1000);
}

void HistoryWidget::onFieldResize() {
	int32 maxKeyboardHeight = int(st::maxFieldHeight) - _field.height();
	_keyboard.resizeToWidth(width(), maxKeyboardHeight);

	int32 kbh = 0;
	if (_kbShown) {
		kbh = qMin(_keyboard.height(), maxKeyboardHeight);
		_kbScroll.setGeometry(0, height() - kbh, width(), kbh);
	}
	_field.move(_attachDocument.x() + _attachDocument.width(), height() - kbh - _field.height() - st::sendPadding);
	_fieldBarCancel.move(width() - _fieldBarCancel.width(), _field.y() - st::sendPadding - _fieldBarCancel.height());

	_attachDocument.move(0, height() - kbh - _attachDocument.height());
	_attachPhoto.move(_attachDocument.x(), _attachDocument.y());
	_botStart.setGeometry(0, _attachDocument.y(), width(), _botStart.height());
	_unblock.setGeometry(0, _attachDocument.y(), width(), _unblock.height());
	_joinChannel.setGeometry(0, _attachDocument.y(), width(), _joinChannel.height());
	_muteUnmute.setGeometry(0, _attachDocument.y(), width(), _muteUnmute.height());
	_send.move(width() - _send.width(), _attachDocument.y());
	_broadcast.move(_send.x() - _broadcast.width(), height() - kbh - _broadcast.height());
	_attachEmoji.move((hasBroadcastToggle() ? _broadcast.x() : _send.x()) - _attachEmoji.width(), height() - kbh - _attachEmoji.height());
	_kbShow.move(_attachEmoji.x() - _kbShow.width(), height() - kbh - _kbShow.height());
	_kbHide.move(_attachEmoji.x(), _attachEmoji.y());
	_cmdStart.move(_attachEmoji.x() - _cmdStart.width(), height() - kbh - _cmdStart.height());
	_silent.move(_attachEmoji.x() - _silent.width(), height() - kbh - _silent.height());

	_attachType.move(0, _attachDocument.y() - _attachType.height());
	_emojiPan.moveBottom(_attachEmoji.y());

	updateListSize();
	updateField();
}

void HistoryWidget::onFieldFocused() {
	if (_list) _list->clearSelectedItems(true);
}

void HistoryWidget::onCheckMentionDropdown() {
	if (!_history || _a_show.animating()) return;

	bool start = false;
	QString query = _inlineBot ? QString() : _field.getMentionHashtagBotCommandPart(start);
	if (!query.isEmpty()) {
		if (query.at(0) == '#' && cRecentWriteHashtags().isEmpty() && cRecentSearchHashtags().isEmpty()) Local::readRecentHashtagsAndBots();
		if (query.at(0) == '@' && cRecentInlineBots().isEmpty()) Local::readRecentHashtagsAndBots();
		if (query.at(0) == '/' && _peer->isUser() && !_peer->asUser()->botInfo) return;
	}
	_attachMention.showFiltered(_peer, query, start);
}

void HistoryWidget::updateFieldPlaceholder() {
	if (_editMsgId) {
		_field.setPlaceholder(lang(lng_edit_message_text));
		_send.setText(lang(lng_settings_save));
	} else {
		if (_inlineBot && _inlineBot != InlineBotLookingUpData) {
			_field.setPlaceholder(_inlineBot->botInfo->inlinePlaceholder.mid(1), _inlineBot->username.size() + 2);
		} else if (hasBroadcastToggle()) {
			_field.setPlaceholder(lang(_broadcast.checked() ? (_silent.checked() ? lng_broadcast_silent_ph : lng_broadcast_ph) : lng_comment_ph));
		} else {
			_field.setPlaceholder(lang((_history && _history->isChannel() && !_history->isMegagroup()) ? (_peer->asChannel()->canPublish() ? (_silent.checked() ? lng_broadcast_silent_ph : lng_broadcast_ph) : lng_comment_ph) : lng_message_ph));
		}
		_send.setText(lang(lng_send_button));
	}
}

void HistoryWidget::uploadImage(const QImage &img, PrepareMediaType type, FileLoadForceConfirmType confirm, const QString &source, bool withText) {
	if (!_history) return;

	App::wnd()->activateWindow();
	FileLoadTask *task = new FileLoadTask(img, type, FileLoadTo(_peer->id, _broadcast.checked(), _silent.checked(), replyToId()), confirm, source);
	if (withText) {
		_confirmWithTextId = task->fileid();
	}
	_fileLoader.addTask(task);
}

void HistoryWidget::uploadFile(const QString &file, PrepareMediaType type, FileLoadForceConfirmType confirm, bool withText) {
	if (!_history) return;

	App::wnd()->activateWindow();
	FileLoadTask *task = new FileLoadTask(file, type, FileLoadTo(_peer->id, _broadcast.checked(), _silent.checked(), replyToId()), confirm);
	if (withText) {
		_confirmWithTextId = task->fileid();
	}
	_fileLoader.addTask(task);
}

void HistoryWidget::uploadFiles(const QStringList &files, PrepareMediaType type) {
	if (!_history || files.isEmpty()) return;

	if (files.size() == 1 && !QFileInfo(files.at(0)).isDir()) return uploadFile(files.at(0), type);

	App::wnd()->activateWindow();

	FileLoadTo to(_peer->id, _broadcast.checked(), _silent.checked(), replyToId());

	TasksList tasks;
	tasks.reserve(files.size());
	for (int32 i = 0, l = files.size(); i < l; ++i) {
		tasks.push_back(TaskPtr(new FileLoadTask(files.at(i), type, to, FileLoadNeverConfirm)));
	}
	_fileLoader.addTasks(tasks);

	cancelReply(lastForceReplyReplied());
}

void HistoryWidget::uploadFileContent(const QByteArray &fileContent, PrepareMediaType type) {
	if (!_history) return;

	App::wnd()->activateWindow();
	_fileLoader.addTask(new FileLoadTask(fileContent, type, FileLoadTo(_peer->id, _broadcast.checked(), _silent.checked(), replyToId())));
	cancelReply(lastForceReplyReplied());
}

void HistoryWidget::shareContactWithConfirm(const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, bool withText) {
	if (!_history) return;

	App::wnd()->activateWindow();
	_confirmWithTextId = 0xFFFFFFFFFFFFFFFFL;
	Ui::showLayer(new PhotoSendBox(phone, fname, lname, replyTo));
}

void HistoryWidget::confirmSendFile(const FileLoadResultPtr &file, bool ctrlShiftEnter) {
	if (_confirmWithTextId && _confirmWithTextId == file->id) {
		onSend(ctrlShiftEnter, file->to.replyTo);
		_confirmWithTextId = 0;
	}

	FullMsgId newId(peerToChannel(file->to.peer), clientMsgId());

	connect(App::uploader(), SIGNAL(photoReady(const FullMsgId&,bool,const MTPInputFile&)), this, SLOT(onPhotoUploaded(const FullMsgId&,bool,const MTPInputFile&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentReady(const FullMsgId&,bool,const MTPInputFile&)), this, SLOT(onDocumentUploaded(const FullMsgId&,bool,const MTPInputFile&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(thumbDocumentReady(const FullMsgId&,bool,const MTPInputFile&,const MTPInputFile&)), this, SLOT(onThumbDocumentUploaded(const FullMsgId&,bool,const MTPInputFile&, const MTPInputFile&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(photoProgress(const FullMsgId&)), this, SLOT(onPhotoProgress(const FullMsgId&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentProgress(const FullMsgId&)), this, SLOT(onDocumentProgress(const FullMsgId&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(photoFailed(const FullMsgId&)), this, SLOT(onPhotoFailed(const FullMsgId&)), Qt::UniqueConnection);
	connect(App::uploader(), SIGNAL(documentFailed(const FullMsgId&)), this, SLOT(onDocumentFailed(const FullMsgId&)), Qt::UniqueConnection);

	App::uploader()->upload(newId, file);

	History *h = App::history(file->to.peer);

	fastShowAtEnd(h);

	int32 flags = newMessageFlags(h->peer) | MTPDmessage::flag_media; // unread, out
	if (file->to.replyTo) flags |= MTPDmessage::flag_reply_to_msg_id;
	bool channelPost = h->peer->isChannel() && !h->peer->isMegagroup() && h->peer->asChannel()->canPublish() && (h->peer->asChannel()->isBroadcast() || file->to.broadcast);
	bool showFromName = !channelPost || h->peer->asChannel()->addsSignature();
	bool silentPost = channelPost && file->to.silent;
	if (channelPost) {
		flags |= MTPDmessage::flag_views;
		flags |= MTPDmessage::flag_post;
	}
	if (showFromName) {
		flags |= MTPDmessage::flag_from_id;
	}
	if (silentPost) {
		flags |= MTPDmessage::flag_silent;
	}
	if (file->type == PreparePhoto) {
		h->addNewMessage(MTP_message(MTP_int(flags), MTP_int(newId.msg), MTP_int(showFromName ? MTP::authedId() : 0), peerToMTP(file->to.peer), MTPnullFwdHeader, MTPint(), MTP_int(file->to.replyTo), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaPhoto(file->photo, MTP_string(file->caption)), MTPnullMarkup, MTPnullEntities, MTP_int(1), MTPint()), NewMessageUnread);
	} else if (file->type == PrepareDocument) {
		h->addNewMessage(MTP_message(MTP_int(flags), MTP_int(newId.msg), MTP_int(showFromName ? MTP::authedId() : 0), peerToMTP(file->to.peer), MTPnullFwdHeader, MTPint(), MTP_int(file->to.replyTo), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaDocument(file->document, MTP_string(file->caption)), MTPnullMarkup, MTPnullEntities, MTP_int(1), MTPint()), NewMessageUnread);
	} else if (file->type == PrepareAudio) {
		if (!h->peer->isChannel()) {
			flags |= MTPDmessage::flag_media_unread;
		}
		h->addNewMessage(MTP_message(MTP_int(flags), MTP_int(newId.msg), MTP_int(showFromName ? MTP::authedId() : 0), peerToMTP(file->to.peer), MTPnullFwdHeader, MTPint(), MTP_int(file->to.replyTo), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaDocument(file->document, MTP_string(file->caption)), MTPnullMarkup, MTPnullEntities, MTP_int(1), MTPint()), NewMessageUnread);
	}

	if (_peer && file->to.peer == _peer->id) {
		App::main()->historyToDown(_history);
	}
	App::main()->dialogsToUp();
	peerMessagesUpdated(file->to.peer);
}

void HistoryWidget::cancelSendFile(const FileLoadResultPtr &file) {
	if (_confirmWithTextId && file->id == _confirmWithTextId) {
		setFieldText(QString());
		_confirmWithTextId = 0;
	}
	if (!file->originalText.isEmpty()) {
		_field.textCursor().insertText(file->originalText);
	}
}

void HistoryWidget::confirmShareContact(const QString &phone, const QString &fname, const QString &lname, MsgId replyTo, bool ctrlShiftEnter) {
	if (!_peer) return;

	PeerId shareToId = _peer->id;
	if (_confirmWithTextId == 0xFFFFFFFFFFFFFFFFL) {
		onSend(ctrlShiftEnter, replyTo);
		_confirmWithTextId = 0;
	}
	shareContact(shareToId, phone, fname, lname, replyTo);
}

void HistoryWidget::cancelShareContact() {
	if (_confirmWithTextId == 0xFFFFFFFFFFFFFFFFL) {
		setFieldText(QString());
		_confirmWithTextId = 0;
	}
}

void HistoryWidget::onPhotoUploaded(const FullMsgId &newId, bool silent, const MTPInputFile &file) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		uint64 randomId = MTP::nonce<uint64>();
		App::historyRegRandom(randomId, newId);
		History *hist = item->history();
		MsgId replyTo = item->toHistoryReply() ? item->toHistoryReply()->replyToId() : 0;
		int32 sendFlags = 0;
		if (replyTo) {
			sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
		}

		bool channelPost = hist->peer->isChannel() && !hist->peer->isMegagroup() && hist->peer->asChannel()->canPublish() && item->isPost();
		bool silentPost = channelPost && silent;
		if (channelPost) {
			sendFlags |= MTPmessages_SendMedia::flag_broadcast;
		}
		if (silentPost) {
			sendFlags |= MTPmessages_SendMedia::flag_silent;
		}
		QString caption = item->getMedia() ? item->getMedia()->getCaption() : QString();
		hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), item->history()->peer->input, MTP_int(replyTo), MTP_inputMediaUploadedPhoto(file, MTP_string(caption)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, hist->sendRequestId);
	}
}

namespace {
	MTPVector<MTPDocumentAttribute> _composeDocumentAttributes(DocumentData *document) {
		QVector<MTPDocumentAttribute> attributes(1, MTP_documentAttributeFilename(MTP_string(document->name)));
		if (document->dimensions.width() > 0 && document->dimensions.height() > 0) {
			int32 duration = document->duration();
			if (duration >= 0) {
				attributes.push_back(MTP_documentAttributeVideo(MTP_int(duration), MTP_int(document->dimensions.width()), MTP_int(document->dimensions.height())));
			} else {
				attributes.push_back(MTP_documentAttributeImageSize(MTP_int(document->dimensions.width()), MTP_int(document->dimensions.height())));
			}
		}
		if (document->type == AnimatedDocument) {
			attributes.push_back(MTP_documentAttributeAnimated());
		} else if (document->type == StickerDocument && document->sticker()) {
			attributes.push_back(MTP_documentAttributeSticker(MTP_string(document->sticker()->alt), document->sticker()->set));
		} else if (document->type == SongDocument && document->song()) {
			attributes.push_back(MTP_documentAttributeAudio(MTP_int(MTPDdocumentAttributeAudio::flag_title | MTPDdocumentAttributeAudio::flag_performer), MTP_int(document->song()->duration), MTP_string(document->song()->title), MTP_string(document->song()->performer), MTPstring()));
		} else if (document->type == VoiceDocument && document->voice()) {
			attributes.push_back(MTP_documentAttributeAudio(MTP_int(MTPDdocumentAttributeAudio::flag_voice | MTPDdocumentAttributeAudio::flag_waveform), MTP_int(document->voice()->duration), MTPstring(), MTPstring(), MTP_string(documentWaveformEncode5bit(document->voice()->waveform))));
		}
		return MTP_vector<MTPDocumentAttribute>(attributes);
	}
}

void HistoryWidget::onDocumentUploaded(const FullMsgId &newId, bool silent, const MTPInputFile &file) {
	if (!MTP::authedId()) return;
	HistoryMessage *item = dynamic_cast<HistoryMessage*>(App::histItemById(newId));
	if (item) {
		DocumentData *document = item->getMedia() ? item->getMedia()->getDocument() : 0;
		if (document) {
			uint64 randomId = MTP::nonce<uint64>();
			App::historyRegRandom(randomId, newId);
			History *hist = item->history();
			MsgId replyTo = item->toHistoryReply() ? item->toHistoryReply()->replyToId() : 0;
			int32 sendFlags = 0;
			if (replyTo) {
				sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
			}

			bool channelPost = hist->peer->isChannel() && !hist->peer->isMegagroup() && hist->peer->asChannel()->canPublish() && item->isPost();
			bool silentPost = channelPost && silent;
			if (channelPost) {
				sendFlags |= MTPmessages_SendMedia::flag_broadcast;
			}
			if (silentPost) {
				sendFlags |= MTPmessages_SendMedia::flag_silent;
			}
			QString caption = item->getMedia() ? item->getMedia()->getCaption() : QString();
			hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), item->history()->peer->input, MTP_int(replyTo), MTP_inputMediaUploadedDocument(file, MTP_string(document->mime), _composeDocumentAttributes(document), MTP_string(caption)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, hist->sendRequestId);
		}
	}
}

void HistoryWidget::onThumbDocumentUploaded(const FullMsgId &newId, bool silent, const MTPInputFile &file, const MTPInputFile &thumb) {
	if (!MTP::authedId()) return;
	HistoryMessage *item = dynamic_cast<HistoryMessage*>(App::histItemById(newId));
	if (item) {
		DocumentData *document = item->getMedia() ? item->getMedia()->getDocument() : 0;
		if (document) {
			uint64 randomId = MTP::nonce<uint64>();
			App::historyRegRandom(randomId, newId);
			History *hist = item->history();
			MsgId replyTo = item->toHistoryReply() ? item->toHistoryReply()->replyToId() : 0;
			int32 sendFlags = 0;
			if (replyTo) {
				sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
			}

			bool channelPost = hist->peer->isChannel() && !hist->peer->isMegagroup() && hist->peer->asChannel()->canPublish() && item->isPost();
			bool silentPost = channelPost && silent;
			if (channelPost) {
				sendFlags |= MTPmessages_SendMedia::flag_broadcast;
			}
			if (silentPost) {
				sendFlags |= MTPmessages_SendMedia::flag_silent;
			}
			QString caption = item->getMedia() ? item->getMedia()->getCaption() : QString();
			hist->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), item->history()->peer->input, MTP_int(replyTo), MTP_inputMediaUploadedThumbDocument(file, thumb, MTP_string(document->mime), _composeDocumentAttributes(document), MTP_string(caption)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, hist->sendRequestId);
		}
	}
}

void HistoryWidget::onPhotoProgress(const FullMsgId &newId) {
	if (!MTP::authedId()) return;
	if (HistoryItem *item = App::histItemById(newId)) {
		PhotoData *photo = (item->getMedia() && item->getMedia()->type() == MediaTypePhoto) ? static_cast<HistoryPhoto*>(item->getMedia())->photo() : 0;
		if (!item->isPost()) {
			updateSendAction(item->history(), SendActionUploadPhoto, 0);
		}
		Ui::repaintHistoryItem(item);
	}
}

void HistoryWidget::onDocumentProgress(const FullMsgId &newId) {
	if (!MTP::authedId()) return;
	if (HistoryItem *item = App::histItemById(newId)) {
		HistoryMedia *media = item->getMedia();
		DocumentData *doc = media ? media->getDocument() : 0;
		if (!item->isPost()) {
			updateSendAction(item->history(), (doc && doc->voice()) ? SendActionUploadVoice : SendActionUploadFile, doc ? doc->uploadOffset : 0);
		}
		Ui::repaintHistoryItem(item);
	}
}

void HistoryWidget::onPhotoFailed(const FullMsgId &newId) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		if (!item->isPost()) {
			updateSendAction(item->history(), SendActionUploadPhoto, -1);
		}
//		Ui::repaintHistoryItem(item);
	}
}

void HistoryWidget::onDocumentFailed(const FullMsgId &newId) {
	if (!MTP::authedId()) return;
	HistoryItem *item = App::histItemById(newId);
	if (item) {
		HistoryMedia *media = item->getMedia();
		DocumentData *doc = media ? media->getDocument() : 0;
		if (!item->isPost()) {
			updateSendAction(item->history(), (doc && doc->voice()) ? SendActionUploadVoice : SendActionUploadFile, -1);
		}
		Ui::repaintHistoryItem(item);
	}
}

void HistoryWidget::onReportSpamClicked() {
	ConfirmBox *box = new ConfirmBox(lang(_peer->isUser() ? lng_report_spam_sure : ((_peer->isChat() || _peer->isMegagroup()) ? lng_report_spam_sure_group : lng_report_spam_sure_channel)), lang(lng_report_spam_ok), st::attentionBoxButton);
	connect(box, SIGNAL(confirmed()), this, SLOT(onReportSpamSure()));
	Ui::showLayer(box);
	_clearPeer = _peer;
}

void HistoryWidget::onReportSpamSure() {
	if (_reportSpamRequest) return;

	Ui::hideLayer();
	if (_clearPeer->isUser()) MTP::send(MTPcontacts_Block(_clearPeer->asUser()->inputUser), rpcDone(&HistoryWidget::blockDone, _clearPeer), RPCFailHandlerPtr(), 0, 5);
	_reportSpamRequest = MTP::send(MTPmessages_ReportSpam(_clearPeer->input), rpcDone(&HistoryWidget::reportSpamDone, _clearPeer), rpcFail(&HistoryWidget::reportSpamFail));
}

void HistoryWidget::reportSpamDone(PeerData *peer, const MTPBool &result, mtpRequestId req) {
	if (req == _reportSpamRequest) {
		_reportSpamRequest = 0;
	}
	if (peer) {
		cRefReportSpamStatuses().insert(peer->id, dbiprsReportSent);
		Local::writeReportSpamStatuses();
	}
	_reportSpamStatus = dbiprsReportSent;
	_reportSpamPanel.setReported(_reportSpamStatus == dbiprsReportSent, peer);
}

bool HistoryWidget::reportSpamFail(const RPCError &error, mtpRequestId req) {
	if (mtpIsFlood(error)) return false;

	if (req == _reportSpamRequest) {
		_reportSpamRequest = 0;
	}
	return false;
}

void HistoryWidget::onReportSpamHide() {
	if (_peer) {
		cRefReportSpamStatuses().insert(_peer->id, dbiprsHidden);
		Local::writeReportSpamStatuses();

		MTP::send(MTPmessages_HideReportSpam(_peer->input));
	}
	_reportSpamStatus = dbiprsHidden;
	updateControlsVisibility();
}

void HistoryWidget::onReportSpamClear() {
	_clearPeer = _peer;
	if (_clearPeer->isUser()) {
		App::main()->deleteConversation(_clearPeer);
	} else if (_clearPeer->isChat()) {
		Ui::showChatsList();
		MTP::send(MTPmessages_DeleteChatUser(_clearPeer->asChat()->inputChat, App::self()->inputUser), App::main()->rpcDone(&MainWidget::deleteHistoryAfterLeave, _clearPeer), App::main()->rpcFail(&MainWidget::leaveChatFailed, _clearPeer));
	} else if (_clearPeer->isChannel()) {
		Ui::showChatsList();
		if (_clearPeer->migrateFrom()) {
			App::main()->deleteConversation(_clearPeer->migrateFrom());
		}
		MTP::send(MTPchannels_LeaveChannel(_clearPeer->asChannel()->inputChannel), App::main()->rpcDone(&MainWidget::sentUpdatesReceived));
	}
}

void HistoryWidget::peerMessagesUpdated(PeerId peer) {
	if (_peer && _list && peer == _peer->id) {
		updateListSize();
		updateBotKeyboard();
		if (!_scroll.isHidden()) {
			bool unblock = isBlocked(), botStart = isBotStart(), joinChannel = isJoinChannel(), muteUnmute = isMuteUnmute();
			bool upd = (_unblock.isHidden() == unblock);
			if (!upd && !unblock) upd = (_botStart.isHidden() == botStart);
			if (!upd && !unblock && !botStart) upd = (_joinChannel.isHidden() == joinChannel);
			if (!upd && !unblock && !botStart && !joinChannel) upd = (_muteUnmute.isHidden() == muteUnmute);
			if (upd) {
				updateControlsVisibility();
				resizeEvent(0);
			}
		}
	}
}

void HistoryWidget::peerMessagesUpdated() {
	if (_list) peerMessagesUpdated(_peer->id);
}

bool HistoryWidget::isItemVisible(HistoryItem *item) {
	if (isHidden() || _a_show.animating() || !_list) {
		return false;
	}
	int32 top = _list->itemTop(item), st = _scroll.scrollTop();
	if (top < 0 || top + item->height() <= st || top >= st + _scroll.height()) {
		return false;
	}
	return true;
}

void HistoryWidget::ui_repaintHistoryItem(const HistoryItem *item) {
	if (_peer && _list && (item->history() == _history || (_migrated && item->history() == _migrated))) {
		uint64 ms = getms();
		if (_lastScrolled + 100 <= ms) {
			_list->repaintItem(item);
		} else {
			_updateHistoryItems.start(_lastScrolled + 100 - ms);
		}
	}
}

void HistoryWidget::onUpdateHistoryItems() {
	if (!_list) return;

	uint64 ms = getms();
	if (_lastScrolled + 100 <= ms) {
		_list->update();
	} else {
		_updateHistoryItems.start(_lastScrolled + 100 - ms);
	}
}

void HistoryWidget::ui_repaintInlineItem(const LayoutInlineItem *layout) {
	_emojiPan.ui_repaintInlineItem(layout);
}

bool HistoryWidget::ui_isInlineItemVisible(const LayoutInlineItem *layout) {
	return _emojiPan.ui_isInlineItemVisible(layout);
}

bool HistoryWidget::ui_isInlineItemBeingChosen() {
	return _emojiPan.ui_isInlineItemBeingChosen();
}

void HistoryWidget::notify_historyItemLayoutChanged(const HistoryItem *item) {
	if (_peer && _list && (item == App::mousedItem() || item == App::hoveredItem() || item == App::hoveredLinkItem())) {
		_list->onUpdateSelected();
	}
}

void HistoryWidget::notify_automaticLoadSettingsChangedGif() {
	_emojiPan.notify_automaticLoadSettingsChangedGif();
}

void HistoryWidget::resizeEvent(QResizeEvent *e) {
	_reportSpamPanel.resize(width(), _reportSpamPanel.height());

	int32 maxKeyboardHeight = int(st::maxFieldHeight) - _field.height();
	_keyboard.resizeToWidth(width(), maxKeyboardHeight);

	int32 kbh = 0;
	if (_kbShown) {
		kbh = qMin(_keyboard.height(), maxKeyboardHeight);
		_kbScroll.setGeometry(0, height() - kbh, width(), kbh);
	}
	_field.move(_attachDocument.x() + _attachDocument.width(), height() - kbh - _field.height() - st::sendPadding);

	if (_pinnedBar) {
		if (_scroll.y() != st::replyHeight) {
			_scroll.move(0, st::replyHeight);
			_attachMention.setBoundings(_scroll.geometry());
		}
		_pinnedBar->cancel.move(width() - _pinnedBar->cancel.width(), 0);
		_pinnedBar->shadow.setGeometry(0, st::replyHeight, width(), st::lineWidth);
	} else if (_scroll.y() != 0) {
		_scroll.move(0, 0);
		_attachMention.setBoundings(_scroll.geometry());
	}

	_attachDocument.move(0, height() - kbh - _attachDocument.height());
	_attachPhoto.move(_attachDocument.x(), _attachDocument.y());

	_fieldBarCancel.move(width() - _fieldBarCancel.width(), _field.y() - st::sendPadding - _fieldBarCancel.height());
	updateListSize(false, false, { ScrollChangeAdd, App::main() ? App::main()->contentScrollAddToY() : 0 });

	bool kbShowShown = _history && !_kbShown && _keyboard.hasMarkup();
	_field.resize(width() - _send.width() - _attachDocument.width() - _attachEmoji.width() - (kbShowShown ? _kbShow.width() : 0) - (_cmdStartShown ? _cmdStart.width() : 0) - (hasBroadcastToggle() ? _broadcast.width() : 0) - (hasSilentToggle() ? _silent.width() : 0), _field.height());

	_toHistoryEnd.move((width() - _toHistoryEnd.width()) / 2, _scroll.y() + _scroll.height() - _toHistoryEnd.height() - st::historyToEndSkip);
	updateCollapseCommentsVisibility();

	_send.move(width() - _send.width(), _attachDocument.y());
	_botStart.setGeometry(0, _attachDocument.y(), width(), _botStart.height());
	_unblock.setGeometry(0, _attachDocument.y(), width(), _unblock.height());
	_joinChannel.setGeometry(0, _attachDocument.y(), width(), _joinChannel.height());
	_muteUnmute.setGeometry(0, _attachDocument.y(), width(), _muteUnmute.height());
	_broadcast.move(_send.x() - _broadcast.width(), height() - kbh - _broadcast.height());
	_attachEmoji.move((hasBroadcastToggle() ? _broadcast.x() : _send.x()) - _attachEmoji.width(), height() - kbh - _attachEmoji.height());
	_kbShow.move(_attachEmoji.x() - _kbShow.width(), height() - kbh - _kbShow.height());
	_kbHide.move(_attachEmoji.x(), _attachEmoji.y());
	_cmdStart.move(_attachEmoji.x() - _cmdStart.width(), height() - kbh - _cmdStart.height());
	_silent.move(_attachEmoji.x() - _silent.width(), height() - kbh - _silent.height());

	_attachType.move(0, _attachDocument.y() - _attachType.height());
	_emojiPan.moveBottom(_attachEmoji.y());
	_emojiPan.setMaxHeight(height() - st::dropdownDef.padding.top() - st::dropdownDef.padding.bottom() - _attachEmoji.height());

	switch (_attachDrag) {
	case DragStateFiles:
		_attachDragDocument.resize(width() - st::dragMargin.left() - st::dragMargin.right(), height() - st::dragMargin.top() - st::dragMargin.bottom());
		_attachDragDocument.move(st::dragMargin.left(), st::dragMargin.top());
	break;
	case DragStatePhotoFiles:
		_attachDragDocument.resize(width() - st::dragMargin.left() - st::dragMargin.right(), (height() - st::dragMargin.top() - st::dragMargin.bottom()) / 2);
		_attachDragDocument.move(st::dragMargin.left(), st::dragMargin.top());
		_attachDragPhoto.resize(_attachDragDocument.width(), _attachDragDocument.height());
		_attachDragPhoto.move(st::dragMargin.left(), height() - _attachDragPhoto.height() - st::dragMargin.bottom());
	break;
	case DragStateImage:
		_attachDragPhoto.resize(width() - st::dragMargin.left() - st::dragMargin.right(), height() - st::dragMargin.top() - st::dragMargin.bottom());
		_attachDragPhoto.move(st::dragMargin.left(), st::dragMargin.top());
	break;
	}

	_topShadow.resize(width() - ((!Adaptive::OneColumn() && !_inGrab) ? st::lineWidth : 0), st::lineWidth);
	_topShadow.moveToLeft((!Adaptive::OneColumn() && !_inGrab) ? st::lineWidth : 0, 0);
	_sideShadow.resize(st::lineWidth, height());
	_sideShadow.moveToLeft(0, 0);
}

void HistoryWidget::itemRemoved(HistoryItem *item) {
	if (_list) _list->itemRemoved(item);
	if (item == _replyEditMsg) {
		if (_editMsgId) {
			cancelEdit();
		} else {
			cancelReply();
		}
	}
	if (item == _replyReturn) {
		calcNextReplyReturn();
	}
	if (_pinnedBar && item->id == _pinnedBar->msgId) {
		pinnedMsgVisibilityUpdated();
	}
	if (_kbReplyTo && item == _kbReplyTo) {
		onKbToggle();
		_kbReplyTo = 0;
	}
}

void HistoryWidget::itemEdited(HistoryItem *item) {
	if (item == _replyEditMsg) {
		updateReplyEditTexts(true);
	}
	if (_pinnedBar && item->id == _pinnedBar->msgId) {
		updatePinnedBar(true);
	}
}

void HistoryWidget::updateScrollColors() {
	if (!App::historyScrollBarColor()) return;
	_scroll.updateColors(App::historyScrollBarColor(), App::historyScrollBgColor(), App::historyScrollBarOverColor(), App::historyScrollBgOverColor());
}

MsgId HistoryWidget::replyToId() const {
	return _replyToId ? _replyToId : (_kbReplyTo ? _kbReplyTo->id : 0);
}

void HistoryWidget::updateListSize(bool initial, bool loadedDown, const ScrollChange &change, const HistoryItem *resizedItem, bool scrollToIt) {
	if (!_history || (initial && _histInited) || (!initial && !_histInited)) return;
	if (_firstLoadRequest) {
		if (resizedItem) _list->recountHeight(resizedItem);
		return; // scrollTopMax etc are not working after recountHeight()
	}

	int32 newScrollHeight = height();
	if (isBlocked() || isBotStart() || isJoinChannel() || isMuteUnmute()) {
		newScrollHeight -= _unblock.height();
	} else {
		if (_canSendMessages) {
			newScrollHeight -= (_field.height() + 2 * st::sendPadding);
		}
		if (_editMsgId || replyToId() || readyToForward() || (_previewData && _previewData->pendingTill >= 0)) {
			newScrollHeight -= st::replyHeight;
		}
		if (_kbShown) {
			newScrollHeight -= _kbScroll.height();
		}
	}
	if (_pinnedBar) {
		newScrollHeight -= st::replyHeight;
	}
	bool wasAtBottom = _scroll.scrollTop() + 1 > _scroll.scrollTopMax(), needResize = _scroll.width() != width() || _scroll.height() != newScrollHeight;
	if (needResize) {
		_scroll.resize(width(), newScrollHeight);
		_attachMention.setBoundings(_scroll.geometry());
		_toHistoryEnd.move((width() - _toHistoryEnd.width()) / 2, _scroll.y() + _scroll.height() - _toHistoryEnd.height() - st::historyToEndSkip);
		updateCollapseCommentsVisibility();
	}

	if (!initial) {
		_history->lastScrollTop = _scroll.scrollTop();
	}
	int32 newSt = _list->recountHeight(resizedItem);
	bool washidden = _scroll.isHidden();
	if (washidden) {
		_scroll.show();
	}
	_list->updateSize();
	int32 historyTop = _list->historyTop(), migratedTop = _list->migratedTop();
	if (resizedItem && !resizedItem->detached() && scrollToIt) {
		int32 resizedTop = _list->itemTop(resizedItem);
		if (resizedTop >= 0) {
			if (newSt + _scroll.height() < resizedTop + resizedItem->height()) {
				newSt = resizedTop + resizedItem->height() - _scroll.height();
			}
			if (newSt > resizedTop) {
				newSt = resizedTop;
			}
			wasAtBottom = false;
		}
	}
	if (washidden) {
		_scroll.hide();
	}

	if ((!initial && !wasAtBottom) || (loadedDown && (!_history->showFrom || _history->unreadBar || _history->loadedAtBottom()) && (!_migrated || !_migrated->showFrom || _migrated->unreadBar || _history->loadedAtBottom()))) {
		int32 addToY = 0;
		if (change.type == ScrollChangeAdd) {
			addToY = change.value;
		} else if (change.type == ScrollChangeOldHistoryHeight) {
			addToY = _list->historyHeight() - change.value;
		}
		_scroll.scrollToY(newSt + addToY);
		return;
	}

	if (initial) {
		_histInited = true;
	}

	int32 toY = ScrollMax;
	if (initial && _history->lastWidth) {
		toY = newSt;
		_history->lastWidth = 0;
	} else if (initial && _migrated && _showAtMsgId < 0 && -_showAtMsgId < ServerMaxMsgId) {
		HistoryItem *item = App::histItemById(0, -_showAtMsgId);
		int32 iy = _list->itemTop(item);
		if (iy < 0) {
			setMsgId(0);
			_histInited = false;
			return updateListSize(initial, false, change);
		} else {
			toY = (_scroll.height() > item->height()) ? qMax(iy - (_scroll.height() - item->height()) / 2, 0) : iy;
			_animActiveStart = getms();
			_animActiveTimer.start(AnimationTimerDelta);
			_activeAnimMsgId = _showAtMsgId;
		}
	} else if (initial && _showAtMsgId > 0) {
		HistoryItem *item = App::histItemById(_channel, _showAtMsgId);
		int32 iy = _list->itemTop(item);
		if (iy < 0) {
			setMsgId(0);
			_histInited = false;
			return updateListSize(initial, false, change);
		} else {
			toY = (_scroll.height() > item->height()) ? qMax(iy - (_scroll.height() - item->height()) / 2, 0) : iy;
			_animActiveStart = getms();
			_animActiveTimer.start(AnimationTimerDelta);
			_activeAnimMsgId = _showAtMsgId;
			if (item->isGroupMigrate() && _migrated && !_migrated->isEmpty() && _migrated->loadedAtBottom() && _migrated->blocks.back()->items.back()->isGroupMigrate() && _list->historyTop() != _list->historyDrawTop()) {
				_activeAnimMsgId = -_migrated->blocks.back()->items.back()->id;
			}
		}
	} else if (initial && _fixedInScrollMsgId > 0) {
		HistoryItem *item = App::histItemById(_channel, _fixedInScrollMsgId);
		int32 iy = _list->itemTop(item);
		if (iy < 0) {
			item = 0;
			for (int32 blockIndex = 0, blocksCount = _history->blocks.size(); blockIndex < blocksCount; ++blockIndex) {
				HistoryBlock *block = _history->blocks.at(blockIndex);
				for (int32 itemIndex = 0, itemsCount = block->items.size(); itemIndex < itemsCount; ++itemIndex) {
					item = block->items.at(itemIndex);
					if (item->id > _fixedInScrollMsgId) {
						break;
					} else if (item->id < 0) {
						if (item->type() == HistoryItemGroup && qMax(static_cast<HistoryGroup*>(item)->minId(), 1) >= _fixedInScrollMsgId) {
							break;
						} else if (item->type() == HistoryItemCollapse && static_cast<HistoryCollapse*>(item)->wasMinId() >= _fixedInScrollMsgId) {
							break;
						}
					}
				}
			}
			iy = _list->itemTop(item);
			if (iy >= 0) {
				toY = qMax(iy - _fixedInScrollMsgTop, 0);
			} else {
				setMsgId(ShowAtUnreadMsgId);
				_fixedInScrollMsgId = 0;
				_fixedInScrollMsgTop = 0;
				_histInited = false;
				return updateListSize(initial, false, change);
			}
		} else {
			toY = qMax(iy + item->height() - _fixedInScrollMsgTop, 0);
		}
	} else if (initial && _migrated && _migrated->unreadBar) {
		toY = _list->itemTop(_migrated->unreadBar);
	} else if (initial && _history->unreadBar) {
		toY = _list->itemTop(_history->unreadBar);
	} else if (_migrated && _migrated->showFrom) {
		toY = _list->itemTop(_migrated->showFrom);
		if (toY < _scroll.scrollTopMax() + st::unreadBarHeight) {
			_migrated->addUnreadBar();
			if (_migrated->unreadBar) {
				setMsgId(ShowAtUnreadMsgId);
				_histInited = false;
				updateListSize(true);
				App::wnd()->checkHistoryActivation();
				return;
			}
		}
	} else if (_history->showFrom) {
		toY = _list->itemTop(_history->showFrom);
		if (toY < _scroll.scrollTopMax() + st::unreadBarHeight) {
			_history->addUnreadBar();
			if (_history->unreadBar) {
				setMsgId(ShowAtUnreadMsgId);
				_histInited = false;
				updateListSize(true);
				App::wnd()->checkHistoryActivation();
				return;
			}
		}
	} else {
	}
	_scroll.scrollToY(toY);
}

void HistoryWidget::addMessagesToFront(PeerData *peer, const QVector<MTPMessage> &messages, const QVector<MTPMessageGroup> *collapsed) {
	int oldH = _list->historyHeight();
	_list->messagesReceived(peer, messages, collapsed);
	if (!_firstLoadRequest) {
		updateListSize(false, false, { ScrollChangeOldHistoryHeight, oldH });
		if (_animActiveTimer.isActive() && _activeAnimMsgId > 0 && _migrated && !_migrated->isEmpty() && _migrated->loadedAtBottom() && _migrated->blocks.back()->items.back()->isGroupMigrate() && _list->historyTop() != _list->historyDrawTop() && _history) {
			HistoryItem *animActiveItem = App::histItemById(_history->channelId(), _activeAnimMsgId);
			if (animActiveItem && animActiveItem->isGroupMigrate()) {
				_activeAnimMsgId = -_migrated->blocks.back()->items.back()->id;
			}
		}
		updateBotKeyboard();
	}
}

void HistoryWidget::addMessagesToBack(PeerData *peer, const QVector<MTPMessage> &messages, const QVector<MTPMessageGroup> *collapsed) {
	_list->messagesReceivedDown(peer, messages, collapsed);
	if (!_firstLoadRequest) {
		updateListSize(false, true);
	}
}

void HistoryWidget::countHistoryShowFrom() {
	if (_migrated && _showAtMsgId == ShowAtUnreadMsgId && _migrated->unreadCount) {
		_migrated->updateShowFrom();
	}
	if ((_migrated && _migrated->showFrom) || _showAtMsgId != ShowAtUnreadMsgId || !_history->unreadCount) {
		_history->showFrom = 0;
		return;
	}
	_history->updateShowFrom();
}

void HistoryWidget::updateBotKeyboard(History *h) {
	if (h && h != _history && h != _migrated) {
		return;
	}

	bool changed = false;
	bool wasVisible = _kbShown || _kbReplyTo;
	if ((_replyToId && !_replyEditMsg) || _editMsgId || !_history) {
		changed = _keyboard.updateMarkup(0);
	} else if (_replyToId && _replyEditMsg) {
		changed = _keyboard.updateMarkup(_replyEditMsg);
	} else {
		changed = _keyboard.updateMarkup(_history->lastKeyboardId ? App::histItemById(_channel, _history->lastKeyboardId) : 0);
	}
	updateCmdStartShown();
	if (!changed) return;

	bool hasMarkup = _keyboard.hasMarkup(), forceReply = _keyboard.forceReply() && (!_replyToId || !_replyEditMsg);
	if (hasMarkup || forceReply) {
		if (_keyboard.singleUse() && _keyboard.hasMarkup() && _keyboard.forMsgId() == FullMsgId(_channel, _history->lastKeyboardId) && _history->lastKeyboardUsed) {
			_history->lastKeyboardHiddenId = _history->lastKeyboardId;
		}
		if (!isBotStart() && !isBlocked() && _canSendMessages && (wasVisible || (_replyToId && _replyEditMsg) || (!_field.hasSendText() && !kbWasHidden()))) {
			if (!_a_show.animating()) {
				if (hasMarkup) {
					_kbScroll.show();
					_attachEmoji.hide();
					_kbHide.show();
				} else {
					_kbScroll.hide();
					_attachEmoji.show();
					_kbHide.hide();
				}
				_kbShow.hide();
				_cmdStart.hide();
			}
			int32 maxh = hasMarkup ? qMin(_keyboard.height(), int(st::maxFieldHeight) - (int(st::maxFieldHeight) / 2)) : 0;
			_field.setMaxHeight(st::maxFieldHeight - maxh);
			_kbShown = hasMarkup;
			_kbReplyTo = (_peer->isChat() || _peer->isChannel() || _keyboard.forceReply()) ? App::histItemById(_keyboard.forMsgId()) : 0;
			if (_kbReplyTo && !_replyToId) {
				updateReplyToName();
				_replyEditMsgText.setText(st::msgFont, _kbReplyTo->inDialogsText(), _textDlgOptions);
				_fieldBarCancel.show();
				updateMouseTracking();
			}
		} else {
			if (!_a_show.animating()) {
				_kbScroll.hide();
				_attachEmoji.show();
				_kbHide.hide();
				_kbShow.show();
				_cmdStart.hide();
			}
			_field.setMaxHeight(st::maxFieldHeight);
			_kbShown = false;
			_kbReplyTo = 0;
			if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_replyToId) {
				_fieldBarCancel.hide();
				updateMouseTracking();
			}
		}
	} else {
		if (!_scroll.isHidden()) {
			_kbScroll.hide();
			_attachEmoji.show();
			_kbHide.hide();
			_kbShow.hide();
			_cmdStart.show();
		}
		_field.setMaxHeight(st::maxFieldHeight);
		_kbShown = false;
		_kbReplyTo = 0;
		if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_replyToId && !_editMsgId) {
			_fieldBarCancel.hide();
			updateMouseTracking();
		}
	}
	resizeEvent(0);
	update();
}

void HistoryWidget::updateToEndVisibility() {
	bool toEndVisible = !_a_show.animating() && _history && !_firstLoadRequest && (!_history->loadedAtBottom() || _replyReturn || _scroll.scrollTop() + st::wndMinHeight < _scroll.scrollTopMax());
	if (toEndVisible && _toHistoryEnd.isHidden()) {
		_toHistoryEnd.show();
	} else if (!toEndVisible && !_toHistoryEnd.isHidden()) {
		_toHistoryEnd.hide();
	}
}

void HistoryWidget::updateCollapseCommentsVisibility() {
	int32 collapseCommentsLeft = (width() - _collapseComments.width()) / 2, collapseCommentsTop = st::msgServiceMargin.top();
	bool collapseCommentsVisible = !_a_show.animating() && _history && !_firstLoadRequest && _history->isChannel() && !_history->isMegagroup() && !_history->asChannelHistory()->onlyImportant();
	if (collapseCommentsVisible) {
		if (HistoryItem *collapse = _history->asChannelHistory()->collapse()) {
			if (!collapse->detached()) {
				int32 collapseY = _list->itemTop(collapse) - _scroll.scrollTop();
				if (collapseY > _scroll.height()) {
					collapseCommentsTop += qMin(collapseY - _scroll.height() - collapse->height(), 0);
				} else {
					collapseCommentsTop += qMax(collapseY, 0);
				}
			}
		}
	}
	if (_collapseComments.x() != collapseCommentsLeft || _collapseComments.y() != collapseCommentsTop) {
		_collapseComments.move(collapseCommentsLeft, collapseCommentsTop);
	}
	if (collapseCommentsVisible && _collapseComments.isHidden()) {
		_collapseComments.show();
	} else if (!collapseCommentsVisible && !_collapseComments.isHidden()) {
		_collapseComments.hide();
	}
}

void HistoryWidget::mousePressEvent(QMouseEvent *e) {
	_replyForwardPressed = QRect(0, _field.y() - st::sendPadding - st::replyHeight, st::replySkip, st::replyHeight).contains(e->pos());
	if (_replyForwardPressed && !_fieldBarCancel.isHidden()) {
		updateField();
	} else if (_inRecord && cHasAudioCapture()) {
		audioCapture()->start();

		_recording = _inField = true;
		updateControlsVisibility();
		activate();

		updateField();

		a_recordDown.start(1);
		a_recordOver.restart();
		_a_record.start();
	} else if (_inReplyEdit) {
		Ui::showPeerHistory(_peer, _editMsgId ? _editMsgId : replyToId());
	} else if (_inPinnedMsg) {
		t_assert(_pinnedBar != nullptr);
		Ui::showPeerHistory(_peer, _pinnedBar->msgId);
	}
}

void HistoryWidget::keyPressEvent(QKeyEvent *e) {
	if (!_history) return;

	if (e->key() == Qt::Key_Escape) {
		e->ignore();
	} else if (e->key() == Qt::Key_Back) {
		Ui::showChatsList();
		emit cancelled();
	} else if (e->key() == Qt::Key_PageDown) {
		_scroll.keyPressEvent(e);
	} else if (e->key() == Qt::Key_PageUp) {
		_scroll.keyPressEvent(e);
	} else if (e->key() == Qt::Key_Down) {
		if (!(e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier))) {
			_scroll.keyPressEvent(e);
		}
	} else if (e->key() == Qt::Key_Up) {
		if (!(e->modifiers() & (Qt::ShiftModifier | Qt::MetaModifier | Qt::ControlModifier))) {
			_scroll.keyPressEvent(e);
		}
	} else {
		e->ignore();
	}
}

void HistoryWidget::onFieldTabbed() {
	QString sel = _attachMention.isHidden() ? QString() : _attachMention.getSelected();
	if (!sel.isEmpty()) {
		_field.onMentionHashtagOrBotCommandInsert(sel);
	}
}

void HistoryWidget::onStickerSend(DocumentData *sticker) {
	sendExistingDocument(sticker, QString());
}

void HistoryWidget::onPhotoSend(PhotoData *photo) {
	sendExistingPhoto(photo, QString());
}

void HistoryWidget::onInlineResultSend(InlineResult *result, UserData *bot) {
	if (!_history || !result || !canSendMessages(_peer)) return;

	App::main()->readServerHistory(_history, false);
	fastShowAtEnd(_history);

	uint64 randomId = MTP::nonce<uint64>();
	FullMsgId newId(_channel, clientMsgId());

	bool lastKeyboardUsed = lastForceReplyReplied();

	bool out = !_peer->isSelf(), unread = !_peer->isSelf();
	int32 flags = newMessageFlags(_peer) | MTPDmessage::flag_media; // unread, out
	int32 sendFlags = 0;
	if (replyToId()) {
		flags |= MTPDmessage::flag_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
	}
	bool channelPost = _peer->isChannel() && !_peer->isMegagroup() && _peer->asChannel()->canPublish() && (_peer->asChannel()->isBroadcast() || _broadcast.checked());
	bool showFromName = !channelPost || _peer->asChannel()->addsSignature();
	bool silentPost = channelPost && _silent.checked();
	if (channelPost) {
		sendFlags |= MTPmessages_SendMedia::flag_broadcast;
		flags |= MTPDmessage::flag_views;
		flags |= MTPDmessage::flag_post;
	}
	if (showFromName) {
		flags |= MTPDmessage::flag_from_id;
	}
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::flag_silent;
	}
	if (bot) {
		flags |= MTPDmessage::flag_via_bot_id;
	}

	if (result->message.isEmpty()) {
		if (result->doc) {
			_history->addNewDocument(newId.msg, flags, bot ? peerToUser(bot->id) : 0, replyToId(), date(MTP_int(unixtime())), showFromName ? MTP::authedId() : 0, result->doc, result->caption);
		} else if (result->photo) {
			_history->addNewPhoto(newId.msg, flags, bot ? peerToUser(bot->id) : 0, replyToId(), date(MTP_int(unixtime())), showFromName ? MTP::authedId() : 0, result->photo, result->caption);
		} else if (result->type == qstr("gif")) {
			MTPPhotoSize thumbSize;
			QPixmap thumb;
			int32 tw = result->thumb->width(), th = result->thumb->height();
			if (tw > 0 && th > 0 && tw < 20 * th && th < 20 * tw && result->thumb->loaded()) {
				if (tw > th) {
					if (tw > 90) {
						th = th * 90 / tw;
						tw = 90;
					}
				} else if (th > 90) {
					tw = tw * 90 / th;
					th = 90;
				}
				thumbSize = MTP_photoSize(MTP_string(""), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(tw), MTP_int(th), MTP_int(0));
				thumb = result->thumb->pixNoCache(tw, th, true, false, false);
			} else {
				tw = th = 0;
				thumbSize = MTP_photoSizeEmpty(MTP_string(""));
			}
			uint64 docId = MTP::nonce<uint64>();
			QVector<MTPDocumentAttribute> attributes(1, MTP_documentAttributeFilename(MTP_string((result->content_type == qstr("video/mp4") ? "animation.gif.mp4" : "animation.gif"))));
			attributes.push_back(MTP_documentAttributeAnimated());
			attributes.push_back(MTP_documentAttributeVideo(MTP_int(result->duration), MTP_int(result->width), MTP_int(result->height)));
			MTPDocument document = MTP_document(MTP_long(docId), MTP_long(0), MTP_int(unixtime()), MTP_string(result->content_type), MTP_int(result->data().size()), thumbSize, MTP_int(MTP::maindc()), MTP_vector<MTPDocumentAttribute>(attributes));
			if (tw > 0 && th > 0) {
				App::feedDocument(document, thumb);
			}
			Local::writeStickerImage(mediaKey(DocumentFileLocation, MTP::maindc(), docId), result->data());
			_history->addNewMessage(MTP_message(MTP_int(flags), MTP_int(newId.msg), MTP_int(showFromName ? MTP::authedId() : 0), peerToMTP(_history->peer->id), MTPnullFwdHeader, MTP_int(bot ? peerToUser(bot->id) : 0), MTP_int(replyToId()), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaDocument(document, MTP_string(result->caption)), MTPnullMarkup, MTPnullEntities, MTP_int(1), MTPint()), NewMessageUnread);
		} else if (result->type == qstr("photo")) {
			QImage fileThumb(result->thumb->pix().toImage());

			QVector<MTPPhotoSize> photoSizes;

			QPixmap thumb = (fileThumb.width() > 100 || fileThumb.height() > 100) ? QPixmap::fromImage(fileThumb.scaled(100, 100, Qt::KeepAspectRatio, Qt::SmoothTransformation), Qt::ColorOnly) : QPixmap::fromImage(fileThumb);
			ImagePtr thumbPtr = ImagePtr(thumb, "JPG");
			photoSizes.push_back(MTP_photoSize(MTP_string("s"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(thumb.width()), MTP_int(thumb.height()), MTP_int(0)));

			QSize medium = resizeKeepAspect(result->width, result->height, 320, 320);
			photoSizes.push_back(MTP_photoSize(MTP_string("m"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(medium.width()), MTP_int(medium.height()), MTP_int(0)));

			photoSizes.push_back(MTP_photoSize(MTP_string("x"), MTP_fileLocationUnavailable(MTP_long(0), MTP_int(0), MTP_long(0)), MTP_int(result->width), MTP_int(result->height), MTP_int(0)));

			uint64 photoId = MTP::nonce<uint64>();
			PhotoData *ph = App::photoSet(photoId, 0, 0, unixtime(), thumbPtr, ImagePtr(medium.width(), medium.height()), ImagePtr(result->width, result->height));
			MTPPhoto photo = MTP_photo(MTP_long(photoId), MTP_long(0), MTP_int(ph->date), MTP_vector<MTPPhotoSize>(photoSizes));

			_history->addNewMessage(MTP_message(MTP_int(flags), MTP_int(newId.msg), MTP_int(showFromName ? MTP::authedId() : 0), peerToMTP(_history->peer->id), MTPnullFwdHeader, MTP_int(bot ? peerToUser(bot->id) : 0), MTP_int(replyToId()), MTP_int(unixtime()), MTP_string(""), MTP_messageMediaPhoto(photo, MTP_string(result->caption)), MTPnullMarkup, MTPnullEntities, MTP_int(1), MTPint()), NewMessageUnread);
		}
	} else {
		flags |= MTPDmessage::flag_entities;
		if (result->noWebPage) {
			sendFlags |= MTPmessages_SendMessage::flag_no_webpage;
		}
		_history->addNewMessage(MTP_message(MTP_int(flags), MTP_int(newId.msg), MTP_int(showFromName ? MTP::authedId() : 0), peerToMTP(_history->peer->id), MTPnullFwdHeader, MTP_int(bot ? peerToUser(bot->id) : 0), MTP_int(replyToId()), MTP_int(unixtime()), MTP_string(result->message), MTP_messageMediaEmpty(), MTPnullMarkup, linksToMTP(result->entities), MTP_int(1), MTPint()), NewMessageUnread);
	}
	_history->sendRequestId = MTP::send(MTPmessages_SendInlineBotResult(MTP_int(sendFlags), _peer->input, MTP_int(replyToId()), MTP_long(randomId), MTP_long(result->queryId), MTP_string(result->id)), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, _history->sendRequestId);
	App::main()->finishForwarding(_history, _broadcast.checked(), _silent.checked());
	cancelReply(lastKeyboardUsed);

	App::historyRegRandom(randomId, newId);

	setFieldText(QString());
	_saveDraftText = true;
	_saveDraftStart = getms();
	onDraftSave();

	RecentInlineBots &bots(cRefRecentInlineBots());
	int32 index = bots.indexOf(bot);
	if (index) {
		if (index > 0) {
			bots.removeAt(index);
		} else if (bots.size() >= RecentInlineBotsLimit) {
			bots.resize(RecentInlineBotsLimit - 1);
		}
		bots.push_front(bot);
		Local::writeRecentHashtagsAndBots();
	}

	if (!_attachMention.isHidden()) _attachMention.hideStart();
	if (!_attachType.isHidden()) _attachType.hideStart();
	if (!_emojiPan.isHidden()) _emojiPan.hideStart();

	_field.setFocus();
}

HistoryWidget::PinnedBar::PinnedBar(MsgId msgId, HistoryWidget *parent)
: msgId(msgId)
, msg(0)
, cancel(parent, st::replyCancel)
, shadow(parent, st::shadowColor) {
}

void HistoryWidget::updatePinnedBar(bool force) {
	if (!_pinnedBar) {
		return;
	}
	if (!force) {
		if (_pinnedBar->msg) {
			return;
		}
	}
	t_assert(_history != nullptr);

	if (!_pinnedBar->msg) {
		_pinnedBar->msg = App::histItemById(_history->channelId(), _pinnedBar->msgId);
	}
	if (_pinnedBar->msg) {
		_pinnedBar->text.setText(st::msgFont, _pinnedBar->msg->inDialogsText(), _textDlgOptions);
		update();
	} else if (force) {
		if (_peer && _peer->isMegagroup()) {
			_peer->asChannel()->mgInfo->pinnedMsgId = 0;
		}
		delete _pinnedBar;
		_pinnedBar = nullptr;
		_inPinnedMsg = false;
		resizeEvent(0);
		update();
	}
}

bool HistoryWidget::pinnedMsgVisibilityUpdated() {
	bool result = false;
	MsgId pinnedMsgId = (_peer && _peer->isMegagroup()) ? _peer->asChannel()->mgInfo->pinnedMsgId : 0;
	if (pinnedMsgId && !_peer->asChannel()->amCreator() && !_peer->asChannel()->amEditor()) {
		Global::HiddenPinnedMessagesMap::const_iterator it = Global::HiddenPinnedMessages().constFind(_peer->id);
		if (it != Global::HiddenPinnedMessages().cend()) {
			if (it.value() == pinnedMsgId) {
				pinnedMsgId = 0;
			} else {
				Global::RefHiddenPinnedMessages().remove(_peer->id);
				Local::writeUserSettings();
			}
		}
	}
	if (pinnedMsgId) {
		if (!_pinnedBar) {
			_pinnedBar = new PinnedBar(pinnedMsgId, this);
			if (_a_show.animating()) {
				_pinnedBar->cancel.hide();
				_pinnedBar->shadow.hide();
			} else {
				_pinnedBar->cancel.show();
				_pinnedBar->shadow.show();
			}
			connect(&_pinnedBar->cancel, SIGNAL(clicked()), this, SLOT(onPinnedHide()));
			_reportSpamPanel.raise();
			_sideShadow.raise();
			_topShadow.raise();
			updatePinnedBar();
			result = true;
			_scroll.scrollToY(_scroll.scrollTop() + st::replyHeight);
		} else if (_pinnedBar->msgId != pinnedMsgId) {
			_pinnedBar->msgId = pinnedMsgId;
			_pinnedBar->msg = 0;
			_pinnedBar->text.clean();
			updatePinnedBar();
			update();
		}
		if (!_pinnedBar->msg && App::api()) {
			App::api()->requestMessageData(_peer->asChannel(), _pinnedBar->msgId, new ReplyEditMessageDataCallback());
		}
	} else if (_pinnedBar) {
		delete _pinnedBar;
		_pinnedBar = nullptr;
		result = true;
		_scroll.scrollToY(_scroll.scrollTop() - st::replyHeight);
		resizeEvent(0);
	}
	return result;
}

void HistoryWidget::ReplyEditMessageDataCallback::call(ChannelData *channel, MsgId msgId) const {
	if (App::main()) {
		App::main()->messageDataReceived(channel, msgId);
	}
}

void HistoryWidget::sendExistingDocument(DocumentData *doc, const QString &caption) {
	if (!_history || !doc || !canSendMessages(_peer)) return;

	App::main()->readServerHistory(_history, false);
	fastShowAtEnd(_history);

	uint64 randomId = MTP::nonce<uint64>();
	FullMsgId newId(_channel, clientMsgId());

	bool lastKeyboardUsed = lastForceReplyReplied();

	bool out = !_peer->isSelf(), unread = !_peer->isSelf();
	int32 flags = newMessageFlags(_peer) | MTPDmessage::flag_media; // unread, out
	int32 sendFlags = 0;
	if (replyToId()) {
		flags |= MTPDmessage::flag_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
	}
	bool channelPost = _peer->isChannel() && !_peer->isMegagroup() && _peer->asChannel()->canPublish() && (_peer->asChannel()->isBroadcast() || _broadcast.checked());
	bool showFromName = !channelPost || _peer->asChannel()->addsSignature();
	bool silentPost = channelPost && _silent.checked();
	if (channelPost) {
		sendFlags |= MTPmessages_SendMedia::flag_broadcast;
		flags |= MTPDmessage::flag_views;
		flags |= MTPDmessage::flag_post;
	}
	if (showFromName) {
		flags |= MTPDmessage::flag_from_id;
	}
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::flag_silent;
	}
	_history->addNewDocument(newId.msg, flags, 0, replyToId(), date(MTP_int(unixtime())), showFromName ? MTP::authedId() : 0, doc, caption);

	_history->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), _peer->input, MTP_int(replyToId()), MTP_inputMediaDocument(MTP_inputDocument(MTP_long(doc->id), MTP_long(doc->access)), MTP_string(caption)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, _history->sendRequestId);
	App::main()->finishForwarding(_history, _broadcast.checked(), _silent.checked());
	cancelReply(lastKeyboardUsed);

	if (doc->sticker()) App::main()->incrementSticker(doc);

	App::historyRegRandom(randomId, newId);

	if (_attachMention.stickersShown()) {
		setFieldText(QString());
		_saveDraftText = true;
		_saveDraftStart = getms();
		onDraftSave();
	}

	if (!_attachMention.isHidden()) _attachMention.hideStart();
	if (!_attachType.isHidden()) _attachType.hideStart();
	if (!_emojiPan.isHidden()) _emojiPan.hideStart();

	_field.setFocus();
}

void HistoryWidget::sendExistingPhoto(PhotoData *photo, const QString &caption) {
	if (!_history || !photo || !canSendMessages(_peer)) return;

	App::main()->readServerHistory(_history, false);
	fastShowAtEnd(_history);

	uint64 randomId = MTP::nonce<uint64>();
	FullMsgId newId(_channel, clientMsgId());

	bool lastKeyboardUsed = lastForceReplyReplied();

	bool out = !_peer->isSelf(), unread = !_peer->isSelf();
	int32 flags = newMessageFlags(_peer) | MTPDmessage::flag_media; // unread, out
	int32 sendFlags = 0;
	if (replyToId()) {
		flags |= MTPDmessage::flag_reply_to_msg_id;
		sendFlags |= MTPmessages_SendMedia::flag_reply_to_msg_id;
	}
	bool channelPost = _peer->isChannel() && !_peer->isMegagroup() && _peer->asChannel()->canPublish() && (_peer->asChannel()->isBroadcast() || _broadcast.checked());
	bool showFromName = !channelPost || _peer->asChannel()->addsSignature();
	bool silentPost = channelPost && _silent.checked();
	if (channelPost) {
		sendFlags |= MTPmessages_SendMedia::flag_broadcast;
		flags |= MTPDmessage::flag_views;
		flags |= MTPDmessage::flag_post;
	}
	if (showFromName) {
		flags |= MTPDmessage::flag_from_id;
	}
	if (silentPost) {
		sendFlags |= MTPmessages_SendMedia::flag_silent;
	}
	_history->addNewPhoto(newId.msg, flags, 0, replyToId(), date(MTP_int(unixtime())), showFromName ? MTP::authedId() : 0, photo, caption);

	_history->sendRequestId = MTP::send(MTPmessages_SendMedia(MTP_int(sendFlags), _peer->input, MTP_int(replyToId()), MTP_inputMediaPhoto(MTP_inputPhoto(MTP_long(photo->id), MTP_long(photo->access)), MTP_string(caption)), MTP_long(randomId), MTPnullMarkup), App::main()->rpcDone(&MainWidget::sentUpdatesReceived), App::main()->rpcFail(&MainWidget::sendMessageFail), 0, 0, _history->sendRequestId);
	App::main()->finishForwarding(_history, _broadcast.checked(), _silent.checked());
	cancelReply(lastKeyboardUsed);

	App::historyRegRandom(randomId, newId);

	if (!_attachMention.isHidden()) _attachMention.hideStart();
	if (!_attachType.isHidden()) _attachType.hideStart();
	if (!_emojiPan.isHidden()) _emojiPan.hideStart();

	_field.setFocus();
}

void HistoryWidget::setFieldText(const QString &text, int32 textUpdateEventsFlags, bool clearUndoHistory) {
	_textUpdateEventsFlags = textUpdateEventsFlags;
	_field.setTextFast(text, clearUndoHistory);
	_textUpdateEventsFlags = TextUpdateEventsSaveDraft | TextUpdateEventsSendTyping;

	_previewCancelled = false;
	_previewData = 0;
	if (_previewRequest) {
		MTP::cancel(_previewRequest);
		_previewRequest = 0;
	}
	_previewLinks.clear();
}

void HistoryWidget::onReplyToMessage() {
	HistoryItem *to = App::contextItem();
	if (!to || to->id <= 0 || !_canSendMessages) return;

	if (to->history() == _migrated) {
		if (to->isGroupMigrate() && _history->blocks.size() > 1 && _history->blocks.at(1)->items.front()->isGroupMigrate() && _history != _migrated) {
			App::contextItem(_history->blocks.at(1)->items.front());
			onReplyToMessage();
			App::contextItem(to);
		} else {
			LayeredWidget *box = 0;
			if (to->type() != HistoryItemMsg || to->serviceMsg()) {
				box = new InformBox(lang(lng_reply_cant));
			} else {
				box = new ConfirmBox(lang(lng_reply_cant_forward), lang(lng_selected_forward));
				connect(box, SIGNAL(confirmed()), this, SLOT(onForwardHere()));
			}
			Ui::showLayer(box);
		}
		return;
	}

	App::main()->cancelForwarding();

	if (_editMsgId) {
		if (!_history->msgDraft) {
			_history->setMsgDraft(new HistoryDraft(QString(), to->id, MessageCursor(), false));
		} else {
			_history->msgDraft->msgId = to->id;
		}
	} else {
		_replyEditMsg = to;
		_replyToId = to->id;
		_replyEditMsgText.setText(st::msgFont, _replyEditMsg->inDialogsText(), _textDlgOptions);

		updateBotKeyboard();

		if (!_field.isHidden()) _fieldBarCancel.show();
		updateMouseTracking();
		updateReplyToName();
		resizeEvent(0);
		updateField();
	}

	_saveDraftText = true;
	_saveDraftStart = getms();
	onDraftSave();

	_field.setFocus();
}

void HistoryWidget::onEditMessage() {
	HistoryItem *to = App::contextItem();
	if (!to || !to->history()->peer->isChannel()) return;

	EditCaptionBox *box = new EditCaptionBox(to);
	if (box->captionFound()) {
		Ui::showLayer(box);
	} else {
		delete box;

		if (_replyToId || !_field.getLastText().isEmpty()) {
			_history->setMsgDraft(new HistoryDraft(_field, _replyToId, _previewCancelled));
		} else {
			_history->setMsgDraft(nullptr);
		}

		QString text(textApplyEntities(to->originalText(), to->originalEntities()));
		_history->setEditDraft(new HistoryEditDraft(text, to->id, MessageCursor(text.size(), text.size(), QFIXED_MAX), false));
		applyDraft(false);

		_previewData = 0;
		if (HistoryMedia *media = to->getMedia()) {
			if (media->type() == MediaTypeWebPage) {
				_previewData = static_cast<HistoryWebPage*>(media)->webpage();
				updatePreview();
			}
		}
		if (!_previewData) {
			onPreviewParse();
		}

		updateBotKeyboard();

		if (!_field.isHidden()) _fieldBarCancel.show();
		updateFieldPlaceholder();
		updateMouseTracking();
		updateReplyToName();
		resizeEvent(0);
		updateField();

		_saveDraftText = true;
		_saveDraftStart = getms();
		onDraftSave();

		_field.setFocus();
	}
}

void HistoryWidget::onPinMessage() {
	HistoryItem *to = App::contextItem();
	if (!to || !to->canPin() || !_peer || !_peer->isMegagroup()) return;

	Ui::showLayer(new PinMessageBox(_peer->asChannel(), to->id));
}

void HistoryWidget::onUnpinMessage() {
	if (!_peer || !_peer->isMegagroup()) return;

	ConfirmBox *box = new ConfirmBox(lang(lng_pinned_unpin_sure), lang(lng_pinned_unpin));
	connect(box, SIGNAL(confirmed()), this, SLOT(onUnpinMessageSure()));
	Ui::showLayer(box);
}

void HistoryWidget::onUnpinMessageSure() {
	if (!_peer || !_peer->isMegagroup()) return;

	_peer->asChannel()->mgInfo->pinnedMsgId = 0;
	if (pinnedMsgVisibilityUpdated()) {
		resizeEvent(0);
		update();
	}

	Ui::hideLayer();
	MTP::send(MTPchannels_UpdatePinnedMessage(MTP_int(0), _peer->asChannel()->inputChannel, MTP_int(0)), rpcDone(&HistoryWidget::unpinDone));
}

void HistoryWidget::unpinDone(const MTPUpdates &updates) {
	if (App::main()) {
		App::main()->sentUpdatesReceived(updates);
	}
}

void HistoryWidget::onPinnedHide() {
	if (!_peer || !_peer->isMegagroup()) return;
	if (!_peer->asChannel()->mgInfo->pinnedMsgId) {
		if (pinnedMsgVisibilityUpdated()) {
			resizeEvent(0);
			update();
		}
		return;
	}

	if (_peer->asChannel()->amCreator() || _peer->asChannel()->amEditor()) {
		onUnpinMessage();
	} else {
		Global::RefHiddenPinnedMessages().insert(_peer->id, _peer->asChannel()->mgInfo->pinnedMsgId);
		Local::writeUserSettings();
		if (pinnedMsgVisibilityUpdated()) {
			resizeEvent(0);
			update();
		}
	}
}

void HistoryWidget::onCopyPostLink() {
	HistoryItem *to = App::contextItem();
	if (!to || !to->hasDirectLink()) return;

	QApplication::clipboard()->setText(to->directLink());
}

bool HistoryWidget::lastForceReplyReplied(const FullMsgId &replyTo) const {
	if (replyTo.msg > 0 && replyTo.channel != _channel) return false;
	return _keyboard.forceReply() && _keyboard.forMsgId() == FullMsgId(_channel, _history->lastKeyboardId) && _keyboard.forMsgId().msg == (replyTo.msg < 0 ? replyToId() : replyTo.msg);
}

void HistoryWidget::cancelReply(bool lastKeyboardUsed) {
	bool wasReply = _replyToId || (_history && _history->msgDraft && _history->msgDraft->msgId);
	if (_replyToId) {
		_replyEditMsg = 0;
		_replyToId = 0;
		mouseMoveEvent(0);
		if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !_kbReplyTo) {
			_fieldBarCancel.hide();
			updateMouseTracking();
		}

		updateBotKeyboard();

		resizeEvent(0);
		update();
	} else if (wasReply) {
		if (_history->msgDraft->text.isEmpty()) {
			_history->setMsgDraft(nullptr);
		} else {
			_history->msgDraft->msgId = 0;
		}
	}
	if (wasReply) {
		_saveDraftText = true;
		_saveDraftStart = getms();
		onDraftSave();
	}
	if (!_editMsgId && _keyboard.singleUse() && _keyboard.forceReply() && lastKeyboardUsed) {
		if (_kbReplyTo) {
			onKbToggle(false);
		}
	}
}

void HistoryWidget::cancelEdit() {
	if (!_editMsgId) return;

	_editMsgId = 0;
	_replyEditMsg = 0;
	_history->setEditDraft(nullptr);
	applyDraft();

	if (_saveEditMsgRequestId) {
		MTP::cancel(_saveEditMsgRequestId);
		_saveEditMsgRequestId = 0;
	}

	_saveDraftText = true;
	_saveDraftStart = getms();
	onDraftSave();

	mouseMoveEvent(0);
	if (!readyToForward() && (!_previewData || _previewData->pendingTill < 0) && !replyToId()) {
		_fieldBarCancel.hide();
		updateMouseTracking();
	}

	int32 old = _textUpdateEventsFlags;
	_textUpdateEventsFlags = 0;
	onTextChange();
	_textUpdateEventsFlags = old;

	updateBotKeyboard();
	updateFieldPlaceholder();

	resizeEvent(0);
	update();
}

void HistoryWidget::cancelForwarding() {
	updateControlsVisibility();
	resizeEvent(0);
	update();
}

void HistoryWidget::onFieldBarCancel() {
	_replyForwardPressed = false;
	if (_previewData && _previewData->pendingTill >= 0) {
		_previewCancelled = true;
		previewCancel();

		_saveDraftText = true;
		_saveDraftStart = getms();
		onDraftSave();
	} else if (_editMsgId) {
		cancelEdit();
	} else if (readyToForward()) {
		App::main()->cancelForwarding();
	} else if (_replyToId) {
		cancelReply();
	} else if (_kbReplyTo) {
		onKbToggle();
	}
}

void HistoryWidget::onStickerPackInfo() {
	if (HistoryMedia *media = (App::contextItem() ? App::contextItem()->getMedia() : 0)) {
		if (media->type() == MediaTypeSticker) {
			DocumentData *doc = media->getDocument();
			if (doc && doc->sticker() && doc->sticker()->set.type() != mtpc_inputStickerSetEmpty) {
				App::main()->stickersBox(doc->sticker()->set);
			}
		}
	}
}

void HistoryWidget::previewCancel() {
	MTP::cancel(_previewRequest);
	_previewRequest = 0;
	_previewData = 0;
	_previewLinks.clear();
	updatePreview();
	if (!_editMsgId && !_replyToId && !readyToForward() && !_kbReplyTo) {
		_fieldBarCancel.hide();
		updateMouseTracking();
	}
}

void HistoryWidget::onPreviewParse() {
	if (_previewCancelled) return;
	_field.parseLinks();
}

void HistoryWidget::onPreviewCheck() {
	if (_previewCancelled) return;
	QStringList linksList = _field.linksList();
	QString newLinks = linksList.join(' ');
	if (newLinks != _previewLinks) {
		MTP::cancel(_previewRequest);
		_previewLinks = newLinks;
		if (_previewLinks.isEmpty()) {
			if (_previewData && _previewData->pendingTill >= 0) previewCancel();
		} else {
			PreviewCache::const_iterator i = _previewCache.constFind(_previewLinks);
			if (i == _previewCache.cend()) {
				_previewRequest = MTP::send(MTPmessages_GetWebPagePreview(MTP_string(_previewLinks)), rpcDone(&HistoryWidget::gotPreview, _previewLinks));
			} else if (i.value()) {
				_previewData = App::webPage(i.value());
				updatePreview();
			} else {
				if (_previewData && _previewData->pendingTill >= 0) previewCancel();
			}
		}
	}
}

void HistoryWidget::onPreviewTimeout() {
	if (_previewData && _previewData->pendingTill > 0 && !_previewLinks.isEmpty()) {
		_previewRequest = MTP::send(MTPmessages_GetWebPagePreview(MTP_string(_previewLinks)), rpcDone(&HistoryWidget::gotPreview, _previewLinks));
	}
}

void HistoryWidget::gotPreview(QString links, const MTPMessageMedia &result, mtpRequestId req) {
	if (req == _previewRequest) {
		_previewRequest = 0;
	}
	if (result.type() == mtpc_messageMediaWebPage) {
		WebPageData *data = App::feedWebPage(result.c_messageMediaWebPage().vwebpage);
		_previewCache.insert(links, data->id);
		if (data->pendingTill > 0 && data->pendingTill <= unixtime()) {
			data->pendingTill = -1;
		}
		if (links == _previewLinks && !_previewCancelled) {
			_previewData = (data->id && data->pendingTill >= 0) ? data : 0;
			updatePreview();
		}
		if (App::main()) App::main()->webPagesUpdate();
	} else if (result.type() == mtpc_messageMediaEmpty) {
		_previewCache.insert(links, 0);
		if (links == _previewLinks && !_previewCancelled) {
			_previewData = 0;
			updatePreview();
		}
	}
}

void HistoryWidget::updatePreview() {
	_previewTimer.stop();
	if (_previewData && _previewData->pendingTill >= 0) {
		_fieldBarCancel.show();
		updateMouseTracking();
		if (_previewData->pendingTill) {
			_previewTitle.setText(st::msgServiceNameFont, lang(lng_preview_loading), _textNameOptions);
			_previewDescription.setText(st::msgFont, _previewLinks.splitRef(' ').at(0).toString(), _textDlgOptions);

			int32 t = (_previewData->pendingTill - unixtime()) * 1000;
			if (t <= 0) t = 1;
			_previewTimer.start(t);
		} else {
			QString title, desc;
			if (_previewData->siteName.isEmpty()) {
				if (_previewData->title.isEmpty()) {
					if (_previewData->description.isEmpty()) {
						title = _previewData->author;
						desc = ((_previewData->doc && !_previewData->doc->name.isEmpty()) ? _previewData->doc->name : _previewData->url);
					} else {
						title = _previewData->description;
						desc = _previewData->author.isEmpty() ? ((_previewData->doc && !_previewData->doc->name.isEmpty()) ? _previewData->doc->name : _previewData->url) : _previewData->author;
					}
				} else {
					title = _previewData->title;
					desc = _previewData->description.isEmpty() ? (_previewData->author.isEmpty() ? ((_previewData->doc && !_previewData->doc->name.isEmpty()) ? _previewData->doc->name : _previewData->url) : _previewData->author) : _previewData->description;
				}
			} else {
				title = _previewData->siteName;
				desc = _previewData->title.isEmpty() ? (_previewData->description.isEmpty() ? (_previewData->author.isEmpty() ? ((_previewData->doc && !_previewData->doc->name.isEmpty()) ? _previewData->doc->name : _previewData->url) : _previewData->author) : _previewData->description) : _previewData->title;
			}
			if (title.isEmpty()) {
				if (_previewData->photo) {
					title = lang(lng_attach_photo);
				} else if (_previewData->doc) {
					title = lang(lng_attach_file);
				}
			}
			_previewTitle.setText(st::msgServiceNameFont, title, _textNameOptions);
			_previewDescription.setText(st::msgFont, desc, _textDlgOptions);
		}
	} else if (!readyToForward() && !replyToId() && !_editMsgId) {
		_fieldBarCancel.hide();
		updateMouseTracking();
	}
	resizeEvent(0);
	update();
}

void HistoryWidget::onCancel() {
	if (_inlineBot && _field.getLastText().startsWith('@' + _inlineBot->username + ' ')) {
		setFieldText(QString(), TextUpdateEventsSaveDraft, false);
	} else if (!_attachMention.isHidden()) {
		_attachMention.hideStart();
	} else  {
		Ui::showChatsList();
		emit cancelled();
	}
}

void HistoryWidget::onFullPeerUpdated(PeerData *data) {
	int32 newScrollTop = _scroll.scrollTop();
	if (_list && data == _peer) {
		bool newCanSendMessages = canSendMessages(_peer);
		if (newCanSendMessages != _canSendMessages) {
			_canSendMessages = newCanSendMessages;
			if (!_canSendMessages) {
				cancelReply();
			}
			updateControlsVisibility();
		}
		onCheckMentionDropdown();
		updateReportSpamStatus();
		int32 lh = _list->height(), st = _scroll.scrollTop();
		_list->updateBotInfo();
		newScrollTop = st + _list->height() - lh;
	}
	if (updateCmdStartShown()) {
		updateControlsVisibility();
		resizeEvent(0);
		update();
	} else if (!_scroll.isHidden() && _unblock.isHidden() == isBlocked()) {
		updateControlsVisibility();
		resizeEvent(0);
	}
	if (newScrollTop != _scroll.scrollTop()) {
		if (_scroll.isVisible()) {
			_scroll.scrollToY(newScrollTop);
		} else {
			_history->lastScrollTop = newScrollTop;
		}
	}
}

void HistoryWidget::peerUpdated(PeerData *data) {
	if (data && data == _peer) {
		if (data->migrateTo()) {
			Ui::showPeerHistory(data->migrateTo(), ShowAtUnreadMsgId);
			QTimer::singleShot(ReloadChannelMembersTimeout, App::api(), SLOT(delayedRequestParticipantsCount()));
			return;
		}
		bool resize = false;
		if (pinnedMsgVisibilityUpdated()) {
			resize = true;
		}
		updateListSize();
		if (_peer->isChannel()) updateReportSpamStatus();
		if (App::api()) {
			if (data->isChat() && data->asChat()->noParticipantInfo()) {
				App::api()->requestFullPeer(data);
			} else if (data->isUser() && data->asUser()->blocked == UserBlockUnknown) {
				App::api()->requestFullPeer(data);
			} else if (data->isMegagroup() && !data->asChannel()->mgInfo->botStatus) {
				App::api()->requestBots(data->asChannel());
			}
		}
		if (!_a_show.animating()) {
			if (_unblock.isHidden() == isBlocked() || (!isBlocked() && _joinChannel.isHidden() == isJoinChannel())) {
				resize = true;
			}
			bool newCanSendMessages = canSendMessages(_peer);
			if (newCanSendMessages != _canSendMessages) {
				_canSendMessages = newCanSendMessages;
				if (!_canSendMessages) {
					cancelReply();
				}
				resize = true;
			}
			updateControlsVisibility();
			if (resize) {
				resizeEvent(0);
				update();
			}
		}
		App::main()->updateOnlineDisplay();
	}
}

void HistoryWidget::onForwardSelected() {
	if (!_list) return;
	App::main()->forwardLayer(true);
}

void HistoryWidget::onDeleteSelected() {
	if (!_list) return;

	SelectedItemSet sel;
	_list->fillSelectedItems(sel);
	if (sel.isEmpty()) return;

	App::main()->deleteLayer(sel.size());
}

void HistoryWidget::onDeleteSelectedSure() {
	if (!_list) return;

	SelectedItemSet sel;
	_list->fillSelectedItems(sel);
	if (sel.isEmpty()) return;

	QMap<PeerData*, QVector<MTPint> > ids;
	for (SelectedItemSet::const_iterator i = sel.cbegin(), e = sel.cend(); i != e; ++i) {
		if (i.value()->id > 0) {
			ids[i.value()->history()->peer].push_back(MTP_int(i.value()->id));
		}
	}

	onClearSelected();
	for (SelectedItemSet::const_iterator i = sel.cbegin(), e = sel.cend(); i != e; ++i) {
		i.value()->destroy();
	}
	Notify::historyItemsResized();
	Ui::hideLayer();

	for (QMap<PeerData*, QVector<MTPint> >::const_iterator i = ids.cbegin(), e = ids.cend(); i != e; ++i) {
		App::main()->deleteMessages(i.key(), i.value());
	}
}

void HistoryWidget::onDeleteContextSure() {
	HistoryItem *item = App::contextItem();
	if (!item || item->type() != HistoryItemMsg) {
		return;
	}

	QVector<MTPint> toDelete(1, MTP_int(item->id));
	History *h = item->history();
	bool wasOnServer = (item->id > 0), wasLast = (h->lastMsg == item);
	item->destroy();
	if (!wasOnServer && wasLast && !h->lastMsg) {
		App::main()->checkPeerHistory(h->peer);
	}

	Notify::historyItemsResized();
	Ui::hideLayer();

	if (wasOnServer) {
		App::main()->deleteMessages(h->peer, toDelete);
	}
}

void HistoryWidget::onListEscapePressed() {
	if (_selCount && _list) {
		onClearSelected();
	} else {
		onCancel();
	}
}

void HistoryWidget::onClearSelected() {
	if (_list) _list->clearSelectedItems();
}

void HistoryWidget::onAnimActiveStep() {
	if (!_history || !_activeAnimMsgId || (_activeAnimMsgId < 0 && (!_migrated || -_activeAnimMsgId >= ServerMaxMsgId))) {
		return _animActiveTimer.stop();
	}

	HistoryItem *item = (_activeAnimMsgId < 0 && -_activeAnimMsgId < ServerMaxMsgId && _migrated) ? App::histItemById(_migrated->channelId(), -_activeAnimMsgId) : App::histItemById(_channel, _activeAnimMsgId);
	if (!item || item->detached()) return _animActiveTimer.stop();

	if (getms() - _animActiveStart > st::activeFadeInDuration + st::activeFadeOutDuration) {
		stopAnimActive();
	} else {
		Ui::repaintHistoryItem(item);
	}
}

uint64 HistoryWidget::animActiveTimeStart(const HistoryItem *msg) const {
	if (!msg) return 0;
	if ((msg->history() == _history && msg->id == _activeAnimMsgId) || (_migrated && msg->history() == _migrated && msg->id == -_activeAnimMsgId)) {
		return _animActiveTimer.isActive() ? _animActiveStart : 0;
	}
	return 0;
}

void HistoryWidget::stopAnimActive() {
	_animActiveTimer.stop();
	_activeAnimMsgId = 0;
}

void HistoryWidget::fillSelectedItems(SelectedItemSet &sel, bool forDelete) {
	if (_list) _list->fillSelectedItems(sel, forDelete);
}

void HistoryWidget::updateTopBarSelection() {
	if (!_list) {
		App::main()->topBar()->showSelected(0);
		return;
	}

	int32 selectedForForward, selectedForDelete;
	_list->getSelectionState(selectedForForward, selectedForDelete);
	_selCount = selectedForForward ? selectedForForward : selectedForDelete;
	App::main()->topBar()->showSelected(_selCount > 0 ? _selCount : 0, (selectedForDelete == selectedForForward));
	updateControlsVisibility();
	updateListSize();
	if (!Ui::isLayerShown() && !App::passcoded()) {
		if (_selCount || (_list && _list->wasSelectedText()) || _recording || isBotStart() || isBlocked() || !_canSendMessages) {
			_list->setFocus();
		} else {
			_field.setFocus();
		}
	}
	App::main()->topBar()->update();
	update();
}

void HistoryWidget::messageDataReceived(ChannelData *channel, MsgId msgId) {
	if (!_peer || _peer->asChannel() != channel || !msgId) return;
	if (_editMsgId == msgId || _replyToId == msgId) {
		updateReplyEditTexts(true);
	}
	if (_pinnedBar && _pinnedBar->msgId == msgId) {
		updatePinnedBar(true);
	}
}

void HistoryWidget::updateReplyEditTexts(bool force) {
	if (!force) {
		if (_replyEditMsg || (!_editMsgId && !_replyToId)) {
			return;
		}
	}
	if (!_replyEditMsg) {
		_replyEditMsg = App::histItemById(_channel, _editMsgId ? _editMsgId : _replyToId);
	}
	if (_replyEditMsg) {
		_replyEditMsgText.setText(st::msgFont, _replyEditMsg->inDialogsText(), _textDlgOptions);

		updateBotKeyboard();

		if (!_field.isHidden() || _recording) {
			_fieldBarCancel.show();
			updateMouseTracking();
		}
		updateReplyToName();
		updateField();
	} else if (force) {
		if (_editMsgId) {
			cancelEdit();
		} else {
			cancelReply();
		}
	}
}

void HistoryWidget::updateForwarding(bool force) {
	if (readyToForward()) {
		updateControlsVisibility();
	} else {
		resizeEvent(0);
		update();
	}
}

void HistoryWidget::updateReplyToName() {
	if (_editMsgId) return;
	if (!_replyEditMsg && (_replyToId || !_kbReplyTo)) return;
	_replyToName.setText(st::msgServiceNameFont, App::peerName((_replyEditMsg ? _replyEditMsg : _kbReplyTo)->author()), _textNameOptions);
	_replyToNameVersion = (_replyEditMsg ? _replyEditMsg : _kbReplyTo)->author()->nameVersion;
}

void HistoryWidget::updateField() {
	int32 fy = _scroll.y() + _scroll.height();
	update(0, fy, width(), height() - fy);
}

void HistoryWidget::drawField(Painter &p) {
	int32 backy = _field.y() - st::sendPadding, backh = _field.height() + 2 * st::sendPadding;
	Text *from = 0, *text = 0;
	bool serviceColor = false, hasForward = readyToForward();
	ImagePtr preview;
	HistoryItem *drawMsgText = (_editMsgId || _replyToId) ? _replyEditMsg : _kbReplyTo;
	if (_editMsgId || _replyToId || (!hasForward && _kbReplyTo)) {
		if (!_editMsgId && drawMsgText && drawMsgText->author()->nameVersion > _replyToNameVersion) {
			updateReplyToName();
		}
		backy -= st::replyHeight;
		backh += st::replyHeight;
	} else if (hasForward) {
		App::main()->fillForwardingInfo(from, text, serviceColor, preview);
		backy -= st::replyHeight;
		backh += st::replyHeight;
	} else if (_previewData && _previewData->pendingTill >= 0) {
		backy -= st::replyHeight;
		backh += st::replyHeight;
	}
	bool drawPreview = (_previewData && _previewData->pendingTill >= 0) && !_replyForwardPressed;
	p.fillRect(0, backy, width(), backh, st::taMsgField.bgColor->b);
	if (_editMsgId || _replyToId || (!hasForward && _kbReplyTo)) {
		int32 replyLeft = st::replySkip;
		p.drawPixmap(QPoint(st::replyIconPos.x(), backy + st::replyIconPos.y()), App::sprite(), _editMsgId ? st::editIcon : st::replyIcon);
		if (!drawPreview) {
			if (drawMsgText) {
				if (drawMsgText->getMedia() && drawMsgText->getMedia()->hasReplyPreview()) {
					ImagePtr replyPreview = drawMsgText->getMedia()->replyPreview();
					if (!replyPreview->isNull()) {
						QRect to(replyLeft, backy + st::msgReplyPadding.top(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
						p.drawPixmap(to.x(), to.y(), replyPreview->pixSingle(replyPreview->width() / cIntRetinaFactor(), replyPreview->height() / cIntRetinaFactor(), to.width(), to.height()));
					}
					replyLeft += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
				}
				p.setPen(st::replyColor);
				if (_editMsgId) {
					p.setFont(st::msgServiceNameFont);
					p.drawText(replyLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->ascent, lang(lng_edit_message));
				} else {
					_replyToName.drawElided(p, replyLeft, backy + st::msgReplyPadding.top(), width() - replyLeft - _fieldBarCancel.width() - st::msgReplyPadding.right());
				}
				p.setPen((((drawMsgText->toHistoryMessage() && drawMsgText->toHistoryMessage()->emptyText()) || drawMsgText->serviceMsg()) ? st::msgInDateFg : st::msgColor)->p);
				_replyEditMsgText.drawElided(p, replyLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, width() - replyLeft - _fieldBarCancel.width() - st::msgReplyPadding.right());
			} else {
				p.setFont(st::msgDateFont->f);
				p.setPen(st::msgInDateFg->p);
				p.drawText(replyLeft, backy + st::msgReplyPadding.top() + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2 + st::msgDateFont->ascent, st::msgDateFont->elided(lang(lng_profile_loading), width() - replyLeft - _fieldBarCancel.width() - st::msgReplyPadding.right()));
			}
		}
	} else if (from && text) {
		int32 forwardLeft = st::replySkip;
		p.drawPixmap(QPoint(st::replyIconPos.x(), backy + st::replyIconPos.y()), App::sprite(), st::forwardIcon);
		if (!drawPreview) {
			if (!preview->isNull()) {
				QRect to(forwardLeft, backy + st::msgReplyPadding.top(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
				if (preview->width() == preview->height()) {
					p.drawPixmap(to.x(), to.y(), preview->pix());
				} else {
					QRect from = (preview->width() > preview->height()) ? QRect((preview->width() - preview->height()) / 2, 0, preview->height(), preview->height()) : QRect(0, (preview->height() - preview->width()) / 2, preview->width(), preview->width());
					p.drawPixmap(to, preview->pix(), from);
				}
				forwardLeft += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
			}
			p.setPen(st::replyColor->p);
			from->drawElided(p, forwardLeft, backy + st::msgReplyPadding.top(), width() - forwardLeft - _fieldBarCancel.width() - st::msgReplyPadding.right());
			p.setPen((serviceColor ? st::msgInDateFg : st::msgColor)->p);
			text->drawElided(p, forwardLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, width() - forwardLeft - _fieldBarCancel.width() - st::msgReplyPadding.right());
		}
	}
	if (drawPreview) {
		int32 previewLeft = st::replySkip + st::webPageLeft;
		p.fillRect(st::replySkip, backy + st::msgReplyPadding.top(), st::webPageBar, st::msgReplyBarSize.height(), st::msgInReplyBarColor->b);
		if ((_previewData->photo && !_previewData->photo->thumb->isNull()) || (_previewData->doc && !_previewData->doc->thumb->isNull())) {
			ImagePtr replyPreview = _previewData->photo ? _previewData->photo->makeReplyPreview() : _previewData->doc->makeReplyPreview();
			if (!replyPreview->isNull()) {
				QRect to(previewLeft, backy + st::msgReplyPadding.top(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
				if (replyPreview->width() == replyPreview->height()) {
					p.drawPixmap(to.x(), to.y(), replyPreview->pix());
				} else {
					QRect from = (replyPreview->width() > replyPreview->height()) ? QRect((replyPreview->width() - replyPreview->height()) / 2, 0, replyPreview->height(), replyPreview->height()) : QRect(0, (replyPreview->height() - replyPreview->width()) / 2, replyPreview->width(), replyPreview->width());
					p.drawPixmap(to, replyPreview->pix(), from);
				}
			}
			previewLeft += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
		}
		p.setPen(st::replyColor->p);
		_previewTitle.drawElided(p, previewLeft, backy + st::msgReplyPadding.top(), width() - previewLeft - _fieldBarCancel.width() - st::msgReplyPadding.right());
		p.setPen(st::msgColor->p);
		_previewDescription.drawElided(p, previewLeft, backy + st::msgReplyPadding.top() + st::msgServiceNameFont->height, width() - previewLeft - _fieldBarCancel.width() - st::msgReplyPadding.right());
	}
}

void HistoryWidget::drawRecordButton(Painter &p) {
	if (a_recordDown.current() < 1) {
		p.setOpacity(st::btnAttachEmoji.opacity * (1 - a_recordOver.current()) + st::btnAttachEmoji.overOpacity * a_recordOver.current());
		p.drawSprite(_send.x() + (_send.width() - st::btnRecordAudio.pxWidth()) / 2, _send.y() + (_send.height() - st::btnRecordAudio.pxHeight()) / 2, st::btnRecordAudio);
	}
	if (a_recordDown.current() > 0) {
		p.setOpacity(a_recordDown.current());
		p.drawSprite(_send.x() + (_send.width() - st::btnRecordAudioActive.pxWidth()) / 2, _send.y() + (_send.height() - st::btnRecordAudioActive.pxHeight()) / 2, st::btnRecordAudioActive);
	}
	p.setOpacity(1);
}

void HistoryWidget::drawRecording(Painter &p) {
	p.setPen(Qt::NoPen);
	p.setBrush(st::recordSignalColor->b);
	p.setRenderHint(QPainter::HighQualityAntialiasing);
	float64 delta = qMin(float64(a_recordingLevel.current()) / 0x4000, 1.);
	int32 d = 2 * qRound(st::recordSignalMin + (delta * (st::recordSignalMax - st::recordSignalMin)));
	p.drawEllipse(_attachPhoto.x() + (_attachEmoji.width() - d) / 2, _attachPhoto.y() + (_attachPhoto.height() - d) / 2, d, d);
	p.setRenderHint(QPainter::HighQualityAntialiasing, false);

	QString duration = formatDurationText(_recordingSamples / AudioVoiceMsgFrequency);
	p.setFont(st::recordFont->f);

	p.setPen(st::black->p);
	p.drawText(_attachPhoto.x() + _attachEmoji.width(), _attachPhoto.y() + st::recordTextTop + st::recordFont->ascent, duration);

	int32 left = _attachPhoto.x() + _attachEmoji.width() + st::recordFont->width(duration) + ((_send.width() - st::btnRecordAudio.pxWidth()) / 2);
	int32 right = width() - _send.width();

	p.setPen(a_recordCancel.current());
	p.drawText(left + (right - left - _recordCancelWidth) / 2, _attachPhoto.y() + st::recordTextTop + st::recordFont->ascent, lang(lng_record_cancel));
}

void HistoryWidget::drawPinnedBar(Painter &p) {
	t_assert(_pinnedBar != nullptr);

	Text *from = 0, *text = 0;
	bool serviceColor = false, hasForward = readyToForward();
	ImagePtr preview;
	p.fillRect(0, 0, width(), st::replyHeight, st::taMsgField.bgColor);

	QRect rbar(rtlrect(st::msgReplyBarSkip + st::msgReplyBarPos.x(), st::msgReplyPadding.top() + st::msgReplyBarPos.y(), st::msgReplyBarSize.width(), st::msgReplyBarSize.height(), width()));
	p.fillRect(rbar, st::msgInReplyBarColor);

	int32 left = st::msgReplyBarSkip + st::msgReplyBarSkip;
	if (_pinnedBar->msg) {
		if (_pinnedBar->msg->getMedia() && _pinnedBar->msg->getMedia()->hasReplyPreview()) {
			ImagePtr replyPreview = _pinnedBar->msg->getMedia()->replyPreview();
			if (!replyPreview->isNull()) {
				QRect to(left, st::msgReplyPadding.top(), st::msgReplyBarSize.height(), st::msgReplyBarSize.height());
				p.drawPixmap(to.x(), to.y(), replyPreview->pixSingle(replyPreview->width() / cIntRetinaFactor(), replyPreview->height() / cIntRetinaFactor(), to.width(), to.height()));
			}
			left += st::msgReplyBarSize.height() + st::msgReplyBarSkip - st::msgReplyBarSize.width() - st::msgReplyBarPos.x();
		}
		p.setPen(st::replyColor);
		p.setFont(st::msgServiceNameFont);
		p.drawText(left, st::msgReplyPadding.top() + st::msgServiceNameFont->ascent, lang(lng_pinned_message));

		p.setPen((((_pinnedBar->msg->toHistoryMessage() && _pinnedBar->msg->toHistoryMessage()->emptyText()) || _pinnedBar->msg->serviceMsg()) ? st::msgInDateFg : st::msgColor)->p);
		_pinnedBar->text.drawElided(p, left, st::msgReplyPadding.top() + st::msgServiceNameFont->height, width() - left -_fieldBarCancel.width() - st::msgReplyPadding.right());
	} else {
		p.setFont(st::msgDateFont);
		p.setPen(st::msgInDateFg);
		p.drawText(left, st::msgReplyPadding.top() + (st::msgReplyBarSize.height() - st::msgDateFont->height) / 2 + st::msgDateFont->ascent, st::msgDateFont->elided(lang(lng_profile_loading), width() - left - _pinnedBar->cancel.width() - st::msgReplyPadding.right()));
	}
}

void HistoryWidget::paintEvent(QPaintEvent *e) {
	if (!App::main() || (App::wnd() && App::wnd()->contentOverlapped(this, e))) return;

	Painter p(this);
	QRect r(e->rect());
	if (r != rect()) {
		p.setClipRect(r);
	}
	if (_a_show.animating()) {
		if (a_coordOver.current() > 0) {
			p.drawPixmap(QRect(0, 0, a_coordOver.current(), height()), _cacheUnder, QRect(-a_coordUnder.current() * cRetinaFactor(), 0, a_coordOver.current() * cRetinaFactor(), height() * cRetinaFactor()));
			p.setOpacity(a_shadow.current() * st::slideFadeOut);
			p.fillRect(0, 0, a_coordOver.current(), height(), st::black->b);
			p.setOpacity(1);
		}
		p.drawPixmap(a_coordOver.current(), 0, _cacheOver);
		p.setOpacity(a_shadow.current());
		p.drawPixmap(QRect(a_coordOver.current() - st::slideShadow.pxWidth(), 0, st::slideShadow.pxWidth(), height()), App::sprite(), st::slideShadow);
		return;
	}

	bool hasTopBar = !App::main()->topBar()->isHidden(), hasPlayer = !App::main()->player()->isHidden();
	QRect fill(0, 0, width(), App::main()->height());
	int fromy = (hasTopBar ? (-st::topBarHeight) : 0) + (hasPlayer ? (-st::playerHeight) : 0), x = 0, y = 0;
	QPixmap cached = App::main()->cachedBackground(fill, x, y);
	if (cached.isNull()) {
		const QPixmap &pix(*cChatBackground());
		if (cTileBackground()) {
			int left = r.left(), top = r.top(), right = r.left() + r.width(), bottom = r.top() + r.height();
			float64 w = pix.width() / cRetinaFactor(), h = pix.height() / cRetinaFactor();
			int sx = qFloor(left / w), sy = qFloor((top - fromy) / h), cx = qCeil(right / w), cy = qCeil((bottom - fromy) / h);
			for (int i = sx; i < cx; ++i) {
				for (int j = sy; j < cy; ++j) {
					p.drawPixmap(QPointF(i * w, fromy + j * h), pix);
				}
			}
		} else {
			bool smooth = p.renderHints().testFlag(QPainter::SmoothPixmapTransform);
			p.setRenderHint(QPainter::SmoothPixmapTransform);

			QRect to, from;
			App::main()->backgroundParams(fill, to, from);
			to.moveTop(to.top() + fromy);
			p.drawPixmap(to, pix, from);

			if (!smooth) p.setRenderHint(QPainter::SmoothPixmapTransform, false);
		}
	} else {
		p.drawPixmap(x, fromy + y, cached);
	}

	if (_list) {
		if (!_field.isHidden() || _recording) {
			drawField(p);
			if (_send.isHidden()) {
				drawRecordButton(p);
				if (_recording) drawRecording(p);
			}
		}
		if (_pinnedBar && !_pinnedBar->cancel.isHidden()) {
			drawPinnedBar(p);
		}
		if (_scroll.isHidden()) {
			QPoint dogPos((width() - st::msgDogImg.pxWidth()) / 2, ((height() - _field.height() - 2 * st::sendPadding - st::msgDogImg.pxHeight()) * 4) / 9);
			p.drawPixmap(dogPos, *cChatDogImage());
		}
	} else {
		style::font font(st::msgServiceFont);
		int32 w = font->width(lang(lng_willbe_history)) + st::msgPadding.left() + st::msgPadding.right(), h = font->height + st::msgServicePadding.top() + st::msgServicePadding.bottom() + 2;
		QRect tr((width() - w) / 2, (height() - _field.height() - 2 * st::sendPadding - h) / 2, w, h);
		App::roundRect(p, tr, App::msgServiceBg(), ServiceCorners);

		p.setPen(st::msgServiceColor->p);
		p.setFont(font->f);
		p.drawText(tr.left() + st::msgPadding.left(), tr.top() + st::msgServicePadding.top() + 1 + font->ascent, lang(lng_willbe_history));
	}
}

QRect HistoryWidget::historyRect() const {
	return _scroll.geometry();
}

void HistoryWidget::destroyData() {
	showHistory(0, 0);
}

QStringList HistoryWidget::getMediasFromMime(const QMimeData *d) {
	QString uriListFormat(qsl("text/uri-list"));
	QStringList photoExtensions(cPhotoExtensions()), files;
	if (!d->hasFormat(uriListFormat)) return QStringList();

	const QList<QUrl> &urls(d->urls());
	if (urls.isEmpty()) return QStringList();

	files.reserve(urls.size());
	for (QList<QUrl>::const_iterator i = urls.cbegin(), en = urls.cend(); i != en; ++i) {
		if (!i->isLocalFile()) return QStringList();

		QString file(i->toLocalFile());
		if (file.startsWith(qsl("/.file/id="))) file = psConvertFileUrl(file);

		QFileInfo info(file);
		uint64 s = info.size();
		if (s >= MaxUploadDocumentSize) {
			if (s >= MaxUploadPhotoSize) {
				continue;
			} else {
				bool foundGoodExtension = false;
				for (QStringList::const_iterator j = photoExtensions.cbegin(), end = photoExtensions.cend(); j != end; ++j) {
					if (file.right(j->size()).toLower() == (*j).toLower()) {
						foundGoodExtension = true;
					}
				}
				if (!foundGoodExtension) {
					continue;
				}
			}
		}
		files.push_back(file);
	}
	return files;
}

QPoint HistoryWidget::clampMousePosition(QPoint point) {
	if (point.x() < 0) {
		point.setX(0);
	} else if (point.x() >= _scroll.width()) {
		point.setX(_scroll.width() - 1);
	}
	if (point.y() < _scroll.scrollTop()) {
		point.setY(_scroll.scrollTop());
	} else if (point.y() >= _scroll.scrollTop() + _scroll.height()) {
		point.setY(_scroll.scrollTop() + _scroll.height() - 1);
	}
	return point;
}

void HistoryWidget::onScrollTimer() {
	int32 d = (_scrollDelta > 0) ? qMin(_scrollDelta * 3 / 20 + 1, int32(MaxScrollSpeed)) : qMax(_scrollDelta * 3 / 20 - 1, -int32(MaxScrollSpeed));
	_scroll.scrollToY(_scroll.scrollTop() + d);
}

void HistoryWidget::checkSelectingScroll(QPoint point) {
	if (point.y() < _scroll.scrollTop()) {
		_scrollDelta = point.y() - _scroll.scrollTop();
	} else if (point.y() >= _scroll.scrollTop() + _scroll.height()) {
		_scrollDelta = point.y() - _scroll.scrollTop() - _scroll.height() + 1;
	} else {
		_scrollDelta = 0;
	}
	if (_scrollDelta) {
		_scrollTimer.start(15);
	} else {
		_scrollTimer.stop();
	}
}

void HistoryWidget::noSelectingScroll() {
	_scrollTimer.stop();
}

bool HistoryWidget::touchScroll(const QPoint &delta) {
	int32 scTop = _scroll.scrollTop(), scMax = _scroll.scrollTopMax(), scNew = snap(scTop - delta.y(), 0, scMax);
	if (scNew == scTop) return false;

	_scroll.scrollToY(scNew);
	return true;
}

HistoryWidget::~HistoryWidget() {
	deleteAndMark(_pinnedBar);
	deleteAndMark(_list);
}
