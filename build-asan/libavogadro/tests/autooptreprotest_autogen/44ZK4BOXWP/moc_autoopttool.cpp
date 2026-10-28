/****************************************************************************
** Meta object code from reading C++ file 'autoopttool.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../libavogadro/src/tools/autoopttool.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>
#include <QtCore/qplugin.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'autoopttool.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_Avogadro__AutoOptThread_t {
    uint offsetsAndSizes[22];
    char stringdata0[24];
    char stringdata1[9];
    char stringdata2[1];
    char stringdata3[11];
    char stringdata4[10];
    char stringdata5[12];
    char stringdata6[12];
    char stringdata7[7];
    char stringdata8[5];
    char stringdata9[18];
    char stringdata10[4];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_Avogadro__AutoOptThread_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_Avogadro__AutoOptThread_t qt_meta_stringdata_Avogadro__AutoOptThread = {
    {
        QT_MOC_LITERAL(0, 23),  // "Avogadro::AutoOptThread"
        QT_MOC_LITERAL(24, 8),  // "finished"
        QT_MOC_LITERAL(33, 0),  // ""
        QT_MOC_LITERAL(34, 10),  // "calculated"
        QT_MOC_LITERAL(45, 9),  // "setupDone"
        QT_MOC_LITERAL(55, 11),  // "setupFailed"
        QT_MOC_LITERAL(67, 11),  // "setupSucces"
        QT_MOC_LITERAL(79, 6),  // "update"
        QT_MOC_LITERAL(86, 4),  // "stop"
        QT_MOC_LITERAL(91, 17),  // "setPendingFixAtom"
        QT_MOC_LITERAL(109, 3)   // "idx"
    },
    "Avogadro::AutoOptThread",
    "finished",
    "",
    "calculated",
    "setupDone",
    "setupFailed",
    "setupSucces",
    "update",
    "stop",
    "setPendingFixAtom",
    "idx"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_Avogadro__AutoOptThread[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       7,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       4,       // signalCount

 // signals: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   56,    2, 0x06,    1 /* Public */,
       4,    0,   59,    2, 0x06,    3 /* Public */,
       5,    0,   60,    2, 0x06,    4 /* Public */,
       6,    0,   61,    2, 0x06,    5 /* Public */,

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       7,    0,   62,    2, 0x0a,    6 /* Public */,
       8,    0,   63,    2, 0x0a,    7 /* Public */,
       9,    1,   64,    2, 0x0a,    8 /* Public */,

 // signals: parameters
    QMetaType::Void, QMetaType::Bool,    3,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::UInt,   10,

       0        // eod
};

Q_CONSTINIT const QMetaObject Avogadro::AutoOptThread::staticMetaObject = { {
    QMetaObject::SuperData::link<QThread::staticMetaObject>(),
    qt_meta_stringdata_Avogadro__AutoOptThread.offsetsAndSizes,
    qt_meta_data_Avogadro__AutoOptThread,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_Avogadro__AutoOptThread_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<AutoOptThread, std::true_type>,
        // method 'finished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'setupDone'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setupFailed'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setupSucces'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'update'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'stop'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setPendingFixAtom'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<unsigned , std::false_type>
    >,
    nullptr
} };

void Avogadro::AutoOptThread::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<AutoOptThread *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->finished((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->setupDone(); break;
        case 2: _t->setupFailed(); break;
        case 3: _t->setupSucces(); break;
        case 4: _t->update(); break;
        case 5: _t->stop(); break;
        case 6: _t->setPendingFixAtom((*reinterpret_cast< std::add_pointer_t<uint>>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (AutoOptThread::*)(bool );
            if (_t _q_method = &AutoOptThread::finished; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (AutoOptThread::*)();
            if (_t _q_method = &AutoOptThread::setupDone; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (AutoOptThread::*)();
            if (_t _q_method = &AutoOptThread::setupFailed; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 2;
                return;
            }
        }
        {
            using _t = void (AutoOptThread::*)();
            if (_t _q_method = &AutoOptThread::setupSucces; *reinterpret_cast<_t *>(_a[1]) == _q_method) {
                *result = 3;
                return;
            }
        }
    }
}

const QMetaObject *Avogadro::AutoOptThread::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Avogadro::AutoOptThread::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Avogadro__AutoOptThread.stringdata0))
        return static_cast<void*>(this);
    return QThread::qt_metacast(_clname);
}

int Avogadro::AutoOptThread::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QThread::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 7)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 7;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 7)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 7;
    }
    return _id;
}

// SIGNAL 0
void Avogadro::AutoOptThread::finished(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void Avogadro::AutoOptThread::setupDone()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void Avogadro::AutoOptThread::setupFailed()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void Avogadro::AutoOptThread::setupSucces()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}
namespace {
struct qt_meta_stringdata_Avogadro__AutoOptTool_t {
    uint offsetsAndSizes[24];
    char stringdata0[22];
    char stringdata1[9];
    char stringdata2[1];
    char stringdata3[11];
    char stringdata4[10];
    char stringdata5[12];
    char stringdata6[12];
    char stringdata7[7];
    char stringdata8[7];
    char stringdata9[8];
    char stringdata10[6];
    char stringdata11[24];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_Avogadro__AutoOptTool_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_Avogadro__AutoOptTool_t qt_meta_stringdata_Avogadro__AutoOptTool = {
    {
        QT_MOC_LITERAL(0, 21),  // "Avogadro::AutoOptTool"
        QT_MOC_LITERAL(22, 8),  // "finished"
        QT_MOC_LITERAL(31, 0),  // ""
        QT_MOC_LITERAL(32, 10),  // "calculated"
        QT_MOC_LITERAL(43, 9),  // "setupDone"
        QT_MOC_LITERAL(53, 11),  // "setupFailed"
        QT_MOC_LITERAL(65, 11),  // "setupSucces"
        QT_MOC_LITERAL(77, 6),  // "toggle"
        QT_MOC_LITERAL(84, 6),  // "enable"
        QT_MOC_LITERAL(91, 7),  // "disable"
        QT_MOC_LITERAL(99, 5),  // "abort"
        QT_MOC_LITERAL(105, 23)   // "settingsWidgetDestroyed"
    },
    "Avogadro::AutoOptTool",
    "finished",
    "",
    "calculated",
    "setupDone",
    "setupFailed",
    "setupSucces",
    "toggle",
    "enable",
    "disable",
    "abort",
    "settingsWidgetDestroyed"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_Avogadro__AutoOptTool[] = {

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
       4,    0,   71,    2, 0x0a,    3 /* Public */,
       5,    0,   72,    2, 0x0a,    4 /* Public */,
       6,    0,   73,    2, 0x0a,    5 /* Public */,
       7,    0,   74,    2, 0x0a,    6 /* Public */,
       8,    0,   75,    2, 0x0a,    7 /* Public */,
       9,    0,   76,    2, 0x0a,    8 /* Public */,
      10,    0,   77,    2, 0x0a,    9 /* Public */,
      11,    0,   78,    2, 0x08,   10 /* Private */,

 // slots: parameters
    QMetaType::Void, QMetaType::Bool,    3,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

Q_CONSTINIT const QMetaObject Avogadro::AutoOptTool::staticMetaObject = { {
    QMetaObject::SuperData::link<Tool::staticMetaObject>(),
    qt_meta_stringdata_Avogadro__AutoOptTool.offsetsAndSizes,
    qt_meta_data_Avogadro__AutoOptTool,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_Avogadro__AutoOptTool_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<AutoOptTool, std::true_type>,
        // method 'finished'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<bool, std::false_type>,
        // method 'setupDone'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setupFailed'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'setupSucces'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'toggle'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'enable'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'disable'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'abort'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        // method 'settingsWidgetDestroyed'
        QtPrivate::TypeAndForceComplete<void, std::false_type>
    >,
    nullptr
} };

void Avogadro::AutoOptTool::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<AutoOptTool *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->finished((*reinterpret_cast< std::add_pointer_t<bool>>(_a[1]))); break;
        case 1: _t->setupDone(); break;
        case 2: _t->setupFailed(); break;
        case 3: _t->setupSucces(); break;
        case 4: _t->toggle(); break;
        case 5: _t->enable(); break;
        case 6: _t->disable(); break;
        case 7: _t->abort(); break;
        case 8: _t->settingsWidgetDestroyed(); break;
        default: ;
        }
    }
}

const QMetaObject *Avogadro::AutoOptTool::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Avogadro::AutoOptTool::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Avogadro__AutoOptTool.stringdata0))
        return static_cast<void*>(this);
    return Tool::qt_metacast(_clname);
}

int Avogadro::AutoOptTool::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 9;
    }
    return _id;
}
namespace {
struct qt_meta_stringdata_Avogadro__AutoOptToolFactory_t {
    uint offsetsAndSizes[2];
    char stringdata0[29];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_Avogadro__AutoOptToolFactory_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_Avogadro__AutoOptToolFactory_t qt_meta_stringdata_Avogadro__AutoOptToolFactory = {
    {
        QT_MOC_LITERAL(0, 28)   // "Avogadro::AutoOptToolFactory"
    },
    "Avogadro::AutoOptToolFactory"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_Avogadro__AutoOptToolFactory[] = {

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

Q_CONSTINIT const QMetaObject Avogadro::AutoOptToolFactory::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_Avogadro__AutoOptToolFactory.offsetsAndSizes,
    qt_meta_data_Avogadro__AutoOptToolFactory,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_Avogadro__AutoOptToolFactory_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<AutoOptToolFactory, std::true_type>
    >,
    nullptr
} };

void Avogadro::AutoOptToolFactory::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    (void)_o;
    (void)_id;
    (void)_c;
    (void)_a;
}

const QMetaObject *Avogadro::AutoOptToolFactory::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Avogadro::AutoOptToolFactory::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Avogadro__AutoOptToolFactory.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "PluginFactory"))
        return static_cast< PluginFactory*>(this);
    if (!strcmp(_clname, "net.sourceforge.avogadro.pluginfactory/1.5"))
        return static_cast< Avogadro::PluginFactory*>(this);
    return QObject::qt_metacast(_clname);
}

int Avogadro::AutoOptToolFactory::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    return _id;
}
using namespace Avogadro;

#ifdef QT_MOC_EXPORT_PLUGIN_V2
static constexpr unsigned char qt_pluginMetaDataV2_AutoOptToolFactory[] = {
    0xbf, 
    // "IID"
    0x02,  0x78,  0x2a,  'n',  'e',  't',  '.',  's', 
    'o',  'u',  'r',  'c',  'e',  'f',  'o',  'r', 
    'g',  'e',  '.',  'a',  'v',  'o',  'g',  'a', 
    'd',  'r',  'o',  '.',  'p',  'l',  'u',  'g', 
    'i',  'n',  'f',  'a',  'c',  't',  'o',  'r', 
    'y',  '/',  '1',  '.',  '5', 
    // "className"
    0x03,  0x72,  'A',  'u',  't',  'o',  'O',  'p', 
    't',  'T',  'o',  'o',  'l',  'F',  'a',  'c', 
    't',  'o',  'r',  'y', 
    0xff, 
};
QT_MOC_EXPORT_PLUGIN_V2(Avogadro::AutoOptToolFactory, AutoOptToolFactory, qt_pluginMetaDataV2_AutoOptToolFactory)
#else
QT_PLUGIN_METADATA_SECTION
Q_CONSTINIT static constexpr unsigned char qt_pluginMetaData_AutoOptToolFactory[] = {
    'Q', 'T', 'M', 'E', 'T', 'A', 'D', 'A', 'T', 'A', ' ', '!',
    // metadata version, Qt version, architectural requirements
    0, QT_VERSION_MAJOR, QT_VERSION_MINOR, qPluginArchRequirements(),
    0xbf, 
    // "IID"
    0x02,  0x78,  0x2a,  'n',  'e',  't',  '.',  's', 
    'o',  'u',  'r',  'c',  'e',  'f',  'o',  'r', 
    'g',  'e',  '.',  'a',  'v',  'o',  'g',  'a', 
    'd',  'r',  'o',  '.',  'p',  'l',  'u',  'g', 
    'i',  'n',  'f',  'a',  'c',  't',  'o',  'r', 
    'y',  '/',  '1',  '.',  '5', 
    // "className"
    0x03,  0x72,  'A',  'u',  't',  'o',  'O',  'p', 
    't',  'T',  'o',  'o',  'l',  'F',  'a',  'c', 
    't',  'o',  'r',  'y', 
    0xff, 
};
QT_MOC_EXPORT_PLUGIN(Avogadro::AutoOptToolFactory, AutoOptToolFactory)
#endif  // QT_MOC_EXPORT_PLUGIN_V2

QT_WARNING_POP
QT_END_MOC_NAMESPACE
