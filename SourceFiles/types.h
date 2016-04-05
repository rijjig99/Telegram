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

template <typename T>
void deleteAndMark(T *&link) {
	delete link;
	link = reinterpret_cast<T*>(0x00000BAD);
}

template <typename T>
T *exchange(T *&ptr) {
	T *result = 0;
	qSwap(result, ptr);
	return result;
}

struct NullType {
};

#if __cplusplus < 199711L
#define TDESKTOP_CUSTOM_NULLPTR
#endif

#ifdef TDESKTOP_CUSTOM_NULLPTR
class NullPointerClass {
public:
	template <typename T>
	operator T*() const {
		return 0;
	}
	template <typename C, typename T>
	operator T C::*() const {
		return 0;
	}

private:
	void operator&() const;
};
extern NullPointerClass nullptr;
#endif

template <typename T>
class OrderedSet : public QMap<T, NullType> {
public:

	void insert(const T &v) {
		QMap<T, NullType>::insert(v, NullType());
	}

};

//typedef unsigned char uchar; // Qt has uchar
typedef qint16 int16;
typedef quint16 uint16;
typedef qint32 int32;
typedef quint32 uint32;
typedef qint64 int64;
typedef quint64 uint64;

static const int32 ScrollMax = INT_MAX;

extern uint64 _SharedMemoryLocation[];
template <typename T, unsigned int N>
T *SharedMemoryLocation() {
	static_assert(N < 4, "Only 4 shared memory locations!");
	return reinterpret_cast<T*>(_SharedMemoryLocation + N);
}

#ifdef Q_OS_WIN
typedef float float32;
typedef double float64;
#else
typedef float float32;
typedef double float64;
#endif

#include <string>
#include <exception>

#include <QtCore/QReadWriteLock>

#include <ctime>

using std::string;
using std::exception;
using std::swap;

#include "logs.h"

static volatile int *t_assert_nullptr = 0;
inline void t_noop() {}
inline void t_assert_fail(const char *message, const char *file, int32 line) {
	LOG(("Assertion Failed! %1 %2:%3").arg(message).arg(file).arg(line));
	*t_assert_nullptr = 0;
}
#define t_assert_full(condition, message, file, line) ((!(condition)) ? t_assert_fail(message, file, line) : t_noop())
#define t_assert_c(condition, comment) t_assert_full(condition, "\"" #condition "\" (" comment ")", __FILE__, __LINE__)
#define t_assert(condition) t_assert_full(condition, "\"" #condition "\"", __FILE__, __LINE__)

class Exception : public exception {
public:

    Exception(const QString &msg, bool isFatal = true) : _fatal(isFatal), _msg(msg.toUtf8()) {
		LOG(("Exception: %1").arg(msg));
	}
	bool fatal() const {
		return _fatal;
	}

    virtual const char *what() const throw() {
        return _msg.constData();
    }
    virtual ~Exception() throw() {
    }

private:
	bool _fatal;
    QByteArray _msg;
};

class MTPint;

int32 myunixtime();
void unixtimeInit();
void unixtimeSet(int32 servertime, bool force = false);
int32 unixtime();
int32 fromServerTime(const MTPint &serverTime);
uint64 msgid();
int32 reqid();

inline QDateTime date(int32 time = -1) {
	QDateTime result;
	if (time >= 0) result.setTime_t(time);
	return result;
}

inline QDateTime date(const MTPint &time) {
	return date(fromServerTime(time));
}

inline void mylocaltime(struct tm * _Tm, const time_t * _Time) {
#ifdef Q_OS_WIN
    localtime_s(_Tm, _Time);
#else
    localtime_r(_Time, _Tm);
#endif
}

namespace ThirdParty {

	void start();
	void finish();

}

bool checkms(); // returns true if time has changed
uint64 getms(bool checked = false);

class SingleTimer : public QTimer { // single shot timer with check
	Q_OBJECT

public:

	SingleTimer();

	void setSingleShot(bool); // is not available
	void start(); // is not available

public slots:

	void start(int msec);
	void startIfNotActive(int msec);
	void adjust() {
		uint64 n = getms(true);
		if (isActive()) {
			if (n >= _finishing) {
				start(0);
			} else {
				start(_finishing - n);
			}
		}
	}

private:
	uint64 _finishing;
	bool _inited;

};

const static uint32 _md5_block_size = 64;
class HashMd5 {
public:

	HashMd5(const void *input = 0, uint32 length = 0);
	void feed(const void *input, uint32 length);
	int32 *result();

private:

	void init();
	void finalize();
	void transform(const uchar *block);

	bool _finalized;
	uchar _buffer[_md5_block_size];
	uint32 _count[2];
	uint32 _state[4];
	uchar _digest[16];

};

int32 hashCrc32(const void *data, uint32 len);
int32 *hashSha1(const void *data, uint32 len, void *dest); // dest - ptr to 20 bytes, returns (int32*)dest
int32 *hashSha256(const void *data, uint32 len, void *dest); // dest - ptr to 32 bytes, returns (int32*)dest
int32 *hashMd5(const void *data, uint32 len, void *dest); // dest = ptr to 16 bytes, returns (int32*)dest
char *hashMd5Hex(const int32 *hashmd5, void *dest); // dest = ptr to 32 bytes, returns (char*)dest
inline char *hashMd5Hex(const void *data, uint32 len, void *dest) { // dest = ptr to 32 bytes, returns (char*)dest
	return hashMd5Hex(HashMd5(data, len).result(), dest);
}

void memset_rand(void *data, uint32 len);

template <typename T>
inline void memsetrnd(T &value) {
	memset_rand(&value, sizeof(value));
}

inline void memset_rand_bad(void *data, uint32 len) {
	for (uchar *i = reinterpret_cast<uchar*>(data), *e = i + len; i != e; ++i) {
		*i = uchar(rand() & 0xFF);
	}
}

template <typename T>
inline void memsetrnd_bad(T &value) {
	memset_rand_bad(&value, sizeof(value));
}

class ReadLockerAttempt {
public:

    ReadLockerAttempt(QReadWriteLock *_lock) : success(_lock->tryLockForRead()), lock(_lock) {
	}
	~ReadLockerAttempt() {
		if (success) {
			lock->unlock();
		}
	}

	operator bool() const {
		return success;
	}

private:

	bool success;
	QReadWriteLock *lock;

};

#define qsl(s) QStringLiteral(s)
#define qstr(s) QLatin1String(s, sizeof(s) - 1)

inline QString fromUtf8Safe(const char *str, int32 size = -1) {
	if (!str || !size) return QString();
	if (size < 0) size = int32(strlen(str));
	QString result(QString::fromUtf8(str, size));
	QByteArray back = result.toUtf8();
	if (back.size() != size || memcmp(back.constData(), str, size)) return QString::fromLocal8Bit(str, size);
	return result;
}

inline QString fromUtf8Safe(const QByteArray &str) {
	return fromUtf8Safe(str.constData(), str.size());
}

static const QRegularExpression::PatternOptions reMultiline(QRegularExpression::DotMatchesEverythingOption | QRegularExpression::MultilineOption);

template <typename T>
inline T snap(const T &v, const T &_min, const T &_max) {
	return (v < _min) ? _min : ((v > _max) ? _max : v);
}

template <typename T>
class ManagedPtr {
public:
	ManagedPtr() : ptr(0) {
	}
	ManagedPtr(T *p) : ptr(p) {
	}
	T *operator->() const {
		return ptr;
	}
	T *v() const {
		return ptr;
	}

protected:

	T *ptr;
	typedef ManagedPtr<T> Parent;
};

QString translitRusEng(const QString &rus);
QString rusKeyboardLayoutSwitch(const QString &from);

enum DBISendKey {
	dbiskEnter = 0,
	dbiskCtrlEnter = 1,
};

enum DBINotifyView {
	dbinvShowPreview = 0,
	dbinvShowName = 1,
	dbinvShowNothing = 2,
};

enum DBIWorkMode {
	dbiwmWindowAndTray = 0,
	dbiwmTrayOnly = 1,
	dbiwmWindowOnly = 2,
};

enum DBIConnectionType {
	dbictAuto = 0,
	dbictHttpAuto = 1, // not used
	dbictHttpProxy = 2,
	dbictTcpProxy = 3,
};

enum DBIDefaultAttach {
	dbidaDocument = 0,
	dbidaPhoto = 1,
};

struct ConnectionProxy {
	ConnectionProxy() : port(0) {
	}
	QString host;
	uint32 port;
	QString user, password;
};

enum DBIScale {
	dbisAuto          = 0,
	dbisOne           = 1,
	dbisOneAndQuarter = 2,
	dbisOneAndHalf    = 3,
	dbisTwo           = 4,

	dbisScaleCount    = 5,
};

static const int MatrixRowShift = 40000;

enum DBIEmojiTab {
	dbietRecent   = -1,
	dbietPeople   =  0,
	dbietNature   =  1,
	dbietFood     =  2,
	dbietActivity =  3,
	dbietTravel   =  4,
	dbietObjects  =  5,
	dbietSymbols  =  6,
	dbietStickers =  666,
};
static const int emojiTabCount = 8;
inline DBIEmojiTab emojiTabAtIndex(int index) {
	return (index < 0 || index >= emojiTabCount) ? dbietRecent : DBIEmojiTab(index - 1);
}

enum DBIPlatform {
    dbipWindows  = 0,
    dbipMac      = 1,
    dbipLinux64  = 2,
    dbipLinux32  = 3,
	dbipMacOld   = 4,
};

enum DBIPeerReportSpamStatus {
	dbiprsNoButton   = 0, // hidden, but not in the cloud settings yet
	dbiprsUnknown    = 1, // contacts not loaded yet
	dbiprsShowButton = 2, // show report spam button, each show peer request setting from cloud
	dbiprsReportSent = 3, // report sent, but the report spam panel is not hidden yet
	dbiprsHidden     = 4, // hidden in the cloud or not needed (bots, contacts, etc), no more requests
	dbiprsRequesting = 5, // requesting the cloud setting right now
};

typedef enum {
	HitTestNone = 0,
	HitTestClient,
	HitTestSysButton,
	HitTestIcon,
	HitTestCaption,
	HitTestTop,
	HitTestTopRight,
	HitTestRight,
	HitTestBottomRight,
	HitTestBottom,
	HitTestBottomLeft,
	HitTestLeft,
	HitTestTopLeft,
} HitTestType;

inline QString strMakeFromLetters(const uint32 *letters, int32 len) {
	QString result;
	result.reserve(len);
	for (int32 i = 0; i < len; ++i) {
		result.push_back(QChar((((letters[i] << 16) & 0xFF) >> 8) | (letters[i] & 0xFF)));
	}
	return result;
}

class MimeType {
public:

	enum TypeEnum {
		Unknown,
		WebP,
	};

	MimeType(const QMimeType &type) : _typeStruct(type), _type(Unknown) {
	}
	MimeType(TypeEnum type) : _type(type) {
	}
	QStringList globPatterns() const;
	QString filterString() const;
	QString name() const;

private:

	QMimeType _typeStruct;
	TypeEnum _type;

};

MimeType mimeTypeForName(const QString &mime);
MimeType mimeTypeForFile(const QFileInfo &file);
MimeType mimeTypeForData(const QByteArray &data);

inline int32 rowscount(int32 count, int32 perrow) {
	return (count + perrow - 1) / perrow;
}
inline int32 floorclamp(int32 value, int32 step, int32 lowest, int32 highest) {
	return qMin(qMax(value / step, lowest), highest);
}
inline int32 floorclamp(float64 value, int32 step, int32 lowest, int32 highest) {
	return qMin(qMax(qFloor(value / step), lowest), highest);
}
inline int32 ceilclamp(int32 value, int32 step, int32 lowest, int32 highest) {
	return qMax(qMin((value / step) + ((value % step) ? 1 : 0), highest), lowest);
}
inline int32 ceilclamp(float64 value, int32 step, int32 lowest, int32 highest) {
	return qMax(qMin(qCeil(value / step), highest), lowest);
}

enum ForwardWhatMessages {
	ForwardSelectedMessages,
	ForwardContextMessage,
	ForwardPressedMessage,
	ForwardPressedLinkMessage
};

enum ShowLayerOption {
	CloseOtherLayers          = 0x00,
	KeepOtherLayers           = 0x01,
	ShowAfterOtherLayers      = 0x03,

	AnimatedShowLayer         = 0x00,
	ForceFastShowLayer        = 0x04,
};
typedef QFlags<ShowLayerOption> ShowLayerOptions;

static int32 FullArcLength = 360 * 16;
static int32 QuarterArcLength = (FullArcLength / 4);
static int32 MinArcLength = (FullArcLength / 360);
static int32 AlmostFullArcLength = (FullArcLength - MinArcLength);

template <typename T1, typename T2>
class RefPairImplementation {
public:
	template <typename T3, typename T4>
	const RefPairImplementation &operator=(const RefPairImplementation<T3, T4> &other) const {
		_first = other._first;
		_second = other._second;
		return *this;
	}

	template <typename T3, typename T4>
	const RefPairImplementation &operator=(const QPair<T3, T4> &other) const {
		_first = other.first;
		_second = other.second;
		return *this;
	}

private:
	RefPairImplementation(T1 &first, T2 &second) : _first(first), _second(second) {
	}
	RefPairImplementation(const RefPairImplementation &other);

	template <typename T3, typename T4>
	friend RefPairImplementation<T3, T4> RefPairCreator(T3 &first, T4 &second);

	T1 &_first;
	T2 &_second;
};

template <typename T1, typename T2>
inline RefPairImplementation<T1, T2> RefPairCreator(T1 &first, T2 &second) {
	return RefPairImplementation<T1, T2>(first, second);
}

#define RefPair(Type1, Name1, Type2, Name2) Type1 Name1; Type2 Name2; RefPairCreator(Name1, Name2)

template <typename I>
inline void destroyImplementation(I *&ptr) {
	if (ptr) {
		ptr->destroy();
		ptr = 0;
	}
	deleteAndMark(ptr);
}

class Interfaces;
typedef void(*InterfaceConstruct)(void *location, Interfaces *interfaces);
typedef void(*InterfaceDestruct)(void *location);
typedef void(*InterfaceAssign)(void *location, void *waslocation);

struct InterfaceWrapStruct {
	InterfaceWrapStruct() : Size(0), Construct(0), Destruct(0) {
	}
	InterfaceWrapStruct(int size, InterfaceConstruct construct, InterfaceDestruct destruct, InterfaceAssign assign)
	: Size(size)
	, Construct(construct)
	, Destruct(destruct)
	, Assign(assign) {
	}
	int Size;
	InterfaceConstruct Construct;
	InterfaceDestruct Destruct;
	InterfaceAssign Assign;
};

template <int Value, int Denominator>
struct CeilDivideMinimumOne {
	static const int Result = ((Value / Denominator) + ((!Value || (Value % Denominator)) ? 1 : 0));
};

template <typename Type>
struct InterfaceWrapTemplate {
	static const int Size = CeilDivideMinimumOne<sizeof(Type), sizeof(uint64)>::Result * sizeof(uint64);
	static void Construct(void *location, Interfaces *interfaces) {
		new (location) Type(interfaces);
	}
	static void Destruct(void *location) {
		((Type*)location)->~Type();
	}
	static void Assign(void *location, void *waslocation) {
		*((Type*)location) = *((Type*)waslocation);
	}
};

extern InterfaceWrapStruct InterfaceWraps[64];
extern QAtomicInt InterfaceIndexLast;

template <typename Type>
class BasicInterface {
public:
	static int Index() {
		static QAtomicInt _index(0);
		if (int index = _index.loadAcquire()) {
			return index - 1;
		}
		while (true) {
			int last = InterfaceIndexLast.loadAcquire();
			if (InterfaceIndexLast.testAndSetOrdered(last, last + 1)) {
				t_assert(last < 64);
				if (_index.testAndSetOrdered(0, last + 1)) {
					InterfaceWraps[last] = InterfaceWrapStruct(InterfaceWrapTemplate<Type>::Size, InterfaceWrapTemplate<Type>::Construct, InterfaceWrapTemplate<Type>::Destruct, InterfaceWrapTemplate<Type>::Assign);
				}
				break;
			}
		}
		return _index.loadAcquire() - 1;
	}
	static uint64 Bit() {
		return (1 << Index());
	}

};

template <typename Type>
class BasicInterfaceWithPointer : public BasicInterface<Type> {
public:
	BasicInterfaceWithPointer(Interfaces *interfaces) : interfaces(interfaces) {
	}
	Interfaces *interfaces = 0;
};

class InterfacesMetadata {
public:

	InterfacesMetadata(uint64 mask) : size(0), last(64), _mask(mask) {
		for (int i = 0; i < 64; ++i) {
			uint64 m = (1 << i);
			if (_mask & m) {
				int s = InterfaceWraps[i].Size;
				if (s) {
					offsets[i] = size;
					size += s;
				} else {
					offsets[i] = -1;
				}
			} else if (_mask < m) {
				last = i;
				for (; i < 64; ++i) {
					offsets[i] = -1;
				}
			} else {
				offsets[i] = -1;
			}
		}
	}

	int size, last;
	int offsets[64];

	bool equals(const uint64 &mask) const {
		return _mask == mask;
	}

private:
	uint64 _mask;

};

const InterfacesMetadata *GetInterfacesMetadata(uint64 mask);

class Interfaces {
public:

	Interfaces(uint64 mask = 0) : _data(zerodata()) {
		if (mask) {
			const InterfacesMetadata *meta = GetInterfacesMetadata(mask);
			int32 size = sizeof(const InterfacesMetadata *) + meta->size;
			void *data = malloc(size);
			if (!data) { // terminate if we can't allocate memory
				throw "Can't allocate memory!";
			}

			_data = data;
			_meta() = meta;
			for (int i = 0; i < meta->last; ++i) {
				int offset = meta->offsets[i];
				if (offset >= 0) {
					try {
						InterfaceWraps[i].Construct(_dataptrunsafe(offset), this);
					} catch (...) {
						while (i > 0) {
							--i;
							offset = meta->offsets[--i];
							if (offset >= 0) {
								InterfaceWraps[i].Destruct(_dataptrunsafe(offset));
							}
						}
						throw;
					}
				}
			}
		}
	}
	void UpdateInterfaces(uint64 mask = 0) {
		if (!_meta()->equals(mask)) {
			Interfaces tmp(mask);
			tmp.swap(*this);

			if (_data != zerodata() && tmp._data != zerodata()) {
				const InterfacesMetadata *meta = _meta(), *wasmeta = tmp._meta();
				for (int i = 0; i < meta->last; ++i) {
					int offset = meta->offsets[i], wasoffset = wasmeta->offsets[i];
					if (offset >= 0 && wasoffset >= 0) {
						InterfaceWraps[i].Assign(_dataptrunsafe(offset), tmp._dataptrunsafe(wasoffset));
					}
				}
			}
		}
	}
	~Interfaces() {
		if (_data != zerodata()) {
			const InterfacesMetadata *meta = _meta();
			for (int i = 0; i < meta->last; ++i) {
				int offset = meta->offsets[i];
				if (offset >= 0) {
					InterfaceWraps[i].Destruct(_dataptrunsafe(offset));
				}
			}
			free(_data);
		}
	}

	template <typename Type>
	Type *Get() {
		return static_cast<Type*>(_dataptr(_meta()->offsets[Type::Index()]));
	}
	template <typename Type>
	const Type *Get() const {
		return static_cast<const Type*>(_dataptr(_meta()->offsets[Type::Index()]));
	}
	template <typename Type>
	bool Is() const {
		return (_meta()->offsets[Type::Index()] >= 0);
	}

private:
	static const InterfacesMetadata *ZeroInterfacesMetadata;
	static void *zerodata() {
		return &ZeroInterfacesMetadata;
	}

	void *_dataptrunsafe(int skip) const {
		return (char*)_data + sizeof(const InterfacesMetadata*) + skip;
	}
	void *_dataptr(int skip) const {
		return (skip >= 0) ? _dataptrunsafe(skip) : 0;
	}
	const InterfacesMetadata *&_meta() const {
		return *static_cast<const InterfacesMetadata**>(_data);
	}
	void *_data;

	Interfaces(const Interfaces &other);
	Interfaces &operator=(const Interfaces &other);

	void swap(Interfaces &other) {
		std::swap(_data, other._data);
	}

};

template <typename R, typename A1, typename A2>
class SharedCallback2 {
public:
	virtual R call(A1 channel, A2 msgId) const = 0;
	virtual ~SharedCallback2() {
	}
	typedef QSharedPointer<SharedCallback2<R, A1, A2> > Ptr;
};

template <typename R>
class FunctionImplementation {
public:
	virtual R call() = 0;
	virtual void destroy() { delete this; }
	virtual ~FunctionImplementation() {}
};
template <typename R>
class NullFunctionImplementation : public FunctionImplementation<R> {
public:
	virtual R call() { return R(); }
	virtual void destroy() {}
	static NullFunctionImplementation<R> SharedInstance;
};
template <typename R>
NullFunctionImplementation<R> NullFunctionImplementation<R>::SharedInstance;
template <typename R>
class FunctionCreator {
public:
	FunctionCreator(FunctionImplementation<R> *ptr) : _ptr(ptr) {}
	FunctionCreator(const FunctionCreator<R> &other) : _ptr(other.create()) {}
	FunctionImplementation<R> *create() const { return exchange(_ptr); }
	~FunctionCreator() { destroyImplementation(_ptr); }
private:
	FunctionCreator<R> &operator=(const FunctionCreator<R> &other);
	mutable FunctionImplementation<R> *_ptr;
};
template <typename R>
class Function {
public:
	typedef FunctionCreator<R> Creator;
	static Creator Null() { return Creator(&NullFunctionImplementation<R>::SharedInstance); }
	Function(const Creator &creator) : _implementation(creator.create()) {}
	R call() { return _implementation->call(); }
	~Function() { destroyImplementation(_implementation); }
private:
	Function(const Function<R> &other);
	Function<R> &operator=(const Function<R> &other);
	FunctionImplementation<R> *_implementation;
};

template <typename R>
class WrappedFunction : public FunctionImplementation<R> {
public:
	typedef R(*Method)();
	WrappedFunction(Method method) : _method(method) {}
	virtual R call() { return (*_method)(); }
private:
	Method _method;
};
template <typename R>
inline FunctionCreator<R> func(R(*method)()) {
	return FunctionCreator<R>(new WrappedFunction<R>(method));
}
template <typename O, typename I, typename R>
class ObjectFunction : public FunctionImplementation<R> {
public:
	typedef R(I::*Method)();
	ObjectFunction(O *obj, Method method) : _obj(obj), _method(method) {}
	virtual R call() { return (_obj->*_method)(); }
private:
	O *_obj;
	Method _method;
};
template <typename O, typename I, typename R>
inline FunctionCreator<R> func(O *obj, R(I::*method)()) {
	return FunctionCreator<R>(new ObjectFunction<O, I, R>(obj, method));
}

template <typename R, typename A1>
class Function1Implementation {
public:
	virtual R call(A1 a1) = 0;
	virtual void destroy() { delete this; }
	virtual ~Function1Implementation() {}
};
template <typename R, typename A1>
class NullFunction1Implementation : public Function1Implementation<R, A1> {
public:
	virtual R call(A1 a1) { return R(); }
	virtual void destroy() {}
	static NullFunction1Implementation<R, A1> SharedInstance;
};
template <typename R, typename A1>
NullFunction1Implementation<R, A1> NullFunction1Implementation<R, A1>::SharedInstance;
template <typename R, typename A1>
class Function1Creator {
public:
	Function1Creator(Function1Implementation<R, A1> *ptr) : _ptr(ptr) {}
	Function1Creator(const Function1Creator<R, A1> &other) : _ptr(other.create()) {}
	Function1Implementation<R, A1> *create() const { return exchange(_ptr); }
	~Function1Creator() { destroyImplementation(_ptr); }
private:
	Function1Creator<R, A1> &operator=(const Function1Creator<R, A1> &other);
	mutable Function1Implementation<R, A1> *_ptr;
};
template <typename R, typename A1>
class Function1 {
public:
	typedef Function1Creator<R, A1> Creator;
	static Creator Null() { return Creator(&NullFunction1Implementation<R, A1>::SharedInstance); }
	Function1(const Creator &creator) : _implementation(creator.create()) {}
	R call(A1 a1) { return _implementation->call(a1); }
	~Function1() { _implementation->destroy(); }
private:
	Function1(const Function1<R, A1> &other);
	Function1<R, A1> &operator=(const Function1<R, A1> &other);
	Function1Implementation<R, A1> *_implementation;
};

template <typename R, typename A1>
class WrappedFunction1 : public Function1Implementation<R, A1> {
public:
	typedef R(*Method)(A1);
	WrappedFunction1(Method method) : _method(method) {}
	virtual R call(A1 a1) { return (*_method)(a1); }
private:
	Method _method;
};
template <typename R, typename A1>
inline Function1Creator<R, A1> func(R(*method)(A1)) {
	return Function1Creator<R, A1>(new WrappedFunction1<R, A1>(method));
}
template <typename O, typename I, typename R, typename A1>
class ObjectFunction1 : public Function1Implementation<R, A1> {
public:
	typedef R(I::*Method)(A1);
	ObjectFunction1(O *obj, Method method) : _obj(obj), _method(method) {}
	virtual R call(A1 a1) { return (_obj->*_method)(a1); }
private:
	O *_obj;
	Method _method;
};
template <typename O, typename I, typename R, typename A1>
Function1Creator<R, A1> func(O *obj, R(I::*method)(A1)) {
	return Function1Creator<R, A1>(new ObjectFunction1<O, I, R, A1>(obj, method));
}

template <typename R, typename A1, typename A2>
class Function2Implementation {
public:
	virtual R call(A1 a1, A2 a2) = 0;
	virtual void destroy() { delete this; }
	virtual ~Function2Implementation() {}
};
template <typename R, typename A1, typename A2>
class NullFunction2Implementation : public Function2Implementation<R, A1, A2> {
public:
	virtual R call(A1 a1, A2 a2) { return R(); }
	virtual void destroy() {}
	static NullFunction2Implementation<R, A1, A2> SharedInstance;
};
template <typename R, typename A1, typename A2>
NullFunction2Implementation<R, A1, A2> NullFunction2Implementation<R, A1, A2>::SharedInstance;
template <typename R, typename A1, typename A2>
class Function2Creator {
public:
	Function2Creator(Function2Implementation<R, A1, A2> *ptr) : _ptr(ptr) {}
	Function2Creator(const Function2Creator<R, A1, A2> &other) : _ptr(other.create()) {}
	Function2Implementation<R, A1, A2> *create() const { return exchange(_ptr); }
	~Function2Creator() { destroyImplementation(_ptr); }
private:
	Function2Creator<R, A1, A2> &operator=(const Function2Creator<R, A1, A2> &other);
	mutable Function2Implementation<R, A1, A2> *_ptr;
};
template <typename R, typename A1, typename A2>
class Function2 {
public:
	typedef Function2Creator<R, A1, A2> Creator;
	static Creator Null() { return Creator(&NullFunction2Implementation<R, A1, A2>::SharedInstance); }
	Function2(const Creator &creator) : _implementation(creator.create()) {}
	R call(A1 a1, A2 a2) { return _implementation->call(a1, a2); }
	~Function2() { destroyImplementation(_implementation); }
private:
	Function2(const Function2<R, A1, A2> &other);
	Function2<R, A1, A2> &operator=(const Function2<R, A1, A2> &other);
	Function2Implementation<R, A1, A2> *_implementation;
};

template <typename R, typename A1, typename A2>
class WrappedFunction2 : public Function2Implementation<R, A1, A2> {
public:
	typedef R(*Method)(A1, A2);
	WrappedFunction2(Method method) : _method(method) {}
	virtual R call(A1 a1, A2 a2) { return (*_method)(a1, a2); }
private:
	Method _method;
};
template <typename R, typename A1, typename A2>
Function2Creator<R, A1, A2> func(R(*method)(A1, A2)) {
	return Function2Creator<R, A1, A2>(new WrappedFunction2<R, A1, A2>(method));
}

template <typename O, typename I, typename R, typename A1, typename A2>
class ObjectFunction2 : public Function2Implementation<R, A1, A2> {
public:
	typedef R(I::*Method)(A1, A2);
	ObjectFunction2(O *obj, Method method) : _obj(obj), _method(method) {}
	virtual R call(A1 a1, A2 a2) { return (_obj->*_method)(a1, a2); }
private:
	O *_obj;
	Method _method;
};
template <typename O, typename I, typename R, typename A1, typename A2>
Function2Creator<R, A1, A2> func(O *obj, R(I::*method)(A1, A2)) {
	return Function2Creator<R, A1, A2>(new ObjectFunction2<O, I, R, A1, A2>(obj, method));
}
