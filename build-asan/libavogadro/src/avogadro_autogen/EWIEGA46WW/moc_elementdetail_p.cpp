/****************************************************************************
** Meta object code from reading C++ file 'elementdetail_p.h'
**
** Created by: The Qt Meta Object Compiler version 68 (Qt 6.4.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "../../../../../libavogadro/src/elementdetail_p.h"
#include <QtCore/qmetatype.h>
#include <QtCore/qplugin.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'elementdetail_p.h' doesn't include <QObject>."
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
struct qt_meta_stringdata_Avogadro__ElementDetail_t {
    uint offsetsAndSizes[8];
    char stringdata0[24];
    char stringdata1[15];
    char stringdata2[1];
    char stringdata3[8];
};
#define QT_MOC_LITERAL(ofs, len) \
    uint(sizeof(qt_meta_stringdata_Avogadro__ElementDetail_t::offsetsAndSizes) + ofs), len 
Q_CONSTINIT static const qt_meta_stringdata_Avogadro__ElementDetail_t qt_meta_stringdata_Avogadro__ElementDetail = {
    {
        QT_MOC_LITERAL(0, 23),  // "Avogadro::ElementDetail"
        QT_MOC_LITERAL(24, 14),  // "elementChanged"
        QT_MOC_LITERAL(39, 0),  // ""
        QT_MOC_LITERAL(40, 7)   // "element"
    },
    "Avogadro::ElementDetail",
    "elementChanged",
    "",
    "element"
};
#undef QT_MOC_LITERAL
} // unnamed namespace

Q_CONSTINIT static const uint qt_meta_data_Avogadro__ElementDetail[] = {

 // content:
      10,       // revision
       0,       // classname
       0,    0, // classinfo
       1,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags, initial metatype offsets
       1,    1,   20,    2, 0x0a,    1 /* Public */,

 // slots: parameters
    QMetaType::Void, QMetaType::Int,    3,

       0        // eod
};

Q_CONSTINIT const QMetaObject Avogadro::ElementDetail::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_Avogadro__ElementDetail.offsetsAndSizes,
    qt_meta_data_Avogadro__ElementDetail,
    qt_static_metacall,
    nullptr,
    qt_incomplete_metaTypeArray<qt_meta_stringdata_Avogadro__ElementDetail_t,
        // Q_OBJECT / Q_GADGET
        QtPrivate::TypeAndForceComplete<ElementDetail, std::true_type>,
        // method 'elementChanged'
        QtPrivate::TypeAndForceComplete<void, std::false_type>,
        QtPrivate::TypeAndForceComplete<int, std::false_type>
    >,
    nullptr
} };

void Avogadro::ElementDetail::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ElementDetail *>(_o);
        (void)_t;
        switch (_id) {
        case 0: _t->elementChanged((*reinterpret_cast< std::add_pointer_t<int>>(_a[1]))); break;
        default: ;
        }
    }
}

const QMetaObject *Avogadro::ElementDetail::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *Avogadro::ElementDetail::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_Avogadro__ElementDetail.stringdata0))
        return static_cast<void*>(this);
    if (!strcmp(_clname, "QGraphicsItem"))
        return static_cast< QGraphicsItem*>(this);
    if (!strcmp(_clname, "org.qt-project.Qt.QGraphicsItem"))
        return static_cast< QGraphicsItem*>(this);
    return QObject::qt_metacast(_clname);
}

int Avogadro::ElementDetail::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
using namespace Avogadro;

#ifdef QT_MOC_EXPORT_PLUGIN_V2
static constexpr unsigned char qt_pluginMetaDataV2_ElementDetail[] = {
    0xbf, 
    // "IID"
    0x02,  0x78,  0x2a,  'n',  'e',  't',  '.',  's', 
    'o',  'u',  'r',  'c',  'e',  'f',  'o',  'r', 
    'g',  'e',  '.',  'a',  'v',  'o',  'g',  'a', 
    'd',  'r',  'o',  '.',  'p',  'l',  'u',  'g', 
    'i',  'n',  'f',  'a',  'c',  't',  'o',  'r', 
    'y',  '/',  '1',  '.',  '5', 
    // "className"
    0x03,  0x6d,  'E',  'l',  'e',  'm',  'e',  'n', 
    't',  'D',  'e',  't',  'a',  'i',  'l', 
    0xff, 
};
QT_MOC_EXPORT_PLUGIN_V2(Avogadro::ElementDetail, ElementDetail, qt_pluginMetaDataV2_ElementDetail)
#else
QT_PLUGIN_METADATA_SECTION
Q_CONSTINIT static constexpr unsigned char qt_pluginMetaData_ElementDetail[] = {
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
    0x03,  0x6d,  'E',  'l',  'e',  'm',  'e',  'n', 
    't',  'D',  'e',  't',  'a',  'i',  'l', 
    0xff, 
};
QT_MOC_EXPORT_PLUGIN(Avogadro::ElementDetail, ElementDetail)
#endif  // QT_MOC_EXPORT_PLUGIN_V2

QT_WARNING_POP
QT_END_MOC_NAMESPACE
