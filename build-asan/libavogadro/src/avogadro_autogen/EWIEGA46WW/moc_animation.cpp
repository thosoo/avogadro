/****************************************************************************
** Meta object code from reading C++ file 'animation.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../libavogadro/src/animation.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'animation.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_Avogadro__Animation_t {
    uint offsetsAndSizes[36];
    char stringdata0[20];
    char stringdata1[13];
    char stringdata2[1];
    char stringdata3[7];
    char stringdata4[4];
    char stringdata5[8];
    char stringdata6[5];
    char stringdata7[9];
    char stringdata8[2];
    char stringdata9[14];
    char stringdata10[14];
    char stringdata11[15];
    char stringdata12[16];
    char stringdata13[7];
    char stringdata14[6];
    char stringdata15[6];
    char stringdata16[5];
    char stringdata17[11];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_Avogadro__Animation_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_Avogadro__Animation_t qt_meta_stringdata_Avogadro__Animation = {
    {
        QT_MOC_LITERAL(0, 19),  // "Avogadro::Animation"
        QT_MOC_LITERAL(20, 12),  // "frameChanged"
        QT_MOC_LITERAL(33, 0),  // ""
        QT_MOC_LITERAL(34, 6),  // "setFps"
        QT_MOC_LITERAL(41, 3),  // "fps"
        QT_MOC_LITERAL(45, 7),  // "setLoop"
        QT_MOC_LITERAL(53, 4),  // "loop"
        QT_MOC_LITERAL(58, 8),  // "setFrame"
        QT_MOC_LITERAL(67, 1),  // "i"
        QT_MOC_LITERAL(69, 13),  // "sliderChanged"
        QT_MOC_LITERAL(83, 13),  // "sliderPressed"
        QT_MOC_LITERAL(97, 14),  // "sliderReleased"
        QT_MOC_LITERAL(112, 15),  // "setDynamicBonds"
        QT_MOC_LITERAL(128, 6),  // "enable"
        QT_MOC_LITERAL(135, 5),  // "start"
        QT_MOC_LITERAL(141, 5),  // "pause"
        QT_MOC_LITERAL(147, 4),  // "stop"
        QT_MOC_LITERAL(152, 10)   // "timerFired"
    },
    "Avogadro::Animation",
    "frameChanged",
    "",
    "setFps",
    "fps",
    "setLoop",
    "loop",
    "setFrame",
    "i",
    "sliderChanged",
    "sliderPressed",
    "sliderReleased",
    "setDynamicBonds",
    "enable",
    "start",
    "pause",
    "stop",
    "timerFired"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_Avogadro__Animation[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
      12,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   86,    2, 0x06,    1 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       3,    1,   89,    2, 0x0a,    3 /* Public */,
       5,    1,   92,    2, 0x0a,    5 /* Public */,
       7,    1,   95,    2, 0x0a,    7 /* Public */,
       9,    1,   98,    2, 0x0a,    9 /* Public */,
      10,    0,  101,    2, 0x0a,   11 /* Public */,
      11,    0,  102,    2, 0x0a,   12 /* Public */,
      12,    1,  103,    2, 0x0a,   13 /* Public */,
      14,    0,  106,    2, 0x0a,   15 /* Public */,
      15,    0,  107,    2, 0x0a,   16 /* Public */,
      16,    0,  108,    2, 0x0a,   17 /* Public */,
      17,    0,  109,    2, 0x0a,   18 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::Int,    2,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    4,
    QMetaType::Void, QMetaType::Bool,    6,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,   13,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject Avogadro::Animation::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_Avogadro__Animation.offsetsAndSizes,
    qt_meta_data_Avogadro__Animation,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_Avogadro__Animation_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<Animation, std::true_type>,
        // method 'frameChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'setFps'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'setLoop'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const bool, std::false_type>,
        // method 'setFrame'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'sliderChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'sliderPressed'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'sliderReleased'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setDynamicBonds'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'start'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'pause'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'stop'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'timerFired'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void Avogadro::Animation::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<Animation *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->frameChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 1: _t->setFps((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 2: _t->setLoop((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 3: _t->setFrame((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->sliderChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 5: _t->sliderPressed(); break;
        case 6: _t->sliderReleased(); break;
        case 7: _t->setDynamicBonds((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 8: _t->start(); break;
        case 9: _t->pause(); break;
        case 10: _t->stop(); break;
        case 11: _t->timerFired(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (Animation::*)(int );
            if (_t _q_method = &Animation::frameChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
    }
}

const QMetaObject *Avogadro::Animation::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Avogadro::Animation::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Avogadro__Animation.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int Avogadro::Animation::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 12)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 12;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 12)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 12;
    }
    return _id;
}

// SIGNAL 0
void Avogadro::Animation::frameChanged(int _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
