/****************************************************************************
** Meta object code from reading C++ file 'drawtool.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../libavogadro/src/tools/drawtool.h"
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'drawtool.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_Avogadro__DrawTool_t {
    uint offsetsAndSizes[32];
    char stringdata0[19];
    char stringdata1[14];
    char stringdata2[1];
    char stringdata3[6];
    char stringdata4[5];
    char stringdata5[16];
    char stringdata6[6];
    char stringdata7[15];
    char stringdata8[6];
    char stringdata9[21];
    char stringdata10[17];
    char stringdata11[13];
    char stringdata12[2];
    char stringdata13[16];
    char stringdata14[20];
    char stringdata15[24];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_Avogadro__DrawTool_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_Avogadro__DrawTool_t qt_meta_stringdata_Avogadro__DrawTool = {
    {
        QT_MOC_LITERAL(0, 18),  // "Avogadro::DrawTool"
        QT_MOC_LITERAL(19, 13),  // "toolActivated"
        QT_MOC_LITERAL(33, 0),  // ""
        QT_MOC_LITERAL(34, 5),  // "Tool*"
        QT_MOC_LITERAL(40, 4),  // "tool"
        QT_MOC_LITERAL(45, 15),  // "setAddHydrogens"
        QT_MOC_LITERAL(61, 5),  // "state"
        QT_MOC_LITERAL(67, 14),  // "elementChanged"
        QT_MOC_LITERAL(82, 5),  // "index"
        QT_MOC_LITERAL(88, 20),  // "customElementChanged"
        QT_MOC_LITERAL(109, 16),  // "bondOrderChanged"
        QT_MOC_LITERAL(126, 12),  // "setBondOrder"
        QT_MOC_LITERAL(139, 1),  // "i"
        QT_MOC_LITERAL(141, 15),  // "toggleDockTable"
        QT_MOC_LITERAL(157, 19),  // "clearKeyPressBuffer"
        QT_MOC_LITERAL(177, 23)   // "settingsWidgetDestroyed"
    },
    "Avogadro::DrawTool",
    "toolActivated",
    "",
    "Tool*",
    "tool",
    "setAddHydrogens",
    "state",
    "elementChanged",
    "index",
    "customElementChanged",
    "bondOrderChanged",
    "setBondOrder",
    "i",
    "toggleDockTable",
    "clearKeyPressBuffer",
    "settingsWidgetDestroyed"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_Avogadro__DrawTool[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       9,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   68,    2, 0x0a,    1 /* Public */,
       5,    1,   71,    2, 0x0a,    3 /* Public */,
       7,    1,   74,    2, 0x0a,    5 /* Public */,
       9,    1,   77,    2, 0x0a,    7 /* Public */,
      10,    1,   80,    2, 0x0a,    9 /* Public */,
      11,    1,   83,    2, 0x0a,   11 /* Public */,
      13,    1,   86,    2, 0x0a,   13 /* Public */,
      14,    0,   89,    2, 0x0a,   15 /* Public */,
      15,    0,   90,    2, 0x08,   16 /* Private */,

 // slots: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, QMetaType::Int,    6,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void, QMetaType::Int,   12,
    QMetaType::Void, QMetaType::Int,    6,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject Avogadro::DrawTool::staticMetaObject = { {
    QMetaObject::SuperData::link<Tool::staticMetaObject>(),
    qt_meta_stringdata_Avogadro__DrawTool.offsetsAndSizes,
    qt_meta_data_Avogadro__DrawTool,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_Avogadro__DrawTool_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<DrawTool, std::true_type>,
        // method 'toolActivated'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<Tool *, std::false_type>,
        // method 'setAddHydrogens'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'elementChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'customElementChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'bondOrderChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'setBondOrder'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'toggleDockTable'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>,
        // method 'clearKeyPressBuffer'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'settingsWidgetDestroyed'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void Avogadro::DrawTool::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<DrawTool *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->toolActivated((*reinterpret_cast< std::add_pointer_t<Tool*>>(_a[1]))); break;
        case 1: _t->setAddHydrogens((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 2: _t->elementChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->customElementChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 4: _t->bondOrderChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 5: _t->setBondOrder((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 6: _t->toggleDockTable((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        case 7: _t->clearKeyPressBuffer(); break;
        case 8: _t->settingsWidgetDestroyed(); break;
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
        }
    }
}

const QMetaObject *Avogadro::DrawTool::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Avogadro::DrawTool::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Avogadro__DrawTool.stringdata0))
        return static_cast<void*>(this);
    return Tool::qt_metacast(_clname);
}

int Avogadro::DrawTool::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = Tool::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    }
    return _id;
}
namespace {
struct qt_meta_stringdata_Avogadro__DrawToolFactory_t {
    uint offsetsAndSizes[2];
    char stringdata0[26];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_Avogadro__DrawToolFactory_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_Avogadro__DrawToolFactory_t qt_meta_stringdata_Avogadro__DrawToolFactory = {
    {
        QT_MOC_LITERAL(0, 25)   // "Avogadro::DrawToolFactory"
    },
    "Avogadro::DrawToolFactory"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_Avogadro__DrawToolFactory[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       0,    0, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

       0        // eod
};

Q_CONSTINIT const QMetaObject Avogadro::DrawToolFactory::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_Avogadro__DrawToolFactory.offsetsAndSizes,
    qt_meta_data_Avogadro__DrawToolFactory,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_Avogadro__DrawToolFactory_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<DrawToolFactory, std::true_type>
    >,
    nullptr
} };

void Avogadro::DrawToolFactory::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

const QMetaObject *Avogadro::DrawToolFactory::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Avogadro::DrawToolFactory::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Avogadro__DrawToolFactory.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "PluginFactory"))
        return static_cast< PluginFactory*>(this);
    if (!strcmp(_clname, "net.sourceforge.avogadro.pluginfactory/1.5"))
        return static_cast< Avogadro::PluginFactory*>(this);
    return QObject::qt_metacast(_clname);
}

int Avogadro::DrawToolFactory::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
