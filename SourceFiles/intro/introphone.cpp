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
#include "lang.h"
#include "style.h"

#include "application.h"

#include "intro/introphone.h"
#include "intro/intro.h"

namespace {
	class SignUpLink : public ITextLink {
		TEXT_LINK_CLASS(SignUpLink)

	public:

		SignUpLink(IntroPhone *widget) : _widget(widget) {
		}

		void onClick(Qt::MouseButton) const {
			_widget->toSignUp();
		}

	private:
		IntroPhone *_widget;
	};
}

IntroPhone::IntroPhone(IntroWidget *parent) : IntroStage(parent)
, a_errorAlpha(0)
, _a_error(animation(this, &IntroPhone::step_error))
, changed(false)
, next(this, lang(lng_intro_next), st::btnIntroNext)
, country(this, st::introCountry)
, phone(this, st::inpIntroPhone)
, code(this, st::inpIntroCountryCode)
, _signup(this, lng_phone_notreg(lt_signup_start, textcmdStartLink(1), lt_signup_end, textcmdStopLink()), st::introErrLabel, st::introErrLabelTextStyle)
, _showSignup(false) {
	setVisible(false);
	setGeometry(parent->innerRect());

	connect(&next, SIGNAL(stateChanged(int, ButtonStateChangeSource)), parent, SLOT(onDoneStateChanged(int, ButtonStateChangeSource)));
	connect(&next, SIGNAL(clicked()), this, SLOT(onSubmitPhone()));
	connect(&phone, SIGNAL(voidBackspace(QKeyEvent*)), &code, SLOT(startErasing(QKeyEvent*)));
	connect(&country, SIGNAL(codeChanged(const QString &)), &code, SLOT(codeSelected(const QString &)));
	connect(&code, SIGNAL(codeChanged(const QString &)), &country, SLOT(onChooseCode(const QString &)));
	connect(&code, SIGNAL(codeChanged(const QString &)), &phone, SLOT(onChooseCode(const QString &)));
	connect(&country, SIGNAL(codeChanged(const QString &)), &phone, SLOT(onChooseCode(const QString &)));
	connect(&code, SIGNAL(addedToNumber(const QString &)), &phone, SLOT(addedToNumber(const QString &)));
	connect(&phone, SIGNAL(changed()), this, SLOT(onInputChange()));
	connect(&code, SIGNAL(changed()), this, SLOT(onInputChange()));
	connect(intro(), SIGNAL(countryChanged()), this, SLOT(countryChanged()));
	connect(&checkRequest, SIGNAL(timeout()), this, SLOT(onCheckRequest()));

	_signup.setLink(1, TextLinkPtr(new SignUpLink(this)));
	_signup.hide();

	_signupCache = myGrab(&_signup);

	if (!country.onChooseCountry(intro()->currentCountry())) {
		country.onChooseCountry(qsl("US"));
	}
	changed = false;
}

void IntroPhone::paintEvent(QPaintEvent *e) {
	bool trivial = (rect() == e->rect());

	QPainter p(this);
	if (!trivial) {
		p.setClipRect(e->rect());
	}
	if (trivial || e->rect().intersects(textRect)) {
		p.setFont(st::introHeaderFont->f);
		p.drawText(textRect, lang(lng_phone_title), style::al_top);
		p.setFont(st::introFont->f);
		p.drawText(textRect, lang(lng_phone_desc), style::al_bottom);
	}
	if (_a_error.animating() || error.length()) {
		int32 errorY = _showSignup ? ((phone.y() + phone.height() + next.y() - st::introErrFont->height) / 2) : (next.y() + next.height() + st::introErrTop);
		p.setOpacity(a_errorAlpha.current());
		p.setFont(st::introErrFont->f);
		p.setPen(st::introErrColor->p);
		p.drawText(QRect(textRect.x(), errorY, textRect.width(), st::introErrFont->height), error, style::al_top);

		if (_signup.isHidden() && _showSignup) {
			p.drawPixmap(_signup.x(), _signup.y(), _signupCache);
		}
	}
}

void IntroPhone::resizeEvent(QResizeEvent *e) {
	if (e->oldSize().width() != width()) {
		next.move((width() - next.width()) / 2, st::introBtnTop);
		country.move((width() - country.width()) / 2, st::introTextTop + st::introTextSize.height() + st::introCountry.top);
		int phoneTop = country.y() + country.height() + st::introPhoneTop;
		phone.move((width() - country.width()) / 2 + country.width() - st::inpIntroPhone.width, phoneTop);
		code.move((width() - country.width()) / 2, phoneTop);
	}
	_signup.move((width() - _signup.width()) / 2, next.y() + next.height() + st::introErrTop - ((st::introErrLabelTextStyle.lineHeight - st::introErrFont->height) / 2));
	textRect = QRect((width() - st::introTextSize.width()) / 2, st::introTextTop, st::introTextSize.width(), st::introTextSize.height());
}

void IntroPhone::showError(const QString &err, bool signUp) {
	if (!err.isEmpty()) {
		phone.notaBene();
		_showSignup = signUp;
	}

	if (!_a_error.animating() && err == error) return;

	if (err.length()) {
		error = err;
		a_errorAlpha.start(1);
	} else {
		a_errorAlpha.start(0);
	}
	_signup.hide();
	_a_error.start();
}

void IntroPhone::step_error(float64 ms, bool timer) {
	float64 dt = ms / st::introErrDuration;

	if (dt >= 1) {
		_a_error.stop();
		a_errorAlpha.finish();
		if (!a_errorAlpha.current()) {
			error = "";
			_signup.hide();
		} else if (!error.isEmpty() && _showSignup) {
			_signup.show();
		}
	} else {
		a_errorAlpha.update(dt, st::introErrFunc);
	}
	if (timer) update();
}

void IntroPhone::countryChanged() {
	if (!changed) {
		selectCountry(intro()->currentCountry());
	}
}

void IntroPhone::onInputChange() {
	changed = true;
	showError("");
}

void IntroPhone::disableAll() {
	next.setDisabled(true);
	phone.setDisabled(true);
	country.setDisabled(true);
	code.setDisabled(true);
	setFocus();
}

void IntroPhone::enableAll(bool failed) {
	next.setDisabled(false);
	phone.setDisabled(false);
	country.setDisabled(false);
	code.setDisabled(false);
	if (failed) phone.setFocus();
}

void IntroPhone::onSubmitPhone(bool force) {
	if (!force && !next.isEnabled()) return;

	if (!App::isValidPhone(fullNumber())) {
		showError(lang(lng_bad_phone));
		phone.setFocus();
		return;
	}

	disableAll();
	showError("");

	checkRequest.start(1000);

	sentPhone = fullNumber();
	sentRequest = MTP::send(MTPauth_CheckPhone(MTP_string(sentPhone)), rpcDone(&IntroPhone::phoneCheckDone), rpcFail(&IntroPhone::phoneSubmitFail));
}

void IntroPhone::stopCheck() {
	checkRequest.stop();
}

void IntroPhone::onCheckRequest() {
	int32 status = MTP::state(sentRequest);
	if (status < 0) {
		int32 leftms = -status;
		if (leftms >= 1000) {
			MTP::cancel(sentRequest);
			sentRequest = 0;
			if (!phone.isEnabled()) enableAll(true);
		}
	}
	if (!sentRequest && status == MTP::RequestSent) {
		stopCheck();
	}
}

void IntroPhone::phoneCheckDone(const MTPauth_CheckedPhone &result) {
	stopCheck();

	const MTPDauth_checkedPhone &d(result.c_auth_checkedPhone());
	if (mtpIsTrue(d.vphone_registered)) {
		disableAll();
		showError("");

		checkRequest.start(1000);

		sentRequest = MTP::send(MTPauth_SendCode(MTP_string(sentPhone), MTP_int(5), MTP_int(ApiId), MTP_string(ApiHash), MTP_string(Sandbox::LangSystemISO())), rpcDone(&IntroPhone::phoneSubmitDone), rpcFail(&IntroPhone::phoneSubmitFail));
	} else {
		showError(lang(lng_bad_phone_noreg), true);
		enableAll(true);
	}
}

void IntroPhone::phoneSubmitDone(const MTPauth_SentCode &result) {
	stopCheck();
	enableAll(false);

	if (result.type() == mtpc_auth_sentCode) {
		const MTPDauth_sentCode &d(result.c_auth_sentCode());
		intro()->setPhone(sentPhone, d.vphone_code_hash.c_string().v.c_str(), mtpIsTrue(d.vphone_registered));
		intro()->setCallTimeout(d.vsend_call_timeout.v);
	} else if (result.type() == mtpc_auth_sentAppCode) {
		const MTPDauth_sentAppCode &d(result.c_auth_sentAppCode());
		intro()->setPhone(sentPhone, d.vphone_code_hash.c_string().v.c_str(), mtpIsTrue(d.vphone_registered));
		intro()->setCallTimeout(d.vsend_call_timeout.v);
		intro()->setCodeByTelegram(true);
	}
	intro()->onIntroNext();
}

void IntroPhone::toSignUp() {
	disableAll();
	showError("");

	checkRequest.start(1000);

	sentRequest = MTP::send(MTPauth_SendCode(MTP_string(sentPhone), MTP_int(0), MTP_int(ApiId), MTP_string(ApiHash), MTP_string(Sandbox::LangSystemISO())), rpcDone(&IntroPhone::phoneSubmitDone), rpcFail(&IntroPhone::phoneSubmitFail));
}

bool IntroPhone::phoneSubmitFail(const RPCError &error) {
	stopCheck();
	const QString &err = error.type();
	if (err == "PHONE_NUMBER_INVALID") { // show error
		showError(lang(lng_bad_phone));
		enableAll(true);
		return true;
	} else if (mtpIsFlood(error)) {
		showError(lang(lng_flood_error));
		enableAll(true);
		return true;
	}
	if (cDebug()) { // internal server error
		showError(err + ": " + error.description());
	} else {
		showError(lang(lng_server_error));
	}
	enableAll(true);
	return false;
}

QString IntroPhone::fullNumber() const {
	return code.text() + phone.text();
}

void IntroPhone::selectCountry(const QString &c) {
	country.onChooseCountry(c);
}

void IntroPhone::activate() {
	error = "";
	a_errorAlpha = anim::fvalue(0);
	show();
	enableAll(true);
}

void IntroPhone::deactivate() {
	checkRequest.stop();
	hide();
	phone.clearFocus();
}

void IntroPhone::onNext() {
	onSubmitPhone();
}

void IntroPhone::onBack() {
}
