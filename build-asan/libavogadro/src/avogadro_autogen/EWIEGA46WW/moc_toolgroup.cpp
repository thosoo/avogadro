/****************************************************************************
** Meta object code from reading C++ file 'toolgroup.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../libavogadro/src/toolgroup.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'toolgroup.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_Avogadro__ToolGroup_t {
    uint offsetsAndSizes[36];
    char stringdata0[20];
    char stringdata1[14];
    char stringdata2[1];
    char stringdata3[6];
    char stringdata4[5];
    char stringdata5[15];
    char stringdata6[14];
    char stringdata7[2];
    char stringdata8[5];
    char stringdata9[12];
    char stringdata10[10];
    char stringdata11[9];
    char stringdata12[14];
    char stringdata13[11];
    char stringdata14[9];
    char stringdata15[13];
    char stringdata16[15];
    char stringdata17[13];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_Avogadro__ToolGroup_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_Avogadro__ToolGroup_t qt_meta_stringdata_Avogadro__ToolGroup = {
    {
        QT_MOC_LITERAL(0, 19),  // "Avogadro::ToolGroup"
        QT_MOC_LITERAL(20, 13),  // "toolActivated"
        QT_MOC_LITERAL(34, 0),  // ""
        QT_MOC_LITERAL(35, 5),  // "Tool*"
        QT_MOC_LITERAL(41, 4),  // "tool"
        QT_MOC_LITERAL(46, 14),  // "toolsDestroyed"
        QT_MOC_LITERAL(61, 13),  // "setActiveTool"
        QT_MOC_LITERAL(75, 1),  // "i"
        QT_MOC_LITERAL(77, 4),  // "name"
        QT_MOC_LITERAL(82, 11),  // "setMolecule"
        QT_MOC_LITERAL(94, 9),  // "Molecule*"
        QT_MOC_LITERAL(104, 8),  // "molecule"
        QT_MOC_LITERAL(113, 13),  // "writeSettings"
        QT_MOC_LITERAL(127, 10),  // "QSettings&"
        QT_MOC_LITERAL(138, 8),  // "settings"
        QT_MOC_LITERAL(147, 12),  // "readSettings"
        QT_MOC_LITERAL(160, 14),  // "removeAllTools"
        QT_MOC_LITERAL(175, 12)   // "activateTool"
    },
    "Avogadro::ToolGroup",
    "toolActivated",
    "",
    "Tool*",
    "tool",
    "toolsDestroyed",
    "setActiveTool",
    "i",
    "name",
    "setMolecule",
    "Molecule*",
    "molecule",
    "writeSettings",
    "QSettings&",
    "settings",
    "readSettings",
    "removeAllTools",
    "activateTool"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_Avogadro__ToolGroup[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
      10,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       2,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   74,    2, 0x06,    1 /* Public */,
       5,    0,   77,    2, 0x06,    3 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       6,    1,   78,    2, 0x0a,    4 /* Public */,
       6,    1,   81,    2, 0x0a,    6 /* Public */,
       6,    1,   84,    2, 0x0a,    8 /* Public */,
       9,    1,   87,    2, 0x0a,   10 /* Public */,
      12,    1,   90,    2, 0x10a,   12 /* Public | MethodIsConst  */,
      15,    1,   93,    2, 0x0a,   14 /* Public */,
      16,    0,   96,    2, 0x0a,   16 /* Public */,
      17,    0,   97,    2, 0x08,   17 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    7,
    QMetaType::Void, QMetaType::QString,    8,
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, 0x80000000 | 10,   11,
    QMetaType::Void, 0x80000000 | 13,   14,
    QMetaType::Void, 0x80000000 | 13,   14,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject Avogadro::ToolGroup::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_Avogadro__ToolGroup.offsetsAndSizes,
    qt_meta_data_Avogadro__ToolGroup,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_Avogadro__ToolGroup_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<ToolGroup, std::true_type>,
        // method 'toolActivated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Tool *, std::false_type>,
        // method 'toolsDestroyed'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setActiveTool'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'setActiveTool'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<const QString &, std::false_type>,
        // method 'setActiveTool'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Tool *, std::false_type>,
        // method 'setMolecule'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Molecule *, std::false_type>,
        // method 'writeSettings'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QSettings &, std::false_type>,
        // method 'readSettings'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<QSettings &, std::false_type>,
        // method 'removeAllTools'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'activateTool'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void Avogadro::ToolGroup::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ToolGroup *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->toolActivated((*reinterpret_cast< std::add_pointer_t<Tool*>>(_a[1]))); break;
        case 1: _t->toolsDestroyed(); break;
        case 2: _t->setActiveTool((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->setActiveTool((*reinterpret_cast< std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->setActiveTool((*reinterpret_cast< std::add_pointer_t<Tool*>>(_a[1]))); break;
        case 5: _t->setMolecule((*reinterpret_cast< std::add_pointer_t<Molecule*>>(_a[1]))); break;
        case 6: _t->writeSettings((*reinterpret_cast< std::add_pointer_t<QSettings&>>(_a[1]))); break;
        case 7: _t->readSettings((*reinterpret_cast< std::add_pointer_t<QSettings&>>(_a[1]))); break;
        case 8: _t->removeAllTools(); break;
        case 9: _t->activateTool(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
        case 0:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< Tool* >(); break;
            }
            break;
        case 4:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType(); break;
            case 0:
                *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType::fromType< Tool* >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (ToolGroup::*)(Tool * );
            if (_t _q_method = &ToolGroup::toolActivated; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (ToolGroup::*)();
            if (_t _q_method = &ToolGroup::toolsDestroyed; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
    }
}

const QMetaObject *Avogadro::ToolGroup::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Avogadro::ToolGroup::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Avogadro__ToolGroup.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int Avogadro::ToolGroup::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    }
    return _id;
}

// SIGNAL 0
void Avogadro::ToolGroup::toolActivated(Tool * _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void Avogadro::ToolGroup::toolsDestroyed()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
