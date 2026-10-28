/****************************************************************************
** Meta object code from reading C++ file 'colorbutton.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../libavogadro/src/colorbutton.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'colorbutton.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_Avogadro__ColorButton_t {
    uint offsetsAndSizes[8];
    char stringdata0[22];
    char stringdata1[13];
    char stringdata2[1];
    char stringdata3[12];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_Avogadro__ColorButton_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_Avogadro__ColorButton_t qt_meta_stringdata_Avogadro__ColorButton = {
    {
        QT_MOC_LITERAL(0, 21),  // "Avogadro::ColorButton"
        QT_MOC_LITERAL(22, 12),  // "colorChanged"
        QT_MOC_LITERAL(35, 0),  // ""
        QT_MOC_LITERAL(36, 11)   // "changeColor"
    },
    "Avogadro::ColorButton",
    "colorChanged",
    "",
    "changeColor"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_Avogadro__ColorButton[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       2,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       1,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   26,    2, 0x06,    1 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       3,    0,   29,    2, 0x0a,    3 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::QColor,    2,

 // slots: parameters
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject Avogadro::ColorButton::staticMetaObject = { {
    QMetaObject::SuperData::link<QAbstractButton::staticMetaObject>(),
    qt_meta_stringdata_Avogadro__ColorButton.offsetsAndSizes,
    qt_meta_data_Avogadro__ColorButton,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_Avogadro__ColorButton_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<ColorButton, std::true_type>,
        // method 'colorChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QColor, std::false_type>,
        // method 'changeColor'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void Avogadro::ColorButton::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ColorButton *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->colorChanged((*reinterpret_cast< std::add_pointer_t<QColor>>(_a[1]))); break;
        case 1: _t->changeColor(); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (ColorButton::*)(QColor );
            if (_t _q_method = &ColorButton::colorChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
    }
}

const QMetaObject *Avogadro::ColorButton::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Avogadro::ColorButton::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Avogadro__ColorButton.stringdata0))
        return static_cast<void*>(this);
    return QAbstractButton::qt_metacast(_clname);
}

int Avogadro::ColorButton::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QAbstractButton::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 2)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 2)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void Avogadro::ColorButton::colorChanged(QColor _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
