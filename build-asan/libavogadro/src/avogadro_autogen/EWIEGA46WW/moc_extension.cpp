/****************************************************************************
** Meta object code from reading C++ file 'extension.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../libavogadro/src/extension.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'extension.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_Avogadro__Extension_t {
    uint offsetsAndSizes[24];
    char stringdata0[20];
    char stringdata1[8];
    char stringdata2[1];
    char stringdata3[2];
    char stringdata4[15];
    char stringdata5[11];
    char stringdata6[16];
    char stringdata7[10];
    char stringdata8[15];
    char stringdata9[14];
    char stringdata10[12];
    char stringdata11[9];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_Avogadro__Extension_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_Avogadro__Extension_t qt_meta_stringdata_Avogadro__Extension = {
    {
        QT_MOC_LITERAL(0, 19),  // "Avogadro::Extension"
        QT_MOC_LITERAL(20, 7),  // "message"
        QT_MOC_LITERAL(28, 0),  // ""
        QT_MOC_LITERAL(29, 1),  // "m"
        QT_MOC_LITERAL(31, 14),  // "actionsChanged"
        QT_MOC_LITERAL(46, 10),  // "Extension*"
        QT_MOC_LITERAL(57, 15),  // "moleculeChanged"
        QT_MOC_LITERAL(73, 9),  // "Molecule*"
        QT_MOC_LITERAL(83, 14),  // "performCommand"
        QT_MOC_LITERAL(98, 13),  // "QUndoCommand*"
        QT_MOC_LITERAL(112, 11),  // "setMolecule"
        QT_MOC_LITERAL(124, 8)   // "molecule"
    },
    "Avogadro::Extension",
    "message",
    "",
    "m",
    "actionsChanged",
    "Extension*",
    "moleculeChanged",
    "Molecule*",
    "performCommand",
    "QUndoCommand*",
    "setMolecule",
    "molecule"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_Avogadro__Extension[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       5,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       4,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   44,    2, 0x06,    1 /* Public */,
       4,    1,   47,    2, 0x06,    3 /* Public */,
       6,    2,   50,    2, 0x06,    5 /* Public */,
       8,    1,   55,    2, 0x06,    8 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
      10,    1,   58,    2, 0x0a,   10 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::QString,    3,
    QMetaType::Void, 0x80000000 | 5,    2,
    QMetaType::Void, 0x80000000 | 7, QMetaType::Int,    2,    2,
    QMetaType::Void, 0x80000000 | 9,    2,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 7,   11,

       0        // eod
};

Q_CONSTINIT const QMetaObject Avogadro::Extension::staticMetaObject = { {
    QMetaObject::SuperData::link<Plugin::staticMetaObject>(),
    qt_meta_stringdata_Avogadro__Extension.offsetsAndSizes,
    qt_meta_data_Avogadro__Extension,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_Avogadro__Extension_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<Extension, std::true_type>,
        // method 'message'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'actionsChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Extension *, std::false_type>,
        // method 'moleculeChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Molecule *, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'performCommand'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QUndoCommand *, std::false_type>,
        // method 'setMolecule'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Molecule *, std::false_type>
    >,
    nullptr
} };

void Avogadro::Extension::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<Extension *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->message((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 1: _t->actionsChanged((*reinterpret_cast< std::add_pointer_t<Extension*>>(_a[1]))); break;
        case 2: _t->moleculeChanged((*reinterpret_cast< std::add_pointer_t<Molecule*>>(_a[1])),(*reinterpret_cast< std::add_pointer_t<int>>(_a[2]))); break;
        case 3: _t->performCommand((*reinterpret_cast< std::add_pointer_t<QUndoCommand*>>(_a[1]))); break;
        case 4: _t->setMolecule((*reinterpret_cast< std::add_pointer_t<Molecule*>>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 1:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< Extension* >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (Extension::*)(const QString & );
            if (_t _q_method = &Extension::message; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (Extension::*)(Extension * );
            if (_t _q_method = &Extension::actionsChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (Extension::*)(Molecule * , int );
            if (_t _q_method = &Extension::moleculeChanged; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (Extension::*)(QUndoCommand * );
            if (_t _q_method = &Extension::performCommand; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
    }
}

const QMetaObject *Avogadro::Extension::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Avogadro::Extension::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Avogadro__Extension.stringdata0))
        return static_cast<void*>(this);
    return Plugin::qt_metacast(_clname);
}

int Avogadro::Extension::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Plugin::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 5)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 5;
    }
    return _id;
}

// SIGNAL 0
void Avogadro::Extension::message(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void Avogadro::Extension::actionsChanged(Extension * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void Avogadro::Extension::moleculeChanged(Molecule * _t1, int _t2)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))), const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t2))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}

// SIGNAL 3
void Avogadro::Extension::performCommand(QUndoCommand * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 3, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
