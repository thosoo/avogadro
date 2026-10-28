/****************************************************************************
** Meta object code from reading C++ file 'builderpool.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../libavogadro/src/rendering/builderpool.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'builderpool.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 68
#error "This file was generated using the moc from 6.4.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
namespace {
struct qt_meta_stringdata_Avogadro__rendering__BuilderWorker_t {
    uint offsetsAndSizes[14];
    char stringdata0[35];
    char stringdata1[9];
    char stringdata2[1];
    char stringdata3[6];
    char stringdata4[19];
    char stringdata5[7];
    char stringdata6[11];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_Avogadro__rendering__BuilderWorker_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_Avogadro__rendering__BuilderWorker_t qt_meta_stringdata_Avogadro__rendering__BuilderWorker = {
    {
        QT_MOC_LITERAL(0, 34),  // "Avogadro::rendering::BuilderW..."
        QT_MOC_LITERAL(35, 8),  // "jobReady"
        QT_MOC_LITERAL(44, 0),  // ""
        QT_MOC_LITERAL(45, 5),  // "jobId"
        QT_MOC_LITERAL(51, 18),  // "TessellationResult"
        QT_MOC_LITERAL(70, 6),  // "result"
        QT_MOC_LITERAL(77, 10)   // "generation"
    },
    "Avogadro::rendering::BuilderWorker",
    "jobReady",
    "",
    "jobId",
    "TessellationResult",
    "result",
    "generation"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_Avogadro__rendering__BuilderWorker[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       1,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    3,   20,    2, 0x06,    1 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::ULong, 0x80000000 | 4, QMetaType::UInt,    3,    5,    6,

       0        // eod
};

Q_CONSTINIT const QMetaObject Avogadro::rendering::BuilderWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QThread::staticMetaObject>(),
    qt_meta_stringdata_Avogadro__rendering__BuilderWorker.offsetsAndSizes,
    qt_meta_data_Avogadro__rendering__BuilderWorker,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_Avogadro__rendering__BuilderWorker_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<BuilderWorker, std::true_type>,
        // method 'jobReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<unsigned long, std::false_type>,
        QtPrivate::TypeAndForceComplete<const TessellationResult &, std::false_type>,
        QtPrivate::TypeAndForceComplete<unsigned int, std::false_type>
    >,
    nullptr
} };

void Avogadro::rendering::BuilderWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<BuilderWorker *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->jobReady((*reinterpret_cast< std::add_pointer_t<ulong>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<TessellationResult>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<uint>>(_a[3]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (BuilderWorker::*)(unsigned long , const TessellationResult & , unsigned int );
            if (_t _q_method = &BuilderWorker::jobReady; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
    }
}

const QMetaObject *Avogadro::rendering::BuilderWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Avogadro::rendering::BuilderWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Avogadro__rendering__BuilderWorker.stringdata0))
        return static_cast<void*>(this);
    return QThread::qt_metacast(_clname);
}

int Avogadro::rendering::BuilderWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QThread::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 1;
    }
    return _id;
}

// SIGNAL 0
void Avogadro::rendering::BuilderWorker::jobReady(unsigned long _t1, const TessellationResult & _t2, unsigned int _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
namespace {
struct qt_meta_stringdata_Avogadro__rendering__BuilderPool_t {
    uint offsetsAndSizes[14];
    char stringdata0[33];
    char stringdata1[9];
    char stringdata2[1];
    char stringdata3[6];
    char stringdata4[19];
    char stringdata5[7];
    char stringdata6[11];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_Avogadro__rendering__BuilderPool_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_Avogadro__rendering__BuilderPool_t qt_meta_stringdata_Avogadro__rendering__BuilderPool = {
    {
        QT_MOC_LITERAL(0, 32),  // "Avogadro::rendering::BuilderPool"
        QT_MOC_LITERAL(33, 8),  // "jobReady"
        QT_MOC_LITERAL(42, 0),  // ""
        QT_MOC_LITERAL(43, 5),  // "jobId"
        QT_MOC_LITERAL(49, 18),  // "TessellationResult"
        QT_MOC_LITERAL(68, 6),  // "result"
        QT_MOC_LITERAL(75, 10)   // "generation"
    },
    "Avogadro::rendering::BuilderPool",
    "jobReady",
    "",
    "jobId",
    "TessellationResult",
    "result",
    "generation"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_Avogadro__rendering__BuilderPool[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       1,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    3,   20,    2, 0x06,    1 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::ULong, 0x80000000 | 4, QMetaType::UInt,    3,    5,    6,

       0        // eod
};

Q_CONSTINIT const QMetaObject Avogadro::rendering::BuilderPool::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_Avogadro__rendering__BuilderPool.offsetsAndSizes,
    qt_meta_data_Avogadro__rendering__BuilderPool,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_Avogadro__rendering__BuilderPool_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<BuilderPool, std::true_type>,
        // method 'jobReady'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<unsigned long, std::false_type>,
        QtPrivate::TypeAndForceComplete<const TessellationResult &, std::false_type>,
        QtPrivate::TypeAndForceComplete<unsigned int, std::false_type>
    >,
    nullptr
} };

void Avogadro::rendering::BuilderPool::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<BuilderPool *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->jobReady((*reinterpret_cast< std::add_pointer_t<ulong>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<TessellationResult>>(_a[2])),(*reinterpret_cast< std::add_pointer_t<uint>>(_a[3]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (BuilderPool::*)(unsigned long , const TessellationResult & , unsigned int );
            if (_t _q_method = &BuilderPool::jobReady; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
    }
}

const QMetaObject *Avogadro::rendering::BuilderPool::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Avogadro::rendering::BuilderPool::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Avogadro__rendering__BuilderPool.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int Avogadro::rendering::BuilderPool::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 1)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 1;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 1)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 1;
    }
    return _id;
}

// SIGNAL 0
void Avogadro::rendering::BuilderPool::jobReady(unsigned long _t1, const TessellationResult & _t2, unsigned int _t3)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t3))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
